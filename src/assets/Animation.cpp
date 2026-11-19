#include "Animation.hpp"

#include <algorithm>
#include <cmath>

namespace hanim {

namespace {

uint32_t keyCount(const Track& tr) { return (uint32_t)tr.times.size(); }

// times[k] <= t < times[k + 1] 인 k(t 가 첫 키보다 앞이면 0, 마지막 키 이상이면 마지막). hint 부터 앞으로 찾고, 안 되면 이분 탐색.
uint32_t findKey(const Track& tr, float t, uint32_t hint) {
    const uint32_t n = keyCount(tr);
    if (n <= 1 || t <= tr.times[0]) return 0;
    if (t >= tr.times[n - 1]) return n - 1;
    if (hint < n - 1 && tr.times[hint] <= t) {
        // 보통 한두 키 앞에 있습니다.
        for (uint32_t k = hint, steps = 0; k < n - 1 && steps < 4; ++k, ++steps)
            if (t < tr.times[k + 1]) return k;
    }
    const auto it = std::upper_bound(tr.times.begin(), tr.times.end(), t);
    return (uint32_t)(it - tr.times.begin()) - 1;
}

// 키 k 의 값(CubicSpline 이면 가운데 값) 시작 위치.
const float* valueAt(const Track& tr, uint32_t k) {
    return tr.interp == Interp::CubicSpline ? &tr.values[(size_t)k * tr.comps * 3 + tr.comps]
                                            : &tr.values[(size_t)k * tr.comps];
}

void normalizeQuat(float* q) {
    const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (len > 1e-12f) { for (int i = 0; i < 4; ++i) q[i] /= len; }
    else { q[0] = q[1] = q[2] = 0.f; q[3] = 1.f; }
}

glm::quat toQuat(const float* v) { return glm::quat(v[3], v[0], v[1], v[2]); }

glm::quat sameHemisphere(const glm::quat& a, const glm::quat& b) { return glm::dot(a, b) < 0.f ? -b : b; }

} // namespace

bool validate(const Clip& clip, int jointCount, int morphCount, std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    for (size_t i = 0; i < clip.tracks.size(); ++i) {
        const Track& tr = clip.tracks[i];
        const std::string at = "track " + std::to_string(i) + ": ";
        if (tr.times.empty()) return fail(at + "no keys");
        for (size_t k = 0; k < tr.times.size(); ++k) {
            if (!std::isfinite(tr.times[k])) return fail(at + "time is not finite");
            if (k && tr.times[k] < tr.times[k - 1]) return fail(at + "times are not ascending");
        }
        uint32_t want = 0;
        switch (tr.path) {
            case Path::Translation: case Path::Scale: want = 3; break;
            case Path::Rotation: want = 4; break;
            case Path::Weights: want = (uint32_t)std::max(morphCount, 0); break;
        }
        if (tr.comps != want || want == 0) return fail(at + "component count " + std::to_string(tr.comps) + " (expected " +
                                                       std::to_string(want) + ")");
        const size_t perKey = (size_t)tr.comps * (tr.interp == Interp::CubicSpline ? 3 : 1);
        if (tr.values.size() != perKey * tr.times.size()) return fail(at + "value count does not match the keys");
        for (float v : tr.values) if (!std::isfinite(v)) return fail(at + "value is not finite");
        if (tr.path != Path::Weights && (tr.target < 0 || tr.target >= jointCount)) return fail(at + "joint is out of range");
    }
    return true;
}

float computeDuration(const Clip& clip) {
    float d = 0.f;
    for (const Track& tr : clip.tracks) if (!tr.times.empty()) d = std::max(d, tr.times.back());
    return d;
}

float wrapTime(float t, float duration, bool loop) {
    if (!(duration > 0.f) || !std::isfinite(t)) return 0.f;
    if (!loop) return std::clamp(t, 0.f, duration);
    float w = std::fmod(t, duration);
    if (w < 0.f) w += duration;
    if (w >= duration) w = 0.f;   // fmod 의 부동소수 경계
    return w;
}

void sampleTrack(const Track& tr, float time, float* out, uint32_t* cursorKey) {
    const uint32_t n = keyCount(tr);
    const uint32_t c = tr.comps;
    if (n == 0) return;
    const uint32_t k = findKey(tr, time, cursorKey ? *cursorKey : 0);
    if (cursorKey) *cursorKey = k;
    const bool rot = tr.path == Path::Rotation;
    if (n == 1 || time <= tr.times[0] || k == n - 1 || tr.interp == Interp::Step) {
        const float* v = valueAt(tr, k);
        for (uint32_t i = 0; i < c; ++i) out[i] = v[i];
        if (rot) normalizeQuat(out);
        return;
    }
    const float t0 = tr.times[k], t1 = tr.times[k + 1];
    const float td = t1 - t0;
    const float u = td > 0.f ? std::clamp((time - t0) / td, 0.f, 1.f) : 0.f;
    if (tr.interp == Interp::Linear) {
        const float* a = valueAt(tr, k);
        const float* b = valueAt(tr, k + 1);
        if (rot) {
            const glm::quat q = glm::slerp(glm::normalize(toQuat(a)), glm::normalize(toQuat(b)), u);   // 짧은 쪽(glm)
            out[0] = q.x; out[1] = q.y; out[2] = q.z; out[3] = q.w;
            normalizeQuat(out);
        } else {
            for (uint32_t i = 0; i < c; ++i) out[i] = a[i] + (b[i] - a[i]) * u;
        }
        return;
    }
    // CubicSpline(glTF 부록 C): p = (2u³-3u²+1)v0 + (u³-2u²+u)·td·b0 + (-2u³+3u²)v1 + (u³-u²)·td·a1
    const float* k0 = &tr.values[(size_t)k * c * 3];
    const float* k1 = &tr.values[(size_t)(k + 1) * c * 3];
    const float u2 = u * u, u3 = u2 * u;
    const float h00 = 2.f * u3 - 3.f * u2 + 1.f, h10 = u3 - 2.f * u2 + u, h01 = -2.f * u3 + 3.f * u2, h11 = u3 - u2;
    for (uint32_t i = 0; i < c; ++i) {
        const float v0 = k0[c + i], b0 = k0[2 * c + i];   // 키 k 의 값 · 나가는 접선
        const float v1 = k1[c + i], a1 = k1[i];           // 키 k+1 의 값 · 들어오는 접선
        out[i] = h00 * v0 + h10 * td * b0 + h01 * v1 + h11 * td * a1;
    }
    if (rot) normalizeQuat(out);
}

void sample(const Clip& clip, float time, Pose& pose, std::vector<float>* weights, Cursor* cursor) {
    if (cursor && cursor->key.size() != clip.tracks.size()) cursor->key.assign(clip.tracks.size(), 0);
    float buf[4];
    for (size_t i = 0; i < clip.tracks.size(); ++i) {
        const Track& tr = clip.tracks[i];
        uint32_t* ck = cursor ? &cursor->key[i] : nullptr;
        if (tr.path == Path::Weights) {
            if (!weights || weights->size() != tr.comps) continue;
            sampleTrack(tr, time, weights->data(), ck);
            continue;
        }
        if (tr.target < 0 || tr.target >= (int)pose.size()) continue;
        sampleTrack(tr, time, buf, ck);
        JointPose& jp = pose[(size_t)tr.target];
        if (tr.path == Path::Translation) jp.t = glm::vec3(buf[0], buf[1], buf[2]);
        else if (tr.path == Path::Scale)  jp.s = glm::vec3(buf[0], buf[1], buf[2]);
        else                              jp.r = toQuat(buf);
    }
}

int findClip(const std::vector<Clip>& clips, const std::string& name) {
    for (size_t i = 0; i < clips.size(); ++i) if (clips[i].name == name) return (int)i;
    return -1;
}

void blend(const Pose& a, const Pose& b, float w, Pose& out) {
    const size_t n = std::min(a.size(), b.size());
    out.resize(n);
    for (size_t j = 0; j < n; ++j) {
        const JointPose pa = a[j], pb = b[j];   // out 이 a · b 와 같아도 되게 복사
        out[j].t = glm::mix(pa.t, pb.t, w);
        out[j].s = glm::mix(pa.s, pb.s, w);
        const glm::quat qb = sameHemisphere(pa.r, pb.r);
        out[j].r = glm::normalize(glm::quat(pa.r.w + (qb.w - pa.r.w) * w, pa.r.x + (qb.x - pa.r.x) * w,
                                            pa.r.y + (qb.y - pa.r.y) * w, pa.r.z + (qb.z - pa.r.z) * w));
    }
}

void addAdditive(Pose& base, const Pose& add, const Pose& ref, float w) {
    const size_t n = std::min({ base.size(), add.size(), ref.size() });
    const glm::quat id(1.f, 0.f, 0.f, 0.f);
    for (size_t j = 0; j < n; ++j) {
        const glm::quat delta = glm::normalize(glm::inverse(ref[j].r) * add[j].r);
        base[j].r = glm::normalize(base[j].r * glm::slerp(id, delta, w));
        base[j].t += w * (add[j].t - ref[j].t);
        glm::vec3 ratio(1.f);
        for (int i = 0; i < 3; ++i) ratio[i] = std::fabs(ref[j].s[i]) > 1e-12f ? add[j].s[i] / ref[j].s[i] : 1.f;
        base[j].s *= glm::mix(glm::vec3(1.f), ratio, w);
    }
}

} // namespace hanim
