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

// 프레임 상수 (set 0, binding 0). 셰이더에서는 블록 안의 ubo 로 둡니다.
// 눈마다 다른 값(행렬 · 눈 위치)은 처음부터 [HARUKA_MAX_VIEWS] 로 둡니다(DEF-09). 멀티뷰로 옮길 때
// 셰이더는 뷰 번호만 gl_ViewIndex 로 바꾸면 됩니다. 단안이면 [1] 은 [0] 과 같은 값입니다.
// prevViewProj(가짜 TAA 용, 읽는 곳 없음)는 2026-10-04 에 지웠습니다(DEF-15). 진짜 TAA 나
// Quest 의 프레임 합성(AppSW)에는 눈별 이전 행렬이 필요하므로 그때 viewProj 처럼 [2] 로 넣습니다.
struct FrameUBO {
    mat4  viewProj[HARUKA_MAX_VIEWS];   // 눈별 view-projection
    mat4  lightViewProj;                // shadow camera VP (두 눈 공용)
    vec4  eyePos[HARUKA_MAX_VIEWS];     // xyz=눈의 월드 위치, w=nearPlane
    vec4  screenParams;    // xy=눈 하나의 렌더 크기, z=farPlane, w=aoStrength
    vec4  timeParams;      // x=time, y=frame, z=bloomThreshold, w=exposure
    ivec4 miscParams;      // x=lightCount, y=frame, z=ssaoEnabled, w=shadowLightIndex(<0=none)
};

// 메시마다 월드 행렬 (set 2, binding 0). 행 번호 = gl_InstanceIndex.
struct GpuTransform {
    mat4 model;
};

// 라이트 (set 1, binding 4).
struct GpuLight {
    vec4 posOrDir;    // xyz=pos or dir, w=type (0=dir, 1=point, 2=spot)
    vec4 colorRange;  // xyz=effectiveColor*intensity, w=attenuationRadius (항상 > 0)
    vec4 spotDir;     // xyz=spot forward direction
    vec4 spotParams;  // x=cosInner, y=cosOuter, zw=reserved
};

#ifdef __cplusplus
} // namespace hgpu
#else
// 뷰(눈) 번호. 지금은 눈마다 따로 그리므로(DEF-09 첫 단계) 렌더러가 푸시 상수로 넘깁니다.
// 멀티뷰로 옮기면 HARUKA_VIEW_INDEX 를 gl_ViewIndex 로 바꾸고 푸시 상수를 지웁니다.
// 뷰 번호가 필요한 셰이더는 본문 앞에 HARUKA_VIEW_PUSH_CONSTANT; 를 한 번 적습니다. 정점 · 조각
// 셰이더가 같은 선언을 쓰므로 범위는 두 단계에 걸칩니다(C++ 쪽 VkPushConstantRange 와 맞춤).
#define HARUKA_VIEW_PUSH_CONSTANT layout(push_constant) uniform HarukaViewPush { int viewIndex; } hkView
#define HARUKA_VIEW_INDEX hkView.viewIndex
#endif

#endif // HARUKA_SHARED_H
