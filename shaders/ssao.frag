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
    // pad 는 두지 않습니다. std140 에서 float 배열은 원소마다 16 바이트로
    // 부풀어 블록 크기가 C++ sizeof 를 넘어섭니다. 여기서 끝내면 양쪽 다 1120 입니다.
} data;

vec3 reconstructViewPos(vec2 uv, float linDepth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 view = data.invProj * clip;
    view /= view.w;
    return (view.xyz / (-view.z)) * linDepth;
}

vec3 depthNormal(vec2 uv) {
    vec2 px = 1.0 / vec2(textureSize(linearDepth, 0));
    float l = texture(linearDepth, uv - vec2(px.x, 0.0)).r;
    float r = texture(linearDepth, uv + vec2(px.x, 0.0)).r;
    float u = texture(linearDepth, uv + vec2(0.0, px.y)).r;
    float d = texture(linearDepth, uv - vec2(0.0, px.y)).r;
    vec3 dX = reconstructViewPos(uv + vec2(px.x, 0.0), r) - reconstructViewPos(uv - vec2(px.x, 0.0), l);
    vec3 dY = reconstructViewPos(uv + vec2(0.0, px.y), u) - reconstructViewPos(uv - vec2(0.0, px.y), d);
    // uv.x 가 커지면 화면 오른쪽 = 뷰 +X 이고, uv.y 가 커지면 화면 아래인데
    // proj[1][1] *= -1 (CameraComponent.cpp) 때문에 그것이 뷰 -Y 입니다.
    // 따라서 cross(dX, dY) 는 (0,0,-1), 즉 **카메라 반대쪽**을 가리킵니다.
    // 부호를 뒤집어야 카메라를 향하는 진짜 표면 법선이 됩니다.
    //
    // 이 반전은 아래 67 행의 비교 부호 반전과 정확히 상쇄되어 있었습니다.
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
    vec3 normal  = depthNormal(inUV);

    vec3 randomVec = texture(noiseTex, inUV * data.noiseScale).rgb * 2.0 - 1.0;
    randomVec.z    = 0.0;
    randomVec      = normalize(randomVec);

    vec3 tangent   = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN       = mat3(tangent, bitangent, normal);

    // Compute projection matrix once (invProj is a uniform)
    mat4 proj = inverse(data.invProj);

    float occlusion = 0.0;
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        vec3 samplePos = fragPos + TBN * data.samples[i].xyz * data.radius;

        // Project sample to UV for depth lookup
        vec4 offset  = proj * vec4(samplePos, 1.0);
        offset.xyz  /= offset.w;
        offset.xy    = offset.xy * 0.5 + 0.5;

        float sampleDepth = texture(linearDepth, offset.xy).r;
        float rangeCheck  = smoothstep(0.0, 1.0, data.radius / abs(depth - sampleDepth));
        // -samplePos.z 는 샘플 지점의 선형 깊이이고 sampleDepth 는 그 화면 위치에서
        // 실제로 보이는 표면의 선형 깊이입니다. 표면이 샘플보다 **카메라에 가까울 때**
        // 그 샘플이 가려진 것입니다. 예전 부호는 반대였습니다 (33 행 참조).
        occlusion        += (sampleDepth <= -samplePos.z - data.bias ? 1.0 : 0.0) * rangeCheck;
    }

    outAO = pow(1.0 - (occlusion / float(NUM_SAMPLES)), data.power);
}
