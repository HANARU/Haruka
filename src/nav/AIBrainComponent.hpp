#pragma once
// 작은 AI 상태 기계(2026-11-21, Docs/Navigation_AI.md D8). 같은 액터의 NavAgentComponent 를 움직입니다.
//   Wait   : waitTime 뒤 순찰점이 있으면 Patrol(타이머 after)
//   Patrol : patrolTag 태그 액터를 이름순으로 돌며 하나씩 감. 도착(NavMoveFinished)하면 Wait
//   Chase  : targetTag 태그 액터가 sightRadius 안에 들면 쫓음. loseRadius 밖으로 나가거나 사라지면 Wait
// 감지는 senseInterval 마다(타이머 every). 상태가 바뀌면 월드 이벤트 버스에 AIStateChanged 를 보냅니다. 게임 시간으로 셉니다.
#include "../scene/Component.hpp"
#include "../scene/EventBus.hpp"
#include "../scene/TimerManager.hpp"
#include "NavEvents.hpp"

#include <string>

class NavAgentComponent;

class AIBrainComponent : public Component {
public:
    std::string patrolTag;            // 순찰점 태그(비었으면 순찰 없음)
    std::string targetTag;            // 쫓을 대상 태그(비었으면 쫓지 않음)
    float sightRadius   = 6.f;        // m, 이 안에 들면 Chase
    float loseRadius    = 9.f;        // m, 이 밖이면 놓침(sightRadius 이상)
    float waitTime      = 1.f;        // 순찰점에서 쉬는 시간(게임 초)
    float senseInterval = 0.2f;       // 감지 주기(게임 초)

    EAIState state() const { return state_; }
    Actor*   target() const;
    int      patrolIndex() const { return patrolIndex_; }
    int      stateChanges() const { return changes_; }

    const char* typeName() const override { return "AIBrainComponent"; }
    void onStart() override;
    void onEnable() override;
    void onDisable() override;
    void onDestroy() override;
    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;
    std::vector<PropDesc> buildProps() override;
    std::string readOnlyInfo() const override;

private:
    void start();
    void enter(EAIState s, Actor* target = nullptr);
    void sense();
    void goToPatrolPoint();
    void onMoveFinished(const NavMoveFinished& e);
    void stopAll();
    NavAgentComponent* agent() const;
    Actor* nearestTarget(float radius) const;

    EAIState          state_ = EAIState::Wait;
    int               patrolIndex_ = -1;
    int               changes_ = 0;
    uint64_t          targetId_ = 0;
    TimerHandle       senseTimer_ = 0;
    TimerHandle       waitTimer_  = 0;
    EventSubscription moveSub_;
    bool              running_ = false;
    bool              started_ = false;
};
