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
    // Linearize Vulkan NDC depth (0=near, 1=far)
    float linearZ = nearPlane * farPlane / (farPlane - rawDepth * (farPlane - nearPlane));
    outLinearDepth = linearZ;
}
