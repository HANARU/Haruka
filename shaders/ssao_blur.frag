#version 450
// SSAO 깊이 인식 블러(2026-11-02). 절반 해상도 AO 를 4x4 창(16탭)으로 한 번 평균합니다.
//
// 왜 4x4 인가: ssao.frag 의 회전 노이즈는 4x4 타일이 절반 해상도에서 반복됩니다(AOPass 의 noiseScale). 연속한 4텍셀
// 창에는 어느 위치에서든 노이즈 값 16개가 정확히 한 번씩 들어가므로, 평탄한 곳에서는 타일 무늬(전체 해상도 8화소 주기)가
// 남김없이 지워집니다. 패스 하나라 눈마다 렌더 패스가 하나만 늘어납니다(분리형 2패스보다 Quest 에 유리).
//
// 반 텍셀 치우침: 짝수 창은 가운데가 없습니다. 오프셋 -2..+1 창의 중심은 출력 텍셀 중심에서 왼쪽 위로 0.5 텍셀입니다.
// 한편 linDepth.frag 는 절반 해상도 텍셀 c 에 전체 해상도 화소 2c+1(오른쪽 아래 화소)을 담으므로, 입력 AO 는 텍셀 중심보다
// 오른쪽 아래로 0.25 텍셀 치우쳐 있습니다. -2..+1 쪽을 골라 두 치우침이 일부 상쇄되고, 남는 것은 왼쪽 위로 0.25 텍셀
// (전체 해상도 0.5 화소)입니다. 톤매핑의 쌍선형 확대보다 작아 보정하지 않습니다. -1..+2 를 고르면 0.75 텍셀이 됩니다.
//
// 깊이 가중: 이웃 깊이를 "중심 깊이 + 경사 x 거리"로 예측하고, 예측과의 차이가 중심 깊이의 kRelTol 배를 넘으면 가중이 0
// 으로 떨어집니다(경사 보정이 있어 비스듬한 바닥은 그대로 평균되고, 물체 윤곽 너머로는 AO 가 번지지 않음). 경사는 축마다
// 중심과 차이가 작은 쪽 이웃으로 구합니다(ssao.frag 의 depthNormal 과 같은 선택). 하늘(zFar)은 넣지 않습니다.
// 가중이 잘린 윤곽 근처는 창 안의 노이즈 값이 16개를 다 채우지 못해 약한 무늬가 남을 수 있습니다.
layout(location = 0) in  vec2 inUV;
layout(location = 0) out float outAO;

layout(set = 0, binding = 0) uniform sampler2D aoRaw;        // ssao.frag 결과(절반 해상도 R8)
layout(set = 0, binding = 1) uniform sampler2D linearDepth;  // linDepth.frag 결과(절반 해상도 R32F). texelFetch 만 씀(DEF-156)

layout(push_constant) uniform Params {
    float zFar;      // 배경 판정(linDepth 가 깊이 클리어를 정확히 zFar 로 보냄)
    float relTol;    // 예측 깊이와의 허용 차(중심 깊이에 대한 비율)
} pc;

void main() {
    const ivec2 sz = textureSize(aoRaw, 0);
    const ivec2 c  = ivec2(gl_FragCoord.xy);
    const float zc = texelFetch(linearDepth, c, 0).r;
    if (zc >= pc.zFar * 0.999) { outAO = 1.0; return; }

    // 축마다 경사(텍셀당 깊이 변화). 화면 가장자리에서는 안쪽 이웃만 씁니다.
    const float zl = texelFetch(linearDepth, clamp(c + ivec2(-1, 0), ivec2(0), sz - 1), 0).r;
    const float zr = texelFetch(linearDepth, clamp(c + ivec2( 1, 0), ivec2(0), sz - 1), 0).r;
    const float zu = texelFetch(linearDepth, clamp(c + ivec2(0, -1), ivec2(0), sz - 1), 0).r;
    const float zd = texelFetch(linearDepth, clamp(c + ivec2(0,  1), ivec2(0), sz - 1), 0).r;
    const bool  useR = c.x == 0 || (c.x < sz.x - 1 && abs(zr - zc) < abs(zl - zc));
    const bool  useD = c.y == 0 || (c.y < sz.y - 1 && abs(zd - zc) < abs(zu - zc));
    const vec2  slope = vec2(useR ? zr - zc : zc - zl, useD ? zd - zc : zc - zu);
    const float tol   = pc.relTol * zc;

    float sum = 0.0, wsum = 0.0;
    for (int dy = -2; dy <= 1; ++dy) {
        for (int dx = -2; dx <= 1; ++dx) {
            const ivec2 p = clamp(c + ivec2(dx, dy), ivec2(0), sz - 1);
            const float z = texelFetch(linearDepth, p, 0).r;
            const float pred = zc + dot(slope, vec2(dx, dy));
            float w = clamp(1.0 - abs(z - pred) / tol, 0.0, 1.0);
            if (z >= pc.zFar * 0.999) w = 0.0;
            sum  += w * texelFetch(aoRaw, p, 0).r;
            wsum += w;
        }
    }
    // 중심(dx = dy = 0)은 늘 가중 1 이라 wsum >= 1 입니다.
    outAO = sum / wsum;
}
