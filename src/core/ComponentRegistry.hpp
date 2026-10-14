#pragma once

// ── 컴포넌트 타입 레지스트리 (DEF-22) ───────────────────────────────────────────
// 씬 · 프리팹 파일의 COMPONENT 블록 이름으로 컴포넌트를 만들고, 에디터의 컴포넌트 추가 버튼을 만듭니다.
// 예전에는 SceneSerializer 와 에디터가 컴포넌트 5종을 손으로 나열해, 새 컴포넌트(Layer 2 포함)는 저장도
// 에디터 추가도 할 수 없었습니다.
//
// 등록은 명시적으로 합니다(사용자 결정, Bevy 의 register_type · Godot 의 ClassDB::register_class 와 같은
// 방식). 엔진은 시작할 때 기본 5종을 등록하고(components/CoreComponents.cpp), Layer 2 는
// EngineContext::components 로 등록합니다. 전역 객체의 생성자로 스스로 등록하는 방식(UE)은 정적
// 라이브러리로 묶을 때 링커가 그 객체를 버릴 수 있어 쓰지 않습니다.
//
// 렌더러 · 에셋과 무관해 화면 없이 검산할 수 있습니다(scene_components 테스트).

#include <functional>
#include <string>
#include <vector>

#include "Actor.hpp"
#include "HLog.hpp"
#include "SceneLog.hpp"

class ComponentRegistry {
public:
    struct Entry {
        std::string              name;          // 파일의 COMPONENT: 뒤에 쓰는 이름(typeName() 과 같아야 함)
        std::vector<std::string> aliases;       // 옛 이름. 이름을 바꿨을 때 옛 파일을 읽으려고 둡니다
        std::string              editorLabel;   // 에디터의 "+ 라벨" 버튼. 비우면 버튼을 만들지 않습니다
        std::function<Component*(Actor&)>       add;    // 액터에 붙여 돌려줌
        std::function<Component*(const Actor&)> find;   // 이미 붙어 있는 것(없으면 null)
    };

    // 엔진 전체에 하나입니다(Godot 의 ClassDB 처럼). 테스트도 같은 것을 씁니다.
    static ComponentRegistry& get() { static ComponentRegistry r; return r; }

    // T::typeName() 과 같은 이름으로 등록합니다. 이름이나 옛 이름이 이미 있으면 경고하고 false.
    template <class T>
    bool registerType(std::string name, std::string editorLabel = {}, std::vector<std::string> aliases = {}) {
        if (find(name)) {
            HLOG(LogScene, Warning) << "component type '" << name << "' is already registered; ignored";
            return false;
        }
        for (const auto& a : aliases)
            if (find(a)) {
                HLOG(LogScene, Warning) << "component alias '" << a << "' of '" << name
                                        << "' is already registered; ignored the type";
                return false;
            }
        Entry e;
        e.name        = std::move(name);
        e.aliases     = std::move(aliases);
        e.editorLabel = std::move(editorLabel);
        e.add  = [](Actor& a) -> Component* { return a.addComponent<T>(); };
        e.find = [](const Actor& a) -> Component* { return a.getComponent<T>(); };
        entries_.push_back(std::move(e));
        return true;
    }

    // 이름이나 옛 이름으로 찾습니다.
    const Entry* find(const std::string& nameOrAlias) const {
        for (const auto& e : entries_) {
            if (e.name == nameOrAlias) return &e;
            for (const auto& a : e.aliases) if (a == nameOrAlias) return &e;
        }
        return nullptr;
    }

    // 등록한 순서입니다(에디터 버튼 순서).
    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
};
