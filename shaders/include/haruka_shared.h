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

// 프레임 상수 (set 0, binding 0). 셰이더에서는 블록 안의 ubo 로 둡니다.
// prevViewProj(가짜 TAA 용, 읽는 곳 없음)는 2026-10-04 에 지웠습니다(DEF-15). 진짜 TAA 나
// Quest 의 프레임 합성(AppSW)에는 눈별 이전 행렬이 필요하므로 그때 viewProj 처럼 [2] 로 넣습니다.
struct FrameUBO {
    mat4  viewProj[2];     // [0]=left/mono, [1]=right (OpenXR stereo)
    mat4  lightViewProj;   // shadow camera VP
    vec4  cameraPos;       // xyz=world pos, w=nearPlane
    vec4  screenParams;    // xy=screenSize, z=farPlane, w=aoStrength
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
#endif

#endif // HARUKA_SHARED_H
