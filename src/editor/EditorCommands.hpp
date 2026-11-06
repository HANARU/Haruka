#pragma once
#include "HCommandStack.hpp"
#include "ActorSnapshot.hpp"
#include "../core/Actor.hpp"
#include "../core/World.hpp"
#include "../core/Component.hpp"
#include "../core/ComponentFields.hpp"
#include "../core/ComponentRegistry.hpp"
#include "../core/Reflect.hpp"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// ── 에디터 Undo 커맨드 ─────────────────────────────────────────────────────────
// 2026-10-15 부터 모든 커맨드가 액터를 포인터가 아니라 ID 로 기억합니다(UE 의 오브젝트 경로 · Unity 의
// InstanceID · Godot 의 ObjectID 와 같은 생각). 실행할 때마다 World::findById 로 찾고, 없으면 아무것도 하지
// 않습니다. 액터 삭제 Undo(ActorPresenceCmd)가 원래 ID 로 되살리므로 "편집 → 삭제 → 삭제 되돌리기 → 편집
// 되돌리기"가 이어집니다. 예전에는 포인터를 붙잡아서, 액터가 지워지면 이력을 걷어 내야 했습니다(refersTo).
// 컴포넌트는 타입 이름과 같은 타입 안의 순번으로 찾습니다. 그래서 커맨드가 해제된 메모리를 만질 길이 없습니다.

namespace hcmd {

// 같은 타입 이름의 컴포넌트 중 ordinal 번째(붙은 순서). 없으면 null.
inline Component* findComponent(const Actor& a, const std::string& type, int ordinal) {
    int n = 0;
    for (const auto& c : a.getComponents())
        if (type == c->typeName() && n++ == ordinal) return c.get();
    return nullptr;
}
inline int ordinalOf(const Actor& a, const Component* comp) {
    int n = 0;
    for (const auto& c : a.getComponents()) {
        if (c.get() == comp) return n;
        if (std::string(c->typeName()) == comp->typeName()) ++n;
    }
    return -1;
}

// 디테일 패널 속성 값. 칸 이름(PropDesc::label, 한 컴포넌트 안에서 겹치지 않음)과 종류로 기억합니다(UE 의 속성 경로 ·
// Unity 의 SerializedProperty 경로와 같은 생각). 예전에는 "종류별 순번"으로 기억해서, 토글 · 열거에 따라 칸이 나타나거나
// 사라지는 컴포넌트(라이트의 Use Temperature · Type, 소리의 Spatialize)에서 Undo · Redo 가 값을 다른 칸에 넣었습니다
// (DEF-131). 세터를 거쳐 되돌리므로 값과 함께 하는 처리(범위 제한 등)가 편집할 때와 같습니다. 무거운 뒷일은
// onPropsCommitted 가 합니다(DEF-43).
struct PropValue {
    std::string    label;
    PropDesc::Kind kind = PropDesc::Float;
    float          f    = 0.f;
    int            i    = 0;
    bool           b    = false;
    bool operator==(const PropValue& o) const {
        return label == o.label && kind == o.kind && f == o.f && i == o.i && b == o.b;
    }
};
struct PropValues {
    std::vector<PropValue> v;
    bool operator==(const PropValues& o) const { return v == o.v; }
    bool operator!=(const PropValues& o) const { return !(*this == o); }
    size_t count(PropDesc::Kind k) const {
        size_t n = 0;
        for (const PropValue& e : v) n += (e.kind == k);
        return n;
    }
};
inline PropValues captureProps(Component& c) {
    PropValues out;
    for (const PropDesc& pd : c.buildProps()) {
        PropValue e;
        e.label = pd.label ? pd.label : "";
        e.kind  = pd.kind;
        if (pd.kind == PropDesc::Float && pd.getF)       e.f = pd.getF();
        else if (pd.kind == PropDesc::Toggle && pd.getB) e.b = pd.getB();
        else if (pd.kind == PropDesc::Enum && pd.getI)   e.i = pd.getI();
        else continue;
        out.v.push_back(std::move(e));
    }
    return out;
}
// 칸 이름 · 종류가 같은 칸에만 넣습니다. 토글 · 열거를 넣으면 칸 목록이 바뀔 수 있으므로(새 칸이 나타남), 그런 값을 넣은
// 차례가 끝나면 목록을 다시 받아 아직 넣지 않은 값을 이어서 넣습니다. 지금 보이지 않는 칸의 값은 넣지 않습니다(그 칸은
// 기억할 때도 보이지 않던 칸이거나, 되돌린 상태에서 숨는 칸입니다).
inline void applyProps(Component& c, const PropValues& v) {
    std::vector<char> done(v.v.size(), 0);
    for (int pass = 0; pass < 4; ++pass) {
        bool layoutMayChange = false;
        for (const PropDesc& pd : c.buildProps()) {
            const char* lbl = pd.label ? pd.label : "";
            for (size_t k = 0; k < v.v.size(); ++k) {
                const PropValue& e = v.v[k];
                if (done[k] || e.kind != pd.kind || e.label != lbl) continue;
                if (pd.kind == PropDesc::Float && pd.setF)       pd.setF(e.f);
                else if (pd.kind == PropDesc::Toggle && pd.setB) { pd.setB(e.b); layoutMayChange = true; }
                else if (pd.kind == PropDesc::Enum && pd.setI)   { pd.setI(e.i); layoutMayChange = true; }
                done[k] = 1;
                break;
            }
        }
        if (!layoutMayChange) break;
    }
    c.onPropsCommitted();
}

} // namespace hcmd

