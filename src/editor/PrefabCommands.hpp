#pragma once
// 링크된 프리팹의 에디터 커맨드(2026-11-21, Docs/Linked_Prefabs.md D9). 액터는 다른 커맨드처럼 ID 로 찾습니다.
#include "HCommandStack.hpp"
#include "../scene/Actor.hpp"
#include "../scene/ComponentFields.hpp"
#include "../scene/World.hpp"
#include "../sceneio/PrefabLink.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

// 액터 하나의 상태(이름 · 트랜스폼 · 활성 · 태그 · 컴포넌트)를 글로 바꿔 넣습니다. 덮어쓰기 되돌리기가 씁니다.
struct ActorStateCmd : ICommand {
    World*                world;
    uint64_t              id;
    std::string           before, after;
    ComponentLoadContext  ctx;
    std::function<void()> onChanged;
    ActorStateCmd(World& w, const Actor& a, std::string b, std::string af, ComponentLoadContext c, std::function<void()> changed)
        : world(&w), id(a.id), before(std::move(b)), after(std::move(af)), ctx(c), onChanged(std::move(changed)) {}
    void set(const std::string& t) {
        if (Actor* a = world->findById(id)) {
            hprefablink::applyActorBlock(*a, t, ctx);
            if (onChanged) onChanged();
        }
    }
    void undo() override { set(before); }
    void redo() override { set(after); }
};

// 링크 끊기(Unity Unpack · Godot Make Local): 인스턴스 루트와 소속 액터의 링크를 지워 보통 액터로 바꿉니다.
struct PrefabUnlinkCmd : ICommand {
    World* world;
    std::vector<std::pair<uint64_t, Actor::PrefabLink>> saved;   // 액터 ID → 끊기 전 링크
    std::function<void()> onChanged;
    PrefabUnlinkCmd(World& w, Actor& root, std::function<void()> changed) : world(&w), onChanged(std::move(changed)) {
        for (Actor* a : hprefablink::membersOf(&root)) saved.emplace_back(a->id, a->prefabLink);
    }
    void undo() override {
        for (const auto& [id, link] : saved)
            if (Actor* a = world->findById(id)) a->prefabLink = link;
        if (onChanged) onChanged();
    }
    void redo() override {
        for (const auto& [id, link] : saved)
            if (Actor* a = world->findById(id)) a->prefabLink = Actor::PrefabLink{};
        if (onChanged) onChanged();
    }
};
