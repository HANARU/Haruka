#pragma once
#include "HCommandStack.hpp"
#include "../core/Actor.hpp"

// Transform 변경 undo/redo 커맨드 — 기즈모 드래그와 디테일 패널 편집이 공용
struct SetTransformCmd : ICommand {
    Actor*    actor;
    Transform before, after;

    SetTransformCmd(Actor* a, Transform b, Transform af) : actor(a), before(b), after(af) {}
    void undo() override { actor->setLocalTransform(before); }
    void redo() override { actor->setLocalTransform(after);  }
    bool refersTo(const Actor* a) const override { return actor == a; }
};
