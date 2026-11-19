#pragma once
#include "../ui/slate/HSlate.hpp"
#include <chrono>
#include "../core/Transform.hpp"
#include "EditorContext.hpp"
#include "EditorUiKit.hpp"
#include "EditorIcons.hpp"
#include "EditorDock.hpp"
#include "EditorMenus.hpp"
#include "EditorChromePanels.hpp"
#include "ViewportToolbar.hpp"
#include "EditorViewportController.hpp"
#include "TextInputController.hpp"
#include "OutlinerPanel.hpp"
#include "DetailsPanel.hpp"
#include "ContentBrowserPanel.hpp"
#include "GpuStatsPanel.hpp"
#include "OutputLogPanel.hpp"
#include "../core/HLog.hpp"
#include "HCommandStack.hpp"
#include "SceneDocument.hpp"
#include "EditTools.hpp"
#include "HotReloader.hpp"
#include "../core/HDelegate.hpp"
#include "../runtime/IEngineFrontend.hpp"
#include "ComponentVisualizers.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <set>
#include <vector>
#include <string>
#include <unordered_set>

class Renderer;
class World;
class AssetManager;
class IWindow;
class Actor;
class CameraComponent;
class AudioSystem;
enum class EHrtfMode : uint8_t;   // audio/AudioSystem.hpp

// 에디터 실행 파일(HarukaEngine)의 엔진 앞단입니다(2026-10-19, core/IEngineFrontend.hpp). 정책 · 생성 함수는
// editor/EditorFrontend.cpp 에 있습니다.
class EditorApp final : public IEngineFrontend {
public:
    // Base panel dimensions at 1080p reference (scaled at runtime by uiScale_)
    static constexpr float kOutlinerW = 290.f;
    static constexpr float kDetailsW  = 330.f;
    static constexpr float kToolbarH  = EditorTheme::toolbarH;
    static constexpr float kBottomH   = 260.f;

    enum class EPlayState { Editor, Playing, Paused };
    EPlayState getPlayState() const { return playState_; }
    bool isPlaying()  const override { return playState_ == EPlayState::Playing; }
    bool isPaused()   const override { return playState_ == EPlayState::Paused;  }
    // 텍스트 입력(리네임·검색·숫자 칸)이 키보드를 쥐고 있는지. 게임 입력계가 이 동안 키를
    // 무시하는 데 씁니다(DEF-67).
    bool isTextInputActive() const override { return currentFocus() != EFocusTarget::None; }
    bool isInEditor() const { return playState_ == EPlayState::Editor;  }

    void beginPlay();
    void pausePlay();
    void stopPlay();

    // 에디터의 실행 규칙(editor/EditorFrontend.cpp): 프로젝트 폴더를 직접 읽고 쓰고, VR 은 --xr 로만, Play 동안만.
    Policy policy() const override;
    void setOptions(const EngineOptions& options) override;
    void init(Renderer& renderer, World& world, AssetManager& assets, IWindow& window) override;
    void onRunFinished() override;
    void cleanup() override;
    void tick(float dt) override;
    void handleInput(float mx, float my, bool ldown, bool lup,
                     float scroll, float winW, float winH) override;

    // Returns true when (mx, my) falls inside the 3D viewport
    bool inViewport(float mx, float my, float winW, float winH) const override;

    // ── 씬 문서 (2026-10-15) ──────────────────────────────────────────────────
    // 엔진이 시작할 때 부릅니다. 마지막으로 연 씬(Config/editor_state.txt)이 있으면 그것을, 없으면
    // 프로젝트 설정의 시작 씬을 엽니다.
    void openInitialScene(const std::string& startScene) override;
    // 창 닫기 요청을 받으면 엔진이 부릅니다. 저장하지 않은 변경이 있으면 확인 창을 열고 false 를
    // 돌려줍니다. 사용자가 저장 · 버리기를 고르면 에디터가 창을 다시 닫습니다.
    bool requestClose() override;
    // 제목 표시줄용 이름("LightingProbe.hscene*" — 별표는 저장하지 않은 변경).
    std::string sceneTitle() const;
    std::string windowTitle() const override { return "Haruka Engine  |  " + sceneTitle(); }

