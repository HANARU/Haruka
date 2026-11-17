#version 450

// 월드 UI 패널(3D UI P1b). 텍스처는 알파를 곱한 색(HSlateRenderer::recordPanel)이라 그대로 냅니다. 혼합은 파이프라인이
// ONE · ONE_MINUS_SRC_ALPHA 로 하고, 알파 채널(후처리 가중치)에는 (1 - 패널 알파)를 곱합니다(GeometryPass).
// 조명 · 그림자 · 안개를 받지 않습니다(UI 는 스스로 빛나는 화면처럼 보임).
layout(set = 1, binding = 0) uniform sampler2D panelTex;
layout(push_constant) uniform PanelPush { mat4 model; int viewIndex; int flags; float brightness; float pad; } pc;   // GpuPanelPush

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    if (!gl_FrontFacing && (pc.flags & 1) == 0) discard;   // 한 면 패널의 뒷면
    // 뒷면에서 보면 로컬 +X 가 왼쪽이라, 좌우를 뒤집어 글자가 바로 읽히게 합니다.
    vec2 uv = gl_FrontFacing ? fragUV : vec2(1.0 - fragUV.x, fragUV.y);
    // 밝기는 색에만 곱합니다(알파를 곱한 색이라 알파 = 덮는 정도는 그대로, 3D UI 구간 4).
    vec4 c = texture(panelTex, uv);
    outColor = vec4(c.rgb * pc.brightness, c.a);
}
