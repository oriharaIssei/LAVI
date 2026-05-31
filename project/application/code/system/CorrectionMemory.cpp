#include "CorrectionMemory.h"

#include "LLMClient.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace {

// UTF-8 先頭バイトからコードポイント長
size_t Utf8Len(unsigned char c) {
    if (c >= 0xF0) return 4;
    if (c >= 0xE0) return 3;
    if (c >= 0xC0) return 2;
    return 1;
}

uint32_t DecodeUtf8(const std::string& s, size_t i, size_t len) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (len == 1) return c;
    if (len == 2) return ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
    if (len == 3) return ((c & 0x0F) << 12) |
                         ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                         (static_cast<unsigned char>(s[i + 2]) & 0x3F);
    return ((c & 0x07) << 18) |
           ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
           ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) |
           (static_cast<unsigned char>(s[i + 3]) & 0x3F);
}

bool IsKatakana(uint32_t cp) {
    return (cp >= 0x30A0 && cp <= 0x30FF) || // 全角カタカナ（長音符含む）
           (cp >= 0x31F0 && cp <= 0x31FF) || // カタカナ拡張
           (cp >= 0xFF66 && cp <= 0xFF9F);   // 半角カタカナ
}

bool IsLatin(uint32_t cp) {
    return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9');
}

// 応答テキストから最初の JSON オブジェクト {...} を取り出す
std::string ExtractJsonObject(const std::string& text) {
    size_t start = text.find('{');
    if (start == std::string::npos) return "";
    int depth = 0;
    for (size_t i = start; i < text.size(); ++i) {
        if (text[i] == '{') ++depth;
        else if (text[i] == '}') {
            --depth;
            if (depth == 0) return text.substr(start, i - start + 1);
        }
    }
    return "";
}

} // namespace

std::vector<std::string> CorrectionMemory::ExtractTokens(const std::string& text) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < text.size()) {
        size_t len = Utf8Len(static_cast<unsigned char>(text[i]));
        if (i + len > text.size()) break;
        uint32_t cp = DecodeUtf8(text, i, len);

        bool kata = IsKatakana(cp);
        bool latin = IsLatin(cp);
        if (kata || latin) {
            size_t startByte = i;
            int cpCount = 0;
            // 同種の文字が続く限り 1 トークンとして取る
            while (i < text.size()) {
                size_t l = Utf8Len(static_cast<unsigned char>(text[i]));
                if (i + l > text.size()) break;
                uint32_t c = DecodeUtf8(text, i, l);
                if (kata && IsKatakana(c)) { i += l; ++cpCount; continue; }
                if (latin && IsLatin(c)) { i += l; ++cpCount; continue; }
                break;
            }
            // 1 文字（長音符のみ等）はノイズになりやすいので 2 文字以上を採用
            if (cpCount >= 2) tokens.push_back(text.substr(startByte, i - startByte));
        } else {
            i += len;
        }
    }
    return tokens;
}

void CorrectionMemory::Load(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    path_ = path;
    names_.clear();
    rules_.clear();
    pending_.clear();

    std::ifstream file(path);
    if (!file.is_open()) return;

    try {
        nlohmann::json data;
        file >> data;
        if (data.contains("names") && data["names"].is_object()) {
            for (auto it = data["names"].begin(); it != data["names"].end(); ++it) {
                names_[it.key()] = it.value().get<int>();
            }
        }
        if (data.contains("rules") && data["rules"].is_array()) {
            for (const auto& r : data["rules"]) {
                Rule rule;
                rule.wrong = r.value("wrong", "");
                rule.right = r.value("right", "");
                rule.count = r.value("count", 1);
                if (!rule.wrong.empty() && !rule.right.empty()) rules_.push_back(rule);
            }
        }
        if (data.contains("pending") && data["pending"].is_array()) {
            for (const auto& p : data["pending"]) {
                pending_.emplace_back(p.value("raw", ""), p.value("corrected", ""));
            }
        }
    } catch (...) {
        // 壊れていたら空で続行
    }
}

void CorrectionMemory::Save() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (path_.empty()) return;

    nlohmann::json data;
    data["names"] = nlohmann::json::object();
    for (const auto& [name, count] : names_) {
        data["names"][name] = count;
    }
    data["rules"] = nlohmann::json::array();
    for (const auto& r : rules_) {
        data["rules"].push_back({{"wrong", r.wrong}, {"right", r.right}, {"count", r.count}});
    }
    data["pending"] = nlohmann::json::array();
    for (const auto& p : pending_) {
        data["pending"].push_back({{"raw", p.first}, {"corrected", p.second}});
    }

    std::ofstream file(path_);
    if (file.is_open()) {
        file << data.dump(2);
    }
}

