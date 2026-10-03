#pragma once
#include "HCommandStack.hpp"
#include "../core/Actor.hpp"

// Transform 변경 undo/redo 커맨드 — 기즈모 드래그와 디테일 패널 편집이 공용
struct SetTransformCmd : ICommand {
    Actor*    actor;
    Transform before, after;

    SetTransformCmd(Actor* a, Transform b, Transform af) : actor(a), before(b), after(af) {}
    // 되돌린 트랜스폼도 편집 확정입니다. 물리 바디처럼 트랜스폼을 따로 들고 있는 컴포넌트가
    // 따라오게 알립니다(DEF-43).
    void undo() override { actor->setLocalTransform(before); actor->notifyTransformCommitted(); }
    void redo() override { actor->setLocalTransform(after);  actor->notifyTransformCommitted(); }
    bool refersTo(const Actor* a) const override { return actor == a; }
};
