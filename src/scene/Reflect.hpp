#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

namespace PropColor {
    constexpr glm::vec4 Red    {0.9f, 0.3f, 0.3f, 1.f};
    constexpr glm::vec4 Green  {0.3f, 0.9f, 0.3f, 1.f};
    constexpr glm::vec4 Blue   {0.3f, 0.6f, 1.0f, 1.f};
    constexpr glm::vec4 Neutral{0.65f, 0.65f, 0.70f, 1.f};
}

// 숫자 속성의 단위 종류. 디테일 패널의 숫자 칸이 끌기 감도와 표시 자릿수를 이것으로 정합니다(DEF-46).
//   Scalar = step 그대로(px 당), 소수 넷째 자리까지
//   Offset = 위치처럼 0 을 지나는 거리: 1cm/px, 다섯째 자리까지
//   Size   = 늘 양수인 크기(배율·반경·질량 등): 끌기 시작값의 1%/px(최소 step 의 1/10), 다섯째 자리까지
//   Angle  = 각도(도): 0.5°/px, 둘째 자리까지
//   Ratio  = 0~1 비율(색 성분·마찰 등): 0.005/px, 넷째 자리까지
enum class PropUnit { Scalar, Offset, Size, Angle, Ratio, Integer };

// ── 디테일 패널 속성 설명(반사) ───────────────────────────────────────────────────────────────────────────────
// 컴포넌트가 buildProps 로 내놓는 칸 하나입니다. 값은 게터 · 세터로만 읽고 씁니다(씬 파일 직렬화는 writeFields · readField 가
// 따로 맡으므로 이 구조를 바꿔도 .hscene 형식은 그대로입니다). 에디터는 칸 이름(label) · 종류로 값을 기억해 Undo 합니다
// (editor/EditorCommands.hpp 의 PropValue).
//
// 종류(H3a, 2026-11-22: String · Vec3 · Color · AssetPath 를 더함. 값 번호는 뒤에 붙여 예전 값이 그대로)
//   Float · Toggle · Enum · ActorRef : 예전 그대로
//   String    : 한 줄 글(getS · setS)
//   Vec3      : 성분 셋(getV · setV 의 xyz). 끌기 감도 · 자릿수는 step · unit 을 세 성분이 함께 씀
//   Color     : 색 성분 셋(RGB) 또는 넷(RGBA, components = 4). 값은 컴포넌트가 저장하는 그대로 보입니다. 라이트 색처럼 선형
//               값이면 srgb = false, 기본 도형 재질처럼 sRGB 로 저장한 값이면 srgb = true(견본을 그릴 때만 선형으로 바꿈)
//   AssetPath : 에셋 논리 경로(getS · setS, assetType = 고를 종류). 칸 · 고르기는 H3b 에서 만들고, 지금은 글로만 보입니다
//
// 기본값(H3a): hasDefault 가 참이면 그 종류의 def* 값이 "컴포넌트를 새로 만들었을 때의 값"입니다. 디테일 패널은 값이 기본값과
// 다르면 행 오른쪽에 되돌리기 화살표를 보이고, 누르면 기본값을 세터로 넣습니다(UE 의 Reset to Default). 기본값은 생성자 기본값이
// 분명한 칸에만 둡니다(목록에서 고르는 클립 · 내용 이름처럼 데이터에 따라 바뀌는 칸은 두지 않음). 정하는 법:
//     makePropFloat(...).withDefault(3.f)            // Float · Toggle(true/false) · Enum(번호 또는 열거값) · ActorRef(0 = 없음)
//     makePropVec3(...).withDefault(glm::vec3(1.f))  // Vec3 · Color
//     makePropString(...).withDefault("")            // String · AssetPath
struct PropDesc {
    // ActorRef(참조 체계 R-7): 다른 액터를 가리키는 값. 디테일 패널은 대상 이름을 보이고 누르면 액터 목록에서 고릅니다.
    enum Kind { Float, Toggle, Enum, ActorRef, String, Vec3, Color, AssetPath };

