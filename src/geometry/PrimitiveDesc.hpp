#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

#include "../components/RigidBodyComponent.hpp"   // EPhysicsShape, HalfExt

// ── 절차 도형의 정체성 ────────────────────────────────────────────────────────
// Vulkan 의존이 0입니다. 디바이스 없이 테스트할 수 있습니다.
//
// 규약은 Docs/Primitive_And_Lighting_Design.md 4장이 정본입니다. 요지입니다.
//   P1  원점은 바운딩 박스의 기하 중심입니다. Arrow / Axis 만 예외입니다.
//   R2  모든 도형의 AABB 는 [-0.5, +0.5]^3 입니다. 즉 Transform.scale 의 각
//       성분이 그대로 그 축 방향의 미터 단위 크기가 됩니다.
//   R1  크기는 절대 정점에 굽지 않습니다. 메가버퍼가 회수되지 않기 때문입니다.
//   W1  바깥에서 보아 반시계 방향인 삼각형이 앞면입니다.

enum class EPrimitiveShape : uint8_t {
    Box, Plane, Sphere, Cylinder, Cone, Capsule, Torus, Arrow, Icosphere, Count
};

// 셰이프 이름. 직렬화에 서수가 아니라 이 문자열을 씁니다.
// 서수로 쓰면 목록 중간에 값을 삽입할 때 기존 씬이 조용히 깨집니다.
const char*     primitiveShapeName(EPrimitiveShape s);
EPrimitiveShape primitiveShapeFromName(std::string_view name, bool* ok = nullptr);

struct PrimitiveDesc {
    static constexpr uint8_t kCapped    = 1 << 0;   // Cylinder / Cone 의 뚜껑
    static constexpr uint8_t kInsideOut = 1 << 1;   // 스카이돔처럼 안쪽을 보는 도형

    EPrimitiveShape shape = EPrimitiveShape::Box;
    uint8_t         flags = kCapped;
    uint16_t        segU  = 32;    // 원주 방향 (래더로 스냅)
    uint16_t        segV  = 16;    // 축 방향   (래더로 스냅)
    uint16_t        _pad  = 0;     // 해시 안정성을 위해 항상 0
    float           ratio = 0.5f;  // Capsule / Torus / Arrow 전용 형태 비율

    bool capped()    const { return (flags & kCapped)    != 0; }
    bool insideOut() const { return (flags & kInsideOut) != 0; }

    // 래더 스냅, 미사용 필드 0 화, -0.0f 를 0.0f 로.
    // 부동소수 표현 차이로 캐시 키가 갈라져 같은 도형이 두 번 올라가는 것을 막습니다.
    void canonicalize();

    // 캐시 키이면서 씬 파일의 PRIM: 값입니다(SceneSerializer). 형식을 바꾸면 저장된 씬을
    // 읽지 못하게 되므로, 바꿀 때는 옛 형식도 읽도록 fromKey 를 함께 고쳐야 합니다.
    std::string toKey() const;                                  // "sphere|32x16|1|0.5"
    static bool fromKey(std::string_view key, PrimitiveDesc& out);

    bool operator==(const PrimitiveDesc& o) const;
    bool operator!=(const PrimitiveDesc& o) const { return !(*this == o); }
};

// 절차 도형 머티리얼의 정체성입니다. 머티리얼 핸들만으로는 무엇을 만들었는지 알 수 없어
// 저장할 수 없으므로, 메시 컴포넌트가 이 값을 함께 들고 다닙니다. 기본값은
// AssetManager::defaultPrimitiveMaterial 과 같습니다.
struct PrimitiveMaterialDesc {
    bool     checker   = false;        // 검증용 체커. 참이면 아래 세 값은 쓰지 않습니다
    uint32_t rgba8     = 0xCCCCCCFFu;  // sRGB 표시값(색 선택기에서 고른 값 그대로), 0xRRGGBBAA
    float    metallic  = 0.f;
    float    roughness = 0.6f;

    // 씬 파일의 PRIM_MAT: 값입니다. "checker" 또는 "color B4B4B4FF 0 0.6"(색, 금속도, 거칠기).
    // 색은 16진수로 써서 저장·불러오기에서 값이 변하지 않습니다. fromText 는 범위 밖 값이나
    // 꼬리의 쓰레기를 조용히 받아들이지 않고 실패합니다.
    std::string toText() const;
    static bool fromText(std::string_view text, PrimitiveMaterialDesc& out);

    bool operator==(const PrimitiveMaterialDesc& o) const;
    bool operator!=(const PrimitiveMaterialDesc& o) const { return !(*this == o); }
};

// R3. 세그먼트 래더입니다. 도달 가능한 키 공간을 유한하게 만드는 메모리 안전
// 장치이며, 동시에 LOD 링 정점이 상위 LOD 의 부분집합이 되게 합니다.
uint16_t snapSegmentLadder(int requested);

// LOD_n 의 desc. 세그먼트만 줄인 같은 도형입니다 (규칙 R4).
PrimitiveDesc primitiveLodDesc(const PrimitiveDesc& base, int level);

// 물리 대응. 해석적 대응이 없으면 false 를 반환합니다 (Cone, Torus, Arrow, Icosphere).
// 호출자가 Box 근사와 스킵 중에 결정합니다.
//
// 반환하는 HalfExt 는 '단위 도형' 기준이며 Transform.scale 을 곱하지 않습니다.
// 스케일 적용은 PhysicsSystem 이 바디를 만들 때 한 번만 합니다 (DEF-41).
// 여기서 곱하면 이중 적용이 됩니다.
//
// EPhysicsShape 와 EPrimitiveShape 를 합치지 않는 이유는 교집합이 셋뿐이라
// 합치면 물리에 없는 Torus 가 PhysicsSystem 의 switch 를 오염시키기 때문입니다.
bool primitiveToPhysics(const PrimitiveDesc& desc,
                        EPhysicsShape&       outShape,
                        HalfExt&             outHalf);
