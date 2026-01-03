#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4  viewProj[2];
    mat4  lightViewProj;
    mat4  prevViewProj;
    vec4  cameraPos;
    vec4  screenParams;
    vec4  timeParams;
    ivec4 miscParams;
} ubo;

struct GpuTransform { mat4 model; };
layout(set = 2, binding = 0) readonly buffer TransformBuffer {
    GpuTransform transforms[];
};

void main() {
    gl_Position = ubo.viewProj[0] * transforms[gl_InstanceIndex].model * vec4(inPosition, 1.0);
}