    const char*                   label   = "";
    glm::vec4                     color   {PropColor::Neutral};
    Kind                          kind    {Float};
    float                         step    {0.01f};
    PropUnit                      unit    {PropUnit::Scalar};
    std::function<float()>        getF;
    std::function<void(float)>    setF;
    std::function<bool()>         getB;
    std::function<void(bool)>     setB;
    std::vector<const char*>      options;   // Enum kind
    std::function<int()>          getI;
    std::function<void(int)>      setI;
    std::function<uint64_t()>     getG;      // ActorRef kind: 대상 GUID 값(0 = 없음)
    std::function<void(uint64_t)> setG;
    // String · AssetPath
    std::function<std::string()>              getS;
    std::function<void(const std::string&)>   setS;
    const char*                               assetType = "";   // AssetPath: 고를 에셋 종류(콘텐츠 브라우저 종류 이름, 비면 모두)
    // Vec3 · Color(성분은 xyz 또는 xyzw)
    std::function<glm::vec4()>                getV;
    std::function<void(const glm::vec4&)>     setV;
    int                                       components = 3;   // Vec3 = 3, Color = 3(RGB) · 4(RGBA)
    bool                                      srgb       = false;   // Color: 값이 sRGB 로 저장된 값인가

    // ── 기본값(H3a) ──
    bool        hasDefault = false;
    float       defF = 0.f;
    bool        defB = false;
    int         defI = 0;
    uint64_t    defG = 0;
    std::string defS;
    glm::vec4   defV { 0.f };

    // 지금 종류에 맞춰 기본값을 둡니다(Float = 값, Toggle = 0 이 아니면 참, Enum = 번호, ActorRef = GUID, Vec3 · Color = 모든 성분).
    PropDesc& withDefault(double v) & {
        hasDefault = true;
        switch (kind) {
            case Float:    defF = (float)v; break;
            case Toggle:   defB = v != 0.0; break;
            case Enum:     defI = (int)v; break;
            case ActorRef: defG = (uint64_t)v; break;
            case Vec3:     defV = glm::vec4((float)v, (float)v, (float)v, 0.f); break;
            case Color:    defV = glm::vec4((float)v, (float)v, (float)v, 1.f); break;
            default:       hasDefault = false; break;   // 글 종류에 수는 받지 않습니다
        }
        return *this;
    }
    PropDesc& withDefault(const glm::vec3& v) & { hasDefault = true; defV = glm::vec4(v, kind == Color ? 1.f : 0.f); return *this; }
    PropDesc& withDefault(const glm::vec4& v) & { hasDefault = true; defV = v; return *this; }
    PropDesc& withDefault(const char* s) &        { hasDefault = true; defS = s ? s : ""; return *this; }
    PropDesc& withDefault(const std::string& s) & { hasDefault = true; defS = s; return *this; }
    template <class E, std::enable_if_t<std::is_enum_v<E>, int> = 0>
    PropDesc& withDefault(E e) & { return withDefault((double)(long long)e); }

    PropDesc&& withDefault(double v) &&                { return std::move(static_cast<PropDesc&>(*this).withDefault(v)); }
    PropDesc&& withDefault(const glm::vec3& v) &&      { return std::move(static_cast<PropDesc&>(*this).withDefault(v)); }
    PropDesc&& withDefault(const glm::vec4& v) &&      { return std::move(static_cast<PropDesc&>(*this).withDefault(v)); }
    PropDesc&& withDefault(const char* s) &&           { return std::move(static_cast<PropDesc&>(*this).withDefault(s)); }
    PropDesc&& withDefault(const std::string& s) &&    { return std::move(static_cast<PropDesc&>(*this).withDefault(s)); }
    template <class E, std::enable_if_t<std::is_enum_v<E>, int> = 0>
    PropDesc&& withDefault(E e) && { return std::move(static_cast<PropDesc&>(*this).withDefault(e)); }
};

// ── Factory helpers ────────────────────────────────────────────────────────────
inline PropDesc makePropEnum(const char* lbl, std::vector<const char*> opts,
                              std::function<int()> g, std::function<void(int)> s) {
    PropDesc d;
    d.label = lbl; d.kind = PropDesc::Enum;
    d.options = std::move(opts);
    d.getI = std::move(g); d.setI = std::move(s);
    return d;
}

