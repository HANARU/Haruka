#version 450

layout(location = 0) in      vec2 fragUV;
layout(location = 1) in      vec4 fragColor;
layout(location = 2) in      vec2 fragLocalPos;
layout(location = 3) flat in vec2 fragHalfSize;
layout(location = 4) flat in vec4 fragParams;       // x=radius, y=borderWidth, z=blur, w=mode
layout(location = 5) flat in vec4 fragBorderColor;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) out vec4 outColor;

// 둥근 사각형 부호 거리(Inigo Quilez): 안쪽 음수, 바깥 양수(픽셀 단위).
float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// Evan Wallace 가우스 그림자 근사: erf(x) 근사 + Y축 4표본 적분.
float erfApprox(float x) {
    float s = sign(x);
    float a = abs(x);
    x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    x *= x;
    return s - s / (x * x);
}

float roundedBoxShadow(vec2 p, vec2 halfSize, float radius, float sigma) {
    vec2  low    = p - halfSize;
    vec2  high   = p + halfSize;
    float startY = clamp(low.y,  -3.0 * sigma, 3.0 * sigma);
    float endY   = clamp(high.y, -3.0 * sigma, 3.0 * sigma);
    if (endY <= startY) return 0.0;
    float stepY = (endY - startY) * 0.25;
    float y     = startY + stepY * 0.5;
    float invS2 = 1.0 / (sigma * 1.41421356);
    float normY = 1.0 / (sigma * 2.50662827);
    float value = 0.0;
    for (int i = 0; i < 4; ++i) {
        float dyFromEdge = min(y - low.y, high.y - y);
        float inset = (dyFromEdge < radius)
            ? (radius - sqrt(max(0.0, radius * radius - (radius - dyFromEdge) * (radius - dyFromEdge))))
            : 0.0;
        float x0 = low.x  + inset;
        float x1 = high.x - inset;
        if (x1 > x0) {
            float integralX = 0.5 * (erfApprox(x1 * invS2) - erfApprox(x0 * invS2));
            float gaussY    = exp(-0.5 * (y * y) / (sigma * sigma)) * normY;
            value += integralX * gaussY * stepY;
        }
        y += stepY;
    }
    return clamp(value, 0.0, 1.0);
}

void main() {
    if (fragParams.w > 0.5) {
        // 그림자 패스
        float r     = clamp(fragParams.x, 0.0, min(fragHalfSize.x, fragHalfSize.y));
        float sigma = max(fragParams.z * 0.5, 0.5);
        float alpha = roundedBoxShadow(fragLocalPos, fragHalfSize, r, sigma);
        outColor = vec4(fragColor.rgb, fragColor.a * alpha);
        return;
    }

    // 반경 0 · 테두리 0 인 일반 사각형 · 텍스처 · 글자는 기존 경로와 비트 단위로 동일하게 냅니다.
    if (fragParams.x <= 0.0 && fragParams.y <= 0.0) {
        outColor = texture(tex, fragUV) * fragColor;
        return;
    }

    float r       = clamp(fragParams.x, 0.0, min(fragHalfSize.x, fragHalfSize.y));
    float bw      = max(fragParams.y, 0.0);
    float d       = sdRoundedBox(fragLocalPos, fragHalfSize, r);
    vec4  fillCol = texture(tex, fragUV) * fragColor;
    vec4  col     = fillCol;

    if (bw > 0.0 && fragBorderColor.a > 0.0) {
        float borderT = smoothstep(-bw - 0.5, -bw + 0.5, d);
        float ba      = fragBorderColor.a * borderT;
        float oa      = ba + fillCol.a * (1.0 - ba);
        vec3  oc      = (oa > 1e-5)
                      ? (fragBorderColor.rgb * ba + fillCol.rgb * fillCol.a * (1.0 - ba)) / oa
                      : vec3(0.0);
        col = vec4(oc, oa);
    }

    float outerAlpha = smoothstep(0.5, -0.5, d);
    outColor = vec4(col.rgb, col.a * outerAlpha);
}
