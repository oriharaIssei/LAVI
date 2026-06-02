#include "KnowledgeBase.h"

#include "SentenceEmbedding.h"

#include <sqlite3.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

std::string SafeText(const unsigned char* t) {
    return t ? reinterpret_cast<const char*>(t) : std::string();
}

std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n　");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t\r\n　");
    return s.substr(b, e - b + 1);
}

// 文末記号・改行で文を分割（区切り文字は前の文に含める）。UTF-8 安全。
std::vector<std::string> SplitSentences(const std::string& s) {
    static const std::vector<std::string> delims = {"。", "！", "？", "．", "\n"}; // 。！？．\n
    std::vector<std::string> out;
    size_t start = 0, i = 0;
    while (i < s.size()) {
        bool matched = false;
        for (const auto& d : delims) {
            if (s.compare(i, d.size(), d) == 0) {
                out.push_back(s.substr(start, i + d.size() - start));
                i += d.size();
                start = i;
                matched = true;
                break;
            }
        }
        if (matched) continue;
        const char c = s[i];
        if (c == '.' || c == '!' || c == '?') {
            out.push_back(s.substr(start, i + 1 - start));
            start = ++i;
        } else {
            ++i;
        }
    }
    if (start < s.size()) out.push_back(s.substr(start));
    return out;
}

// 長すぎる断片を UTF-8 コードポイント境界で強制分割。
void HardSplit(const std::string& seg, size_t maxBytes, std::vector<std::string>& out) {
    size_t p = 0;
    while (p < seg.size()) {
        size_t end = (std::min)(p + maxBytes, seg.size());
        while (end < seg.size() && (static_cast<unsigned char>(seg[end]) & 0xC0) == 0x80) --end;
        if (end <= p) end = (std::min)(p + maxBytes, seg.size()); // 念のため前進保証
        out.push_back(seg.substr(p, end - p));
        p = end;
    }
}

std::vector<float> BlobToVec(const void* blob, int bytes) {
    std::vector<float> v(static_cast<size_t>(bytes) / sizeof(float));
    if (blob && bytes > 0) std::memcpy(v.data(), blob, static_cast<size_t>(bytes));
    return v;
}

} // namespace

std::vector<std::string> KnowledgeBase::ChunkText(const std::string& text, size_t maxBytes, int overlapSegments) {
    const std::string trimmed = Trim(text);
    if (trimmed.empty()) return {};
    if (maxBytes < 64) maxBytes = 64;

    std::vector<std::string> segs;
    for (const auto& seg : SplitSentences(trimmed)) {
        if (seg.size() <= maxBytes) segs.push_back(seg);
        else HardSplit(seg, maxBytes, segs);
    }
    if (segs.empty()) return {};

    const int overlap = (overlapSegments < 0) ? 0 : overlapSegments;
    std::vector<std::string> chunks;
    size_t i = 0;
    while (i < segs.size()) {
        std::string cur;
        size_t j = i;
        while (j < segs.size() && (cur.empty() || cur.size() + segs[j].size() <= maxBytes)) {
            cur += segs[j];
            ++j;
        }
        const std::string c = Trim(cur);
        if (!c.empty()) chunks.push_back(c);
        if (j >= segs.size()) break;
        size_t nextI = (j > static_cast<size_t>(overlap)) ? j - overlap : j;
        if (nextI <= i) nextI = j; // 前進保証（無限ループ防止）
        i = nextI;
    }
    return chunks;
}

bool KnowledgeBase::Initialize(const std::string& dbPath, SentenceEmbedding* emb) {
    std::lock_guard<std::mutex> lock(mutex_);
    emb_ = emb;

    std::error_code ec;
    const std::filesystem::path p(dbPath);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);

    if (sqlite3_open(dbPath.c_str(), &db_) != SQLITE_OK) {
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
        return false;
    }

    const char* schema =
        "CREATE TABLE IF NOT EXISTS knowledge_chunks ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  source TEXT NOT NULL,"
        "  text TEXT NOT NULL,"
        "  dim INTEGER NOT NULL,"
        "  embedding BLOB NOT NULL,"
        "  created TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_kb_source ON knowledge_chunks(source);";
    char* err = nullptr;
    sqlite3_exec(db_, schema, nullptr, nullptr, &err);
    if (err) { sqlite3_free(err); }
    return true;
}

void KnowledgeBase::Finalize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
    emb_ = nullptr;
}

