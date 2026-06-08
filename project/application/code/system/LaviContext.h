#pragma once

struct SharedMediaContext;

/// <summary>
/// LAVI の各機能（音声・映像・会話・記憶・RAG 等）が共有する状態 SharedMediaContext への
/// 単一アクセス点（シングルトンサービス）。
///
/// ECS 化に伴い、分解された各 ISystem はこのサービス経由で共有状態を参照する。
/// 旧来は MediaCaptureDemoSystem が unique_ptr で所有していたが、複数 System から
/// 触れるよう所有を一元化した。インスタンスはプログラム存続期間まで生存する。
/// </summary>
class LaviContext {
public:
    static SharedMediaContext& Get();
};