inline PropDesc makePropFloat(const char* lbl, glm::vec4 col, float stp,
                               std::function<float()> g, std::function<void(float)> s,
                               PropUnit unit = PropUnit::Scalar) {
    PropDesc d;
    d.label = lbl; d.color = col; d.kind = PropDesc::Float; d.step = stp; d.unit = unit;
    d.getF  = std::move(g); d.setF = std::move(s);
    return d;
}

// 액터 참조 칸. 값은 대상 GUID(HGuid::value, 0 = 없음)입니다. 필드가 HActorRef 이면 XPROP_ActorRef 를 쓰십시오(scene/ActorRef.hpp).
// 기본값은 늘 "없음"(0)입니다(UE 의 오브젝트 참조 기본값 None).
inline PropDesc makePropActorRef(const char* lbl,
                                 std::function<uint64_t()> g, std::function<void(uint64_t)> s) {
    PropDesc d;
    d.label = lbl; d.kind = PropDesc::ActorRef;
    d.getG  = std::move(g); d.setG = std::move(s);
    d.hasDefault = true; d.defG = 0;
    return d;
}

inline PropDesc makePropToggle(const char* lbl,
                                std::function<bool()> g, std::function<void(bool)> s) {
    PropDesc d;
    d.label = lbl; d.kind = PropDesc::Toggle;
    d.getB  = std::move(g); d.setB = std::move(s);
    return d;
}

// 한 줄 글 칸(H3a).
inline PropDesc makePropString(const char* lbl, std::function<std::string()> g, std::function<void(const std::string&)> s) {
    PropDesc d;
    d.label = lbl; d.kind = PropDesc::String;
    d.getS  = std::move(g); d.setS = std::move(s);
    return d;
}

// 에셋 경로 칸(H3a 는 종류 · 데이터만, 칸은 H3b). assetType 은 콘텐츠 브라우저 종류 이름("Model" · "Sound" …, 비면 모두).
inline PropDesc makePropAssetPath(const char* lbl, const char* assetType, std::function<std::string()> g,
                                  std::function<void(const std::string&)> s) {
    PropDesc d;
    d.label = lbl; d.kind = PropDesc::AssetPath; d.assetType = assetType ? assetType : "";
    d.getS  = std::move(g); d.setS = std::move(s);
    return d;
}

// 성분 셋 칸(H3a). 세터는 세 성분을 한 번에 받습니다(성분별 범위 제한은 세터가 함).
inline PropDesc makePropVec3(const char* lbl, float stp, std::function<glm::vec3()> g, std::function<void(const glm::vec3&)> s,
                             PropUnit unit = PropUnit::Scalar) {
    PropDesc d;
    d.label = lbl; d.kind = PropDesc::Vec3; d.step = stp; d.unit = unit; d.components = 3;
    d.getV = [g = std::move(g)] { return glm::vec4(g(), 0.f); };
    d.setV = [s = std::move(s)](const glm::vec4& v) { s(glm::vec3(v)); };
    return d;
}

// 색 칸(H3a). alpha 가 참이면 RGBA 네 성분, 아니면 RGB(게터의 w 는 무시). srgb: 값이 sRGB 로 저장된 값. 끌기 감도는 unit · step(기본 Ratio).
inline PropDesc makePropColor(const char* lbl, std::function<glm::vec4()> g, std::function<void(const glm::vec4&)> s,
                              bool alpha = false, bool srgb = false, float stp = 0.01f, PropUnit unit = PropUnit::Ratio) {
    PropDesc d;
    d.label = lbl; d.kind = PropDesc::Color; d.step = stp; d.unit = unit;
    d.components = alpha ? 4 : 3; d.srgb = srgb;
    d.getV = std::move(g); d.setV = std::move(s);
    return d;
}

