#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragLightDir;
layout(location = 3) out vec4 fragLightColor;
layout(location = 4) out float fragAmbient;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 viewProj[2];
    vec4 lightDir;    // xyz=direction, w=ambient
    vec4 lightColor;  // xyz=color*intensity, w=unused
} ubo;

struct GpuTransform { mat4 model; };
layout(set = 2, binding = 0) readonly buffer TransformBuffer {
    GpuTransform transforms[];
};

void main() {
    mat4 model     = transforms[gl_InstanceIndex].model;
    gl_Position    = ubo.viewProj[0] * model * vec4(inPosition, 1.0);
    fragNormal     = mat3(model) * inNormal;
    fragUV         = inUV;
    fragLightDir   = ubo.lightDir.xyz;
    fragLightColor = ubo.lightColor;
    fragAmbient    = ubo.lightDir.w;
}