    // ── 소리 출력 장치 (2026-10-16, EditorApp_scene.cpp) ───────────────────────
    // 에디터에서 소리를 낼 장치는 이 PC 에 속한 설정이라 Config/editor_state.txt 에 둡니다(UE 의 에디터 Audio 설정과 같은
    // 성격, 프로젝트 설정이 아님). 엔진이 openInitialScene 전에 오디오 시스템을 넘겨 주면, 저장된 장치를 바로 적용합니다.
    void setAudio(AudioSystem* audio) override { audio_ = audio; }
    // XR Play 동안 소리를 헤드셋 장치로 바꿀지(기본 켜짐). 엔진이 세션을 열고 닫을 때 묻습니다.
    bool headsetAudioInVr() const override { return headsetAudioInVr_; }

    // 월드 UI 패널 포인터(3D UI 구간 5): Play 중 뷰포트가 게임 카메라를 보일 때만 광선을 줍니다.
    bool pointerRay(float mx, float my, float winW, float winH, glm::vec3& origin, glm::vec3& dir) const override;
    void setPointerOverWorldUi(bool over) override { pointerOverWorldUi_ = over; }

private:
    bool pointerOverWorldUi_ = false;   // 엔진이 알림: 마우스가 월드 패널 위(같은 클릭으로 액터를 고르지 않음)
    // UI scale factor: 1.0 at 1080p, proportional to framebuffer height
    float uiScale_ = 1.f;
    float s(float x) const { return x * uiScale_; }
    EditorUiKit         ui_;
    EditorDock          dock_;
    EditorMenus         menus_;
    EditorToolbar       toolbar_;
    ViewportToolbar     vpToolbar_;
    EditorViewportController viewport_;
    StatusBarPanel      statusBar_;
    TextInputController textInput_;
    OutlinerPanel       outlinerPanel_;
    DetailsPanel        detailsPanel_;
    ContentBrowserPanel contentPanel_;
    GpuStatsPanel       gpuStatsPanel_;
    OutputLogPanel      logPanel_;

    EditorContext  ctx_;
    EditorIcons    icons_;            // 글꼴 아틀라스가 가리키므로 slateRenderer_ 보다 먼저 선언(나중에 사라짐, D3)
    HSlateRenderer slateRenderer_;
    HCommandStack  cmdStack_;
    HotReloader    hotReloader_;

    // onActorDestroyed 구독 핸들. init 에서 붙이고 cleanup 에서 뗍니다. 이 구독이
    // 파괴되는 액터를 참조하는 커맨드 제거와 선택 포인터 무효화를 한곳에서 담당합니다.
    HEvent<Actor*>::Handle actorDestroyedSub_ = 0;

    // 컴포넌트 형별 아이콘 · 범위 표시(2026-11-18 S4, 예전 Component::editorIcon · drawEditorVisual).
    ComponentVisualizers visualizers_;

    void initPanels();
    void subscribeWorldEvents(World& world);
    void tickStats();
    void tickSelectionSync(int actorCount);

    // Dynamic panel refs
    std::shared_ptr<SVerticalBox>   logList_;
    std::shared_ptr<SVerticalBox>   contentList_;
    std::shared_ptr<SVerticalBox>   bottomContent_;
    // 스크롤 상자(DEF-56·57). 내용을 다시 만들어도 상자는 남아 스크롤 위치를 지킵니다.
    std::shared_ptr<SScrollBox>     bottomScroll_;      // Content Browser · GPU Stats
    std::shared_ptr<SBorder>        bottomHost_;        // 탭에 따라 bottomScroll_ 또는 로그를 담음
    int                             bottomScrollTab_ = -1;   // 스크롤 위치가 속한 탭

    // ── Panel sizes (viewportFbRect 프레임0 fallback + 기본 비율 시드로만 사용) ──
    // 실제 레이아웃은 도크 트리 비율(ratios)이 구동합니다. 이 값들은 첫 배치 전 프레임의
    // 뷰포트 rect 추정과 기본 도크 비율의 시드로만 쓰입니다.
    float outlinerW_ = kOutlinerW;
    float detailsW_  = kDetailsW;
    float bottomH_   = kBottomH;

