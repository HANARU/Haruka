#version 450
// 9-tap tent filter upsample
layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D srcImage;   // 이전(더 작은) 레벨 — 텐트 블러로 확대
layout(set = 0, binding = 1) uniform sampler2D downImage;  // 대응 down 밉 — 스킵-커넥션으로 가산

void main() {
    vec2 s = 1.0 / vec2(textureSize(srcImage, 0));
    vec3 c = vec3(0.0);
    c += texture(srcImage, inUV + vec2(-s.x,  s.y)).rgb * 1.0;
    c += texture(srcImage, inUV + vec2( 0.0,  s.y)).rgb * 2.0;
    c += texture(srcImage, inUV + vec2( s.x,  s.y)).rgb * 1.0;
    c += texture(srcImage, inUV + vec2(-s.x,  0.0)).rgb * 2.0;
    c += texture(srcImage, inUV + vec2( 0.0,  0.0)).rgb * 4.0;
    c += texture(srcImage, inUV + vec2( s.x,  0.0)).rgb * 2.0;
    c += texture(srcImage, inUV + vec2(-s.x, -s.y)).rgb * 1.0;
    c += texture(srcImage, inUV + vec2( 0.0, -s.y)).rgb * 2.0;
    c += texture(srcImage, inUV + vec2( s.x, -s.y)).rgb * 1.0;
    // 스킵-커넥션(D-4): 확대·블러한 하위 레벨에 이 해상도의 down 밉을 더해 중간 주파수를
    // 되살립니다. 안 그러면 최종 블룸이 최소 밉 하나를 확대한 저주파 덩어리가 됩니다.
    outColor = vec4(c / 16.0 + texture(downImage, inUV).rgb, 1.0);
}
