#version 450
// 13-tap Kawase-style downsample
layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D srcImage;

void main() {
    vec2 src = 1.0 / vec2(textureSize(srcImage, 0));
    vec3 c   = texture(srcImage, inUV).rgb * 4.0;
    c += texture(srcImage, inUV + vec2(-1,-1) * src).rgb;
    c += texture(srcImage, inUV + vec2( 1,-1) * src).rgb;
    c += texture(srcImage, inUV + vec2(-1, 1) * src).rgb;
    c += texture(srcImage, inUV + vec2( 1, 1) * src).rgb;
    c += texture(srcImage, inUV + vec2(-2, 0) * src).rgb * 0.5;
    c += texture(srcImage, inUV + vec2( 2, 0) * src).rgb * 0.5;
    c += texture(srcImage, inUV + vec2( 0,-2) * src).rgb * 0.5;
    c += texture(srcImage, inUV + vec2( 0, 2) * src).rgb * 0.5;
    c += texture(srcImage, inUV + vec2(-2,-2) * src).rgb * 0.25;
    c += texture(srcImage, inUV + vec2( 2,-2) * src).rgb * 0.25;
    c += texture(srcImage, inUV + vec2(-2, 2) * src).rgb * 0.25;
    c += texture(srcImage, inUV + vec2( 2, 2) * src).rgb * 0.25;
    outColor = vec4(c / 9.0, 1.0);
}
