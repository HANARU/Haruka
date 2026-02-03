#pragma once
#include <functional>
#include <type_traits>
#include <string>

// ── Attr<T> (Slate의 TAttribute<T>) ──────────────────────────────────────────
// 고정값 또는 람다 바인딩을 통해 동적으로 평가되는 속성
template<typename T>
class Attr {
public:
    Attr() = default;
    Attr(T value)                     : value_(std::move(value)) {}
    Attr(std::function<T()> getter)   : getter_(std::move(getter)) {}

    T get() const { return getter_ ? getter_() : value_; }

    void set(T v)                     { value_ = std::move(v); getter_ = nullptr; }
    void bind(std::function<T()> fn)  { getter_ = std::move(fn); }

    bool isBound() const { return static_cast<bool>(getter_); }

    // 암묵적 변환 (표현식에서 바로 T로 쓸 수 있게)
    operator T() const { return get(); }

private:
    T                   value_  {};
    std::function<T()>  getter_ {};
};

// 자주 쓰는 특수화
using AttrBool  = Attr<bool>;
using AttrFloat = Attr<float>;
using AttrStr   = Attr<std::string>;
