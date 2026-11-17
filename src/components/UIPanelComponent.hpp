#pragma once
#include "../core/Component.hpp"
#include "../ui/slate/HSlateContext.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// ── 월드 UI 패널 (3D UI 패널 P1b, 2026-11-16, Docs/Spatial_Representation_Design.md 6단계) ─────────────────
// HSlate 위젯 트리를 텍스처에 그려 월드의 얇은 판에 붙입니다(UE 의 Widget Component(World), Unity 의 World Space Canvas).
//
//  - 판은 액터 로컬 XY 평면의 width × height(m) 사각형이고 가운데가 액터 원점, 앞면이 +Z 입니다. 액터 크기(scale)를 곱합니다.
//  - 텍스처 크기는 width · height × pixelsPerMeter(기본 1,250 px/m, Quest Pro 약 22 PPD 기준)를 반올림하고 4096 으로 자릅니다.
//    편집을 확정할 때만 다시 정합니다(끄는 동안은 판만 늘고 텍스처는 그대로라 GPU 를 기다리지 않음).
//  - 화소는 조명 · 노출 · AO · 블룸 · 톤매핑을 받지 않습니다(D1-나: HDR 알파 = 후처리 가중치). 장면 깊이에는 가려집니다.
//  - 내용은 이름으로 고릅니다(content). 엔진에는 "Demo" 가 있고, 게임 모듈이 registerContent 로 더합니다.
//  - 다시 그리기는 자동입니다: 렌더러가 보이는 패널마다 프레임마다 그리기 목록을 만들어 직전과 해시가 다를 때만 GPU 로
//    다시 그리고 밉을 만듭니다(Attr 바인딩 글자 · 색도 따라감). markDirty 는 목록이 같아도 다시 그리게 하는 강제용입니다.
class UIPanelComponent : public Component {
public:
    float       width          = 0.8f;   // m(액터 크기를 곱하기 전)
    float       height         = 0.5f;
    float       pixelsPerMeter = kDefaultPixelsPerMeter;
    bool        twoSided       = false;  // 뒷면도 그림(좌우를 뒤집어 글자가 바로 읽힘)
    std::string content        = "Demo";
    // 패널 화소에 곱하는 밝기(선형, [kMinBrightness, 1]). 패널은 톤매핑을 받지 않아 흰색 1.0 이 화면 최대 밝기라, 어두운
    // 장면에서 눈부시면 낮춥니다. 1 이면 텍스처 그대로입니다.
    float       brightness     = 1.f;

    // 배치(3D UI 구간 7): World = 액터 자리에 고정(기본), FollowHead = Play 중 머리 앞으로 천천히 따라옴(lazy follow).
    // 따라오기는 머리가 크게 돌거나(30° 넘게) 거리 · 높이가 0.35 m 넘게 어긋날 때 움직이기 시작해 목표에 닿으면 멈춥니다.
    // 고개를 조금 돌리는 정도로는 움직이지 않아 글을 읽기 편합니다. 목표는 머리 앞 수평 followDistance(m), 눈높이 +
    // followHeight(m)이고, 패널 앞면이 머리를 향합니다(요만, 기울이지 않음). 에디터는 Play 를 멈추면 월드를 되돌립니다.
    enum class Placement { World = 0, FollowHead = 1 };
    Placement   placement      = Placement::World;
    float       followDistance = 1.0f;
    float       followHeight   = -0.15f;

    static constexpr float kDefaultPixelsPerMeter = 1250.f;
    static constexpr float kMinPixelsPerMeter     = 100.f;
    static constexpr float kMaxPixelsPerMeter     = 4000.f;
    static constexpr float kMinSize               = 0.01f;   // m
    static constexpr float kMaxSize               = 100.f;
    static constexpr float kThickness             = 0.01f;   // 에디터 선택 판정용 두께(m)
    static constexpr uint32_t kMaxTextureSide     = 4096;    // UIPanelPass::kMaxSide 와 같음
    static constexpr float kMinBrightness         = 0.05f;
    static constexpr float kMinFollowDistance     = 0.3f;
    static constexpr float kMaxFollowDistance     = 5.f;
    static constexpr float kMaxFollowHeight       = 1.f;     // ±
    static constexpr float kFollowStartAngleDeg   = 30.f;
    static constexpr float kFollowStartOffset     = 0.35f;   // m
    static constexpr float kFollowRate            = 5.f;     // 1/s(지수 접근)

    // width · height · pixelsPerMeter 로 정한 텍스처 크기(텍셀, [1, kMaxTextureSide]).
    static glm::uvec2 computeTextureSize(float width, float height, float pixelsPerMeter);
    // 지금 쓰는 텍스처 크기(마지막으로 확정한 값). 렌더러가 이 크기로 텍스처를 만듭니다.
    glm::uvec2 textureSize() const { return texSize_; }
    // 지금 값으로 텍스처 크기를 다시 정하고 다시 그리게 합니다. 코드에서 크기를 바꿨으면 부릅니다.
    void applyTextureSize();

    // 위젯 트리 문맥. 처음 부를 때(또는 content 가 바뀐 뒤) content 로 트리를 만듭니다.
    HSlateContext& slate();
    void markDirty() { dirty_ = true; }
    bool takeDirty() { const bool d = dirty_; dirty_ = false; return d; }   // 렌더러 전용

