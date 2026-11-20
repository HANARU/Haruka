#version 450
// Post-process: SSAO apply + Bloom composite + ACES tone mapping
layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

// Binding order matches PostPass::recordTonemap push descriptor order:
//   0=HDR, 1=ssao, 2=bloom
layout(set = 0, binding = 0) uniform sampler2D hdrImage;
layout(set = 0, binding = 1) uniform sampler2D ssaoImage;
layout(set = 0, binding = 2) uniform sampler2D bloomImage;

// Push constant layout matches C++ PostPC struct (PostPass.cpp)
layout(push_constant) uniform Params {
    float aoStrength;
    float bloomStrength;
    float exposure;
    float aoEnabled;
    float bloomEnabled;
    float encodeSrgb;   // 1 = 스왑체인이 UNORM 이라 여기서 sRGB 곡선을 적용, 0 = GPU 가 저장할 때 적용
    // 씬 이미지에서 읽을 부분. 보통 전체(1,1 · 0,0)이고, XR 왼쪽 눈을 에디터 뷰포트에 비출 때
    // 뷰포트 비율에 맞춰 가운데를 잘라 읽습니다.
    vec2  uvScale;
    vec2  uvOffset;
    // 접근성(2026-11-21, Docs/Accessibility.md D6 · D9). 0 이면 그 단계를 건너뛰어 예전과 같은 화소입니다.
    float vignette;      // 이동 중 비네트 세기 0 ~ 1
    float colorFilter;   // 1 = 색 보정 행렬을 곱함
    vec4  colorCol0;     // 보정 행렬 열(xyz)
    vec4  colorCol1;
    vec4  colorCol2;
};

// 선형 → sRGB (IEC 61966-2-1 곡선).
vec3 linearToSrgb(vec3 c) {
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}

// ACES fitted by Krzysztof Narkowicz
vec3 ACESFilm(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec2 uv  = inUV * uvScale + uvOffset;
    vec4 src = texture(hdrImage, uv);
    vec3 hdr = src.rgb * exposure;

    // SSAO — multiplicative on whole output (approximation for forward rendering)
    if (aoEnabled > 0.5) {
        float ao = texture(ssaoImage, uv).r;
        hdr *= mix(1.0, ao, aoStrength);
    }

    // Bloom additive
    if (bloomEnabled > 0.5) {
        vec3 bloom = texture(bloomImage, uv).rgb;
        hdr += bloom * bloomStrength;
    }

    // ACES tone mapping. 결과는 선형값입니다. sRGB 스왑체인은 GPU 가 저장할 때 sRGB 로 바꾸므로
    // 여기서 또 바꾸면 두 번 들어가 화면이 밝고 흐려집니다(DEF-96, 예전에는 늘 pow(1/2.2) 를 적용).
    vec3 ldr = ACESFilm(hdr);

    // 3D UI 패널(D1-나, 2026-11-16): HDR 알파는 후처리 가중치입니다. 장면은 1(지우기 값, 장면 파이프라인은 알파를 쓰지
    // 않음)이고 패널 화소는 0 이라, 패널 색을 노출 · AO · 블룸 · 톤매핑 없이 에디터 UI 와 같은 색으로 냅니다. MSAA 로 섞인
    // 가장자리는 두 결과를 가중치로 섞습니다. 1 이면 예전과 같은 경로라 패널이 없는 화면은 바이트 단위로 같습니다.
    if (src.a < 1.0) ldr = mix(clamp(src.rgb, 0.0, 1.0), ldr, clamp(src.a, 0.0, 1.0));

    // 색 보정(D9): Machado 시뮬레이션으로 만든 보정 행렬을 선형 색에 곱합니다.
    if (colorFilter > 0.5)
        ldr = clamp(mat3(colorCol0.xyz, colorCol1.xyz, colorCol2.xyz) * ldr, 0.0, 1.0);

    // 이동 중 비네트(D6): 눈 화면 가운데에서 멀수록 어둡게. haccess::vignetteMask 와 같은 식입니다.
    if (vignette > 0.0) {
        float r     = length((inUV - 0.5) * 2.0);
        float inner = mix(0.95, 0.3, vignette);
        ldr *= 1.0 - min(2.0 * vignette, 1.0) * smoothstep(inner, inner + 0.45, r);
    }

    if (encodeSrgb > 0.5) ldr = linearToSrgb(ldr);
    outColor = vec4(ldr, 1.0);
}
