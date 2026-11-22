#pragma once
// ── 색 선택기 상태 (H3b, 2026-11-22) ────────────────────────────────────────────────────────────────────────────
// 디테일 패널 색 칸(PropDesc::Color)의 견본을 누르면 열리는 색 선택기(UE 의 SColorPicker 축소판)의 값 계산입니다. 화면 없이
// 시험합니다(tests/EditorCommandTest.cpp). 위젯 · 팝업은 ColorPickerPopup, Undo 는 디테일 패널이 연 세션(hcmd::PropSession)이 맡습니다.
//
// 색 공간 규칙(H3a 색 칸과 같음): value 는 컴포넌트가 저장하는 값 그대로입니다.
//   - srgb = false(라이트 · 안개 · 데칼 색): 선형 값. 숫자 칸 R · G · B 와 H · S · V 는 선형 값에서 셉니다(UE 의 기본 = 선형 HSV).
//   - srgb = true(기본 도형 재질 색): sRGB 로 저장한 값. 숫자 칸 · HSV 는 그 sRGB 값에서 셉니다.
//   - 16진수 칸은 늘 sRGB 입니다(UE 의 Hex sRGB): 선형 값이면 바꿔 보이고, 친 값은 선형으로 바꿔 넣습니다. 알파는 바꾸지 않습니다.
//   - 견본 · 판 · 막대에 칠하는 색은 렌더러에 넘기는 선형 값(display)입니다.
// HDR(1 을 넘는 값, 안개 색 0 ~ 100): 따로 세기 칸을 두지 않고 HSV 의 V 가 1 을 넘습니다(UE 의 선형 HSV 와 같음). 채도 · 명도 판의 세로
// 범위는 열 때의 max(1, V) 이고(vScale), 16진수 칸은 0 ~ 1 로 자른 값을 보입니다. 칠하는 색도 0 ~ 1 로 자릅니다.
#include "../math/Color.hpp"

#include <algorithm>
#include <string>
#include <glm/glm.hpp>

namespace hcolorpick {

struct State {
    int       components = 3;      // 3 = RGB, 4 = RGBA
    bool      srgb       = false;  // value 가 sRGB 로 저장된 값인가
    glm::vec4 original { 0.f, 0.f, 0.f, 1.f };   // 열 때의 값(취소 · 이전 색 견본)
    glm::vec4 value    { 0.f, 0.f, 0.f, 1.f };   // 지금 값(저장 값 그대로)
    glm::vec3 hsv      { 0.f, 0.f, 0.f };        // value 의 HSV(색상 · 채도를 회색 · 검정에서도 기억)
    float     vScale   = 1.f;                     // 채도 · 명도 판 세로 범위(HDR 이면 1 보다 큼)

    bool hasAlpha() const { return components >= 4; }

    void open(const glm::vec4& v, int comps, bool isSrgb) {
        components = std::clamp(comps, 3, 4);
        srgb = isSrgb;
        original = v;
        if (!hasAlpha()) original.a = 1.f;
        value = original;
        hsv = hcolor::rgbToHsv(glm::vec3(value), 0.f);
        vScale = std::max(1.f, hsv.z);
    }

    // 값을 바깥에서 받음(세터가 잘랐을 수 있음, 숫자 칸 입력). 회색 · 검정이면 지금 색상(과 검정이면 채도)을 지킵니다.
    void setValue(const glm::vec4& v) {
        value = v;
        if (!hasAlpha()) value.a = 1.f;
        const glm::vec3 n = hcolor::rgbToHsv(glm::vec3(value), hsv.x);
        hsv = glm::vec3(n.x, n.z > 0.f ? n.y : hsv.y, n.z);
    }
    void setHsv(const glm::vec3& h) {
        hsv = glm::vec3(std::clamp(h.x, 0.f, 360.f), std::clamp(h.y, 0.f, 1.f), std::max(0.f, h.z));
        const glm::vec3 rgb = hcolor::hsvToRgb(hsv);
        value = glm::vec4(rgb, value.a);
    }
    void setHue(float h)              { setHsv({ h, hsv.y, hsv.z }); }
    void setSaturationValue(float s, float v) { setHsv({ hsv.x, s, v }); }
    void setAlpha(float a)            { if (hasAlpha()) value.a = std::clamp(a, 0.f, 1.f); }
    void setComponent(int i, float c) {
        glm::vec4 v = value;
        v[std::clamp(i, 0, 3)] = c;
        setValue(v);
    }
    // 판 자리(u = 채도, v = 위가 0 인 명도 비율) ↔ 값.
    void setPlane(float u, float v) { setSaturationValue(std::clamp(u, 0.f, 1.f), (1.f - std::clamp(v, 0.f, 1.f)) * vScale); }
    glm::vec2 planeMarker() const { return { hsv.y, 1.f - std::clamp(hsv.z / vScale, 0.f, 1.f) }; }
    float hueMarker() const { return std::clamp(hsv.x / 360.f, 0.f, 1.f); }
    float alphaMarker() const { return 1.f - std::clamp(value.a, 0.f, 1.f); }   // 세로 막대, 위 = 불투명

    // 저장 값 → 렌더러에 넘길 선형 값(0 ~ 1 로 자름).
    glm::vec4 display(const glm::vec4& stored) const {
        glm::vec3 c = glm::clamp(glm::vec3(stored), glm::vec3(0.f), glm::vec3(1.f));
        if (srgb) c = hcolor::srgbToLinear(c);
        return glm::vec4(c, hasAlpha() ? std::clamp(stored.a, 0.f, 1.f) : 1.f);
    }
    glm::vec4 displayValue() const    { return display(value); }
    glm::vec4 displayOriginal() const { return display(original); }
    // 판의 (u, v) 자리 색(표시용). 색상은 지금 값, 알파 1.
    glm::vec4 planeColor(float u, float v) const {
        const glm::vec3 rgb = hcolor::hsvToRgb({ hsv.x, u, (1.f - v) * vScale });
        glm::vec4 d = display(glm::vec4(rgb, 1.f));
        d.a = 1.f;
        return d;
    }
    // 색상 막대 t(0 = 위 = 0 도) 자리 색(채도 · 명도 1).
    glm::vec4 hueColor(float t) const {
        glm::vec4 d = display(glm::vec4(hcolor::hsvToRgb({ t * 360.f, 1.f, 1.f }), 1.f));
        d.a = 1.f;
        return d;
    }
    // 알파 막대 t(0 = 위 = 불투명) 자리 색.
    glm::vec4 alphaColor(float t) const {
        glm::vec4 d = display(glm::vec4(glm::vec3(value), 1.f));
        d.a = 1.f - t;
        return d;
    }

    // 16진수(sRGB). 알파 칸이 있으면 8자리.
    std::string hex() const {
        glm::vec3 c = glm::clamp(glm::vec3(value), glm::vec3(0.f), glm::vec3(1.f));
        if (!srgb) c = hcolor::linearToSrgb(c);
        return hcolor::toHex(glm::vec4(c, value.a), hasAlpha());
    }
    // 친 16진수를 넣습니다. 모양이 틀리면 거짓(값 그대로). 알파 자리가 없으면 알파는 그대로.
    bool setHex(const std::string& text) {
        glm::vec4 s;
        bool a = false;
        if (!hcolor::parseHex(text, s, &a)) return false;
        glm::vec3 c = glm::vec3(s);
        if (!srgb) c = hcolor::srgbToLinear(c);
        setValue(glm::vec4(c, (a && hasAlpha()) ? s.a : value.a));
        return true;
    }

    bool changed() const { return value != original; }
};

} // namespace hcolorpick
