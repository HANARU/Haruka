#pragma once
// 접촉 · 트리거 알림의 형(2026-11-20, Docs/Physics_Joints_And_Events.md D4).
//
// 물리 틱 끝(모든 스텝 뒤, PostPhysics 앞)에 메인 스레드에서 보냅니다. 같은 내용이 두 곳으로 갑니다.
//  - 양쪽 RigidBodyComponent 의 함수 칸(onTriggerEnter · onTriggerExit · onContactBegin · onContactStay · onContactEnd), 자기 쪽 기준
//  - 월드 이벤트 버스의 PhysicsContactEvent(쌍마다 한 번)
// 액터 포인터는 보낼 때 id 로 다시 찾습니다. 지워진 액터는 nullptr 입니다(몸을 지우면 그 몸의 쌍은 끝 · 나감을 받음).
#include <cstdint>
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

class Actor;

enum class EContactPhase : uint8_t { Begin, Stay, End };

// 컴포넌트 함수 칸이 받는 값(자기 쪽 기준).
struct PhysicsContact {
    EContactPhase phase   = EContactPhase::Begin;
    bool          trigger = false;     // 두 몸 중 하나가 트리거
    Actor*        other   = nullptr;   // 상대 액터(지워졌으면 nullptr)
    uint64_t      otherId = 0;         // 상대 액터 id(지워져도 남음)
    glm::vec3     point { 0.f };       // 시작 때의 접촉점(월드). 유지 · 끝은 시작 때 값
    glm::vec3     normal{ 0.f };       // 자기 → 상대 방향의 법선
};

// 이벤트 버스로 보내는 값(쌍마다 한 번). a · b 의 순서는 정해져 있지 않습니다.
struct PhysicsContactEvent {
    EContactPhase phase   = EContactPhase::Begin;
    bool          trigger = false;
    Actor*        a       = nullptr;
    Actor*        b       = nullptr;
    uint64_t      aId     = 0;
    uint64_t      bId     = 0;
    glm::vec3     point { 0.f };
    glm::vec3     normal{ 0.f };       // a → b
};
