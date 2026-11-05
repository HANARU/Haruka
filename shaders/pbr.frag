#version 450
#extension GL_GOOGLE_include_directive : require
#include "haruka_shared.h"

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragT;
layout(location = 3) in vec3 fragB;
layout(location = 4) in vec3 fragN;
layout(location = 5) in vec4 fragShadowCoord;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

// 기하 스페큘러 AA(Tokuyoshi · Kaplanyan 2019 식 (4), Filament 와 같은 꼴).
//   α'² = saturate(α² + min(2σ²(|dFdx n|² + |dFdy n|²), κ))
// 곡면(구 · 원기둥)의 하이라이트가 화소보다 좁아질 때 생기는 반짝임을 거칠기를 넓혀 줄입니다.
// κ 는 Filament 입력 threshold 0.2 를 제곱한 값입니다. '0.2' 와 '0.04' 를 섞어 쓰지 마십시오(0 이면 꺼짐).
// 렌더 스케일이나 FFR 을 넣으면 미분 항에 해상도 보정 계수의 제곱을 곱할 자리입니다(지금은 1).
const float kSpecAAVariance   = 0.15;   // σ², 화면 공간 필터 분산
const float kSpecAAThreshold2 = 0.04;   // κ, α² 영역 상한

// Set 0: FrameUBO (haruka_shared.h)
layout(set = 0, binding = 0) uniform FrameBlock { FrameUBO ubo; };
HARUKA_VIEW_PUSH_CONSTANT;   // 눈 번호(DEF-09). 반사 방향은 그 눈의 위치로 구합니다

// Set 1: Scene resources
layout(set = 1, binding = 0) uniform sampler2DShadow shadowMap;
layout(set = 1, binding = 1) uniform samplerCube     irradianceMap;
layout(set = 1, binding = 2) uniform samplerCube     prefilterMap;
layout(set = 1, binding = 3) uniform sampler2D       brdfLUT;

layout(set = 1, binding = 4) readonly buffer LightBuffer { GpuLight lights[]; };   // GpuLight: haruka_shared.h

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

// α = roughness² 규약은 D_GGX 와 같습니다. 기하 법선(노멀맵 전)을 미분하며 균일한 흐름에서만 부릅니다.
float specularAARoughness(float perceptualRoughness, vec3 geomN) {
    vec3  du     = dFdx(geomN);
    vec3  dv     = dFdy(geomN);
    float kernel = min(2.0 * kSpecAAVariance * (dot(du, du) + dot(dv, dv)), kSpecAAThreshold2);
    float a      = perceptualRoughness * perceptualRoughness;
    float a2     = clamp(a * a + kernel, 0.0, 1.0);
    return sqrt(sqrt(a2));
}

// 섀도 커널 등급. 파이프라인을 만들 때 정하는 특수화 상수라 분기는 드라이버가 접습니다(GeometryPass::setShadowFilter).
layout(constant_id = HARUKA_SPEC_SHADOW_FILTER) const int kShadowFilter = HARUKA_SHADOW_FILTER_LEGACY9;

// 기본(LEGACY9)은 하드웨어 쌍선형 비교 9탭(정수 오프셋 3x3)입니다. 실효 커널은 지지폭 3~4 텍셀의 끝이
// 매끈한 사다리꼴입니다(PC 하드웨어 PCF 전제). HARD 는 1탭(지지폭 2텍셀)입니다.
float sampleShadow(vec4 coord) {
    vec3 proj = coord.xyz / coord.w;
    proj.xy   = proj.xy * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;
    // 리시버 상수 깊이 바이어스(섀도 NDC)와 텍셀 하나의 UV 크기는 updateUBO 가 FrameUBO.shadowParams 로 넘깁니다.
    // 바이어스의 월드 길이 · acne 방지 세 겹의 설명은 ShadowPass::kReceiverDepthBiasM 주석을 보십시오.
    proj.z -= ubo.shadowParams.z;
    if (kShadowFilter == HARUKA_SHADOW_FILTER_HARD)
        return textureLod(shadowMap, proj, 0.0);
    float shadow   = 0.0;
    vec2  texelSz  = vec2(ubo.shadowParams.y);
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            // 비균일 흐름(라이트 기여가 0 이 아닐 때만 호출) 안이라 암시적 미분이 정의되지
            // 않으므로 LOD 0 을 명시합니다. 섀도 샘플러는 mip 하나 · maxLod 0 이라 결과는 같습니다.
            shadow += textureLod(shadowMap, vec3(proj.xy + vec2(x, y) * texelSz, proj.z), 0.0);
    return shadow / 9.0;
}

