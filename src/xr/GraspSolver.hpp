#pragma once
// ── 손 자동 맞춤(손 쥐기 D, 2026-11-17, Docs/Hand_Grasp_Design.md 4-4) ───────────────────────────────
// 잡는 순간 한 번 계산하는 순수 함수입니다. 손 자세 계산(관절 굽힘 → 관절 월드 위치)과 충돌 판정(캡슐 → 침투 깊이)은 함수로
// 받아서, 엔진에서는 스킨 메시 · Jolt 를, 시험에서는 합성 손 · 해석 도형을 넣습니다.
//
// 손가락 감싸기(4-4 3번): 손가락마다 세 관절을 함께 0 → 1 로 steps 단계 굽히며, 그 관절이 움직이는 마디(관절 → 다음 관절의
// 캡슐)가 물체에 tolerance 보다 깊이 박히는지 봅니다. 처음 박힌 단계에서 refine 번 이분 탐색해 박히지 않는 가장 큰 값을 찾고,
// 박힌 마디보다 손바닥 쪽 관절은 그 자리에 멈춥니다. 끝 쪽 관절만 계속 굽혀 닿거나 최대(1)에 이를 때까지 감쌉니다.
// 잡기 조건(4-4 4번): 손바닥이 닿거나, 엄지와 다른 손가락 하나가 닿으면 맞춤 잡기입니다. 아니면 fitted = false 로 돌려주고
// 호출자는 예전처럼 상대 위치를 지키는 잡기로 갑니다(잡기가 실패하는 일은 없음).

#include "HandSkeleton.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

#include <array>
#include <functional>

namespace hgrasp {

using Flex      = std::array<std::array<float, 3>, hhand::kFingerCount>;   // [손가락][관절] 0 ~ 1
using JointPos  = std::array<glm::vec3, hhand::kJointCount>;               // 관절 월드 위치
using PoseFn    = std::function<void(const Flex& flex, JointPos& out)>;
// 캡슐(a-b, 반지름)과 물체의 침투 깊이(m). 닿지 않으면 음수.
using OverlapFn = std::function<float(const glm::vec3& a, const glm::vec3& b, float radius)>;

struct Settings {
    float boneRadius = 0.011f;   // 마디 캡슐 반지름(장갑 두께 포함, m)
    float palmRadius = 0.025f;   // 손바닥 캡슐 반지름
    float tolerance  = 0.002f;   // 허용 침투(m)
    int   steps      = 16;
    int   refine     = 3;
};

struct Result {
    Flex flex{};                                   // 맞춘 굽힘
    bool fingerContact[hhand::kFingerCount] = {};
    bool palmContact = false;
    bool fitted      = false;
    int  queries     = 0;                          // 충돌 판정 횟수(비용 확인)
};

// 손가락 f 의 마디 k(0 ~ 2)의 양 끝 관절 번호: 굽힘 관절 k → 다음 굽힘 관절(마지막은 끝점).
inline void boneJoints(int f, int k, int& a, int& b) {
    a = (int)hhand::kFingerFlexJoints[f][k];
    b = k < 2 ? (int)hhand::kFingerFlexJoints[f][k + 1] : (int)hhand::kFingerTip[f];
}

Result solve(const PoseFn& pose, const OverlapFn& overlap, const Settings& s = {});

} // namespace hgrasp