    // 배치 배율: 내용은 기준 해상도(kReferencePixelsPerMeter = 1,250 px/m)의 픽셀로 짜고, 팩토리가 크기 · 여백 · 글자에
    // 이 배율을 곱해 트리를 만듭니다(에디터 uiScale 과 같은 방식). 그래서 pixelsPerMeter 를 바꾸면 선명도만 바뀌고 배치는
    // 미터 기준으로 같습니다. 보통 pixelsPerMeter ÷ 1,250 이고, 텍스처 한 변이 상한에 잘렸으면 그 변에 맞게 줄입니다.
    // 배치 픽셀 = 텍스처 픽셀이라 입력 좌표는 UV × textureSize 입니다.
    static constexpr float kReferencePixelsPerMeter = 1250.f;
    static float computeLayoutScale(float width, float height, float pixelsPerMeter);
    float layoutScale() const { return layoutScale_; }   // 마지막으로 확정한 값(textureSize 와 같이 바뀜)

    // 내용 등록. 이름은 씬 파일에 저장되므로 한 번 정하면 바꾸지 않습니다. 같은 이름이면 덮어씁니다.
    // 팩토리는 패널(값 읽기 · 콜백 연결용)과 배치 배율을 받습니다. 배율이 바뀌면 트리를 다시 만듭니다.
    using ContentFactory = std::function<std::shared_ptr<HWidget>(UIPanelComponent& panel, float scale)>;
    static void                     registerContent(const std::string& name, ContentFactory fn);
    static std::vector<std::string> contentNames();

    // ── 게임 API (3D UI 구간 4) ─────────────────────────────────────────────
    // 패널별 값: 같은 내용(content)을 쓰는 패널마다 다른 글자 · 숫자를 넣습니다. 씬 파일에 `param.이름=값` 으로 저장되고,
    // 게임 코드가 실행 중에 바꿔도 됩니다. 팩토리는 param · paramAttr 로 읽습니다. 값이 바뀌어 그리기 목록이 달라지면
    // 렌더러가 알아서 다시 그립니다(markDirty 불필요). 이름은 영문자 · 숫자 · '_' · '-' · '.' 만 씁니다.
    static bool        isValidParamName(const std::string& name);
    bool               setParam(const std::string& name, std::string value);   // 이름이 틀리면 false
    void               clearParam(const std::string& name) { params_.erase(name); }
    std::string        param(const std::string& name, const std::string& fallback = {}) const;
    float              paramFloat(const std::string& name, float fallback) const;
    const std::map<std::string, std::string>& params() const { return params_; }
    // 위젯 Attr 바인딩용: 그릴 때마다 지금 값을 읽습니다. 패널이 트리를 소유하므로 패널보다 오래 살지 않습니다.
    Attr<std::string>  paramAttr(const std::string& name, std::string fallback = {});

    // 동작: 팩토리가 버튼 등에 actionCallback("이름") 을 연결하고, 게임 코드는 setActionHandler 로 받습니다(내용과
    // 게임 논리를 나눔). 처리기가 없으면 아무 일도 하지 않습니다.
    using ActionHandler = std::function<void(UIPanelComponent& panel, const std::string& action)>;
    void                  setActionHandler(ActionHandler fn) { actionHandler_ = std::move(fn); }
    void                  fireAction(const std::string& action);
    std::function<void()> actionCallback(std::string action);

    // ── 따라오기 (구간 7) ───────────────────────────────────────────────────
    struct Pose { glm::vec3 position{ 0.f }; glm::quat rotation{ 1.f, 0.f, 0.f, 0.f }; };
    // 머리 자세(월드)에서 목표 자세. 머리가 거의 바로 위 · 아래를 보면 수평 앞쪽이 없으므로 fallbackForward(수평)를 씁니다.
    static Pose followTarget(const glm::vec3& headPos, const glm::quat& headRot, float distance, float height,
                             const glm::vec3& fallbackForward);
    // Play 중 엔진이 프레임마다 부릅니다(placement 가 FollowHead 일 때). 처음 부르면 목표로 바로 옮기고, 그 뒤로는 위 규칙대로
    // 천천히 따라갑니다. 액터의 월드 자세를 바꿉니다(부모가 있어도 월드 기준). 움직이는 중이면 true.
    bool stepFollow(const glm::vec3& headPos, const glm::quat& headRot, float dt);
    bool followMoving() const { return followMoving_; }

    bool localBounds(glm::vec3& outMin, glm::vec3& outMax) const override;
    bool blocksFloor() const override { return false; }   // 눕힌 패널도 밟는 면이 아님(XR 리그 바닥 찾기)

    void onAttach() override { applyTextureSize(); }
    void onPropsCommitted() override;
    void onFieldsLoaded(const ComponentLoadContext& ctx) override;

    const char*           typeName() const override { return "UIPanelComponent"; }
    void                  writeFields(ComponentFieldWriter& w) const override;
    bool                  readField(const std::string& key, const std::string& value) override;
    std::vector<PropDesc> buildProps() override;
    std::string           readOnlyInfo() const override;

    // ── 렌더러 전용(직렬화하지 않음) ───────────────────────────────────────
    uint32_t   gpuTarget = 0;      // UIPanelPass 핸들
    glm::uvec2 gpuSize{ 0, 0 };    // 그 텍스처의 크기

private:
    HSlateContext slate_;
    std::string   builtContent_;   // 지금 트리를 만든 content(다르면 다시 만듦)
    float         builtScale_  = 0.f;   // 지금 트리를 만든 배율(다르면 다시 만듦)
    float         layoutScale_ = 1.f;
    bool          built_   = false;
    bool          dirty_   = true;
    glm::uvec2    texSize_{ 1000, 625 };
    std::map<std::string, std::string> params_;   // 이름 순(저장 순서가 늘 같음)
    ActionHandler actionHandler_;
    bool          followStarted_ = false;
    bool          followMoving_  = false;
};