    // ── 도킹 (Phase 0) ───────────────────────────────────────────────────────
    PanelRegistry             panelReg_;              // id → {제목, 위젯}

    // ── 상단 메뉴 바 (Phase 1) ────────────────────────────────────────────────
    static constexpr float kMenuBarH   = EditorTheme::menuBarH;
    static constexpr float kStatusBarH = EditorTheme::rowH;
    void buildMenus();
    bool bottomDirty_    = false;   // 하단 패널(콘텐츠 브라우저) 지연 리빌드
    void flushChromeRebuilds();

    // Gizmo mode & space
    int  gizmoMode_  = 1;   // 0=select, 1=move, 2=rotate, 3=scale
    bool worldSpace_ = true;

    // ── 뷰포트 툴바 (Phase 2) ────────────────────────────────────────────────
    // 뷰포트 오버레이 상단에 떠 있는 툴바입니다. 변환 도구·좌표계는 메인 툴바에서
    // 여기로 이관했고, 카메라 속도가 새로 붙습니다. 툴바가 차지한 rect 는 inViewport
    // 에서 배제해, 그 위 클릭이 픽·오빗·기즈모·RMB 카메라 모드로 새지 않게 합니다.
    static constexpr float kVpToolbarInset = 6.f;   // 뷰포트 가장자리 인셋(미스케일)
    float camSpeed_          = 5.f;    // 현행 하드코딩과 같은 기본값(m/s)
    int   vpToolbarLevel_    = -1;     // 폭 사다리 레벨(-1 = 미초기화)
    // 단계별로 툴바가 실제로 요구하는 폭(스케일 픽셀, 인셋 포함). 라벨이 가장 길어지는 값으로
    // 세운 툴바를 재서 얻습니다(DEF-60). 잰 배율·폰트가 지금과 다르면 tick 이 다시 잽니다.
    float vpLevelNeed_[5]    = {};
    float vpLevelNeedScale_  = 0.f;
    const HFontAtlas* vpLevelNeedFont_ = nullptr;
    bool  vpToolbarDirty_    = true;   // 지연 리빌드 요청
    // 기즈모 스냅. 소유자는 여기이고, 기즈모에는 매 프레임 사본을 밀어 넣습니다.
    HGizmoSnap snap_{};

    // Bottom panel tabs
    int bottomTab_ = 1;     // 0=content, 1=log, 2=gpu stats

    EFocusTarget currentFocus() const { return textInput_.currentFocus(); }
    void         setTextFocus(EFocusTarget t) { textInput_.setFocus(t); }
    bool         focusWidgetContains(float x, float y) const { return textInput_.focusWidgetContains(x, y); }

    bool        outlinerVisible_ = true;
    // 직전 선택(= 지금 선택의 하나 앞). Actor 메뉴의 부착 액션이 부모로 씁니다.
    Actor*      prevSelected2_ = nullptr;

    // Viewport overlay stats
    using VpTimings = GpuStatsSnapshot;
    VpTimings vpTimings_;
    // FPS 는 실시간으로 셉니다. tick(dt) 의 dt 는 main.cpp 에서 0.05 로 클램프된
    // 값이라, 그것으로 세면 제목 표시줄(실시간 기준)과 다른 숫자가 나옵니다.
    // 실제로 83 과 91 이 동시에 표시되던 원인의 일부입니다.
    std::chrono::steady_clock::time_point fpsMark_  = std::chrono::steady_clock::now();
    int       fpsCount_ = 0;
    uint64_t  prevRecreate_ = 0;

    bool      prevLeftDown_   = false;
    Actor*    prevSelected_   = nullptr;
    uint64_t  prevSelSig_     = 0;   // 앞 프레임 선택 집합의 서명(DEF-135)
    int       prevActorCount_ = -1;
    glm::vec3 cachedEuler_    {0.f, 0.f, 0.f};
    Transform undoSnapshot_;

