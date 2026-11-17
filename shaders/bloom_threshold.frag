#version 450
layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D srcImage;
layout(push_constant) uniform Params { float threshold; float knee; };

void main() {
    // 알파는 후처리 가중치입니다(post.frag 참고). UI 패널 화소(0)는 블룸을 내지 않습니다. 장면은 1 이라 예전과 같습니다.
    vec4 src    = texture(srcImage, inUV);
    vec3 color  = src.rgb * clamp(src.a, 0.0, 1.0);
    float lum   = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float rq    = clamp(lum - (threshold - knee), 0.0, 2.0 * knee);
    float weight = (rq * rq) / (4.0 * knee * knee + 0.00001);
    weight = max(weight, lum - threshold) / max(lum, 0.00001);
    outColor = vec4(color * weight, 1.0);
}
