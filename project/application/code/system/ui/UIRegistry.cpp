#include "UIRegistry.h"

//========================================================================================
// UIActionRegistry
//========================================================================================
UIActionRegistry& UIActionRegistry::Get() {
    static UIActionRegistry instance;
    return instance;
}

void UIActionRegistry::Register(const std::string& _id, std::function<void()> _fn) {
    actions_[_id] = std::move(_fn);
}

void UIActionRegistry::Unregister(const std::string& _id) {
    actions_.erase(_id);
}

bool UIActionRegistry::Invoke(const std::string& _id) const {
    auto itr = actions_.find(_id);
    if (itr == actions_.end() || !itr->second) {
        return false;
    }
    itr->second();
    return true;
}

bool UIActionRegistry::Has(const std::string& _id) const {
    return actions_.find(_id) != actions_.end();
}

void UIActionRegistry::RegisterText(const std::string& _id, std::function<void(const std::string&)> _fn) {
    textActions_[_id] = std::move(_fn);
}

void UIActionRegistry::UnregisterText(const std::string& _id) {
    textActions_.erase(_id);
}

bool UIActionRegistry::InvokeText(const std::string& _id, const std::string& _arg) const {
    auto itr = textActions_.find(_id);
    if (itr == textActions_.end() || !itr->second) {
        return false;
    }
    itr->second(_arg);
    return true;
}

bool UIActionRegistry::HasText(const std::string& _id) const {
    return textActions_.find(_id) != textActions_.end();
}

void UIActionRegistry::Clear() {
    actions_.clear();
    textActions_.clear();
}

//========================================================================================
// UIBindingRegistry
//========================================================================================
UIBindingRegistry& UIBindingRegistry::Get() {
    static UIBindingRegistry instance;
    return instance;
}

void UIBindingRegistry::RegisterFloat(const std::string& _key, std::function<float()> _get,
                                      std::function<void(float)> _set) {
    bindings_[_key] = FloatBinding{std::move(_get), std::move(_set)};
}

void UIBindingRegistry::BindFloatPtr(const std::string& _key, float* _ptr) {
    if (!_ptr) {
        return;
    }
    bindings_[_key] = FloatBinding{
        [_ptr]() { return *_ptr; },
        [_ptr](float v) { *_ptr = v; }};
}

void UIBindingRegistry::Unregister(const std::string& _key) {
    bindings_.erase(_key);
}

bool UIBindingRegistry::Has(const std::string& _key) const {
    return bindings_.find(_key) != bindings_.end();
}

float UIBindingRegistry::GetValue(const std::string& _key, float _fallback) const {
    auto itr = bindings_.find(_key);
    if (itr == bindings_.end() || !itr->second.get) {
        return _fallback;
    }
    return itr->second.get();
}

bool UIBindingRegistry::SetValue(const std::string& _key, float _value) const {
    auto itr = bindings_.find(_key);
    if (itr == bindings_.end() || !itr->second.set) {
        return false;
    }
    itr->second.set(_value);
    return true;
}

void UIBindingRegistry::Clear() {
    bindings_.clear();
}