    EPlayState  playState_     = EPlayState::Editor;
    std::string worldSnapshot_;

    // ── Key edge tracking ─────────────────────────────────────────────────────
    bool prevZDown_         = false;
    bool prevYDown_         = false;
    bool prevSDown_         = false;
    bool prevFDown_         = false;
    bool prevODown_         = false;
    bool prevNDown_         = false;

    // ── Widget helpers (EditorUiKit 으로 위임) ──────────────────────────────────
    std::shared_ptr<HWidget> spacer() const { return ui_.spacer(); }
    std::shared_ptr<HWidget> panelHeader(const std::string& title) const { return ui_.panelHeader(title); }
    std::shared_ptr<HWidget> sectionHeader(const std::string& title,
                                           glm::vec4 color = kTextHdr,
                                           float fontSize = 12.f) const {
        return ui_.sectionHeader(title, color, fontSize);
    }
    std::shared_ptr<SButton> textButton(const std::string& label, float fontSize,
                                        glm::vec4 textColor, HPadding pad,
                                        glm::vec4 normal, glm::vec4 hovered,
                                        std::function<void()> onClick) const {
        return ui_.textButton(label, fontSize, textColor, pad, normal, hovered, std::move(onClick));
    }

    void buildWidgetTree();
    // 폭을 넘는 텍스트를 ".." 로 줄입니다.
    std::string ellipsizeText(const std::string& text, float maxPx, float fontPx) const {
        return ui_.ellipsizeText(text, maxPx, fontPx);
    }
    void rebuildLog();
    void rebuildContent();
    void rebuildBottomPanel();

    void deleteSelectedActor();

    // ── 편집 기본기 (2026-10-15, EditorApp_edit.cpp) ──────────────────────────
    // 선택은 반드시 이 함수들로 바꿉니다. ctx_.selected(주 선택)와 ctx_.selection(전체)을 함께 맞춥니다.
    void selectOnly(Actor* a);       // a 하나만(null 이면 비움)
    void toggleSelect(Actor* a);     // Ctrl+클릭
    void addSelect(Actor* a);        // 되살린 액터 · 붙여넣은 액터
    void selectRange(Actor* a);      // Shift+클릭(아웃라이너에 보이는 행 순서로 주 선택부터 a 까지)
    bool isSelected(const Actor* a) const;
    // 편집 대상 루트: 선택 중 조상이 함께 선택된 것과 에디터 카메라를 뺀 것(선택 순서).
    std::vector<Actor*> selectionRoots() const;
    void deleteSelection();
    // 복사 · 붙여넣기 · 복제. 클립보드는 OS 클립보드에 씬 형식 글로 둡니다(hclip). 붙여넣은 루트는 원래 부모가
    // 있으면 그 아래, 같은 로컬 트랜스폼으로 놓고 이름 끝에 " (n)" 을 붙입니다.
    void copySelection();
    void pasteClipboard();
    void duplicateSelection();
    std::vector<Actor*> spawnActorSet(const std::string& clip);
    // 부모 바꾸기(월드 자세 유지, Undo 가능). newParent 가 null 이면 루트로. 사이클이면 하지 않고 false.
    bool reparentUndoable(Actor* child, Actor* newParent, int index = -1);
    // 선택 대상으로 화면 맞추기(F). 에디터 카메라를 바운드 구가 꽉 차는 거리로 0.25초 동안 옮깁니다.
    void frameSelection();
    // 기즈모로 여러 액터를 함께 움직이기. 드래그를 시작할 때 다른 루트의 월드 자세를 떠 두고, 주 선택이 바뀐 만큼
    // 따라 움직인 뒤(hxform::followPivot), 드래그가 끝나면 모두 한 커맨드로 쌓습니다.
    struct GroupMember { uint64_t id = 0; hxform::WorldPose start; Transform startLocal; };
    std::vector<GroupMember> groupDrag_;
    hxform::WorldPose        groupPivotStart_;
    void beginGroupDrag();
    void updateGroupDrag();
    void commitGizmoDrag(const Transform& before, const Transform& after);
    bool prevDDown_    = false;   // Ctrl+D 엣지
    // 액터 서브트리 생성 · 삭제를 Undo 로 되돌릴 수 있게 합니다(ActorPresenceCmd, 2026-10-15).
    // deleteActorUndoable 은 지우고 커맨드를 쌓고, recordActorCreation 은 이미 만든 루트들을 쌓습니다.
    void deleteActorUndoable(Actor* root);
    void recordActorCreation(const std::vector<Actor*>& roots);
    std::unique_ptr<ICommand> makePresenceCmd(bool create, std::vector<uint64_t> rootIds);
    void tickShortcuts();
    void undo();
    void redo();
    // 기즈모나 숫자 칸을 끄는 중인지. 그동안 저장 · 복제 · 붙여넣기 · 열기 단축키를 막습니다(DEF-132 · 133).
    bool editDragActive() const;
    void syncEulerCache();

