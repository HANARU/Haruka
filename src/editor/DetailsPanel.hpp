#pragma once
#include "EditorUiKit.hpp"
#include "EditorContext.hpp"
#include "HCommandStack.hpp"
#include "../core/HLog.hpp"
#include "../core/Transform.hpp"
#include "../ui/slate/HSlate.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Actor;
class Component;
struct PropDesc;

// 디테일(Inspector) 패널(S6b). 선택 액터의 트랜스폼 · 컴포넌트 속성 · 컴포넌트 추가 · 프리팹 저장 UI 를 그립니다.
// 여러 패널이 함께 쓰는 상태(ctx_, cmdStack_, cachedEuler_, undoSnapshot_, prefabFiles_)는 EditorApp 이 소유하고
// 참조/포인터와 콜백으로 받습니다. EditorApp* 는 들고 있지 않습니다.
class DetailsPanel {
public:
    struct Deps {
        EditorUiKit*          ui           = nullptr;
        EditorContext*        ctx          = nullptr;
        HCommandStack*        cmdStack     = nullptr;
        glm::vec3*            cachedEuler  = nullptr;
        Transform*            undoSnapshot = nullptr;
        HLogCategory*         logCat       = nullptr;
        std::function<void()> onPrefabSaved;
    };

    void init(Deps deps);
    std::shared_ptr<HWidget> build();

    void rebuild();
    void flushRebuild();
    void markDirty() { dirty_ = true; }

    void checkWidthChange();
    void onActorDestroyed(Actor* a);
    bool scrollContains(const glm::vec2& p) const;

private:
    float s(float x) const { return deps_.ui->s(x); }
    void buildTransformSection(Actor& sel);
    void buildComponentsSection(Actor& sel);
    void buildComponentHeader(Actor& sel, Component& comp);
    void buildComponentProp(Actor& sel, Component& comp, const PropDesc& pd,
                            const std::function<void()>& compBegin,
                            const std::function<void()>& compEnd,
                            const std::function<void(const std::function<void()>&)>& commitClick,
                            const std::vector<Component*>& peers);
    void buildAddComponentSection(Actor& sel);
    void buildSavePrefabRow(Actor& sel);

    void addNumRow(const char* lbl, glm::vec4 lc,
                   std::function<float()>     getter,
                   std::function<void(float)> setter,
                   float step, ENumberUnit unit,
                   std::function<void()> bFn,
                   std::function<void()> eFn);
    void addEnumRow(const char* lbl,
                    const std::vector<const char*>& opts,
                    std::function<int()>    getI,
                    std::function<void(int)> setI);
    void addBoolRow(const char* lbl,
                    std::function<bool()>    getB,
                    std::function<void(bool)> setB);

    Deps deps_;
    std::shared_ptr<SVerticalBox> list_;
    std::shared_ptr<SScrollBox>   scroll_;
    bool   dirty_           = false;
    int    addCompWidthSig_ = -1;
    Actor* actor_           = nullptr;
    bool   txExpanded_      = true;
};
