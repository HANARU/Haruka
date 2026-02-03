#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) out vec4 outColor;

void main() {
    // 폰트 아틀라스 / 텍스처 / 단색(white 픽셀) 모두 동일 경로로 처리:
    //   단색    → tex=white(1,1,1,1) → fragColor
    //   텍스처  → tex=RGBA           → tex * fragColor (tint)
    //   폰트    → tex=RGBA(1,1,1,α)  → vec4(fragColor.rgb, fragColor.a * alpha)
    outColor = texture(tex, fragUV) * fragColor;
}
