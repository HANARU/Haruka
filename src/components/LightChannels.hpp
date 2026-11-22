#pragma once
// ── 라이트 채널 (Light Channels, 2026-11-17, Lighting_UE_Design Phase 2 남은 항목) ─────────────────────────
// 라이트와 메시가 각자 채널 비트(0 ~ 2, UE 와 같은 세 채널)를 가지고, 둘이 겹치는 채널이 하나라도 있을 때만 그 라이트가 그 메시를
// 비춥니다. 기본은 둘 다 채널 0 만 켜져 있어 모든 라이트가 모든 메시를 비춥니다(예전과 같음). 하늘 반사광(IBL)과 그림자 깊이는
// 채널과 무관합니다(UE 도 직접광에만 적용).
//
// 전달: 라이트 마스크는 GpuLight.spotParams.z(실수로 넣은 정수), 메시 마스크는 드로마다 푸시 상수(GpuViewPush.lightChannels)입니다.
// 같은 지오메트리 · 머티리얼이라도 채널이 다르면 다른 묶음입니다(DrawBatching).

#include "../scene/ComponentFields.hpp"
#include "../scene/Reflect.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hchan {

constexpr uint8_t kDefault = 1;   // 채널 0
constexpr int     kCount   = 3;
constexpr uint8_t kAll     = (1u << kCount) - 1u;

// defaultMask: 그 컴포넌트의 생성자 기본값(디테일의 기본값 되돌리기, H3a). 지금 쓰는 곳은 모두 kDefault 입니다.
inline void addProps(std::vector<PropDesc>& props, uint8_t* mask, uint8_t defaultMask = kDefault) {
    static const char* const kLabels[kCount] = { "Light Channel 0", "Light Channel 1", "Light Channel 2" };
    for (int i = 0; i < kCount; ++i) {
        const uint8_t bit = (uint8_t)(1u << i);
        props.push_back(makePropToggle(kLabels[i], [mask, bit] { return (*mask & bit) != 0; },
                                       [mask, bit](bool v) { *mask = v ? (uint8_t)(*mask | bit) : (uint8_t)(*mask & ~bit); })
                            .withDefault((defaultMask & bit) != 0));
    }
}

// 키 "lightChannels" = 0 ~ 7 정수. 기본값(1)이면 쓰지 않습니다(기존 씬 파일이 바뀌지 않게).
inline void write(ComponentFieldWriter& w, uint8_t mask) {
    if (mask != kDefault) w.i("lightChannels", (int)mask);
}
inline bool read(const std::string& value, uint8_t& mask) {
    int v = 0;
    if (!fieldparse::toInt(value, v) || v < 0 || v > (int)kAll) return false;
    mask = (uint8_t)v;
    return true;
}

} // namespace hchan
