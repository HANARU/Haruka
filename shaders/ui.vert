#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inLocalPos;
layout(location = 4) in vec2 inHalfSize;
layout(location = 5) in vec4 inParams;       // x=radius, y=borderWidth, z=blur, w=mode (0=rect/tex, 1=shadow)
layout(location = 6) in vec4 inBorderColor;

layout(push_constant) uniform PC {
    vec2 screenSize; // 렌더 타겟 픽셀 크기
} pc;

layout(location = 0) out      vec2 fragUV;
layout(location = 1) out      vec4 fragColor;
layout(location = 2) out      vec2 fragLocalPos;
layout(location = 3) flat out vec2 fragHalfSize;
layout(location = 4) flat out vec4 fragParams;
layout(location = 5) flat out vec4 fragBorderColor;

void main() {
    fragUV          = inUV;
    fragColor       = inColor;
    fragLocalPos    = inLocalPos;
    fragHalfSize    = inHalfSize;
    fragParams      = inParams;
    fragBorderColor = inBorderColor;
    // 픽셀 좌표 → NDC (좌상단 원점, y 아래)
    vec2 ndc = (inPos / pc.screenSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
