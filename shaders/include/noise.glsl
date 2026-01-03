// Haruka Procedural Library — noise.glsl
// Include with: #include "noise.glsl" (compile with glslc -I shaders/include)
#ifndef HARUKA_NOISE_GLSL
#define HARUKA_NOISE_GLSL

// ── Hash functions ─────────────────────────────────────────────────────────────
float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

vec3 hash33(vec3 p) {
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)),
             dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return fract(sin(p) * 43758.5453123);
}

// ── Value Noise 2D ─────────────────────────────────────────────────────────────
float valueNoise2D(vec2 uv) {
    vec2 i = floor(uv);
    vec2 f = fract(uv);
    vec2 u = f * f * (3.0 - 2.0 * f);  // smoothstep

    float a = hash12(i + vec2(0.0, 0.0));
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// ── Value Noise 3D ─────────────────────────────────────────────────────────────
float valueNoise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);

    return mix(mix(mix(hash13(i + vec3(0,0,0)), hash13(i + vec3(1,0,0)), u.x),
                   mix(hash13(i + vec3(0,1,0)), hash13(i + vec3(1,1,0)), u.x), u.y),
               mix(mix(hash13(i + vec3(0,0,1)), hash13(i + vec3(1,0,1)), u.x),
                   mix(hash13(i + vec3(0,1,1)), hash13(i + vec3(1,1,1)), u.x), u.y), u.z);
}

// ── Gradient Noise (Perlin) 2D ─────────────────────────────────────────────────
float gradientNoise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);  // quintic

    vec2 ga = hash22(i + vec2(0.0, 0.0)) * 2.0 - 1.0;
    vec2 gb = hash22(i + vec2(1.0, 0.0)) * 2.0 - 1.0;
    vec2 gc = hash22(i + vec2(0.0, 1.0)) * 2.0 - 1.0;
    vec2 gd = hash22(i + vec2(1.0, 1.0)) * 2.0 - 1.0;

    float va = dot(ga, f - vec2(0.0, 0.0));
    float vb = dot(gb, f - vec2(1.0, 0.0));
    float vc = dot(gc, f - vec2(0.0, 1.0));
    float vd = dot(gd, f - vec2(1.0, 1.0));

    return va + u.x * (vb - va) + u.y * (vc - va) + u.x * u.y * (va - vb - vc + vd);
}

// ── Gradient Noise (Perlin) 3D ─────────────────────────────────────────────────
float gradientNoise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    float n000 = dot(hash33(i + vec3(0,0,0)) * 2.0 - 1.0, f - vec3(0,0,0));
    float n100 = dot(hash33(i + vec3(1,0,0)) * 2.0 - 1.0, f - vec3(1,0,0));
    float n010 = dot(hash33(i + vec3(0,1,0)) * 2.0 - 1.0, f - vec3(0,1,0));
    float n110 = dot(hash33(i + vec3(1,1,0)) * 2.0 - 1.0, f - vec3(1,1,0));
    float n001 = dot(hash33(i + vec3(0,0,1)) * 2.0 - 1.0, f - vec3(0,0,1));
    float n101 = dot(hash33(i + vec3(1,0,1)) * 2.0 - 1.0, f - vec3(1,0,1));
    float n011 = dot(hash33(i + vec3(0,1,1)) * 2.0 - 1.0, f - vec3(0,1,1));
    float n111 = dot(hash33(i + vec3(1,1,1)) * 2.0 - 1.0, f - vec3(1,1,1));

    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

// ── Fractional Brownian Motion ─────────────────────────────────────────────────
float fbm2D(vec2 p, int octaves, float lacunarity, float gain) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    for (int i = 0; i < octaves; i++) {
        value += amplitude * gradientNoise2D(p * frequency);
        frequency *= lacunarity;
        amplitude *= gain;
    }
    return value;
}

float fbm3D(vec3 p, int octaves, float lacunarity, float gain) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    for (int i = 0; i < octaves; i++) {
        value += amplitude * gradientNoise3D(p * frequency);
        frequency *= lacunarity;
        amplitude *= gain;
    }
    return value;
}

// Convenience overloads with typical defaults (4 octaves, lacunarity=2, gain=0.5)
float fbm2D(vec2 p)  { return fbm2D(p, 4, 2.0, 0.5); }
float fbm3D(vec3 p)  { return fbm3D(p, 4, 2.0, 0.5); }

// ── Worley (Cellular) Noise 2D ─────────────────────────────────────────────────
float worleyNoise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float minDist = 8.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighbor = vec2(float(x), float(y));
            vec2 point    = hash22(i + neighbor);
            float dist    = length(neighbor + point - f);
            minDist = min(minDist, dist);
        }
    }
    return minDist;
}

#endif // HARUKA_NOISE_GLSL
