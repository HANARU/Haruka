#pragma once
// 애니메이션 클립 자료(2026-11-20, Docs/Animation_System.md A1 · D1).
//
// glTF 의 animation 하나를 스킨 관절 기준으로 옮긴 것입니다. 트랙은 glTF 노드가 아니라 스킨의 관절 번호를 가리키고,
// 같은 스킨(SkinSourceData::clips)에 묶여 모델을 쓰는 모든 액터가 공유합니다. 자료만 있고 계산은 assets/Animation.hpp 에 있습니다.

#include <cstdint>
#include <string>
#include <vector>

namespace hanim {

enum class Path : uint8_t { Translation = 0, Rotation = 1, Scale = 2, Weights = 3 };
enum class Interp : uint8_t { Step = 0, Linear = 1, CubicSpline = 2 };

struct Track {
    int                target = -1;          // 관절 번호(Weights 트랙은 쓰지 않음, -1)
    Path               path   = Path::Rotation;
    Interp             interp = Interp::Linear;
    uint32_t           comps  = 4;           // 키 하나의 성분 수: 이동 · 배율 3, 회전 4(x y z w), 무게 = 모프 타깃 수
    std::vector<float> times;                // 초, 오름차순
    // 키마다 comps 개. CubicSpline 은 키마다 (들어오는 접선, 값, 나가는 접선) 순서로 3 x comps 개(glTF 규약).
    std::vector<float> values;
};

struct Clip {
    std::string        name;
    float              duration = 0.f;       // 모든 트랙의 마지막 키 시간 중 가장 큰 값
    std::vector<Track> tracks;
};

} // namespace hanim