// 여러 커맨드를 한 번의 Undo 로 묶습니다(여러 액터를 함께 옮긴 기즈모 드래그, 컴포넌트를 붙이고 값을 넣은 편집).
struct CompositeCmd : ICommand {
    std::vector<std::unique_ptr<ICommand>> cmds;
    void add(std::unique_ptr<ICommand> c) { if (c) cmds.push_back(std::move(c)); }
    bool empty() const { return cmds.empty(); }
    void undo() override { for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) (*it)->undo(); }
    void redo() override { for (auto& c : cmds) c->redo(); }
};

// Transform 변경 — 기즈모 드래그와 디테일 패널 편집이 공용
struct SetTransformCmd : ICommand {
    World*    world;
    uint64_t  id;
    Transform before, after;

    SetTransformCmd(World& w, const Actor* a, Transform b, Transform af)
        : world(&w), id(a ? a->id : 0), before(b), after(af) {}
    // 되돌린 트랜스폼도 편집 확정입니다. 물리 바디처럼 트랜스폼을 따로 들고 있는 컴포넌트가
    // 따라오게 알립니다(DEF-43).
    void apply(const Transform& t) {
        if (Actor* a = world->findById(id)) { a->setLocalTransform(t); a->notifyTransformCommitted(); }
    }
    void undo() override { apply(before); }
    void redo() override { apply(after); }
};

struct RenameActorCmd : ICommand {
    World* world; uint64_t id; std::string before, after;
    RenameActorCmd(World& w, const Actor* a, std::string b, std::string af)
        : world(&w), id(a->id), before(std::move(b)), after(std::move(af)) {}
    void undo() override { if (Actor* a = world->findById(id)) a->name = before; }
    void redo() override { if (Actor* a = world->findById(id)) a->name = after; }
};

// 아웃라이너의 켜기/끄기(Actor::setActive)
struct SetActiveCmd : ICommand {
    World* world; uint64_t id; bool before, after;
    SetActiveCmd(World& w, const Actor* a, bool b, bool af) : world(&w), id(a->id), before(b), after(af) {}
    void undo() override { if (Actor* a = world->findById(id)) a->setActive(before); }
    void redo() override { if (Actor* a = world->findById(id)) a->setActive(after); }
};

// 부모 바꾸기. 월드 자세를 유지하도록 미리 구한 로컬 트랜스폼과 형제 자리를 함께 기억합니다(UE · Unity · Godot 의
// 에디터와 같음). 부모 ID 0 은 월드 루트입니다. 루트의 순서는 ID 순이라 자리를 기억하지 않습니다.
struct ReparentCmd : ICommand {
    World* world; uint64_t id;
    uint64_t  oldParent, newParent;
    int       oldIndex,  newIndex;
    Transform oldLocal,  newLocal;
    ReparentCmd(World& w, uint64_t actorId, uint64_t op, int oi, Transform ol, uint64_t np, int ni, Transform nl)
        : world(&w), id(actorId), oldParent(op), newParent(np), oldIndex(oi), newIndex(ni),
          oldLocal(ol), newLocal(nl) {}
    void apply(uint64_t parentId, int index, const Transform& local) {
        Actor* a = world->findById(id);
        if (!a) return;
        Actor* p = parentId ? world->findById(parentId) : nullptr;
        if (parentId && !p) return;   // 부모가 사라졌으면 하지 않습니다
        a->attachTo(p, index);
        a->setLocalTransform(local);
        a->notifyTransformCommitted();
    }
    void undo() override { apply(oldParent, oldIndex, oldLocal); }
    void redo() override { apply(newParent, newIndex, newLocal); }
};

