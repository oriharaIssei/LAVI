#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

/// <summary>
/// Windows 位置情報（C++/WinRT, Wi-Fi/IP 統合測位）で現在地の緯度経度を取得し、
/// OSM Nominatim で地名へ逆ジオコーディングする。取得はバックグラウンドで 1 回だけ行い
/// 結果をキャッシュする。権限拒否・未対応・ネットワーク失敗などでは valid=false のままになり、
/// 呼び出し側は「位置情報なし」として動作を継続できる（グレースフル劣化）。
///
/// ワーカースレッドは「デタッチ＋共有状態(shared_ptr)」で動かす。こうすることで、
/// 取得処理が万一ハングしてもアプリ終了時に join でブロックせず、必ず終了できる。
/// （共有状態はスレッドも shared_ptr を保持するため、本オブジェクト破棄後の書き込みも安全。）
/// ※ Windows 設定で「位置情報サービス」と「デスクトップアプリの位置情報アクセス」が
///   ON でないと取得できない。
/// </summary>
class LocationProvider {
public:
    struct Location {
        bool        valid = false;
        double      lat   = 0.0;
        double      lon   = 0.0;
        std::string placeName; // 例: "東京都新宿区"（逆ジオコーディングできなければ空）
    };

    // 非同期取得を開始する（多重呼び出しは無視）。
    void Start();

    // 現在のキャッシュを返す（取得前/失敗時は valid=false）。
    Location Get() const;

    // プロンプト注入用テキスト "## 現在地: ...\n" を返す（未取得なら空文字）。
    std::string BuildContext() const;

private:
    struct State {
        std::mutex mtx;
        Location   loc;
    };

    static void        Fetch(std::shared_ptr<State> st);
    static bool        QueryWinRtCoord(double& lat, double& lon);
    static std::string ReverseGeocode(double lat, double lon);

    std::shared_ptr<State> state_ = std::make_shared<State>();
    std::atomic<bool>      started_{false};
};
