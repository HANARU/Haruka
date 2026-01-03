#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragT;
layout(location = 3) in vec3 fragB;
layout(location = 4) in vec3 fragN;
layout(location = 5) in vec4 fragShadowCoord;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

// Set 0: FrameUBO
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 viewProj[2];
    mat4 lightViewProj;
    mat4 prevViewProj;
    vec4 cameraPos;
    vec4 screenParams;
    vec4 timeParams;
    ivec4 miscParams;
} ubo;

// Set 1: Scene resources
layout(set = 1, binding = 0) uniform sampler2DShadow shadowMap;
layout(set = 1, binding = 1) uniform samplerCube     irradianceMap;
layout(set = 1, binding = 2) uniform samplerCube     prefilterMap;
layout(set = 1, binding = 3) uniform sampler2D       brdfLUT;

struct GpuLight {
    vec4 posOrDir;   // xyz=pos or dir, w=type (0=dir,1=point,2=spot)
    vec4 colorRange; // xyz=color*intensity, w=range
    vec4 spotDir;    // xyz=spot forward direction
    vec4 spotParams; // x=cosInner, y=cosOuter
};
layout(set = 1, binding = 4) readonly buffer LightBuffer { GpuLight lights[]; };

// Set 3: PBR material (push descriptor)
layout(set = 3, binding = 0) uniform sampler2D albedoMap;
layout(set = 3, binding = 1) uniform sampler2D normalMap;
layout(set = 3, binding = 2) uniform sampler2D metalRoughMap;
layout(set = 3, binding = 3) uniform sampler2D aoMap;
layout(set = 3, binding = 4) uniform sampler2D emissiveMap;