// 그림자 거리 페이드(2026-11-02, ShadowFit.hpp). 섀도맵은 머리(두 눈 가운데) 중심 구만 덮으므로, 머리에서 shadowFade.y
// 이상 떨어진 화소는 맵을 읽지 않고 1(그림자 없음)이고, shadowFade.x ~ .y 사이는 섞습니다. 스냅하지 않은 머리 위치를
// 써서 경계가 텍셀 단위로 튀지 않고 부드럽게 움직입니다. 단안이면 eyePos[1] = eyePos[0] 입니다.
float shadowWithFade(vec4 coord) {
    vec3  head = 0.5 * (ubo.eyePos[0].xyz + ubo.eyePos[1].xyz);
    float d    = distance(fragWorldPos, head);
    if (d >= ubo.shadowFade.y) return 1.0;
    return mix(sampleShadow(coord), 1.0, smoothstep(ubo.shadowFade.x, ubo.shadowFade.y, d));
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
        vec3  toLight = L.posOrDir.xyz - worldPos;
        float dist2   = dot(toLight, toLight);
        // 픽셀이 라이트 위치와 겹치면(거리 0) 0 으로 나눠 NaN 이 났습니다(DEF-35). 거리 제곱에
        // 최솟값을 두면 겹친 픽셀의 방향은 0 벡터가 되어 아래 NdotL 조기 종료로 빠집니다.
        lightDir      = toLight * inversesqrt(max(dist2, 1e-12));
        // 거리 감쇠는 실제 점광원처럼 역제곱입니다. 같은 빛이 넓이 4πd² 인 구면에 퍼지므로 거리가
        // 두 배면 밝기는 1/4 입니다. 예전 (1 - d/r)² 은 반경에 묶인 비물리 곡선이었습니다(2026-10-04).
        //  - 1cm 안쪽은 1cm 로 계산합니다. 실제 광원은 점이 아니라 크기가 있기 때문입니다
        //    (Frostbite · Filament 와 같은 값).
        //  - Attenuation Radius 는 꼬리를 0 으로 자르는 지점이지만 반경 안쪽의 밝기도 깎습니다.
        //    창 함수 (1 - (d/r)^4)^2 는 0.5r 에서 0.88, 0.74r 에서 0.50, 0.9r 에서 0.12 이고,
        //    d 가 r 보다 훨씬 작을 때만 순수 역제곱과 같습니다. 이 창 함수는 UE(Karis 2013
        //    강의 노트) · Frostbite · Filament 와 같습니다.
        //  - 세기는 여전히 상대값입니다. intensity 는 1m 거리에서 받는 밝기입니다.
        float range   = L.colorRange.w;   // CPU 가 항상 0 보다 크게 올립니다(최솟값 0.01m)
        float r2      = range * range;
        float t       = clamp(1.0 - (dist2 * dist2) / (r2 * r2), 0.0, 1.0);
        atten         = (t * t) / max(dist2, 1e-4);
        if (type == 2) {
            // 원뿔 계수(scale, offset)는 CPU 가 계산합니다(SpotCone.hpp). 값은 예전
            // (cosA - cosOuter) / max(cosInner - cosOuter, 0.001) 과 같고 곡선은 cos 에 대해 선형입니다.
            // spotDir 은 CPU 가 정규화해 올리므로 다시 normalize 하지 않습니다.
            float cone = clamp(dot(-lightDir, L.spotDir.xyz) * L.spotParams.x + L.spotParams.y, 0.0, 1.0);
            atten *= cone;
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
    // 스페큘러 AA 를 먼저, 최소 거칠기 클램프를 나중에 둡니다(Filament 와 같은 순서). fragN 은 보간으로 길이가
    // 1 이 아니므로 정규화한 값을 미분합니다. 직접광 · IBL mip · BRDF LUT 가 모두 이 값을 씁니다.
    float roughness = max(specularAARoughness(mrTex.y, normalize(fragN)), 0.04);

    float ao        = texture(aoMap,       fragUV).r;
    vec3  emissive  = texture(emissiveMap, fragUV).rgb;   // SRGB 포맷이라 이미 선형이다

    vec3  V   = normalize(ubo.eyePos[HARUKA_VIEW_INDEX].xyz - fragWorldPos);
    vec3  F0  = mix(vec3(0.04), albedo, metallic);

    // ── Direct lighting ───────────────────────────────────────────────────────
    // miscParams.w = 섀도 캐스터 라이트의 SSBO 슬롯 번호(<0 이면 섀도 없음, DEF-26). 슬롯 0 을
    // 가정하지 않고, 계산한 그림자를 **그 라이트의 기여에만** 곱한다. 예전에는 Lo 전체에 곱해
    // 포인트/스팟이 비추는 영역까지 디렉셔널 그림자로 어두워졌다.
    // 캐스터 라이트의 기여가 0 인 픽셀(해를 등진 면 등)은 섀도맵을 읽지 않는다. 0 에 무엇을
    // 곱해도 0 이므로 결과는 같고, 눈마다 9탭이 줄어든다.
    int   lightCount = ubo.miscParams.x;
    int   shadowIdx  = ubo.miscParams.w;

    vec3  Lo = vec3(0.0);
    for (int i = 0; i < lightCount; i++) {
        vec3 c = evalLight(lights[i], N, V, fragWorldPos, albedo, metallic, roughness, F0);
        if (i == shadowIdx && any(notEqual(c, vec3(0.0)))) c *= shadowWithFade(fragShadowCoord);
        Lo += c;
    }

    // ── IBL ambient ───────────────────────────────────────────────────────────
    float NdotV       = max(dot(N, V), 0.0);
    vec3  F_ibl       = F_Schlick(NdotV, F0);
    vec3  kD_ibl      = (1.0 - F_ibl) * (1.0 - metallic);

    vec3  irradiance  = texture(irradianceMap, N).rgb;
    vec3  diffuseIBL  = kD_ibl * irradiance * albedo;

    vec3  R           = reflect(-V, N);
    // 굽기 쪽 거칠기 = mip / (kPrefMips - 1)(Renderer.cpp 프리필터 루프)과 같은 선형 대응입니다.
    // mip 수는 숫자로 적지 않고 이미지 뷰(levelCount = kPrefMips)에서 읽습니다.
    float maxMip      = float(textureQueryLevels(prefilterMap) - 1);
    float mipLevel    = roughness * maxMip;
    vec3  prefiltered = textureLod(prefilterMap, R, mipLevel).rgb;
    vec2  brdf        = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    vec3  specularIBL = prefiltered * (F_ibl * brdf.x + brdf.y);
    // 수평선 오클루전: 노멀맵이 기울인 반사 R 이 기하 법선 기준으로 표면 아래를 향하면 하늘 큐브 아래 반구(땅색)를
    // 반사하므로 줄입니다. h = min(1 + R·Ng, 1), h² 를 곱함(Godot · Babylon 계열). 평평한 노멀맵에서는 h = 1 이고,
    // 확산과 직접광에는 곱하지 않습니다.
    vec3  Ng          = normalize(fragN);
    float horizon     = min(1.0 + dot(R, Ng), 1.0);
    specularIBL      *= horizon * horizon;

    // 머티리얼 AO 는 람베르트 가정으로 유도된 값이라 간접 확산에만 그대로 곱합니다. 간접
    // 스페큘러에는 Lagarde · de Rousiers, "Moving Frostbite to PBR"(2014) Listing 26 의
    // 스페큘러 오클루전을 씁니다. 식의 roughness 자리는 GGX α(= 지각 거칠기²)입니다.
    // ao = 1 이면 예전 식을 그대로 써서, occlusion 텍스처가 없는 머티리얼의 결과를 바이트까지 둡니다.
    vec3  ambient  = (diffuseIBL + specularIBL) * ao;
    if (ao < 1.0) {
        float aSpec  = roughness * roughness;
        float specAO = clamp(pow(NdotV + ao, exp2(-16.0 * aSpec - 1.0)) - 1.0 + ao, 0.0, 1.0);
        ambient      = diffuseIBL * ao + specularIBL * specAO;
    }

    // ── Final ─────────────────────────────────────────────────────────────────
    vec3 color = ambient + Lo + emissive;
    // HDR 타깃은 16비트 부동소수(최대 65504)입니다. 역제곱 감쇠에서는 라이트 바로 앞의 매끈한 면이
    // 이 값을 넘을 수 있고, 넘으면 inf 가 되어 블룸·AO 로 번집니다. 그 전에 잘라 둡니다.
    color      = min(color, vec3(60000.0));
    outColor   = vec4(color, alpha);
}
