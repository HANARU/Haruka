#pragma once
// 손 뼈대 규약(2026-11-07, Docs/Hand_Grasp_Design.md 4-1).
//
// 엔진의 손 뼈대는 OpenXR XR_EXT_hand_tracking 의 26관절(XrHandJointEXT)과 번호 · 이름이 같습니다. 나중에 손 추적을
// 붙일 때 관절을 그대로 대응시키려는 것입니다. 손 모델(glb)의 관절 노드 이름은 kHandJointNames 와 같아야 합니다.
//
// 관절 로컬 축(OpenXR "Conventions of hand joints"): +Z 는 뼈를 따라 손가락 끝의 반대쪽, +Y 는 손등 바깥(손톱에 수직),
// X 는 오른손 법칙(Y x Z)입니다. 손목은 +Z 가 손목에서 중지 metacarpal 의 반대쪽, 손바닥(Palm)은 중지 metacarpal 뼈의
// 가운데에 놓입니다. 두 손에 같은 규칙이므로 손가락을 손바닥 쪽으로 굽히는 것은 양손 모두 로컬 X 둘레의 음의 회전입니다.
// 손 모델은 쉬는 자세에서 각 관절 노드의 로컬 축이 이 규약을 따르게 만듭니다(HandPoseComponent 가 확인하고 경고).
//
// 부모 관계는 OpenXR 사양에 없어 엔진이 정합니다: 손목이 뿌리이고, 손바닥 · 각 손가락의 metacarpal 이 손목의 자식이며,
// 손가락은 metacarpal → proximal → (intermediate) → distal → tip 으로 이어집니다. 엄지에는 intermediate 가 없습니다.

#include <cstdint>
#include <cstring>
#include <string_view>

namespace hhand {

enum class EJoint : uint8_t {
    Palm = 0, Wrist = 1,
    ThumbMetacarpal = 2, ThumbProximal = 3, ThumbDistal = 4, ThumbTip = 5,
    IndexMetacarpal = 6, IndexProximal = 7, IndexIntermediate = 8, IndexDistal = 9, IndexTip = 10,
    MiddleMetacarpal = 11, MiddleProximal = 12, MiddleIntermediate = 13, MiddleDistal = 14, MiddleTip = 15,
    RingMetacarpal = 16, RingProximal = 17, RingIntermediate = 18, RingDistal = 19, RingTip = 20,
    LittleMetacarpal = 21, LittleProximal = 22, LittleIntermediate = 23, LittleDistal = 24, LittleTip = 25,
};
constexpr int kJointCount = 26;

inline constexpr const char* kHandJointNames[kJointCount] = {
    "Palm", "Wrist",
    "ThumbMetacarpal", "ThumbProximal", "ThumbDistal", "ThumbTip",
    "IndexMetacarpal", "IndexProximal", "IndexIntermediate", "IndexDistal", "IndexTip",
    "MiddleMetacarpal", "MiddleProximal", "MiddleIntermediate", "MiddleDistal", "MiddleTip",
    "RingMetacarpal", "RingProximal", "RingIntermediate", "RingDistal", "RingTip",
    "LittleMetacarpal", "LittleProximal", "LittleIntermediate", "LittleDistal", "LittleTip",
};

// 엔진이 정한 부모(-1 = 뿌리).
inline constexpr int8_t kHandJointParent[kJointCount] = {
    1, -1,
    1, 2, 3, 4,
    1, 6, 7, 8, 9,
    1, 11, 12, 13, 14,
    1, 16, 17, 18, 19,
    1, 21, 22, 23, 24,
};

enum class EFinger : uint8_t { Thumb = 0, Index = 1, Middle = 2, Ring = 3, Little = 4 };
constexpr int kFingerCount = 5;

inline constexpr const char* kFingerNames[kFingerCount] = { "Thumb", "Index", "Middle", "Ring", "Little" };

// 손가락마다 굽히는 관절 세 개(손바닥 쪽부터). 관절을 돌리면 그 관절에서 시작하는 뼈와 그 끝 쪽이 함께 돕니다.
//   검지 ~ 소지: proximal(MCP, 손가락 뿌리 마디) · intermediate(PIP) · distal(DIP)
//   엄지:        metacarpal(CMC) · proximal(MCP) · distal(IP)
inline constexpr EJoint kFingerFlexJoints[kFingerCount][3] = {
    { EJoint::ThumbMetacarpal, EJoint::ThumbProximal,      EJoint::ThumbDistal  },
    { EJoint::IndexProximal,   EJoint::IndexIntermediate,  EJoint::IndexDistal  },
    { EJoint::MiddleProximal,  EJoint::MiddleIntermediate, EJoint::MiddleDistal },
    { EJoint::RingProximal,    EJoint::RingIntermediate,   EJoint::RingDistal   },
    { EJoint::LittleProximal,  EJoint::LittleIntermediate, EJoint::LittleDistal },
};
inline constexpr EJoint kFingerTip[kFingerCount] = {
    EJoint::ThumbTip, EJoint::IndexTip, EJoint::MiddleTip, EJoint::RingTip, EJoint::LittleTip,
};

// 굽힘 값 1 일 때 관절별 최대 각(도). 맨손의 움직임 범위보다 조금 작게 잡았습니다(장갑). 헤드셋에서 맞춥니다.
inline constexpr float kDefaultMaxFlexDeg[kFingerCount][3] = {
    { 25.f, 45.f, 60.f },   // 엄지
    { 80.f, 95.f, 65.f },   // 검지
    { 85.f, 100.f, 65.f },  // 중지
    { 85.f, 100.f, 65.f },  // 약지
    { 85.f, 95.f, 65.f },   // 소지
};

inline const char* jointName(EJoint j) { return kHandJointNames[(int)j]; }

// 이름 → 관절 번호. 대소문자를 구분합니다. 없으면 -1.
inline int jointIndex(std::string_view name) {
    for (int i = 0; i < kJointCount; ++i)
        if (name == kHandJointNames[i]) return i;
    return -1;
}

} // namespace hhand