// ── 속성 값 도우미(H3a): 기본값 비교 · 되돌리기 · 글 바꾸기(복사 · 붙여넣기) ─────────────────────────────────────
namespace hprop {

// 기본값 비교의 허용 오차. 값 크기에 비례(1 이하는 절대값 1e-6)합니다. 숫자 칸 끌기는 float 잡음이 섞이므로 정확히 같지 않아도
// 기본값으로 봅니다(숫자 칸의 "값이 바뀌었는가" 기준과 같은 크기).
inline bool nearlyEqual(float a, float b) {
    return a == b || std::abs(a - b) <= 1e-6f * std::max({ 1.f, std::abs(a), std::abs(b) });
}

inline bool isTextKind(PropDesc::Kind k) { return k == PropDesc::String || k == PropDesc::AssetPath; }
inline bool isVectorKind(PropDesc::Kind k) { return k == PropDesc::Vec3 || k == PropDesc::Color; }

// 그 종류의 게터가 있는가(값을 읽을 수 있는가).
inline bool readable(const PropDesc& pd) {
    switch (pd.kind) {
        case PropDesc::Float:    return (bool)pd.getF;
        case PropDesc::Toggle:   return (bool)pd.getB;
        case PropDesc::Enum:     return (bool)pd.getI;
        case PropDesc::ActorRef: return (bool)pd.getG;
        case PropDesc::String:
        case PropDesc::AssetPath: return (bool)pd.getS;
        case PropDesc::Vec3:
        case PropDesc::Color:    return (bool)pd.getV;
    }
    return false;
}
inline bool writable(const PropDesc& pd) {
    switch (pd.kind) {
        case PropDesc::Float:    return (bool)pd.setF;
        case PropDesc::Toggle:   return (bool)pd.setB;
        case PropDesc::Enum:     return (bool)pd.setI;
        case PropDesc::ActorRef: return (bool)pd.setG;
        case PropDesc::String:
        case PropDesc::AssetPath: return (bool)pd.setS;
        case PropDesc::Vec3:
        case PropDesc::Color:    return (bool)pd.setV;
    }
    return false;
}

inline bool vecEqual(const PropDesc& pd, const glm::vec4& a, const glm::vec4& b) {
    const int n = std::clamp(pd.components, 1, 4);
    for (int i = 0; i < n; ++i)
        if (!nearlyEqual(a[i], b[i])) return false;
    return true;
}

// 지금 값이 기본값과 같은가. 기본값이 없거나 읽을 수 없으면 참(되돌릴 것이 없음).
inline bool isDefault(const PropDesc& pd) {
    if (!pd.hasDefault || !readable(pd)) return true;
    switch (pd.kind) {
        case PropDesc::Float:    return nearlyEqual(pd.getF(), pd.defF);
        case PropDesc::Toggle:   return pd.getB() == pd.defB;
        case PropDesc::Enum:     return pd.getI() == pd.defI;
        case PropDesc::ActorRef: return pd.getG() == pd.defG;
        case PropDesc::String:
        case PropDesc::AssetPath: return pd.getS() == pd.defS;
        case PropDesc::Vec3:
        case PropDesc::Color:    return vecEqual(pd, pd.getV(), pd.defV);
    }
    return true;
}
// 되돌릴 수 있는가(기본값이 있고, 쓸 수 있고, 지금 값이 다름). 되돌리기 화살표 · 메뉴 Reset to Default 가 씁니다.
inline bool canReset(const PropDesc& pd) { return pd.hasDefault && writable(pd) && !isDefault(pd); }

// 기본값을 세터로 넣습니다(세터의 범위 제한을 거침). 넣었으면 참.
inline bool resetToDefault(const PropDesc& pd) {
    if (!pd.hasDefault || !writable(pd)) return false;
    switch (pd.kind) {
        case PropDesc::Float:    pd.setF(pd.defF); return true;
        case PropDesc::Toggle:   pd.setB(pd.defB); return true;
        case PropDesc::Enum:     pd.setI(pd.defI); return true;
        case PropDesc::ActorRef: pd.setG(pd.defG); return true;
        case PropDesc::String:
        case PropDesc::AssetPath: pd.setS(pd.defS); return true;
        case PropDesc::Vec3:
        case PropDesc::Color:    pd.setV(pd.defV); return true;
    }
    return false;
}

// 성분 이름(Vec3 = X Y Z, Color = R G B A).
inline const char* componentName(const PropDesc& pd, int i) {
    static const char* const kXyz[4]  = { "X", "Y", "Z", "W" };
    static const char* const kRgba[4] = { "R", "G", "B", "A" };
    return (pd.kind == PropDesc::Color ? kRgba : kXyz)[std::clamp(i, 0, 3)];
}

inline std::string floatText(float v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.6g", (double)v);
    return buf;
}

// 값을 글로(디테일 행 오른쪽 메뉴의 Copy). UE 의 속성 복사 글과 같은 모양입니다: 수 = 6자리, 토글 = True · False, 열거 = 항목 글,
// 성분 = (X=1,Y=2,Z=3) · (R=..,G=..,B=..[,A=..]), 액터 참조 = GUID 수, 글 = 그대로.
inline std::string valueText(const PropDesc& pd) {
    if (!readable(pd)) return {};
    switch (pd.kind) {
        case PropDesc::Float:    return floatText(pd.getF());
        case PropDesc::Toggle:   return pd.getB() ? "True" : "False";
        case PropDesc::Enum: {
            const int i = pd.getI();
            return (i >= 0 && i < (int)pd.options.size() && pd.options[(size_t)i]) ? pd.options[(size_t)i] : std::to_string(i);
        }
        case PropDesc::ActorRef: return std::to_string((unsigned long long)pd.getG());
        case PropDesc::String:
        case PropDesc::AssetPath: return pd.getS();
        case PropDesc::Vec3:
        case PropDesc::Color: {
            const glm::vec4 v = pd.getV();
            std::string s = "(";
            for (int i = 0; i < std::clamp(pd.components, 1, 4); ++i) {
                if (i) s += ",";
                s += componentName(pd, i);
                s += "=";
                s += floatText(v[i]);
            }
            return s + ")";
        }
    }
    return {};
}

namespace detail {
inline bool parseFloat(const std::string& s, float& out) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    if (b == e) return false;
    const std::string t = s.substr(b, e - b);
    char* end = nullptr;
    const float v = std::strtof(t.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}
inline std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}
inline std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}
}  // namespace detail

