#version 450

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4  viewProj[2];
    mat4  lightViewProj;
    mat4  prevViewProj;
    vec4  cameraPos;
    vec4  screenParams;
    vec4  timeParams;
    ivec4 miscParams;
} ubo;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

void main() {
    gl_Position = ubo.viewProj[0] * vec4(inPos, 1.0);
    fragColor   = inColor;
}
