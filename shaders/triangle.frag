#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragLightDir;
layout(location = 3) in vec4 fragLightColor;
layout(location = 4) in float fragAmbient;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D texSampler;

void main() {
    vec3  normal  = normalize(fragNormal);
    float diffuse = max(dot(normal, normalize(fragLightDir)), 0.0);

    vec4 texColor = texture(texSampler, fragUV);
    vec3 lit      = texColor.rgb * (fragAmbient + diffuse * fragLightColor.rgb);
    outColor      = vec4(lit, texColor.a);
}