    std::string serializeWorld();
    void        deserializeWorld(const std::string& data);

    // ── 씬 파일 (2026-10-15, EditorApp_scene.cpp) ────────────────────────────
    // 저장 · 열기는 프로젝트 폴더의 assets/scenes 아래 논리 경로로 합니다(hproj). Play 중에는 막습니다
    // (Play 월드는 Stop 하면 버려지므로, 그것을 저장하면 편집 상태가 아니라 시뮬레이션 결과가 남음).
    SceneDocument            doc_;
    std::string              lastScene_;           // Config/editor_state.txt 의 lastScene
    bool                     quitApproved_ = false;
    bool sceneDirty() const { return doc_.dirty(cmdStack_.stateToken()); }
    // Undo 이력에 남지 않는 편집이 씬을 바꿨음을 알립니다(되돌릴 수 없는 편집 경로).
    void markSceneDirty() { doc_.markDirty(); }
    void newScene();
    void openSceneDialog();
    void openScene(const std::string& logicalPath);
    void saveScene();                                     // 현재 파일에, 이름이 없으면 다른 이름으로
    void saveSceneAs(std::function<void()> then = {});    // then: 저장에 성공하면 이어서 할 일
    bool writeSceneTo(const std::string& logicalPath);
    // 저장하지 않은 변경이 있으면 저장 · 버리기 · 취소를 묻고, 취소가 아니면 action 을 합니다.
    void guardUnsaved(const HText& verb, std::function<void()> action);
    bool blockedDuringPlay(const char* what) const;
    void loadEditorState();
    void saveEditorState() const;
    // 문화권(글자 체계 T3): 표를 읽고(htext::useCulture) UI 를 다시 만듭니다. save 면 에디터 상태에 남깁니다.
    void applyCulture(const std::string& culture, bool save);
    std::string culture_;            // 에디터 상태의 문화권(비면 en)
    std::string cultureOverride_;    // --culture(저장하지 않음)
    std::string pendingCulture_;     // 메뉴에서 고른 문화권(다음 프레임에 적용)

    // 소리 출력 장치(2026-10-16). audioOutput_ 이 비면 시스템 기본 장치입니다.
    AudioSystem* audio_            = nullptr;
    std::string  audioOutput_;              // Config/editor_state.txt 의 audioOutput
    bool         headsetAudioInVr_ = true;  // Config/editor_state.txt 의 audioHeadsetInVr
    EHrtfMode    hrtfMode_{};               // Config/editor_state.txt 의 audioHrtf(2026-11-06). {} = Auto(0)
    void openAudioOutputDialog();

