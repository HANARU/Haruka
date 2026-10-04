#version 450
#extension GL_GOOGLE_include_directive : require
#include "haruka_shared.h"

layout(set = 0, binding = 0) uniform FrameBlock { FrameUBO ubo; };

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

void main() {
    gl_Position = ubo.viewProj[0] * vec4(inPos, 1.0);
    fragColor   = inColor;
}
