#version 450
#extension GL_GOOGLE_include_directive : require
#include "haruka_shared.h"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;  // xyz=tangent, w=handedness

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragT;
layout(location = 3) out vec3 fragB;
layout(location = 4) out vec3 fragN;
layout(location = 5) out vec4 fragShadowCoord;

layout(set = 0, binding = 0) uniform FrameBlock { FrameUBO ubo; };
HARUKA_VIEW_PUSH_CONSTANT;   // 눈 번호(DEF-09). pbr.frag 와 같은 선언

layout(set = 2, binding = 0) readonly buffer TransformBuffer {
    GpuTransform transforms[];
};

// 깊이 프리패스(depth.vert)와 같은 깊이를 내야 색 서브패스의 LESS_OR_EQUAL 비교를 통과합니다.
// invariant 와 글자 그대로 같은 식으로 이를 보장합니다. 위치 식을 바꿀 때는 depth.vert 와 같이 바꿉니다.
invariant gl_Position;

void main() {
    mat4 model   = transforms[gl_InstanceIndex].model;
    vec4 worldPos = model * vec4(inPosition, 1.0);
    fragWorldPos  = worldPos.xyz;
    fragUV        = inUV;

    mat3 normalMat = transpose(inverse(mat3(model)));
    vec3 N = normalize(normalMat * inNormal);
    vec3 T = normalize(normalMat * inTangent.xyz);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * inTangent.w;
    fragT = T; fragB = B; fragN = N;

    // Shadow clip-space coordinate (perspective divide done in frag)
    // 리시버 노멀 오프셋(2026-11-02): 조회 위치를 빛 쪽 노멀 방향으로 shadowParams.w × sinθ 만큼 옮깁니다(Filament 식).
    // 섀도 패스가 뒷면 컬링이라 빛을 받는 면은 자기 깊이와 비교되므로, 명암 경계 앞의 얼룩을 이것이 막습니다.
    // 빛 방향은 lightViewProj 의 셋째 행에서 얻습니다. 직교 투영이라 그 행의 xyz 는 (표면 → 태양) 방향의 음수배입니다.
    // 섀도 투영의 w 는 1 이라 옮긴 위치를 정점에서 보간해도 정확합니다. gl_Position 식은 건드리지 않습니다(invariant).
    vec3  toSun = -normalize(vec3(ubo.lightViewProj[0][2], ubo.lightViewProj[1][2], ubo.lightViewProj[2][2]));
    float cosL  = dot(N, toSun);
    vec3  nL    = cosL >= 0.0 ? N : -N;
    float sinL  = sqrt(max(1.0 - cosL * cosL, 0.0));
    fragShadowCoord = ubo.lightViewProj * vec4(worldPos.xyz + nL * (ubo.shadowParams.w * sinL), 1.0);

    gl_Position = ubo.viewProj[HARUKA_VIEW_INDEX] * worldPos;
}
