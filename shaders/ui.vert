#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(push_constant) uniform PC {
    vec2 screenSize; // 렌더 타겟 픽셀 크기
} pc;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;

void main() {
    fragUV    = inUV;
    fragColor = inColor;
    // 픽셀 좌표 → NDC (좌상단 원점, y 아래)
    vec2 ndc = (inPos / pc.screenSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
