// Haruka 공용 GPU 선언. C++(src/renderer/RenderTypes.hpp)과 GLSL 셰이더가 함께 include 합니다.
// 예전에는 같은 구조체를 셰이더마다 따로 적어, 한 곳만 바꾸면 나머지와 조용히 어긋났습니다(DEF-14).
//
// 규칙
//  - vec4 / ivec4 / mat4 만 씁니다. std140·std430 배치와 C++(glm) 배치가 같아지는 형만 남기기
//    위해서입니다. vec3 은 16바이트로 정렬되고, std430 의 mat3 은 48바이트인데 glm::mat3 은 36바이트입니다.
//  - 필드를 바꾸면 RenderTypes.hpp 의 static_assert 도 함께 고칩니다.
//  - 이 파일을 고치면 CMake depfile 이 이 파일을 include 하는 셰이더를 다시 컴파일합니다(DEF-11).
#ifndef HARUKA_SHARED_H
#define HARUKA_SHARED_H

#ifdef __cplusplus
#include <glm/glm.hpp>
namespace hgpu {
using vec4  = glm::vec4;
using ivec4 = glm::ivec4;
using mat4  = glm::mat4;
#endif

// 한 프레임에 그리는 뷰(눈)의 최대 수. 단안은 0 번만, 스테레오는 0=왼쪽 · 1=오른쪽입니다.
#define HARUKA_MAX_VIEWS 2

// 한 프레임에 싣는 상자 투영 데칼의 최대 수(파티클 · 데칼 D11, 2026-11-20). DecalComponent::kMaxDecals 와 같습니다.
#define HARUKA_MAX_DECALS 8

// 특수화 상수(specialization constant) 번호(2026-11-01). 셰이더의 layout(constant_id = …) 와 C++ 의
// SpecConstants::addInt 가 이 번호를 함께 씁니다. 겹치지 않도록 새 항목은 여기에 다음 번호(1, 2 …)로 더합니다.
// 값은 모두 4바이트 정수입니다(SpecConstants.hpp).
#define HARUKA_SPEC_SHADOW_FILTER     0   // pbr.frag 섀도 커널 등급(GeometryPass::setShadowFilter)

// 섀도 커널 등급(HARUKA_SPEC_SHADOW_FILTER 의 값). Low · Medium 커널의 번호는 커널을 정할 때 더합니다.
#define HARUKA_SHADOW_FILTER_LEGACY9  0   // 하드웨어 쌍선형 비교 9탭(3x3), 기본
#define HARUKA_SHADOW_FILTER_HARD     1   // 하드웨어 쌍선형 비교 1탭

// 프레임 상수 (set 0, binding 0). 셰이더에서는 블록 안의 ubo 로 둡니다.
// 눈마다 다른 값(행렬 · 눈 위치)은 [HARUKA_MAX_VIEWS] 로 둡니다(DEF-09). 셰이더는 HARUKA_EYE_INDEX(푸시 상수 +
// gl_ViewIndex)로 고릅니다(멀티뷰 2026-11-18). 단안이면 [1] 은 [0] 과 같은 값입니다.
// prevViewProj(가짜 TAA 용, 읽는 곳 없음)는 2026-10-04 에 지웠습니다(DEF-15). 진짜 TAA 나
// Quest 의 프레임 합성(AppSW)에는 눈별 이전 행렬이 필요하므로 그때 viewProj 처럼 [2] 로 넣습니다.
// 상자 투영 데칼 하나(FrameUBO.decals, 2026-11-20, Docs/Particles_And_Decals.md D11). pbr.frag 의 applyDecals 가 읽습니다.
struct GpuDecal {
    mat4 worldToBox;   // 월드 → 상자 로컬([-0.5, 0.5]³ 안이면 영향), inverse(액터 월드 × scale(size))
    vec4 color;        // rgb = 선형 알베도, a = 불투명도
    vec4 axis;         // xyz = 상자 +Y 의 월드 방향(투영은 −Y 로), w = 거칠기(음수면 그대로)
    ivec4 mode;        // x = 섞기(0 Replace · 1 Multiply), y = 모양(0 원 · 1 부드러운 원 · 2 고리 · 3 사각), zw = 예약
};

struct FrameUBO {
    mat4  viewProj[HARUKA_MAX_VIEWS];   // 눈별 view-projection
    mat4  lightViewProj;                // shadow camera VP (두 눈 공용)
    vec4  eyePos[HARUKA_MAX_VIEWS];     // xyz=눈의 월드 위치, w=nearPlane
    vec4  screenParams;    // xy=눈 하나의 렌더 크기, z=farPlane, w=aoStrength
    vec4  timeParams;      // x=engine time in seconds (wraps every 3600 s), y=frame, z=bloomThreshold, w=exposure
    ivec4 miscParams;      // x=lightCount, y=frame, z=ssaoEnabled, w=shadowLightIndex(<0=none)
    // 섀도맵 상수(2026-11-01). x=텍셀 하나의 월드 크기(m), y=1/해상도(텍셀 하나의 UV 크기, 정사각 맵),
    // z=리시버 상수 깊이 바이어스(섀도 NDC, sampleShadow 가 proj.z 에서 뺌),
    // w=리시버 노멀 오프셋의 월드 길이(m, pbr.vert 가 sinθ 를 곱해 섀도 조회 위치를 옮김, 2026-11-02).
    vec4  shadowParams;
    // 그림자 거리 페이드(2026-11-02, ShadowFit.hpp). 머리(eyePos[0] · [1] 의 가운데)에서 화소까지의 거리로
    // x=페이드 시작(m), y=페이드 끝(m, 여기부터 섀도맵을 읽지 않음), zw=예약(0). 더 필요한 값은 이 뒤에 붙입니다.
    vec4  shadowFade;
    // 지수 고도 안개(2026-11-17, HeightFogComponent). fogColor: rgb = 선형 HDR 안개 색, a = 최대 불투명도.
    // fogParams: x = 밀도(0 이면 안개 없음), y = 높이 감쇠, z = 안개 기준 높이(월드 y), w = 시작 거리(m).
    vec4  fogColor;
    vec4  fogParams;
    // 가까운 캐스케이드(CSM, 2026-11-17, ShadowFit.hpp). lightViewProj 는 먼 단(예전 단일 맵)이고 이것은 가까운 단입니다.
    // 섀도 이미지는 가로 두 칸(왼쪽 = 가까운 단, 오른쪽 = 먼 단)이라 shadowParams.y 는 한 칸의 1/해상도입니다.
    // cascadeNear: x = 가까운 단 페이드 시작(m), y = 페이드 끝(여기부터 먼 단만), z = 리시버 바이어스(섀도 NDC), w = 노멀 오프셋(m).
    mat4  lightViewProjNear;
    vec4  cascadeNear;
    // 반사 캡처(2026-11-17, ReflectionCaptureComponent): x y z = 캡처 위치, w = 영향 반경(0 = 없음). captureParams.x = 밝기.
    vec4  capturePos;
    vec4  captureParams;
    // 상자 투영 데칼(2026-11-20). decalInfo.x = 데칼 수(0 ~ HARUKA_MAX_DECALS, 0 이면 pbr.frag 가 반복하지 않음), yzw = 예약.
    ivec4 decalInfo;
    GpuDecal decals[HARUKA_MAX_DECALS];
};

// 메시마다 월드 행렬 (set 2, binding 0). 행 번호 = gl_InstanceIndex.
// 월드 행렬은 언제나 아핀이라 마지막 행 (0, 0, 0, 1) 을 빼고 위 세 행만 둡니다(수학 보강 O15, 64 → 48 B). rows[r] 는 행렬의
// r 번째 행(열 우선 m 이면 (m[0][r], m[1][r], m[2][r], m[3][r]))입니다. 셰이더는 hkModel() 로 mat4 를 다시 만듭니다.
struct GpuTransform {
    vec4 rows[3];
};

// 라이트 (set 1, binding 4).
struct GpuLight {
    vec4 posOrDir;    // xyz=pos or dir, w=type (0=dir, 1=point, 2=spot)
    vec4 colorRange;  // xyz=effectiveColor*intensity, w=attenuationRadius (항상 > 0)
    vec4 spotDir;     // xyz=spot forward direction (단위 길이, CPU 가 정규화)
    vec4 spotParams;  // x=cosScale, y=cosOffset (스팟 원뿔 계수, SpotCone.hpp), z=라이트 채널 비트(실수로 넣은 정수, 2026-11-17), w=reserved
};

#ifdef __cplusplus
} // namespace hgpu
#else
// 뷰(눈) 번호 푸시 상수. 뷰 번호가 필요한 셰이더는 본문 앞에 HARUKA_VIEW_PUSH_CONSTANT; 를 한 번 적습니다. 정점 · 조각
// 셰이더가 같은 선언을 쓰므로 범위는 두 단계에 걸칩니다(C++ 쪽 VkPushConstantRange 와 맞춤).
// lightChannels: 이 드로의 메시 라이트 채널 비트(2026-11-17, pbr.frag 만 읽음).
#define HARUKA_VIEW_PUSH_CONSTANT layout(push_constant) uniform HarukaViewPush { int viewIndex; uint lightChannels; } hkView
// 푸시 상수 그대로의 값입니다. 섀도 패스는 이 자리에 캐스케이드 번호를 넣습니다(shadow.vert).
#define HARUKA_VIEW_INDEX hkView.viewIndex
// 메인 패스의 눈 번호(멀티뷰, 2026-11-18). 두 눈을 멀티뷰(viewMask 0b11)로 한 번에 그리면 푸시 상수는 0 이고 gl_ViewIndex 가
// 0 · 1 입니다. 눈마다 그리는 경로(단안 · --no-multiview · 반사 캡처)는 렌더 패스가 멀티뷰가 아니라 gl_ViewIndex 가 0 이고
// 푸시 상수가 눈 번호입니다. 그래서 두 값을 더하면 어느 경로든 같은 셰이더로 맞는 눈을 고릅니다(파이프라인만 두 벌).
// 쓰는 셰이더는 맨 앞에 #extension GL_EXT_multiview : require 를 둡니다(장치는 멀티뷰 기능을 늘 켭니다, VulkanBaseline.hpp).
#define HARUKA_EYE_INDEX (hkView.viewIndex + int(gl_ViewIndex))
// QTangent(C2) → 법선 · 접선 · 바이탱전트 손잡이. C++ 쪽 hqtan::decode 와 같은 식입니다.
void hkDecodeQTangent(vec4 q, out vec3 n, out vec3 t, out float sgn) {
    q = normalize(q);
    t = vec3(1.0 - 2.0 * (q.y * q.y + q.z * q.z), 2.0 * (q.x * q.y + q.w * q.z), 2.0 * (q.x * q.z - q.w * q.y));
    n = vec3(2.0 * (q.x * q.z + q.w * q.y), 2.0 * (q.y * q.z - q.w * q.x), 1.0 - 2.0 * (q.x * q.x + q.y * q.y));
    sgn = q.w < 0.0 ? -1.0 : 1.0;
}
// 3 행 아핀 트랜스폼 → mat4 (O15). transpose 한 번은 정점마다 상수 비용입니다.
mat4 hkModel(GpuTransform t) {
    return transpose(mat4(t.rows[0], t.rows[1], t.rows[2], vec4(0.0, 0.0, 0.0, 1.0)));
}
#endif

#endif // HARUKA_SHARED_H
