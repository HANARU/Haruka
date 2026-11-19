#pragma once
// 애니메이션 상태 기계(2026-11-20, Docs/Animation_System.md A4 · D6). 자료 파일 없이 게임 코드가 C++ 로 만듭니다.
//
// 상태 = 클립 이름 · 반복 · 속도. 전환 = (출발 상태 또는 아무 상태, 도착 상태, 블렌드 시간, 조건 목록). 조건은 모두 맞아야
// 전환합니다(AND). 파라미터(bool · float · 트리거)는 게임 코드가 넣고, 트리거는 전환에 쓰이면 지워집니다. AnimationComponent 가
// 매 틱 update 를 불러 고른 전환대로 크로스페이드합니다. 재생과 분리된 순수 논리라 장치 없이 시험합니다.

#include <string>
#include <unordered_map>
#include <vector>

namespace hanim {

class StateMachine {
public:
    struct State {
        std::string name;
        std::string clip;
        bool        loop  = true;
        float       speed = 1.f;
    };
    struct Condition {
        enum Kind { BoolTrue, BoolFalse, FloatGreater, FloatLess, Trigger, ClipEnd };
        Kind        kind  = BoolTrue;
        std::string param;
        float       value = 0.f;
    };
    struct Transition {
        int                    from  = -1;   // -1 = 아무 상태(지금 상태가 도착 상태이면 건너뜀)
        int                    to    = -1;
        float                  blend = 0.2f; // 초
        std::vector<Condition> conditions;   // 비면 늘 참(ClipEnd 없이 쓰면 바로 넘어감)
    };
    struct Step {
        int   to    = -1;    // -1 = 바뀌지 않음
        float blend = 0.f;
    };

    int  addState(const std::string& name, const std::string& clip, bool loop = true, float speed = 1.f);
    // 처음 등록한 전환이 먼저입니다(같은 프레임에 여러 전환이 맞으면 하나만).
    void addTransition(int from, int to, float blend, std::vector<Condition> conditions);
    int  findState(const std::string& name) const;

    void setBool(const std::string& p, bool v)   { bools_[p] = v; }
    void setFloat(const std::string& p, float v) { floats_[p] = v; }
    void setTrigger(const std::string& p)        { triggers_[p] = true; }
    bool getBool(const std::string& p) const;
    float getFloat(const std::string& p) const;
    bool triggerSet(const std::string& p) const;

    // 시작 상태로 되돌립니다(파라미터는 그대로). 다음 update 가 그 상태를 처음 재생하라고 알립니다.
    void reset(int startState = 0);
    // clipEnded = 지금 상태의 클립이 이번 틱에 끝났거나(한 번 재생) 한 바퀴를 넘음(반복). 첫 호출은 시작 상태로 가는 Step(블렌드 0).
    Step update(bool clipEnded);

    int          current() const { return current_; }
    const State& state(int i) const { return states_[(size_t)i]; }
    int          stateCount() const { return (int)states_.size(); }

private:
    bool met(const Transition& t, bool clipEnded) const;

    std::vector<State>                     states_;
    std::vector<Transition>                transitions_;
    std::unordered_map<std::string, bool>  bools_;
    std::unordered_map<std::string, float> floats_;
    std::unordered_map<std::string, bool>  triggers_;
    int  current_ = -1;
    int  start_   = 0;
};

} // namespace hanim
