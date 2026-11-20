#pragma once
// 내비 에이전트(2026-11-21, Docs/Navigation_AI.md D6). 월드 서비스 NavSystem 으로 경로를 찾아 꺾이는 점을 따라갑니다(UE
// PathFollowingComponent · Unity NavMeshAgent 자리). 액터 원점 = 발 위치입니다. 물리가 있고 useCharacter 면 Jolt CharacterVirtual 로
// 움직여(중력 · 미끄러지며 막히기) 동적 몸에 막히고, 아니면 메시 위를 미끄러집니다(moveAlongSurface). 다른 에이전트와는 분리 힘으로
// 비켜 갑니다(간단한 회피). 이동이 끝나면(도착 · 실패) 월드 이벤트 버스에 NavMoveFinished 를 보냅니다.
#include "../scene/Component.hpp"
#include "NavSystem.hpp"

#include <cstdint>

enum class ENavMoveStatus : uint8_t { Idle = 0, Moving = 1, Arrived = 2, Failed = 3 };
const char* navMoveStatusName(ENavMoveStatus s);

class NavAgentComponent : public Component {
public:
    float speed          = 2.f;     // m/s
    float angularSpeed   = 360.f;   // 도/s, 진행 방향으로 몸을 돌림(-Z 가 앞)
    float arrivalRadius  = 0.3f;    // 끝점까지 가로 거리가 이 안이면 도착
    float radius         = 0.3f;    // 캐릭터 캡슐 · 회피 반경
    float height         = 1.8f;    // 캐릭터 캡슐 높이
    bool  useCharacter   = true;    // 물리가 있으면 CharacterVirtual 로 움직임
    bool  avoidance      = true;    // 다른 에이전트와 분리
    bool  allowPartial   = true;    // 끝점에 닿을 수 없으면 가장 가까운 곳까지(끄면 실패)
    float repathInterval = 0.5f;    // 쫓는 액터가 움직였거나 경로를 벗어났을 때 다시 찾는 최소 간격(게임 초)

    // 점으로 갑니다. 경로가 없으면(또는 부분 경로를 거부하면) false 이고 Failed 상태 + 이벤트.
    bool moveTo(const glm::vec3& goal);
    // 액터를 쫓습니다(repathInterval 마다 그 액터의 지금 위치로 다시 찾음). 액터가 사라지면 멈춤(실패).
    bool moveToActor(Actor* target);
    void stop();

    ENavMoveStatus status() const { return status_; }
    const NavPath& path() const { return path_; }
    glm::vec3      velocity() const { return velocity_; }
    glm::vec3      goal() const { return goal_; }
    int            repathCount() const { return repaths_; }

    const char* typeName() const override { return "NavAgentComponent"; }
    void tick(float dt) override;
    void onDisable() override;
    void onDestroy() override;
    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;
    std::vector<PropDesc> buildProps() override;
    std::string readOnlyInfo() const override;

private:
    bool  plan(const glm::vec3& goal, bool notifyOnFail);
    void  finish(bool success);
    void  releaseCharacter();
    glm::vec3 separation(const glm::vec3& pos, const glm::vec3& heading) const;
    void  moveBy(const glm::vec3& horizontalVelocity, float dt);
    void  turnToward(const glm::vec3& dir, float dt);

    ENavMoveStatus status_ = ENavMoveStatus::Idle;
    NavPath   path_;
    size_t    corner_ = 0;
    glm::vec3 goal_{ 0.f };
    glm::vec3 velocity_{ 0.f };
    uint64_t  targetId_ = 0;           // 쫓는 액터(Actor::id, 0 = 점)
    glm::vec3 plannedTargetPos_{ 0.f };
    double    lastPlanTime_ = -1e9;
    int       repaths_ = 0;
    uint32_t  character_ = 0;          // PhysicsSystem 캐릭터 핸들
    float     fallSpeed_ = 0.f;
    float     stuckTime_ = 0.f;
};
