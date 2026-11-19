#version 450
// Resolves MSAA depth to a single-sample R32F linear depth image
layout(location = 0) in  vec2 inUV;
layout(location = 0) out float outLinearDepth;

layout(set = 0, binding = 0) uniform sampler2DMS msaaDepth;
layout(push_constant) uniform Params {
    float nearPlane;
    float farPlane;
    vec2  screenSize;
};

void main() {
    // 절반 해상도 텍셀 c 는 전체 해상도 화소 2c+1(오른쪽 아래)을 담습니다(ssao_blur.frag 의 치우침 설명과 짝). 예전에는
    // ivec2(inUV * screenSize) 로 골랐는데 그 값이 정확히 정수 2c+1 에 떨어져, 보간된 UV 의 작은 오차로 일부 행이 2c 를 집었습니다.
    // 깊이 표본 간격이 행마다 달라져 SSAO 가 바닥에 가로 띠를 그렸습니다(DEF-173, 뷰포트 높이에 따라 나타남).
    ivec2 coord   = min(ivec2(gl_FragCoord.xy) * 2 + 1, ivec2(screenSize) - 1);
    float rawDepth = texelFetch(msaaDepth, coord, 0).r;  // sample 0 of MSAA
    // Reverse-Z 무한 원평면(C1, CameraComponent.cpp): 깊이 = near / 거리 → 거리 = near / 깊이. 지운 곳(0)과 zFar 너머는 zFar 로
    // 보냅니다(ssao.frag · ssao_blur.frag 가 zFar 를 배경으로 봄).
    float linearZ = rawDepth > 0.0 ? min(nearPlane / rawDepth, farPlane) : farPlane;
    outLinearDepth = linearZ;
}