bool KnowledgeBase::InsertChunk(const std::string& source, const std::string& text,
                                const std::vector<float>& emb) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO knowledge_chunks (source, text, dim, embedding, created) "
        "VALUES (?1, ?2, ?3, ?4, datetime('now'))";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, static_cast<int>(emb.size()));
    sqlite3_bind_blob(stmt, 4, emb.data(),
                      static_cast<int>(emb.size() * sizeof(float)), SQLITE_TRANSIENT);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

int KnowledgeBase::AddDocument(const std::string& source, const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ || !emb_ || !emb_->IsReady()) return 0;

    // 同一 source は置き換え（更新）
    {
        sqlite3_stmt* del = nullptr;
        if (sqlite3_prepare_v2(db_, "DELETE FROM knowledge_chunks WHERE source = ?1", -1, &del, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(del, 1, source.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(del);
            sqlite3_finalize(del);
        }
    }

    const std::vector<std::string> chunks = ChunkText(text);
    int added = 0;
    sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr);
    for (const auto& chunk : chunks) {
        std::vector<float> v = emb_->Encode(chunk);
        if (v.empty()) continue;
        if (InsertChunk(source, chunk, v)) ++added;
    }
    sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    return added;
}

int KnowledgeBase::AddFile(const std::string& filePath) {
    std::ifstream f(filePath, std::ios::binary);
    if (!f.is_open()) return 0;
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string source = std::filesystem::path(filePath).filename().string();
    return AddDocument(source, ss.str());
}

int KnowledgeBase::ImportFolder(const std::string& dirPath) {
    std::error_code ec;
    if (!std::filesystem::exists(dirPath, ec)) {
        std::filesystem::create_directories(dirPath, ec);
        return 0;
    }
    // 既存 source 集合を取得
    std::vector<std::string> existing = ListSources();

    int files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension().string();
        if (ext != ".txt" && ext != ".md") continue;
        const std::string name = entry.path().filename().string();
        if (std::find(existing.begin(), existing.end(), name) != existing.end()) continue;
        if (AddFile(entry.path().string()) > 0) ++files;
    }
    return files;
}

bool KnowledgeBase::RemoveSource(const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM knowledge_chunks WHERE source = ?1", -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_text(stmt, 1, source.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

void KnowledgeBase::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return;
    sqlite3_exec(db_, "DELETE FROM knowledge_chunks", nullptr, nullptr, nullptr);
}

std::vector<KnowledgeChunk> KnowledgeBase::Search(const std::string& query, int topK, float minScore) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<KnowledgeChunk> results;
    if (!db_ || !emb_ || !emb_->IsReady() || Trim(query).empty()) return results;

    const std::vector<float> q = emb_->Encode(query);
    if (q.empty()) return results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, source, text, embedding FROM knowledge_chunks";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    std::vector<KnowledgeChunk> scored;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        KnowledgeChunk c;
        c.id     = sqlite3_column_int64(stmt, 0);
        c.source = SafeText(sqlite3_column_text(stmt, 1));
        c.text   = SafeText(sqlite3_column_text(stmt, 2));
        const void* blob = sqlite3_column_blob(stmt, 3);
        const int   bytes = sqlite3_column_bytes(stmt, 3);
        const std::vector<float> v = BlobToVec(blob, bytes);
        if (v.size() != q.size()) continue; // 次元不一致（モデル変更等）はスキップ
        c.score = SentenceEmbedding::CosineSimilarity(q, v);
        if (c.score >= minScore) scored.push_back(std::move(c));
    }
    sqlite3_finalize(stmt);

    std::sort(scored.begin(), scored.end(),
              [](const KnowledgeChunk& a, const KnowledgeChunk& b) { return a.score > b.score; });
    if (topK > 0 && static_cast<int>(scored.size()) > topK) scored.resize(topK);
    return scored;
}

std::string KnowledgeBase::BuildContext(const std::string& query, int topK, float minScore) const {
    const std::vector<KnowledgeChunk> hits = Search(query, topK, minScore);
    if (hits.empty()) return std::string();

    std::ostringstream ctx;
    ctx << "## 知識ベース（参考情報）\n"; // ## 知識ベース（参考情報）
    for (const auto& h : hits) {
        ctx << "- " << h.text;
        if (!h.source.empty()) ctx << "（出典: " << h.source << "）"; // （出典: ...）
        ctx << "\n";
    }
    return ctx.str();
}

int KnowledgeBase::ChunkCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return 0;
    sqlite3_stmt* stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM knowledge_chunks", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return count;
}

std::vector<std::string> KnowledgeBase::ListSources() const {
    std::vector<std::string> sources;
    if (!db_) return sources;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT DISTINCT source FROM knowledge_chunks ORDER BY source", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            sources.push_back(SafeText(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
    }
    return sources;
}
