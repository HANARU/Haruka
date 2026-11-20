#pragma once
// 내비 · AI 가 월드 이벤트 버스(World::events())로 보내는 사건(2026-11-21, Docs/Navigation_AI.md D6 · D8).
#include "NavMesh.hpp"
#include <cstdint>

class Actor;

// 에이전트의 이동이 끝남: 도착(success) 또는 실패(경로 없음 · 부분 경로를 거부 · 막힘). 보낸 뒤 에이전트는 멈춰 있습니다.
struct NavMoveFinished {
    Actor*         agent   = nullptr;
    bool           success = false;
    ENavPathResult path    = ENavPathResult::NoPath;   // 마지막으로 찾은 경로의 결과
};

enum class EAIState : uint8_t { Wait = 0, Patrol = 1, Chase = 2 };
const char* aiStateName(EAIState s);

// AIBrainComponent 의 상태가 바뀜.
struct AIStateChanged {
    Actor*   actor = nullptr;
    EAIState from  = EAIState::Wait;
    EAIState to    = EAIState::Wait;
    Actor*   target = nullptr;   // Chase 면 쫓는 액터
};
