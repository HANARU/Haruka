#version 450
// Generates the split-sum BRDF LUT (GGX, importance sampling)
// UV.x = NdotV, UV.y = roughness  →  output = (scale, bias) for specular split-sum
layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec2 outBRDF;

const float PI = 3.14159265359;
const uint  SAMPLE_COUNT = 1024u;

float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(uint i, uint N) {
    return vec2(float(i) / float(N), RadicalInverse_VdC(i));
}

vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a   = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosT = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinT = sqrt(1.0 - cosT * cosT);
    vec3  H    = vec3(cos(phi) * sinT, sin(phi) * sinT, cosT);
    vec3  up   = abs(N.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3  T    = normalize(cross(up, N));
    vec3  B    = cross(N, T);
    return normalize(T * H.x + B * H.y + N * H.z);
}

float GeometrySchlickGGX_IBL(float NdotV, float roughness) {
    float a = roughness;
    float k = (a * a) / 2.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith_IBL(float NdotV, float NdotL, float roughness) {
    return GeometrySchlickGGX_IBL(NdotV, roughness) * GeometrySchlickGGX_IBL(NdotL, roughness);
}

void main() {
    float NdotV     = max(inUV.x, 0.001);
    float roughness = inUV.y;
    vec3  V   = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    vec3  N   = vec3(0, 0, 1);
    float A = 0.0, B = 0.0;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2  Xi   = Hammersley(i, SAMPLE_COUNT);
        vec3  H    = ImportanceSampleGGX(Xi, N, roughness);
        vec3  L    = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);
        if (NdotL > 0.0) {
            float G     = GeometrySmith_IBL(NdotV, NdotL, roughness);
            float G_Vis = (G * VdotH) / max(NdotH * NdotV, 0.001);
            float Fc    = pow(1.0 - VdotH, 5.0);
            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    outBRDF = vec2(A, B) / float(SAMPLE_COUNT);
}
