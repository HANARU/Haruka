#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_multiview : require   // gl_ViewIndex(HARUKA_EYE_INDEX, 멀티뷰 메인 패스)
#include "haruka_shared.h"

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inQTangent;   // QTangent(C2, snorm16): 법선 · 접선 · 손잡이

layout(set = 0, binding = 0) uniform FrameBlock { FrameUBO ubo; };
HARUKA_VIEW_PUSH_CONSTANT;   // 눈 번호(DEF-09)

layout(set = 2, binding = 0) readonly buffer TransformBuffer {
    GpuTransform transforms[];
};

// 깊이 프리패스(GREATER · 깊이 쓰기, Reverse-Z)와 불투명 색 서브패스(GREATER_OR_EQUAL · 쓰기 끔)가 같은 깊이를 내야
// 같은 표면이 비교를 통과합니다. invariant 와 글자 그대로 같은 식으로 이를 보장합니다.
// 위치 식을 바꿀 때는 pbr.vert 와 같이 바꿉니다.
invariant gl_Position;

void main() {
    mat4 model   = hkModel(transforms[gl_InstanceIndex]);
    vec4 worldPos = model * vec4(inPosition, 1.0);
    gl_Position = ubo.viewProj[HARUKA_EYE_INDEX] * worldPos;
}
