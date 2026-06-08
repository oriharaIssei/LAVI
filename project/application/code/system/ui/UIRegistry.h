#pragma once

#include <functional>
#include <string>
#include <unordered_map>

/// <summary>
/// UI ウィジェット（JSON データ）と実際の振る舞い/状態を疎結合につなぐレジストリ群（シングルトン）。
///
/// - UIActionRegistry: ボタンの actionId -> 実行ラムダ。各ロジック System が Initialize で名前付き
///   アクションを登録し、UIInputSystem がクリック時に発火する（例: "mic.start", "llm.send"）。
/// - UIBindingRegistry: スライダーの bindKey -> float の get/set。config / 状態フィールドへ読み書きする
///   （例: "turn.startThreshold"）。
///
/// データ(JSON)は ID 参照のみを持ち、振る舞いは登録側に委ねる（ハードコード回避・動的解決）。
/// MediaCaptureDemoSystem の ImGui 即時 UI を ECS リテインドモード UI へ移行するための土台。
/// </summary>
class UIActionRegistry {
public:
    static UIActionRegistry& Get();

    /// <summary>名前付きアクションを登録（既存は上書き）</summary>
    void Register(const std::string& _id, std::function<void()> _fn);
    /// <summary>登録解除</summary>
    void Unregister(const std::string& _id);
    /// <summary>発火（未登録なら false）</summary>
    bool Invoke(const std::string& _id) const;
    bool Has(const std::string& _id) const;

    /// <summary>文字列引数を取るアクションを登録（テキスト入力の確定など。既存は上書き）</summary>
    void RegisterText(const std::string& _id, std::function<void(const std::string&)> _fn);
    void UnregisterText(const std::string& _id);
    /// <summary>文字列付きで発火（未登録なら false）</summary>
    bool InvokeText(const std::string& _id, const std::string& _arg) const;
    bool HasText(const std::string& _id) const;

    void Clear();

private:
    UIActionRegistry()                                   = default;
    UIActionRegistry(const UIActionRegistry&)            = delete;
    UIActionRegistry& operator=(const UIActionRegistry&) = delete;

    std::unordered_map<std::string, std::function<void()>> actions_;
    std::unordered_map<std::string, std::function<void(const std::string&)>> textActions_;
};

class UIBindingRegistry {
public:
    /// <summary>float 値の get/set ペア</summary>
    struct FloatBinding {
        std::function<float()> get;
        std::function<void(float)> set;
    };

    static UIBindingRegistry& Get();

    /// <summary>get/set 両方を持つバインドを登録</summary>
    void RegisterFloat(const std::string& _key, std::function<float()> _get, std::function<void(float)> _set);
    /// <summary>生ポインタを直接束ねる簡便登録</summary>
    void BindFloatPtr(const std::string& _key, float* _ptr);
    void Unregister(const std::string& _key);
    bool Has(const std::string& _key) const;

    /// <summary>現在値を取得（未登録なら _fallback）</summary>
    float GetValue(const std::string& _key, float _fallback = 0.0f) const;
    /// <summary>値を書き込む（未登録なら false）</summary>
    bool SetValue(const std::string& _key, float _value) const;
    void Clear();

private:
    UIBindingRegistry()                                    = default;
    UIBindingRegistry(const UIBindingRegistry&)            = delete;
    UIBindingRegistry& operator=(const UIBindingRegistry&) = delete;

    std::unordered_map<std::string, FloatBinding> bindings_;
};
