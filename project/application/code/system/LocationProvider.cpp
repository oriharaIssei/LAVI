#include "LocationProvider.h"

#include <chrono>
#include <cstdio>
#include <thread>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

// C++/WinRT 位置情報。RuntimeObject.lib のリンクが必要（premake で links に追加済み）。
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Geolocation.h>

namespace {
size_t WriteCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string HttpGetJson(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return std::string();
    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Accept-Language: ja,en;q=0.8");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // Nominatim の利用規約: アプリを識別する User-Agent を必須とする。
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "LAVI/1.0 (desktop AI assistant)");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L); // ワーカースレッドでのタイムアウト確実化（必須）
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    const CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) return std::string();
    return response;
}
} // namespace

void LocationProvider::Start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) return; // 多重起動防止
    // デタッチして動かす。共有状態(state_)を値キャプチャするので、本オブジェクトが先に
    // 破棄されてもスレッド側の書き込みは安全（State は shared_ptr で生存）。
    std::shared_ptr<State> st = state_;
    std::thread(&LocationProvider::Fetch, st).detach();
}

LocationProvider::Location LocationProvider::Get() const {
    std::lock_guard<std::mutex> lock(state_->mtx);
    return state_->loc;
}

std::string LocationProvider::BuildContext() const {
    Location l = Get();
    if (!l.valid) return std::string();
    char buf[256];
    if (!l.placeName.empty()) {
        std::snprintf(buf, sizeof(buf), "## 現在地: %s (緯度%.4f, 経度%.4f)\n",
                      l.placeName.c_str(), l.lat, l.lon);
    } else {
        std::snprintf(buf, sizeof(buf), "## 現在地: 緯度%.4f, 経度%.4f\n", l.lat, l.lon);
    }
    return std::string(buf);
}

void LocationProvider::Fetch(std::shared_ptr<State> st) {
    double lat = 0.0, lon = 0.0;
    if (!QueryWinRtCoord(lat, lon)) return; // 位置取得失敗 → valid=false のまま

    std::string place = ReverseGeocode(lat, lon); // 失敗しても空のまま続行

    std::lock_guard<std::mutex> lock(st->mtx);
    st->loc.valid     = true;
    st->loc.lat       = lat;
    st->loc.lon       = lon;
    st->loc.placeName = place;
}

bool LocationProvider::QueryWinRtCoord(double& lat, double& lon) {
    using namespace winrt::Windows::Devices::Geolocation;
    try {
        winrt::init_apartment();
        // デスクトップアプリ（CoreWindow 無し）では RequestAccessAsync が例外/未対応のことがある。
        // 失敗しても続行し、実取得で権限を判定する。
        try {
            auto status = Geolocator::RequestAccessAsync().get();
            if (status == GeolocationAccessStatus::Denied) return false;
        } catch (...) {
            // 続行
        }
        Geolocator locator;
        locator.DesiredAccuracy(PositionAccuracy::Default);
        // maximumAge=1時間（キャッシュ許容） / timeout=10秒。
        Geoposition pos = locator
            .GetGeopositionAsync(std::chrono::hours(1), std::chrono::seconds(10))
            .get();
        auto p = pos.Coordinate().Point().Position();
        lat = p.Latitude;
        lon = p.Longitude;
        return true;
    } catch (...) {
        return false;
    }
}

std::string LocationProvider::ReverseGeocode(double lat, double lon) {
    char url[256];
    std::snprintf(url, sizeof(url),
        "https://nominatim.openstreetmap.org/reverse?format=jsonv2&lat=%.6f&lon=%.6f&accept-language=ja&zoom=12",
        lat, lon);
    const std::string body = HttpGetJson(url);
    if (body.empty()) return std::string();
    try {
        auto j = nlohmann::json::parse(body);
        if (!j.contains("address")) return j.value("display_name", std::string());
        const auto& a = j["address"];
        auto pick = [&a](std::initializer_list<const char*> keys) -> std::string {
            for (const char* k : keys) {
                if (a.contains(k) && a[k].is_string()) return a[k].get<std::string>();
            }
            return std::string();
        };
        const std::string pref = pick({ "state", "province" });               // 都道府県
        const std::string city = pick({ "city", "county", "town", "village" }); // 市/郡
        const std::string ward = pick({ "city_district", "suburb", "ward" });  // 区/地区
        std::string place = pref + city + ward;
        if (place.empty()) place = j.value("display_name", std::string());
        return place;
    } catch (...) {
        return std::string();
    }
}
