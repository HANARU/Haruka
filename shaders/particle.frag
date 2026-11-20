#version 450

// 빌보드 파티클(2026-11-20, Docs/Particles_And_Decals.md D7). 색은 파티클 색 × 모양 아틀라스이고, 알파를 곱한 값을 냅니다.
// 섞기는 파이프라인이 정합니다: 알파 = (ONE, ONE_MINUS_SRC_ALPHA), 가산 = (ONE, ONE). HDR 의 알파(후처리 가중치)는 (ZERO, ONE) 으로
// 그대로 둡니다. 조명 · 그림자 · 안개는 받지 않습니다.
layout(set = 1, binding = 0) uniform sampler2D spriteAtlas;

layout(location = 0) in  vec2 fragUV;
layout(location = 1) in  vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    vec4  t = texture(spriteAtlas, fragUV);
    float a = fragColor.a * t.a;
    outColor = vec4(fragColor.rgb * t.rgb * a, a);
}
