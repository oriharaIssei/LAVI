#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;
class SentenceEmbedding;

/// <summary>
/// 検索でヒットした知識チャンク（プロンプト注入・UI 表示用）。
/// </summary>
struct KnowledgeChunk {
    int64_t     id = 0;
    std::string source; // 出典（ファイル名・タイトル・URL 等）
    std::string text;   // チャンク本文
    float       score = 0.0f; // クエリとのコサイン類似（検索時のみ有効）
};

/// <summary>
/// 知識ベース RAG（知識不足対策）。
/// 文書をチャンク分割し SentenceEmbedding で埋め込み → SQLite にベクトル保存。
/// 質問を埋め込みコサイン類似で関連チャンクを取り出し、プロンプトに注入する。
/// 重みの再学習なしで一般知識を補う。SentenceEmbedding は非所有（共有インスタンス）。
/// </summary>
class KnowledgeBase {
public:
    bool Initialize(const std::string& dbPath, SentenceEmbedding* emb);
    void Finalize();
    bool IsReady() const { return db_ != nullptr && emb_ != nullptr; }

    // --- 知識ソースの管理（追加・更新・削除）---
    /// 文書を追加: チャンク分割 → 埋め込み → 保存。追加できたチャンク数を返す。
    /// 同一 source が既にあれば置き換える（更新）。
    int  AddDocument(const std::string& source, const std::string& text);
    /// テキストファイル（.txt / .md）を読み込んで追加。source はファイル名。
    int  AddFile(const std::string& filePath);
    /// フォルダ内の .txt / .md を走査し、未登録のものだけ取り込む。取り込んだファイル数を返す。
    int  ImportFolder(const std::string& dirPath);
    bool RemoveSource(const std::string& source);
    void Clear();

    // --- 検索・注入 ---
    /// 上位 topK の関連チャンク（minScore 以上）をスコア降順で返す。
    std::vector<KnowledgeChunk> Search(const std::string& query, int topK = 3, float minScore = 0.30f) const;
    /// プロンプト注入用テキスト（"## 知識ベース ..." 形式）。ヒットなしなら空文字。
    std::string BuildContext(const std::string& query, int topK = 3, float minScore = 0.30f) const;

    // --- 統計・一覧 ---
    int ChunkCount() const;
    std::vector<std::string> ListSources() const;

    // チャンク分割（公開: テスト・調整用）。UTF-8 境界・文末記号を尊重する。
    static std::vector<std::string> ChunkText(const std::string& text,
                                              size_t maxBytes = 700,
                                              int overlapSegments = 1);

private:
    bool   InsertChunk(const std::string& source, const std::string& text,
                       const std::vector<float>& emb);

    sqlite3*           db_  = nullptr;
    SentenceEmbedding* emb_ = nullptr; // 非所有
    mutable std::mutex mutex_;
};