void CorrectionMemory::MergeName(const std::string& name, int add) {
    names_[name] += add;
}

void CorrectionMemory::MergeRule(const std::string& wrong, const std::string& right, int add) {
    for (auto& r : rules_) {
        if (r.wrong == wrong && r.right == right) {
            r.count += add;
            return;
        }
    }
    rules_.push_back({wrong, right, add});
}

void CorrectionMemory::HeuristicLearn(const std::string& raw, const std::string& corrected) {
    // 校正後にだけ現れたカタカナ/英字トークンを固有名詞候補として加算
    std::vector<std::string> rawTokens = ExtractTokens(raw);
    std::unordered_set<std::string> rawSet(rawTokens.begin(), rawTokens.end());

    for (const auto& tok : ExtractTokens(corrected)) {
        if (rawSet.find(tok) == rawSet.end()) {
            MergeName(tok, 1);
        }
    }
}

void CorrectionMemory::RecordPair(const std::string& raw, const std::string& corrected) {
    if (raw.empty() || corrected.empty() || raw == corrected) return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        HeuristicLearn(raw, corrected);
        pending_.emplace_back(raw, corrected);
    }
    Save();
}

std::vector<std::string> CorrectionMemory::GetActiveNames(int minCount) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    for (const auto& [name, count] : names_) {
        if (count >= minCount) out.push_back(name);
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> CorrectionMemory::GetActiveRules(int minCount, int maxN) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Rule> sorted = rules_;
    std::sort(sorted.begin(), sorted.end(), [](const Rule& a, const Rule& b) { return a.count > b.count; });

    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& r : sorted) {
        if (r.count < minCount) continue;
        out.emplace_back(r.wrong, r.right);
        if (static_cast<int>(out.size()) >= maxN) break;
    }
    return out;
}

bool CorrectionMemory::ShouldConsolidate(int threshold) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(pending_.size()) >= threshold;
}

int CorrectionMemory::NameCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(names_.size());
}

int CorrectionMemory::RuleCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(rules_.size());
}

int CorrectionMemory::PendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(pending_.size());
}

std::future<bool> CorrectionMemory::ConsolidateAsync(const std::string& apiKey, const std::string& model) {
    // 未集約ペアを取り出す（失敗時は戻す）
    std::vector<std::pair<std::string, std::string>> batch;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        batch.swap(pending_);
    }

    return std::async(std::launch::async, [this, apiKey, model, batch]() -> bool {
        if (apiKey.empty() || batch.empty()) {
            // 戻す
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& p : batch) pending_.push_back(p);
            return false;
        }

        std::string user = "次は音声認識(raw)とその校正後(corrected)のペアです。\n";
        for (const auto& p : batch) {
            user += "- raw: " + p.first + "\n  corrected: " + p.second + "\n";
        }
        user += "\nこれらから、繰り返し現れる『固有名詞』と『誤り→正しい綴りの置換ルール』を抽出してください。";

        const char* sys =
            "あなたは音声認識の校正ログから学習するアシスタントです。"
            "与えられた (raw, corrected) ペア群から、今後の認識・校正に役立つ"
            "『固有名詞』と『誤り→正しい綴りの置換ルール』を抽出してください。"
            "一般的すぎる語や一度きり・偶発的な違いは含めないこと。"
            "出力は厳密な JSON のみ: "
            "{\"names\":[\"固有名詞\"...],\"rules\":[{\"wrong\":\"誤\",\"right\":\"正\"}...]}";

        LLMClient client;
        client.SetApiKey(apiKey);
        if (!model.empty()) client.SetModel(model);
        client.SetSystemPrompt(sys);
        client.SetMaxTokens(1024);
        client.AddMessage("user", user);

        LLMResponse res = client.SendBlocking();
        if (!res.success) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& p : batch) pending_.push_back(p);
            return false;
        }

        std::string jsonStr = ExtractJsonObject(res.content);
        if (jsonStr.empty()) {
            // 解析できなくても batch は破棄（無限リトライ回避）
            return false;
        }

        try {
            nlohmann::json out = nlohmann::json::parse(jsonStr);
            std::lock_guard<std::mutex> lock(mutex_);
            if (out.contains("names") && out["names"].is_array()) {
                for (const auto& n : out["names"]) {
                    if (n.is_string()) MergeName(n.get<std::string>(), 2); // LLM 確認分は重め
                }
            }
            if (out.contains("rules") && out["rules"].is_array()) {
                for (const auto& r : out["rules"]) {
                    std::string w = r.value("wrong", "");
                    std::string c = r.value("right", "");
                    if (!w.empty() && !c.empty()) MergeRule(w, c, 2);
                }
            }
        } catch (...) {
            return false;
        }

        Save();
        return true;
    });
}