// 디테일 패널의 컴포넌트 켜기/끄기(Component::setEnabled)
struct ComponentEnabledCmd : ICommand {
    World* world; uint64_t id; std::string type; int ordinal; bool before, after;
    ComponentEnabledCmd(World& w, const Actor* a, const Component* c, bool b, bool af)
        : world(&w), id(a->id), type(c->typeName()), ordinal(hcmd::ordinalOf(*a, c)), before(b), after(af) {}
    void set(bool v) {
        if (Actor* a = world->findById(id))
            if (Component* c = hcmd::findComponent(*a, type, ordinal)) c->setEnabled(v);
    }
    void undo() override { set(before); }
    void redo() override { set(after); }
};

// 디테일 패널의 속성 편집(숫자 · 토글 · 열거). 예전 PropsSnapshotCmd 는 세터가 붙잡은 컴포넌트 포인터를 썼고 숫자만
// 되돌렸습니다. 이제 실행할 때 컴포넌트를 다시 찾고 buildProps 로 새 세터를 받아, 토글 · 열거도 되돌립니다.
struct ComponentPropsCmd : ICommand {
    World* world; uint64_t id; std::string type; int ordinal;
    hcmd::PropValues before, after;
    ComponentPropsCmd(World& w, const Actor* a, const Component* c, hcmd::PropValues b, hcmd::PropValues af)
        : world(&w), id(a->id), type(c->typeName()), ordinal(hcmd::ordinalOf(*a, c)),
          before(std::move(b)), after(std::move(af)) {}
    void set(const hcmd::PropValues& v) {
        if (Actor* a = world->findById(id))
            if (Component* c = hcmd::findComponent(*a, type, ordinal)) hcmd::applyProps(*c, v);
    }
    void undo() override { set(before); }
    void redo() override { set(after); }
};

// 컴포넌트 추가 · 제거. 지울 때마다 그 순간의 값을 씬 저장과 같은 이름 · 값 쌍으로 떠 두고(writeFields), 되살릴 때
// 씬을 읽을 때와 같은 길(레지스트리로 만들고 readField → onFieldsLoaded)로 원래 자리에 다시 붙입니다.
//   Remove: 실행 = 지움, undo = 되살림, redo = 다시 지움
//   Add:    (이미 붙인 뒤 쌓음) undo = 지움, redo = 되살림
struct ComponentPresenceCmd : ICommand {
    enum class Kind { Add, Remove };
    Kind     kind;
    World*   world;
    uint64_t id;
    std::string type;
    int      index   = -1;      // 붙은 순서 안의 자리
    bool     enabled = true;
    std::vector<ComponentFieldWriter::Field> fields;
    ComponentLoadContext  ctx;
    std::function<void()> onChanged;   // 디테일 패널 다시 만들기 등

    ComponentPresenceCmd(Kind k, World& w, const Actor* a, const Component* c, ComponentLoadContext lc,
                         std::function<void()> changed)
        : kind(k), world(&w), id(a->id), type(c->typeName()), index(a->componentIndex(c)),
          ctx(lc), onChanged(std::move(changed)) {}

    void remove() {
        Actor* a = world->findById(id);
        if (!a) return;
        Component* c = (index >= 0 && index < (int)a->getComponents().size() &&
                        type == a->getComponents()[(size_t)index]->typeName())
                     ? a->getComponents()[(size_t)index].get() : hcmd::findComponent(*a, type, 0);
        if (!c) return;
        ComponentFieldWriter w;
        c->writeFields(w);
        fields  = w.fields();
        enabled = c->isEnabled();
        index   = a->componentIndex(c);
        a->removeComponent(c);
        if (onChanged) onChanged();
    }
    void restore() {
        Actor* a = world->findById(id);
        const ComponentRegistry::Entry* e = ComponentRegistry::get().find(type);
        if (!a || !e) return;
        Component* c = e->add(*a);
        if (!c) return;
        for (const auto& [k, v] : fields) c->readField(k, v);
        c->setEnabled(enabled);
        c->onFieldsLoaded(ctx);
        if (index >= 0) a->moveComponent(c, (size_t)index);
        if (onChanged) onChanged();
    }
    void undo() override { if (kind == Kind::Remove) restore(); else remove(); }
    void redo() override { if (kind == Kind::Remove) remove();  else restore(); }
};

// 액터 서브트리의 생성 · 삭제 undo/redo 커맨드(2026-10-15). 지울 때마다 그 순간의 상태를 떠 두고(ActorSnapshot),
// 되살릴 때 원래 ID 와 원래 부모로 다시 만듭니다. 원래 ID 로 되살리므로 그 액터를 가리키던 다른 커맨드도
// 다시 이어집니다.
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
