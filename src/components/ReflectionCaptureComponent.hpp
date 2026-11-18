#pragma once
#include "../scene/Component.hpp"

#include <cstdint>
#include <string>

// ── 반사 캡처 (Reflection Capture, 라이팅 Phase 6, 2026-11-17) ──────────────────────────────
// 액터 위치에서 장면을 큐브맵 6면으로 한 번 그려(섀도 · 메인 패스, 톤매핑 전 HDR) GGX 프리필터 밉 사슬을 굽고, 영향 반경 안 화소의
// 스페큘러 반사에 하늘 대신 씁니다(UE 의 Sphere Reflection Capture). 반사 방향은 반경 구에 맞춰 시차 보정하므로(구 프록시) 캡처
// 지점에서 떨어진 곳의 반사도 방 크기만큼 맞게 보입니다. 반경 바깥 30 % 구간에서 하늘 반사와 섞습니다.
//
//  - 한 장면에 하나만 씁니다(켜진 첫 컴포넌트). 여러 개 · 상자 프록시는 수요가 생기면 더합니다.
//  - 정적 캡처입니다. Play 를 시작하거나 위치 · 값을 바꿨을 때, 또는 recapture() 를 부를 때 다시 찍습니다. 움직이는 물체는
//    찍은 순간의 자리로 남습니다(UE 와 같음).
//  - 확산(diffuse) 간접광은 하늘 그대로입니다.
class ReflectionCaptureComponent : public Component {
public:
    float radius    = 5.f;   // 영향 반경(m)
    float intensity = 1.f;   // 반사 밝기 배율

    static constexpr float kMinRadius = 0.5f;
    static constexpr float kMaxRadius = 200.f;

    // 다시 찍기를 요청합니다(렌더러가 다음 프레임에 찍음).
    void     recapture() { ++version_; }
    uint32_t version() const { return version_; }

    void onPropsCommitted() override { recapture(); }
    void onFieldsLoaded(const ComponentLoadContext&) override { recapture(); }
    void onEnable() override { recapture(); }

    const char*           typeName() const override { return "ReflectionCaptureComponent"; }
    void                  writeFields(ComponentFieldWriter& w) const override;
    bool                  readField(const std::string& key, const std::string& value) override;
    std::vector<PropDesc> buildProps() override;

private:
    uint32_t version_ = 1;
};
