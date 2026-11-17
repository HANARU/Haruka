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
    ivec2 coord   = ivec2(inUV * screenSize);
    float rawDepth = texelFetch(msaaDepth, coord, 0).r;  // sample 0 of MSAA
    // Reverse-Z 무한 원평면(C1, CameraComponent.cpp): 깊이 = near / 거리 → 거리 = near / 깊이. 지운 곳(0)과 zFar 너머는 zFar 로
    // 보냅니다(ssao.frag · ssao_blur.frag 가 zFar 를 배경으로 봄).
    float linearZ = rawDepth > 0.0 ? min(nearPlane / rawDepth, farPlane) : farPlane;
    outLinearDepth = linearZ;
}
