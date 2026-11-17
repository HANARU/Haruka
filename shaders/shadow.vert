#version 450
#extension GL_GOOGLE_include_directive : require
#include "haruka_shared.h"

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inQTangent;   // QTangent(C2, snorm16): 법선 · 접선 · 손잡이

layout(set = 0, binding = 0) uniform FrameBlock { FrameUBO ubo; };
// 캐스케이드 번호(CSM, 2026-11-17): ShadowPass 가 눈 번호 자리(viewIndex)에 0 = 가까운 단, 1 = 먼 단을 넣습니다.
HARUKA_VIEW_PUSH_CONSTANT;

layout(set = 2, binding = 0) readonly buffer TransformBuffer {
    GpuTransform transforms[];
};

void main() {
    mat4 vp = HARUKA_VIEW_INDEX == 0 ? ubo.lightViewProjNear : ubo.lightViewProj;
    gl_Position = vp * hkModel(transforms[gl_InstanceIndex]) * vec4(inPosition, 1.0);
}