// 글을 값으로(Paste). 모양이 맞지 않으면 아무것도 하지 않고 거짓. 성분 글은 (X=1,Y=2,Z=3) 또는 "1, 2, 3"(괄호 · 이름 없이)도 받고,
// 빠진 성분은 지금 값 그대로 둡니다.
inline bool setFromText(const PropDesc& pd, const std::string& text) {
    if (!writable(pd)) return false;
    const std::string t = detail::trim(text);
    switch (pd.kind) {
        case PropDesc::Float: {
            float v = 0.f;
            if (!detail::parseFloat(t, v)) return false;
            pd.setF(v);
            return true;
        }
        case PropDesc::Toggle: {
            const std::string l = detail::lower(t);
            if (l == "true" || l == "1" || l == "on")   { pd.setB(true);  return true; }
            if (l == "false" || l == "0" || l == "off") { pd.setB(false); return true; }
            return false;
        }
        case PropDesc::Enum: {
            for (size_t i = 0; i < pd.options.size(); ++i)
                if (pd.options[i] && t == pd.options[i]) { pd.setI((int)i); return true; }
            float v = 0.f;
            if (!detail::parseFloat(t, v) || v != std::floor(v) || v < 0.f || v >= (float)pd.options.size()) return false;
            pd.setI((int)v);
            return true;
        }
        case PropDesc::ActorRef: {
            if (t.empty()) return false;
            for (char c : t) if (c < '0' || c > '9') return false;
            pd.setG((uint64_t)std::strtoull(t.c_str(), nullptr, 10));
            return true;
        }
        case PropDesc::String:
        case PropDesc::AssetPath:
            pd.setS(text);
            return true;
        case PropDesc::Vec3:
        case PropDesc::Color: {
            std::string body = t;
            if (!body.empty() && body.front() == '(') body.erase(body.begin());
            if (!body.empty() && body.back() == ')') body.pop_back();
            glm::vec4 v = pd.getV ? pd.getV() : glm::vec4(0.f);
            const int n = std::clamp(pd.components, 1, 4);
            int next = 0, got = 0;
            size_t pos = 0;
            while (pos <= body.size()) {
                size_t comma = body.find(',', pos);
                if (comma == std::string::npos) comma = body.size();
                std::string part = detail::trim(body.substr(pos, comma - pos));
                pos = comma + 1;
                if (part.empty()) { if (comma >= body.size()) break; continue; }
                int idx = next;
                const size_t eq = part.find('=');
                if (eq != std::string::npos) {
                    const std::string name = detail::lower(detail::trim(part.substr(0, eq)));
                    idx = -1;
                    for (int i = 0; i < 4; ++i)
                        if (name == detail::lower(componentName(pd, i))) idx = i;
                    part = part.substr(eq + 1);
                }
                float f = 0.f;
                if (idx < 0 || idx >= n || !detail::parseFloat(part, f)) return false;
                v[idx] = f;
                next = idx + 1;
                ++got;
                if (comma >= body.size()) break;
            }
            if (got == 0) return false;
            pd.setV(v);
            return true;
        }
    }
    return false;
}

}  // namespace hprop

