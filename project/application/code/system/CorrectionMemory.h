#pragma once

#include <future>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/// <summary>
/// 校正の自己進化ストア。校正ペア (raw, corrected) を蓄積し、
/// ・ヒューリスティック（カタカナ/英字トークンの差分）で固有名詞候補を学習
/// ・一定数たまったら LLM で一括集約し、固有名詞と誤り→正しい綴りのルールを抽出
/// 学習結果は JSON に永続化し、語彙(Whisper/校正プロンプト)と few-shot 例として反映する。
/// </summary>
class CorrectionMemory {
public:
    void Load(const std::string& path);
    void Save() const;

    /// <summary>校正ペアを記録（ヒューリスティック学習＋集約用に蓄積）。</summary>
    void RecordPair(const std::string& raw, const std::string& corrected);

    /// <summary>出現回数が minCount 以上の学習済み固有名詞。</summary>
    std::vector<std::string> GetActiveNames(int minCount = 1) const;

    /// <summary>出現回数が minCount 以上の誤りルール（few-shot 用、最大 maxN 件）。</summary>
    std::vector<std::pair<std::string, std::string>> GetActiveRules(int minCount = 1, int maxN = 20) const;

    /// <summary>未集約ペアが閾値以上たまっているか。</summary>
    bool ShouldConsolidate(int threshold = 5) const;

    /// <summary>蓄積ペアを LLM で集約しマージする（非同期）。成功で true。</summary>
    std::future<bool> ConsolidateAsync(const std::string& apiKey, const std::string& model);

    // 統計（UI 表示用）
    int NameCount() const;
    int RuleCount() const;
    int PendingCount() const;

private:
    struct Rule {
        std::string wrong;
        std::string right;
        int count = 0;
    };

    void HeuristicLearn(const std::string& raw, const std::string& corrected);
    void MergeName(const std::string& name, int add);
    void MergeRule(const std::string& wrong, const std::string& right, int add);

    static std::vector<std::string> ExtractTokens(const std::string& text); // カタカナ/英字の連続

    mutable std::mutex mutex_;
    std::unordered_map<std::string, int> names_;
    std::vector<Rule> rules_;
    std::vector<std::pair<std::string, std::string>> pending_; // 未集約 (raw, corrected)
    std::string path_;
};
