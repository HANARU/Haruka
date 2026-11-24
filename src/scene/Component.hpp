#pragma once
#include "Reflect.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <string>

class Actor;
class ComponentFieldWriter;
struct ComponentLoadContext;

// 틱 단계(2026-11-20, Docs/Gameplay_Tools.md D5; 2026-11-24 D2a).
//   PrePhysics  : 시뮬레이션 틱(물리 앞)
//   PostPhysics : 시뮬레이션 틱(물리 뒤, World::tickPostPhysics)
//   Display     : 표시 단계(프레임마다 1회, 프레임 dt, World::tickDisplay) — 애니메이션 · 파티클 · 손/머리 추적
enum class ETickGroup : uint8_t { PrePhysics, PostPhysics, Display };

class Component {
public:
    virtual ~Component() = default;

    // ── 공간 표현 (Spatial_Representation_Design.md) ────────────────────────────
    // 에디터 픽·프레이밍·기즈모 박스용 **로컬 솔리드 바운드**. 눈에 보이는 솔리드
    // 지오메트리를 가진 컴포넌트(메시, 3D UI 패널)만 재정의합니다. 라이트·카메라처럼
    // 솔리드 몸체가 없는 것은 false 를 두고 아이콘으로 잡힙니다(editor/ComponentVisualizers).
    // 반환하는 바운드는 액터 로컬 공간입니다. 픽은 여기에 worldMatrix 를 곱해 OBB 로 씁니다.
    virtual bool localBounds(glm::vec3& /*outMin*/, glm::vec3& /*outMax*/) const { return false; }
    // 시스템별 질의는 기반 클래스에 두지 않고, 그 질의를 받는 컴포넌트가 질의 인터페이스를 함께 상속합니다(2026-11-18,
    // 엔진 구조 안정화 S4): 물리의 메시 형상 = ICollisionMeshSource(geometry/CollisionMesh.hpp), XR 리그 바닥 찾기에서
    // 빼기 = INotFloorSurface(components/FloorSurface.hpp). 예전 가상 함수 collisionMesh · blocksFloor 자리입니다.

    // 에디터 아이콘과 선택했을 때의 범위 표시는 에디터가 형마다 등록합니다(editor/ComponentVisualizers.hpp, 2026-11-18 S4,
    // UE FComponentVisualizer 와 같은 자리). 예전 가상 함수 editorIcon · editorIconShape · drawEditorVisual 자리입니다.

    // ── Lifecycle ──────────────────────────────────────────────────────────────
    virtual void onAttach()         {}  // immediately after addComponent()
    virtual void onStart()          {}  // first tick while enabled
    virtual void onDestroy()        {}  // before actor is removed from world
    virtual void onEnable()         {}  // enabled_ transitions false→true
    virtual void onDisable()        {}  // enabled_ transitions true→false
    virtual void tick(float /*dt*/) {}

    // 틱 단계. 바꾸려면 생성자에서 정하십시오(붙인 뒤에 바꾸면 다음 구조 변경 때까지 월드가 모를 수 있음).
    ETickGroup tickGroup = ETickGroup::PrePhysics;

    virtual const char*           typeName()     const { return "Component"; }
    virtual std::vector<PropDesc> buildProps()         { return {}; }
    virtual std::string           readOnlyInfo() const { return {}; }

    // ── 에디터 편집 확정 ────────────────────────────────────────────────────────
    // 에디터가 사용자의 편집 한 번을 확정할 때 부릅니다(숫자 칸 드래그 끝·입력 확정, 열거·
    // 토글 칸 클릭, 그 편집의 undo·redo). 드래그 중에는 buildProps 의 세터가 마우스가
    // 움직일 때마다 불리므로, 값이 바뀔 때 하는 무거운 일(물리 바디 재생성 등)은 세터가
    // 아니라 여기서 합니다(DEF-43).
    virtual void onPropsCommitted() {}
    // 에디터가 소유 액터(또는 조상)의 트랜스폼 편집을 확정할 때 부릅니다(기즈모 드래그 끝,
    // 디테일 TRANSFORM 확정, 그 undo·redo). Actor::notifyTransformCommitted 가 부릅니다.
    virtual void onTransformCommitted() {}

    // ── 직렬화 (DEF-22, core/ComponentFields.hpp) ─────────────────────────────
    // 씬 · 프리팹 파일의 COMPONENT 블록에 쓸 값입니다. 이름은 멤버 이름을 따르고, 한 번 저장한 이름은
    // 바꾸지 않습니다. enabled 는 직렬화기가 모든 컴포넌트에 공통으로 쓰므로 여기서 쓰지 않습니다.
    // 새 컴포넌트는 이 셋을 구현하고 ComponentRegistry 에 등록하면 저장 · 불러오기 · 에디터 추가가 됩니다.
    virtual void writeFields(ComponentFieldWriter& /*w*/) const {}
    // 이름 하나를 읽습니다. 모르는 이름이면 false 를 돌려주고 직렬화기가 경고합니다. 값이 깨졌으면
    // true 를 돌려주되 기본값을 두고 reportBadField 로 알립니다.
    virtual bool readField(const std::string& /*key*/, const std::string& /*value*/) { return false; }
    // 블록의 값을 다 읽은 뒤 한 번 부릅니다. 에셋을 불러오는 것처럼 값 여러 개가 함께 필요한 일을 합니다.
    virtual void onFieldsLoaded(const ComponentLoadContext& /*ctx*/) {}
    // 에디터가 디테일 패널에 이 컴포넌트를 보일 때 부릅니다(H3b). 에셋 경로 칸의 세터가 에셋을 다시 읽을 곳을 받아 둡니다(코드로 만든
    // 컴포넌트는 onFieldsLoaded 를 거치지 않아 모름). 값은 바꾸지 않습니다.
    virtual void bindLoadContext(const ComponentLoadContext& /*ctx*/) {}
    // 깨진 값을 알립니다(액터 이름 · 타입 · 이름 · 값). readField 구현이 씁니다.
    void reportBadField(const std::string& key, const std::string& value) const;

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