    // ── 모달 대화상자 (2026-10-15) ───────────────────────────────────────────
    // 화면 전체를 덮는 캐처 위에 가운데 패널을 띄웁니다. 열린 동안에는 다른 패널 · 뷰포트 · 단축키가
    // 입력을 받지 않습니다. 버튼 콜백은 위젯 디스패치 도중이라 바로 실행하지 않고, 프레임 끝
    // (flushChromeRebuilds 의 맨 앞)에서 대화상자를 닫은 뒤 실행합니다. 그 동작이 다른 대화상자를 열 수 있고, 씬을 바꾸면
    // 같은 소진에서 아웃라이너 · 디테일을 다시 만듭니다(DEF-130).
    struct ModalButton {
        std::string           label;
        std::function<void()> action;
        bool                  isDefault = false;   // Enter
        bool                  isCancel  = false;   // Esc
    };
    struct ModalSpec {
        std::string              title;
        std::string              message;
        bool                     hasInput = false;   // 이름 입력 칸(dialogEdit_)
        std::string              inputPlaceholder;
        std::vector<std::string> list;               // 고를 항목(누르면 onPick, 대화상자는 닫힘)
        std::function<void(const std::string&)> onPick;
        std::vector<ModalButton> buttons;
    };
    std::unique_ptr<ModalSpec> modal_;
    std::shared_ptr<SBorder>   modalHost_;
    std::shared_ptr<HWidget>   modalPanel_;
    std::shared_ptr<SSearchBox> dialogBox_;
    std::function<void()>      modalPending_;     // 프레임 끝에 실행할 버튼 · 항목 동작
    bool                       modalDirty_   = false;
    HTextEditState             dialogEdit_;   // 대화상자 이름 칸(글자 체계 T1). 글은 dialogEdit_.text
    bool                       dialogFocused_ = false;
    bool modalOpen() const { return (bool)modal_; }
    void openModal(ModalSpec spec);
    void triggerModal(std::function<void()> action);   // 대화상자를 닫고 action 을 프레임 끝에
    void triggerModalDefault();
    void triggerModalCancel();
    void rebuildModal();
    void flushModal();
    // 떠 있는 대화상자를 지금 취소로 닫습니다(취소 버튼의 동작을 실행). 창 닫기 요청이 닫기와 관계없는 대화상자를 치울 때
    // 씁니다(DEF-137). 위젯 처리 밖에서만 부릅니다.
    void cancelModalNow();
    bool closeFlow_ = false;   // 창 닫기 흐름(Unsaved Changes → Save As → Replace Scene)의 대화상자가 떠 있음

    // ── S6-0 회귀 덤프 상태 ───────────────────────────────────────────────────
    bool        dumpMode_         = false;
    bool        dumpStateApplied_ = false;
    uint32_t    dumpFrameCount_   = 0;
    std::string uiDumpPath_;
    std::string uiSelect_;
    std::string uiOpenMenu_;
    std::string uiBottomTab_;
    bool        uiLogCategories_ = false;
    std::string uiOutlinerSearch_;
    std::string uiRename_;
    std::string uiType_;          // 글자 체계 T1 · T2 덤프
    int         uiCaretLeft_  = 0;
    int         uiSelectLeft_ = 0;
    std::string uiImeCompose_;
    HTextEditState* focusedEdit();   // 포커스된 글 칸의 편집 상태(없으면 nullptr)
    std::string uiOutlinerCollapse_;
    bool        uiPlay_ = false;
    std::string uiLayout_;
    std::string viewportDumpPath_;
    std::string uiGizmoMode_;
    bool        uiLocalSpace_ = false;
    std::string uiPick_;
    std::string uiHover_;
    bool        uiHoverActive_ = false;
    glm::vec2   uiHoverPos_{ 0.f, 0.f };
    std::vector<LineVertex> lastEditorLines_;
    std::vector<LineVertex> lastGizmoTris_;
    void applyDumpState();

    // 에디터 색 토큰(EditorTheme 정본 별칭)
    static constexpr glm::vec4 kBgPanel   = EditorTheme::bgPanel;
    static constexpr glm::vec4 kBgHeader  = EditorTheme::bgHeader;
    static constexpr glm::vec4 kBgToolbar = EditorTheme::bgWindow;
    static constexpr glm::vec4 kBorder    = EditorTheme::lineDivider;
    static constexpr glm::vec4 kTextHdr   = EditorTheme::textPrimary;
    static constexpr glm::vec4 kTextBody  = EditorTheme::textPrimary;
    static constexpr glm::vec4 kTextDim   = EditorTheme::textSecondary;
    static constexpr glm::vec4 kSelected  = EditorTheme::selectRow;
    static constexpr glm::vec4 kAccent    = EditorTheme::accent;
};
