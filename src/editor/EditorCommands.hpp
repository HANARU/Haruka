#pragma once
#include "HCommandStack.hpp"
#include "ActorSnapshot.hpp"
#include "../core/Actor.hpp"
#include "../core/World.hpp"

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

// 액터 서브트리의 생성 · 삭제 undo/redo 커맨드(2026-10-15). 지울 때마다 그 순간의 상태를 떠 두고(ActorSnapshot),
// 되살릴 때 원래 ID 와 원래 부모로 다시 만듭니다. 액터를 포인터가 아니라 ID 로 기억하므로, 이 커맨드가 지운
// 액터 때문에 purge 되지 않습니다(refersTo 는 기본값 false). 삭제 전에 그 액터를 붙잡던 다른 커맨드는 지울 때
// onActorDestroyed 로 걷힙니다(사용자 결정: 삭제 전 이력은 보존하지 않음).
//   Delete: 실행 = 지움, undo = 되살림, redo = 다시 지움
//   Create: (이미 만들어진 뒤 쌓음) undo = 지움, redo = 되살림
struct ActorPresenceCmd : ICommand {
    enum class Kind { Delete, Create };

    Kind                       kind;
    World*                     world;
    SnapshotWriter             write;
    SnapshotReader             read;
    std::function<void(Actor*)> onRestored;   // 되살린 루트마다(선택 · 아웃라이너 갱신)
    std::vector<uint64_t>      rootIds;
    std::vector<ActorSnapshot> snaps;          // rootIds 와 같은 순서. 지운 동안만 채워져 있음

    ActorPresenceCmd(Kind k, World& w, std::vector<uint64_t> roots, SnapshotWriter wr, SnapshotReader rd,
                     std::function<void(Actor*)> restored)
        : kind(k), world(&w), write(std::move(wr)), read(std::move(rd)),
          onRestored(std::move(restored)), rootIds(std::move(roots)) {}

    void undo() override { if (kind == Kind::Delete) restore(); else remove(); }
    void redo() override { if (kind == Kind::Delete) remove();  else restore(); }

    // 지금 상태를 떠 두고 지웁니다. 다른 경로로 이미 사라진 루트는 건너뜁니다.
    void remove() {
        snaps.clear();
        for (uint64_t id : rootIds) {
            Actor* a = world->findById(id);
            if (!a) { snaps.emplace_back(); continue; }
            snaps.push_back(hsnap::capture(a, write));
            world->destroyActor(a);
        }
    }
    void restore() {
        for (const ActorSnapshot& s : snaps) {
            if (s.ids.empty()) continue;
            if (Actor* root = hsnap::restore(s, *world, read))
                if (onRestored) onRestored(root);
        }
        snaps.clear();
    }
};