// ── BRDF helpers ──────────────────────────────────────────────────────────────
vec3 F_Schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float D_GGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float G_SchlickGGX(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / (NdotX * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float roughness) {
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

// PCF shadow (3x3 kernel)
float sampleShadow(vec4 coord) {
    vec3 proj = coord.xyz / coord.w;
    proj.xy   = proj.xy * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;
    // 상수 깊이 바이어스입니다. 단위는 NDC 이고 섀도 투영의 깊이 범위가
    // ortho(.., 0.1, 50) 이므로 1 NDC = 49.9 m 입니다 (Renderer_render.cpp:187).
    // 따라서 0.0004 x 49.9 m = 19.96 mm 이고, 섀도맵 텍셀 하나(40 m / 2048 = 19.53 mm)
    // 와 거의 같습니다.
    //
    // 예전 값 0.003 은 149.7 mm 로 텍셀의 7.7 배였습니다. acne 방지는 이미 세 겹인데
    // (ShadowPass 의 앞면 컬링 = second-depth, 래스터라이저 경사 바이어스 1.75,
    // 그리고 이 상수) 그 위에 텍셀 8 개짜리 상수를 더 얹고 있었습니다.
    proj.z -= 0.0004;
    float shadow   = 0.0;
    vec2  texelSz  = 1.0 / vec2(2048.0);
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            shadow += texture(shadowMap, vec3(proj.xy + vec2(x, y) * texelSz, proj.z));
    return shadow / 9.0;
}

// Evaluate one light's contribution
vec3 evalLight(GpuLight L, vec3 N, vec3 V, vec3 worldPos,
               vec3 albedo, float metallic, float roughness, vec3 F0) {
    int type = int(L.posOrDir.w);
    vec3 lightDir;
    float atten = 1.0;

    if (type == 0) {
        lightDir = normalize(-L.posOrDir.xyz);
    } else {
        vec3 toLight = L.posOrDir.xyz - worldPos;
        float dist   = length(toLight);
        lightDir     = toLight / dist;
        float range  = L.colorRange.w;
        float ratio  = clamp(1.0 - (dist / range), 0.0, 1.0);
        atten        = ratio * ratio;
        if (type == 2) {
            float cosA     = dot(-lightDir, normalize(L.spotDir.xyz));
            float cosInner = L.spotParams.x;
            float cosOuter = L.spotParams.y;
            atten *= clamp((cosA - cosOuter) / max(cosInner - cosOuter, 0.001), 0.0, 1.0);
        }
        // 사거리 밖이거나 스팟 원뿔 밖이면 최종 결과가 정확히 0이다.
        // 아래 GGX/Smith/Fresnel을 계산하고 0을 곱하는 대신 여기서 끝낸다.
        // 출력은 비트 단위로 동일하고 낭비만 사라진다.
        if (atten <= 0.0) return vec3(0.0);
    }

    // 표면이 라이트를 등지고 있으면 마지막 * NdotL 때문에 역시 결과가 0이다.
    // NdotL을 H보다 먼저 구해서 조기 종료한다.
    float NdotL = max(dot(N, lightDir), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);

    vec3  H     = normalize(V + lightDir);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    vec3  F = F_Schlick(VdotH, F0);
    float D = D_GGX(NdotH, roughness);
    float G = G_Smith(NdotV, NdotL, roughness);

    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
    vec3 kD       = (1.0 - F) * (1.0 - metallic);

    return (kD * albedo / PI + specular) * L.colorRange.rgb * NdotL * atten;
}

void main() {
    // ── Sample PBR maps ───────────────────────────────────────────────────────
    // albedoMap과 emissiveMap은 VK_FORMAT_R8G8B8A8_SRGB로 업로드되므로 샘플러가
    // 이미 선형 값을 돌려준다. 예전에는 여기서 pow(rgb, 2.2)로 한 번 더 디코드해서
    // 감마가 이중 적용되었고, 색이 들어간 머티리얼이 실제보다 어둡게 나왔다.
    // 흰색(1.0)은 pow(1,2.2)=1 이라 증상이 드러나지 않아 오래 남아 있었다.
    vec4  albedoA   = texture(albedoMap, fragUV);
    vec3  albedo    = albedoA.rgb;
    float alpha     = albedoA.a;

    vec3  nrmTex    = texture(normalMap, fragUV).rgb * 2.0 - 1.0;
    mat3  TBN       = mat3(fragT, fragB, fragN);
    vec3  N         = normalize(TBN * nrmTex);

    vec2  mrTex     = texture(metalRoughMap, fragUV).bg;  // glTF: B=metallic, G=roughness
    float metallic  = mrTex.x;
    float roughness = max(mrTex.y, 0.04);

    float ao        = texture(aoMap,       fragUV).r;
    vec3  emissive  = texture(emissiveMap, fragUV).rgb;   // SRGB 포맷이라 이미 선형이다

    vec3  V   = normalize(ubo.cameraPos.xyz - fragWorldPos);
    vec3  F0  = mix(vec3(0.04), albedo, metallic);

    // ── Direct lighting ───────────────────────────────────────────────────────
    // miscParams.w = 섀도 캐스터 라이트의 SSBO 슬롯 번호(<0 이면 섀도 없음, DEF-26). 슬롯 0 을
    // 가정하지 않고, 계산한 그림자를 **그 라이트의 기여에만** 곱한다. 예전에는 Lo 전체에 곱해
    // 포인트/스팟이 비추는 영역까지 디렉셔널 그림자로 어두워졌다.
    int   lightCount = ubo.miscParams.x;
    int   shadowIdx  = ubo.miscParams.w;
    float shadow     = (shadowIdx >= 0) ? sampleShadow(fragShadowCoord) : 1.0;

    vec3  Lo = vec3(0.0);
    for (int i = 0; i < lightCount; i++) {
        vec3 c = evalLight(lights[i], N, V, fragWorldPos, albedo, metallic, roughness, F0);
        if (i == shadowIdx) c *= shadow;
        Lo += c;
    }

    // ── IBL ambient ───────────────────────────────────────────────────────────
    float NdotV       = max(dot(N, V), 0.0);
    vec3  F_ibl       = F_Schlick(NdotV, F0);
    vec3  kD_ibl      = (1.0 - F_ibl) * (1.0 - metallic);

    vec3  irradiance  = texture(irradianceMap, N).rgb;
    vec3  diffuseIBL  = kD_ibl * irradiance * albedo;

    vec3  R           = reflect(-V, N);
    float mipLevel    = roughness * 4.0;
    vec3  prefiltered = textureLod(prefilterMap, R, mipLevel).rgb;
    vec2  brdf        = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    vec3  specularIBL = prefiltered * (F_ibl * brdf.x + brdf.y);

    vec3  ambient  = (diffuseIBL + specularIBL) * ao;

    // ── Final ─────────────────────────────────────────────────────────────────
    vec3 color = ambient + Lo + emissive;
    outColor   = vec4(color, alpha);
}
