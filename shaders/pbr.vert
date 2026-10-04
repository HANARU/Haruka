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
    fragShadowCoord = ubo.lightViewProj * worldPos;

    gl_Position = ubo.viewProj[HARUKA_VIEW_INDEX] * worldPos;
}