// ── X-Macro expansion helpers ─────────────────────────────────────────────────
// Usage pattern in component header:
//   #define MY_PROPS(p) \
//       XPROP_Float(p, "Speed", PropColor::Neutral, 0.1f, speed), \
//       XPROP_Toggle(p, "Active", active)
//
// Usage in component .cpp:
//   std::vector<PropDesc> MyComponent::buildProps() {
//       auto* p = this; return { MY_PROPS(p) };
//   }
// 기본값은 매크로 뒤에 붙입니다(H3a): XPROP_Float(p, "Speed", PropColor::Neutral, 0.1f, speed).withDefault(1.f)

#define XPROP_Float(p, lbl, col, stp, field) \
    makePropFloat(lbl, col, stp, \
        [p]() -> float { return static_cast<float>((p)->field); }, \
        [p](float _v){ (p)->field = static_cast<decltype((p)->field)>(_v); })

#define XPROP_FloatMin(p, lbl, col, stp, field, mn) \
    makePropFloat(lbl, col, stp, \
        [p]() -> float { return static_cast<float>((p)->field); }, \
        [p](float _v){ (p)->field = std::max(static_cast<float>(mn), _v); })

#define XPROP_FloatClamp(p, lbl, col, stp, field, mn, mx) \
    makePropFloat(lbl, col, stp, \
        [p]() -> float { return static_cast<float>((p)->field); }, \
        [p](float _v){ (p)->field = std::clamp(_v, static_cast<float>(mn), static_cast<float>(mx)); })

// field 는 HActorRef 여야 합니다(scene/ActorRef.hpp 를 포함한 곳에서 씀).
#define XPROP_ActorRef(p, lbl, field) \
    makePropActorRef(lbl, \
        [p]() -> uint64_t { return (p)->field.guid().value; }, \
        [p](uint64_t _v){ (p)->field.setGuid(HGuid{ _v }); })

#define XPROP_Toggle(p, lbl, field) \
    makePropToggle(lbl, \
        [p]() -> bool { return static_cast<bool>((p)->field); }, \
        [p](bool _v){ (p)->field = static_cast<decltype((p)->field)>(_v); })

// field 는 std::string(H3a).
#define XPROP_String(p, lbl, field) \
    makePropString(lbl, \
        [p]() -> std::string { return (p)->field; }, \
        [p](const std::string& _v){ (p)->field = _v; })

// field 는 glm::vec3(H3a). 성분마다 mn 아래로 내려가지 않습니다.
#define XPROP_Vec3Min(p, lbl, stp, field, mn, unit) \
    makePropVec3(lbl, stp, \
        [p]() -> glm::vec3 { return (p)->field; }, \
        [p](const glm::vec3& _v){ (p)->field = glm::max(_v, glm::vec3(static_cast<float>(mn))); }, unit)
