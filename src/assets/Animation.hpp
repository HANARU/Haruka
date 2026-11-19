#pragma once
// 애니메이션 계산(2026-11-20, Docs/Animation_System.md A1): 클립 검사 · 샘플링(STEP · LINEAR · CUBICSPLINE) · 자세 섞기 · 가산.
// 자료는 AnimClip.hpp · Skin.hpp(JointPose) 에 있고, 여기는 GPU · 씬 의존이 없는 순수 계산입니다.

#include "AnimClip.hpp"
#include "Skin.hpp"

#include <string>
#include <vector>

namespace hanim {

using Pose = std::vector<JointPose>;

// 트랙마다 마지막으로 쓴 키 번호. 다음 샘플을 그 자리부터 찾습니다(D3). 클립이 바뀌면 reset.
struct Cursor {
    std::vector<uint32_t> key;
    void reset() { key.clear(); }
};

// 클립이 쓸 수 있는지 봅니다: 시간이 유한 · 오름차순, 값 개수 = 키 x 성분(x3, CubicSpline), 대상 관절 범위, 무게 성분 = morphCount.
// 틀리면 err 에 이유를 쓰고 false.
bool validate(const Clip& clip, int jointCount, int morphCount, std::string* err);

// 트랙의 마지막 키 시간 중 가장 큰 값.
float computeDuration(const Clip& clip);

// 재생 시간을 클립 안으로 옮깁니다. loop 이면 [0, duration) 로 감고, 아니면 [0, duration] 로 자릅니다. duration <= 0 이면 0.
float wrapTime(float t, float duration, bool loop);

// 트랙 하나를 time 에서 샘플링해 out 에 comps 개를 씁니다(회전은 x y z w, 정규화). cursorKey 는 앞 키 번호(입출력, null 가능).
void sampleTrack(const Track& tr, float time, float* out, uint32_t* cursorKey = nullptr);

// 클립을 time 에서 샘플링합니다. pose 는 미리 쉬는 자세로 채워 두십시오(트랙이 없는 관절 · 경로는 그대로 남음).
// weights 가 null 이 아니면 무게 트랙이 그 값을 덮습니다(크기 = 모프 타깃 수).
void sample(const Clip& clip, float time, Pose& pose, std::vector<float>* weights = nullptr, Cursor* cursor = nullptr);

// 이름으로 클립을 찾습니다(없으면 -1).
int findClip(const std::vector<Clip>& clips, const std::string& name);

// 두 자세를 섞습니다: w = 0 이면 a, 1 이면 b. 이동 · 배율은 선형, 회전은 같은 반구로 맞춘 nlerp(D2). out 은 a 나 b 여도 됩니다.
void blend(const Pose& a, const Pose& b, float w, Pose& out);

// 가산: base 에 (add - ref) 를 w 만큼 더합니다. 회전 = base * slerp(1, ref^-1 * add, w), 이동 += w (add - ref),
// 배율 *= mix(1, add / ref, w). add == ref 이면 base 가 바뀌지 않습니다.
void addAdditive(Pose& base, const Pose& add, const Pose& ref, float w);

} // namespace hanim
