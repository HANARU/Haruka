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
        // 프리팹 파일을 썼음(Save as Prefab · 원본에 적용): 경로 · 쓴 글. EditorApp 이 인스턴스를 다시 펼치고 목록을 갱신합니다.
        std::function<void(const std::string&, const std::string&)> onPrefabWritten;
        // 액터 참조 칸(참조 체계 R-7)을 누르면 부릅니다: 제목 · 지금 값(GUID) · 고른 값을 받을 함수. EditorApp 이 목록 대화상자를 엽니다.
        std::function<void(const std::string&, uint64_t, std::function<void(uint64_t)>)> pickActor;
        // 프리팹 링크가 바뀜(링크 끊기 · 되돌리기): 아웃라이너 표시를 다시 만듭니다.
        std::function<void()> onPrefabLinksChanged;
        // Add Component 단추를 누름(2026-11-21): 대상 액터 · 단추 위젯. EditorApp 이 그 아래에 검색 팝업을 엽니다.
        std::function<void(Actor*, const std::shared_ptr<HWidget>&)> onOpenComponentPicker;
    };

    void init(Deps deps);
    std::shared_ptr<HWidget> build();

    void rebuild();
    void flushRebuild();
    void markDirty() { dirty_ = true; }

    void onActorDestroyed(Actor* a);
    // ComponentRegistry::entries()[entryIndex] 타입을 붙입니다(Undo 가능). 이미 있으면 아무것도 하지 않습니다.
    void addComponentUndoable(Actor* a, int entryIndex);
    // 마지막으로 만든 Add Component 단추(덤프가 팝업 자리를 정할 때). 없으면 null.
    std::shared_ptr<HWidget> addComponentButton() const { return addCompBtn_.lock(); }
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
    // 링크된 프리팹(Docs/Linked_Prefabs.md D9): 원본 · 덮어쓰기 수 · 되돌리기 · 원본에 적용 · 링크 끊기.
    void buildPrefabSection(Actor& sel);
    // 지금 고른 소속 액터에서 path 로 시작하는 덮어쓰기가 있는지(build 때 한 번 구함).
    bool overridden(const std::string& prefix) const;

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
    void addActorRefRow(const char* lbl, std::function<uint64_t()> getG, std::function<void(uint64_t)> setG);
    void addBoolRow(const char* lbl,
                    std::function<bool()>    getB,
                    std::function<void(bool)> setB);

    Deps deps_;
    std::shared_ptr<SVerticalBox> list_;
    std::shared_ptr<SScrollBox>   scroll_;
    bool   dirty_           = false;
    Actor* actor_           = nullptr;
    std::weak_ptr<HWidget> addCompBtn_;
    bool   txExpanded_      = true;
    std::vector<std::string> overrides_;   // 고른 소속 액터의 덮어쓴 경로(build 때 구함)
};
