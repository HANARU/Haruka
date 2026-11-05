#version 450
// Screen-space ambient occlusion — hemisphere sampling in view space
layout(location = 0) in  vec2 inUV;
layout(location = 0) out float outAO;

layout(set = 0, binding = 0) uniform sampler2D linearDepth;
layout(set = 0, binding = 1) uniform sampler2D noiseTex;
layout(set = 0, binding = 2) uniform SSAOData {
    mat4  invProj;
    vec4  samples[64];     // view-space hemisphere samples
    vec2  noiseScale;
    float radius;
    float bias;
    float power;
    float zFar;      // 배경 조기 탈출용. C++ SSAOData 와 오프셋이 일치해야 합니다.
    // pad 는 두지 않습니다. std140 에서 float 배열은 원소마다 16 바이트로 부풀기 때문입니다.
    // 끝의 proj 는 std140 정렬로 1120 에서 시작하고, C++ 의 pad[2] 가 그 앞 8 바이트를 채웁니다. 양쪽 다 1184 입니다.
    mat4  proj;      // 샘플을 화면으로 투영하는 행렬(CPU 가 넘김)
} data;

vec3 reconstructViewPos(vec2 uv, float linDepth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 view = data.invProj * clip;
    view /= view.w;
    return (view.xyz / (-view.z)) * linDepth;
}

// 축마다 중심 깊이와 차이가 작은 이웃으로 한쪽 차분을 씁니다. 예전 중앙 차분(±1 텍셀)은 깊이 경계에서
// 앞 물체와 뒤 배경을 섞어 실루엣 · 모서리 안쪽 띠의 법선이 기울었습니다. 평면에서는 두 방식이 같은 법선을
// 냅니다(원근에서 1/z 가 화면 좌표의 1차식이라 재구성한 점이 늘 한 평면 위). 얇은 물체처럼 양쪽이 모두
// 다른 면이면 여전히 틀리며, 완전한 처리는 축마다 5탭 외삽(약 9페치)입니다.
vec3 depthNormal(vec2 uv, float c, vec3 pc) {
    ivec2 sz = textureSize(linearDepth, 0);
    vec2  px = 1.0 / vec2(sz);
    ivec2 tc = ivec2(gl_FragCoord.xy);   // SSAO 타깃과 linearDepth 는 둘 다 절반 해상도라 같은 텍셀입니다
    float l = texture(linearDepth, uv - vec2(px.x, 0.0)).r;
    float r = texture(linearDepth, uv + vec2(px.x, 0.0)).r;
    float u = texture(linearDepth, uv + vec2(0.0, px.y)).r;   // +uv.y = 화면 아래
    float d = texture(linearDepth, uv - vec2(0.0, px.y)).r;
    // 화면 가장자리에서는 CLAMP_TO_EDGE 가 중심 값을 돌려주므로(차이 0) 그 쪽을 고르면 안 됩니다
    bool useR = tc.x == 0 || (tc.x < sz.x - 1 && abs(r - c) < abs(l - c));
    bool useU = tc.y == 0 || (tc.y < sz.y - 1 && abs(u - c) < abs(d - c));
    // dX 는 어느 쪽을 고르든 +uv.x 방향, dY 는 +uv.y 방향입니다(아래 부호 규약 유지).
    vec3 dX = useR ? reconstructViewPos(uv + vec2(px.x, 0.0), r) - pc
                   : pc - reconstructViewPos(uv - vec2(px.x, 0.0), l);
    vec3 dY = useU ? reconstructViewPos(uv + vec2(0.0, px.y), u) - pc
                   : pc - reconstructViewPos(uv - vec2(0.0, px.y), d);
    // uv.x 가 커지면 화면 오른쪽 = 뷰 +X 이고, uv.y 가 커지면 화면 아래인데
    // proj[1][1] *= -1 (CameraComponent.cpp) 때문에 그것이 뷰 -Y 입니다.
    // 따라서 cross(dX, dY) 는 (0,0,-1), 즉 **카메라 반대쪽**을 가리킵니다.
    // 부호를 뒤집어야 카메라를 향하는 진짜 표면 법선이 됩니다.
    //
    // 이 반전은 main() 의 가림 비교(sampleDepth <= -samplePos.z - data.bias)의 부호와 짝입니다.
    // 두 곳을 반드시 함께 고쳐야 합니다. 하나만 고치면 화면이 통째로 어두워집니다.
    return -normalize(cross(dX, dY));
}

const int NUM_SAMPLES = 64;

void main() {
    float depth = texture(linearDepth, inUV).r;
    // 배경은 깊이 클리어 1.0 이고 linDepth.frag:17 이 그것을 **정확히 zFar** 로 보냅니다.
    // 예전 조건 `depth <= 0.0` 은 그래서 한 번도 참이 되지 않았고, 하늘 픽셀에도
    // 70 번의 텍스처 페치를 전부 돌리고 있었습니다.
    if (depth >= data.zFar * 0.999) { outAO = 1.0; return; }

    vec3 fragPos = reconstructViewPos(inUV, depth);
    vec3 normal  = depthNormal(inUV, depth, fragPos);

    vec3 randomVec = texture(noiseTex, inUV * data.noiseScale).rgb * 2.0 - 1.0;
    randomVec.z    = 0.0;
    randomVec      = normalize(randomVec);

    vec3 tangent   = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN       = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        vec3 samplePos = fragPos + TBN * data.samples[i].xyz * data.radius;

        // Project sample to UV for depth lookup
        vec4 offset  = data.proj * vec4(samplePos, 1.0);
        offset.xyz  /= offset.w;
        offset.xy    = offset.xy * 0.5 + 0.5;

        float sampleDepth = texture(linearDepth, offset.xy).r;
        float rangeCheck  = smoothstep(0.0, 1.0, data.radius / abs(depth - sampleDepth));
        // -samplePos.z 는 샘플 지점의 선형 깊이이고 sampleDepth 는 그 화면 위치에서
        // 실제로 보이는 표면의 선형 깊이입니다. 표면이 샘플보다 **카메라에 가까울 때**
        // 그 샘플이 가려진 것입니다. 예전 부호는 반대였습니다 (depthNormal 의 부호 반전 참조).
        occlusion        += (sampleDepth <= -samplePos.z - data.bias ? 1.0 : 0.0) * rangeCheck;
    }

    outAO = pow(1.0 - (occlusion / float(NUM_SAMPLES)), data.power);
}
