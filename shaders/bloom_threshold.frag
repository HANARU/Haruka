#version 450
layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D srcImage;
layout(push_constant) uniform Params { float threshold; float knee; };

void main() {
    vec3 color  = texture(srcImage, inUV).rgb;
    float lum   = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float rq    = clamp(lum - (threshold - knee), 0.0, 2.0 * knee);
    float weight = (rq * rq) / (4.0 * knee * knee + 0.00001);
    weight = max(weight, lum - threshold) / max(lum, 0.00001);
    outColor = vec4(color * weight, 1.0);
}
