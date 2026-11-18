#pragma once
#include "../scene/Component.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <vector>

class World;

enum class LightType : uint32_t {
    Directional = 0,
    Point       = 1,
    Spot        = 2,
};

// UE 어휘를 채택한 라이트 컴포넌트입니다. 값은 물리 단위가 아니라 상대값(무단위)이며,
// 이름·드롭다운만 UE 를 따릅니다. 프로퍼티 행은 buildProps()(LightComponent.cpp)에서
// lightType 에 따라 조건부로 구성합니다.
class LightComponent : public Component {
public:
    LightType lightType    = LightType::Directional;
    // 발광 방향은 액터 월드 회전 × (0, 0, -1) 로만 정합니다(유니티: 라이트 방향 = Transform).
    // 회전이 항등이면 -Z 로 비춥니다.
    glm::vec3 color        = {1.0f, 1.0f, 1.0f};   // 선형 RGB 틴트
    // 0.85 이던 값. 그때 하늘의 등가 조도는 pi * 0.6017 = 1.890 이어서 하늘이
    // 태양보다 2.2 배 밝았다. 물리적으로 흐린 날이고, 흐린 날에는 정의상 음영이
    // 생기지 않는다. 3.0 이면 LightingCalibration.hpp 의 kSkyRadianceScale 0.30 과 맞물려
    // 직사광 대 천공광이 5.3 : 1 이 되고, 이는 맑은 날의 범위(5~10 : 1)에 든다.
    // 이 값만 올리고 노출을 그대로 두면 ACES 숄더에 올라타 더 밝기만 하고
    // 대비는 늘지 않으니, LightingCalibration.hpp 의 kBaseExposure 와 함께 움직여야 한다.
    float     intensity    = 3.0f;

    // UE: Use Temperature / Temperature(K). 켜지면 흑체 색온도에서 유도한 틴트를
    // color 에 곱합니다(effectiveColor). 기본은 꺼짐이라 기존 씬의 렌더 결과가 그대로
    // 유지되고, 6500K 근처가 중성(백색)입니다. 노출·강도와는 직교합니다.
    bool      useTemperature = false;
    float     temperatureK   = 6500.f;

    // UE: Attenuation Radius — Point/Spot 의 도달 반경. Directional 은 무시.
    // 2026-10-04 부터 밝기는 실제 점광원처럼 거리의 역제곱으로 줄고(intensity = 1m 거리의 밝기,
    // 2m 면 1/4), 이 반경은 꼬리를 0 으로 부드럽게 자르는 지점이며, 창 함수 때문에 반경 안쪽도
    // 0.5r 에서 0.88배, 0.74r 에서 0.5배, 0.9r 에서 0.12배로 깎입니다(d ≪ r 일 때만 순수 역제곱, pbr.frag).
    // 0 이하는 허용하지 않습니다. 예전에는 0 이면 라이트가 꺼지고 음수면 감쇠 없이 무한히
    // 비췄는데, 주석은 "0=무한" 이라고 적혀 있었습니다(DEF-35). UE·Unity·Godot 모두 반경을
    // 0 보다 크게 강제합니다. UI·씬 불러오기·GPU 업로드 세 곳이 이 최솟값을 씁니다.
    static constexpr float kMinAttenuationRadius = 0.01f;   // 1 cm
    float     attenuationRadius = 10.f;
    // UE: Inner/Outer Cone Angle(반각, 도) — Spot 원뿔. Inner 안쪽은 최대, Outer 바깥은 0, 그 사이는
    // cos 에 대해 선형. inner > outer 로 저장된 값은 업로드할 때만 inner = outer 로 봅니다(SpotCone.hpp, 저장값 불변).
    float     innerConeAngle = 15.f;
    float     outerConeAngle = 30.f;

    // UE: Cast Shadows(per-light). 현재 섀도맵은 방향광 하나만 지원하므로, 실질적으로는
    // **첫 Directional 라이트 중 이 값이 켜진 것**이 캐스터로 선택됩니다(Renderer::updateLightSSBO).
    // 기본 true 라 기존 씬은 그대로 그림자를 던집니다. UI 에는 Directional 일 때만 노출합니다.
    bool      castShadows = true;
    uint8_t   lightChannels = 1;
    // 접촉 그림자(Contact Shadows, 2026-11-17): 섀도 캐스터 방향광에서 화면 공간으로 태양 쪽 광선을 이 길이(m)만큼 걸어, 섀도맵
    // 텍셀보다 작은 접촉부의 그림자를 더합니다. 0 이면 끔(기본). UE 의 Contact Shadow Length 와 같은 역할(그쪽은 화면 비율).
    float     contactShadowLength = 0.f;   // 비출 메시의 채널(LightChannels.hpp, 2026-11-17). 기본 채널 0

    // 빛이 나아가는 월드 방향(발광 방향, 단위 벡터)입니다. 액터 월드 회전 × (0, 0, -1) 이며
    // owner 가 없으면 (0, 0, -1) 을 돌려줍니다. pbr.frag 가 spotDir 을 다시 정규화하지 않으므로
    // 항상 단위 길이를 보장합니다. Point 에서도 같은 값을 돌려주되 뜻은 없습니다.
    glm::vec3 emitDirection() const;

    // ── 옛 direction 접기(방향은 회전에서만, 축 -Z) ─────────────────────────────────────────
    // 로컬 -Z 를 e 로 보내는 롤 0 회전(요 · 피치만)입니다. e 가 0 · NaN · 무한이면 항등 회전을 돌려줍니다.
    static glm::quat rotationForEmitDirection(const glm::vec3& e);
    // 옛 direction 값 d 가 뜻하던 발광 방향입니다. Directional 은 -normalize(d), Spot 은 +normalize(d) 입니다.
    // Point 와, 0 · NaN · 무한인 d 는 nullopt 입니다.
    static std::optional<glm::vec3> legacyEmitDirection(LightType t, const glm::vec3& d);
    // 옛 direction 값 d 를 액터의 로컬 회전에 오른쪽으로 접어 넣습니다(lightType 설정 뒤 호출).
    // Point 이거나 owner 가 없으면 아무것도 하지 않으며, 0 · NaN · 무한은 예전 대체값으로 접고 경고를 남깁니다.
    void foldLegacyDirection(const glm::vec3& d);
    // color 에 색온도 틴트를 접은 유효 색(intensity 는 곱하지 않음). useTemperature 가
    // 꺼져 있으면 color 그대로라 렌더가 불변입니다. 정의는 LightComponent.cpp.
    glm::vec3 effectiveColor() const;

    const char*           typeName()   const override { return "LightComponent"; }
    std::vector<PropDesc> buildProps()       override;

    // 켜진 라이트(예전 World::getLights, 2026-11-18 S3): 활성 · 켜짐, 액터 순서.
    static std::vector<LightComponent*> enabledIn(const World& world);

    // 직렬화 (DEF-22). 이름은 멤버 이름을 따릅니다.
    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;
    void onFieldsLoaded(const ComponentLoadContext& ctx) override;   // 옛 direction 접기 + 반경 최솟값(DEF-35)

private:
    std::optional<glm::vec3> pendingLegacyDirection_;
};
