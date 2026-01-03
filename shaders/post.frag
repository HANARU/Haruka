#version 450
// Post-process: TAA + SSAO apply + Bloom composite + ACES tone mapping
layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

// Binding order matches Renderer_render.cpp push descriptor order:
//   0=HDR, 1=taaHistory, 2=ssao, 3=bloom
layout(set = 0, binding = 0) uniform sampler2D hdrImage;
layout(set = 0, binding = 1) uniform sampler2D historyImage;
layout(set = 0, binding = 2) uniform sampler2D ssaoImage;
layout(set = 0, binding = 3) uniform sampler2D bloomImage;

// Push constant layout matches C++ PostPC struct:
//   aoStr, bloomStr, exposure, taaBlend, enableAO, enableBloom, enableTAA, pad
layout(push_constant) uniform Params {
    float aoStrength;
    float bloomStrength;
    float exposure;
    float taaBlend;
    float aoEnabled;
    float bloomEnabled;
    float taaEnabled;
    float pad;
};

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

    // TAA temporal blend with history
    if (taaEnabled > 0.5) {
        vec3 history = texture(historyImage, inUV).rgb;
        hdr = mix(hdr, history, taaBlend);
    }

    // ACES tone mapping + gamma
    vec3 ldr = ACESFilm(hdr);
    ldr      = pow(ldr, vec3(1.0 / 2.2));
    outColor = vec4(ldr, 1.0);
}
