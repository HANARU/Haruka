#version 450
#extension GL_GOOGLE_include_directive : require
#include "haruka_shared.h"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;

layout(set = 0, binding = 0) uniform FrameBlock { FrameUBO ubo; };
HARUKA_VIEW_PUSH_CONSTANT;   // 눈 번호(DEF-09)

layout(set = 2, binding = 0) readonly buffer TransformBuffer {
    GpuTransform transforms[];
};

void main() {
    gl_Position = ubo.viewProj[HARUKA_VIEW_INDEX] * transforms[gl_InstanceIndex].model * vec4(inPosition, 1.0);
}
