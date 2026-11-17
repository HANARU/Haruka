#include "GraspSolver.hpp"

#include <algorithm>

namespace hgrasp {

Result solve(const PoseFn& pose, const OverlapFn& overlap, const Settings& s) {
    Result r;
    const int steps  = std::max(1, s.steps);
    const int refine = std::max(0, s.refine);
    JointPos jp{};

    auto evalHit = [&](const Flex& flex, int f, int fromBone) -> int {
        pose(flex, jp);
        for (int k = fromBone; k < 3; ++k) {
            int a, b;
            boneJoints(f, k, a, b);
            ++r.queries;
            if (overlap(jp[a], jp[b], s.boneRadius) > s.tolerance) return k;
        }
        return -1;
    };

    // 손바닥: 쉬는 자세의 손바닥 관절 → 중지 뿌리 관절 캡슐.
    {
        pose(r.flex, jp);
        ++r.queries;
        r.palmContact = overlap(jp[(int)hhand::EJoint::Palm], jp[(int)hhand::EJoint::MiddleProximal], s.palmRadius) > -s.tolerance;
    }

    for (int f = 0; f < hhand::kFingerCount; ++f) {
        int   frozen = 0;      // 이 번호보다 앞의 관절은 멈춤
        float t0     = 0.f;
        // 편 손에서 이미 박혀 있으면(물체가 손가락 안) 그 마디까지 멈춤
        if (int hit0 = evalHit(r.flex, f, 0); hit0 >= 0) {
            r.fingerContact[f] = true;
            frozen = hit0 + 1;
        }
        while (frozen < 3) {
            auto setFrom = [&](Flex& fl, float t) { for (int k = frozen; k < 3; ++k) fl[f][k] = t; };
            float lo = t0, hi = -1.f;
            int   hitBone = -1;
            for (int i = 1; i <= steps; ++i) {
                const float t = t0 + (1.f - t0) * (float)i / (float)steps;
                Flex trial = r.flex;
                setFrom(trial, t);
                const int h = evalHit(trial, f, frozen);
                if (h >= 0) { hitBone = h; hi = t; break; }
                lo = t;
            }
            if (hitBone < 0) { setFrom(r.flex, 1.f); break; }   // 끝까지 닿지 않음
            for (int i = 0; i < refine; ++i) {
                const float mid = 0.5f * (lo + hi);
                Flex trial = r.flex;
                setFrom(trial, mid);
                const int h = evalHit(trial, f, frozen);
                if (h >= 0) { hi = mid; hitBone = h; } else lo = mid;
            }
            setFrom(r.flex, lo);
            r.fingerContact[f] = true;
            frozen = hitBone + 1;
            t0     = lo;
        }
    }

    bool other = false;
    for (int f = 1; f < hhand::kFingerCount; ++f) other = other || r.fingerContact[f];
    r.fitted = r.palmContact || (r.fingerContact[(int)hhand::EFinger::Thumb] && other);
    return r;
}

} // namespace hgrasp
