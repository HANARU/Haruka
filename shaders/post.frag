#version 450
// Post-process: SSAO apply + Bloom composite + ACES tone mapping
layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

// Binding order matches PostPass::recordTonemap push descriptor order:
//   0=HDR, 1=ssao, 2=bloom
layout(set = 0, binding = 0) uniform sampler2D hdrImage;
layout(set = 0, binding = 1) uniform sampler2D ssaoImage;
layout(set = 0, binding = 2) uniform sampler2D bloomImage;

// Push constant layout matches C++ PostPC struct:
//   aoStr, bloomStr, exposure, enableAO, enableBloom, encodeSrgb, pad x2
layout(push_constant) uniform Params {
    float aoStrength;
    float bloomStrength;
    float exposure;
    float aoEnabled;
    float bloomEnabled;
    float encodeSrgb;   // 1 = 스왑체인이 UNORM 이라 여기서 sRGB 곡선을 적용, 0 = GPU 가 저장할 때 적용
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
    vec3 hdr = texture(hdrImage, inUV).rgb * exposure;

    // SSAO — multiplicative on whole output (approximation for forward rendering)
    if (aoEnabled > 0.5) {
        float ao = texture(ssaoImage, inUV).r;
        hdr *= mix(1.0, ao, aoStrength);
    }

    // Bloom additive
    if (bloomEnabled > 0.5) {
        vec3 bloom = texture(bloomImage, inUV).rgb;
        hdr += bloom * bloomStrength;
    }

    // ACES tone mapping. 결과는 선형값입니다. sRGB 스왑체인은 GPU 가 저장할 때 sRGB 로 바꾸므로
    // 여기서 또 바꾸면 두 번 들어가 화면이 밝고 흐려집니다(DEF-96, 예전에는 늘 pow(1/2.2) 를 적용).
    vec3 ldr = ACESFilm(hdr);
    if (encodeSrgb > 0.5) ldr = linearToSrgb(ldr);
    outColor = vec4(ldr, 1.0);
}
