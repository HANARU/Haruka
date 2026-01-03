// Haruka Procedural Library — sdf.glsl
// Signed Distance Functions (SDF primitives + combinators)
// Include with: #include "sdf.glsl"
#ifndef HARUKA_SDF_GLSL
#define HARUKA_SDF_GLSL

// ── Primitives ─────────────────────────────────────────────────────────────────

// Sphere at origin, radius r
float sdSphere(vec3 p, float r) {
    return length(p) - r;
}

// Axis-aligned box centered at origin, half-extents b
float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

// Rounded box — box with rounded edges, rounding radius r
float sdRoundBox(vec3 p, vec3 b, float r) {
    vec3 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
}

// Infinite plane: normal n (normalized), height h
float sdPlane(vec3 p, vec3 n, float h) {
    return dot(p, n) + h;
}

// Torus: major radius R, minor radius r
float sdTorus(vec3 p, float R, float r) {
    vec2 q = vec2(length(p.xz) - R, p.y);
    return length(q) - r;
}

// Cylinder (infinite vertical, radius r)
float sdCylinder(vec3 p, float r) {
    return length(p.xz) - r;
}

// Capped cylinder along Y axis, height h, radius r
float sdCappedCylinder(vec3 p, float h, float r) {
    vec2 d = abs(vec2(length(p.xz), p.y)) - vec2(r, h);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

// Capsule from point a to point b, radius r
float sdCapsule(vec3 p, vec3 a, vec3 b, float r) {
    vec3  ab = b - a;
    vec3  ap = p - a;
    float t  = clamp(dot(ap, ab) / dot(ab, ab), 0.0, 1.0);
    return length(ap - t * ab) - r;
}

// Cone (tip at origin, opens toward +Y), half-angle in radians
float sdCone(vec3 p, float angle, float height) {
    vec2  q = height * vec2(tan(angle), -1.0);
    vec2  w = vec2(length(p.xz), p.y);
    vec2  a = w - q * clamp(dot(w, q) / dot(q, q), 0.0, 1.0);
    vec2  b = w - q * vec2(clamp(w.x / q.x, 0.0, 1.0), 1.0);
    float k = sign(q.y);
    float d = min(dot(a, a), dot(b, b));
    float s = max(k * (w.x * q.y - w.y * q.x), k * (w.y - q.y));
    return sqrt(d) * sign(s);
}

// ── Boolean Operators ──────────────────────────────────────────────────────────

float opUnion(float d1, float d2)        { return min(d1, d2); }
float opSubtract(float d1, float d2)     { return max(-d2, d1); }
float opIntersect(float d1, float d2)    { return max(d1, d2); }

// Smooth union (k controls blend radius — typical k = 0.1)
float opSmoothUnion(float d1, float d2, float k) {
    float h = clamp(0.5 + 0.5 * (d2 - d1) / k, 0.0, 1.0);
    return mix(d2, d1, h) - k * h * (1.0 - h);
}

// Smooth subtraction
float opSmoothSubtract(float d1, float d2, float k) {
    float h = clamp(0.5 - 0.5 * (d2 + d1) / k, 0.0, 1.0);
    return mix(d2, -d1, h) + k * h * (1.0 - h);
}

// Smooth intersection
float opSmoothIntersect(float d1, float d2, float k) {
    float h = clamp(0.5 - 0.5 * (d2 - d1) / k, 0.0, 1.0);
    return mix(d2, d1, h) + k * h * (1.0 - h);
}

// ── Domain Operations ──────────────────────────────────────────────────────────

// Repetition: tile space with cell size c
vec3 opRepeat(vec3 p, vec3 c) {
    return mod(p + 0.5 * c, c) - 0.5 * c;
}

// Repetition with finite count (max n tiles in each direction)
vec3 opRepeatLimited(vec3 p, float c, vec3 n) {
    return p - c * clamp(round(p / c), -n, n);
}

// Twist around Y axis (k = twist amount per unit Y)
vec3 opTwist(vec3 p, float k) {
    float c = cos(k * p.y);
    float s = sin(k * p.y);
    return vec3(c * p.x - s * p.z, p.y, s * p.x + c * p.z);
}

// Bend around Z axis (k = bend strength)
vec3 opBend(vec3 p, float k) {
    float c = cos(k * p.x);
    float s = sin(k * p.x);
    return vec3(c * p.x - s * p.y, s * p.x + c * p.y, p.z);
}

// ── Surface Normal via Tetrahedron Sampling ────────────────────────────────────
// Call with your SDF function: vec3 n = sdfNormal(p, SCENE_SDF(p));
// Usage: pass the SDF result of the center + 4 tetrahedron samples
// Typical: vec3 n = normalize(vec3(sdSphere(p+e.xyy,...) - sdSphere(p-e.xyy,...), ...))
// Simplified helper — requires user to provide sample results
vec3 sdfNormalFromSamples(float d_xyy, float d_yyx, float d_yxy, float d_xxx) {
    return normalize(vec3(d_xyy - d_xxx, d_yyx - d_xxx, d_yxy - d_xxx));
}

#endif // HARUKA_SDF_GLSL
