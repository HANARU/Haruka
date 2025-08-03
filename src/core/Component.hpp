#pragma once
#include "Reflect.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <string>

class Actor;

class Component {
public:
    virtual ~Component() = default;

    // ── 공간 표현 (Spatial_Representation_Design.md) ────────────────────────────
    // 에디터 픽·프레이밍·기즈모 박스용 **로컬 솔리드 바운드**. 눈에 보이는 솔리드
    // 지오메트리를 가진 컴포넌트(메시, 3D UI 패널)만 재정의합니다. 라이트·카메라처럼
    // 솔리드 몸체가 없는 것은 false 를 두고 아이콘으로 잡힙니다(editorIcon).
    // 반환하는 바운드는 액터 로컬 공간입니다. 픽은 여기에 worldMatrix 를 곱해 OBB 로 씁니다.
    virtual bool localBounds(glm::vec3& /*outMin*/, glm::vec3& /*outMax*/) const { return false; }

    // 솔리드 바운드가 없는 엔티티(라이트·카메라·오디오)를 뷰포트에서 보이고 잡히게
    // 하는 에디터 아이콘입니다. true 면 outColor 로 아이콘을 그리고 화면 원판 판정으로
    // 픽합니다. 아이콘은 액터 원점에 화면 고정 크기로 그려집니다.
    virtual bool editorIcon(glm::vec4& /*outColor*/) const { return false; }

    // ── Lifecycle ──────────────────────────────────────────────────────────────
    virtual void onAttach()         {}  // immediately after addComponent()
    virtual void onStart()          {}  // first tick while enabled
    virtual void onDestroy()        {}  // before actor is removed from world
    virtual void onEnable()         {}  // enabled_ transitions false→true
    virtual void onDisable()        {}  // enabled_ transitions true→false
    virtual void tick(float /*dt*/) {}

    virtual const char*           typeName()     const { return "Component"; }
    virtual std::vector<PropDesc> buildProps()         { return {}; }
    virtual std::string           readOnlyInfo() const { return {}; }

    Actor* owner = nullptr;

    bool isEnabled() const { return enabled_; }
    void setEnabled(bool e) {
        if (e == enabled_) return;
        enabled_ = e;
        syncLive();   // enabled 변화가 실효를 바꾸면 onEnable/onDisable 발화
    }

    // 컴포넌트가 실제로 '살아있는'(enabled_ && 소유 액터의 activeInHierarchy) 상태로
    // 전이되는지 확인하고, 전이할 때만 onEnable/onDisable 을 한 번 발화합니다.
    // setEnabled 와 Actor 의 활성·계층 변경(Actor::refreshSubtreeActive)이 호출합니다.
    // owner->activeInHierarchy() 를 읽어야 해서 Actor 완전정의가 필요하므로 .cpp 에 둡니다.
    void syncLive();

    // enabled_ 이면서 소유 액터가 activeInHierarchy 인 실효 실행 상태인지.
    bool isLive() const { return live_; }

    void tickIfReady(float dt) {
        if (!live_) return;                 // enabled_ && 소유 액터 활성일 때만 틱
        if (!started_) { onStart(); started_ = true; }
        tick(dt);
    }

private:
    bool enabled_ = true;
    bool started_ = false;
    // onEnable 발화 후 true, onDisable 발화 후 false. onEnable/onDisable 엣지 판정용.
    bool live_    = false;
};
