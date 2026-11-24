#pragma once
#include "XrRunMode.hpp"
#include <memory>
#include <string>
#include <vector>
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

class Renderer;
class World;
class AssetManager;
class IWindow;
class AudioSystem;
struct EngineOptions;
struct ProjectSettings;

// ── 엔진 앞단 (2026-10-19, 런타임 분리 DEF-12) ─────────────────────────────────────
// 엔진(Engine)은 에디터를 직접 알지 않고 이 인터페이스로만 묻습니다. 실행 파일마다 앞단 하나를 링크합니다.
//   에디터 실행 파일(HarukaEngine): EditorApp. Play 버튼으로 시작 · 멈춤, 저장하지 않은 씬이 있으면 닫기를 미룸.
//   게임 실행 파일(HarukaGame):     GameFrontend(game/GameFrontend.cpp). 시작 씬을 열고 처음부터 Play 상태로 돕니다.
// UE 가 타깃마다 엔진 클래스(UEditorEngine · UGameEngine)를 고르는 것과 같은 자리입니다. 엔진 쪽 코드는 HarukaRuntime
// 라이브러리로 한 번만 빌드되므로, 앞단의 차이는 #ifdef 가 아니라 이 인터페이스와 아래 정책으로만 나뉩니다.
class IEngineFrontend {
public:
    // 실행 형태마다 다른 규칙(2026-10-19 사용자 결정). 엔진이 부팅할 때 한 번 읽습니다.
    struct Policy {
        // 에셋 루트로 쓸 프로젝트 폴더(그 아래 Content/). 비어 있으면 게임은 실행 파일 옆을 씁니다.
        std::string projectDir;
        // /Engine/ 으로 마운트할 엔진 콘텐츠 폴더(D3, 2026-11-19, <엔진>/Engine/Content). 게임은 없으면 실행 폴더의
        // Engine/Content 를 찾습니다.
        std::string engineContentDir;
        // 에디터(E5): true 이면 프로젝트와 엔진 콘텐츠를 반드시 찾아야 하며, 못 찾으면 실행 폴더 복사본으로 넘어가지 않고 멈춥니다.
        bool requireProject       = false;
        bool requireEngineContent = false;
        std::string projectResolveError;
        std::string engineResolveError;
        std::vector<std::string> startupLogs;
        // 명령줄에 --xr · --no-xr 이 없을 때 프로젝트 설정 xr.startInVr 로 VR 자동 감지를 켬(에디터 · 게임).
        bool startXrFromProject = false;
        // 헤드셋이 입력 초점을 잃으면(벗음 · 시스템 메뉴) 게임을 일시정지(Meta VRC.Quest.Functional.2).
        bool pauseOnXrFocusLoss = false;
        // 런타임이 앱을 끝내라고 하면(EXITING, 대시보드에서 종료 등) 엔진을 끝냄. 거짓이면 데스크톱으로 계속.
        bool quitOnXrExit       = false;
        // 헤드셋 연결이 끊겼다가(LOSS_PENDING) 돌아오면 세션을 다시 엶. 거짓이면 다음 Play 에서 엶.
        bool retryXrSession     = false;
        // 설정 · 로그 · 캐시를 사용자 폴더(Windows %LOCALAPPDATA%)에 씀. 거짓이면 실행 폴더에 씀(에디터).
        bool userDataFolder     = false;
        // 실행 폴더의 묶음 파일(game.hpak · dlc_*.hpak · game_p<N>.hpak)을 낱개 파일 위에 마운트(게임, 2026-10-24).
        bool mountGamePacks     = false;
        // --headless 로 OS 창 없이 돌 수 있음(게임, 2026-10-31). 에디터는 창이 0x0 이면 Play 를 누를 수 없어 거짓입니다.
        bool allowHeadless      = false;
    };
    virtual ~IEngineFrontend() = default;
    virtual Policy policy() const = 0;

    // ── 부팅 · 종료 ─────────────────────────────────────────────────────────────
    virtual void resolveRuntimePaths(const std::string& /*cliProjectArg*/) {}
    virtual void setOptions(const EngineOptions& /*options*/) {}
    virtual void init(Renderer& renderer, World& world, AssetManager& assets, IWindow& window) = 0;
    virtual void setAudio(AudioSystem* /*audio*/) {}
    // 처음 열 씬. 에디터는 마지막으로 연 씬을 먼저 열고, 게임은 프로젝트 설정의 시작 씬을 엽니다.
    virtual void openInitialScene(const std::string& startScene) = 0;
    // 실행 중 씬 전환(비동기 로딩 D6). 지원하지 않으면 false. sync 면 미리 읽기 없이 바로 바꿉니다(비교 확인용).
    virtual bool requestScene(const std::string& /*logicalPath*/, bool /*sync*/) { return false; }
    // 매 프레임 월드 틱 전에 부릅니다(창이 최소화돼도, 창 없는 실행에서도). 씬 전환을 바꾸는 자리입니다.
    virtual void updateSceneTransition() {}
    virtual void onRunFinished() {}
    virtual void cleanup() = 0;

    // ── 상태 ────────────────────────────────────────────────────────────────────
    virtual bool isPlaying() const = 0;   // 월드 · 게임 모듈 · 동적 모듈이 tick 함
    virtual bool isPaused()  const = 0;   // Play 중이지만 멈춤(isPlaying 은 거짓)
    // 창을 닫아도 되는지. 거짓이면 앞단이 확인을 받은 뒤 창을 다시 닫습니다.
    virtual bool requestClose() = 0;

    // ── 프레임 ──────────────────────────────────────────────────────────────────
    virtual void tick(float dt) = 0;
    // 메인 창이 최소화된 프레임에 tick · handleInput 대신 부릅니다(DEF-106 규칙: 0 크기 배치를 하지 않음). 에디터는 떠 있는 창의 OS 닫기
    // 단추 · 생성 · 파괴만 처리합니다(다중 OS 창 M3a). 기본은 아무것도 하지 않습니다(게임 실행 파일).
    virtual void tickWhileMinimized(float /*dt*/) {}
    virtual void handleInput(float mx, float my, bool ldown, bool lup, float scroll, float winW, float winH) = 0;
    // (mx, my) 가 3D 화면 안인지. 오른쪽 버튼 끌기(카메라 · 시선)를 이 안에서만 시작합니다.
    virtual bool inViewport(float mx, float my, float winW, float winH) const = 0;
    // 텍스트 입력이 키보드를 쥐고 있으면 게임 입력이 키를 무시합니다(DEF-67).
    virtual bool isTextInputActive() const = 0;
    // 게임 입력(키 · 마우스 시선 · 월드 패널 마우스)을 막는지(결정 D7, 다중 OS 창 M3b). 에디터는 Play 중 떠 있는 창에 OS 포커스가
    // 있으면 참입니다(UE PIE 와 같음). 패드 · XR 컨트롤러는 막지 않습니다. 기본은 거짓(게임 실행 파일).
    virtual bool blocksGameInput() const { return false; }
    virtual std::string windowTitle() const = 0;   // 창 제목 앞부분(엔진이 FPS 를 덧붙임)
    // XR 세션 동안 소리를 헤드셋 장치로 바꿀지(2026-10-16).
    virtual bool headsetAudioInVr() const = 0;
    // 이번 Play 에서 XR 세션을 열지(에디터 Play on Desktop 이면 false).
    virtual bool wantsXrForPlay() const { return true; }
    // 엔진이 시작할 때 · 헤드셋 연결 상태가 바뀔 때 앞단에 알립니다.
    virtual void setXrRunStatus(const hxrmode::XrRunStatus& /*status*/) {}
    // 시작 때 읽은 프로젝트 설정을 앞단에 알립니다(다시 시작 띠 기준값 · 바로 적용 초기값).
    virtual void setInitialProjectSettings(const ProjectSettings& /*ps*/) {}
    // 실행 중 바뀐 바로 적용 항목(render.ao · render.bloom · xr.height · xr.eyeHeight)을 엔진에 넘깁니다.
    virtual bool consumeLiveProjectSettings(ProjectSettings& /*out*/) { return false; }

    // ── 월드 UI 패널 포인터 (3D UI 구간 5) ─────────────────────────────────────
    // Play 중 마우스 (mx, my) 를 지나는 게임 화면의 월드 광선. 마우스가 게임 화면 밖이거나 게임 카메라가 보이지 않으면
    // false 입니다(에디터 카메라로 보는 중 등). 엔진이 이 광선으로 월드 패널을 가리키고 누릅니다.
    virtual bool pointerRay(float /*mx*/, float /*my*/, float /*winW*/, float /*winH*/,
                            glm::vec3& /*origin*/, glm::vec3& /*dir*/) const { return false; }
    // 마우스가 월드 패널을 가리키거나 누르는 중인지 엔진이 프레임마다 알립니다. 참이면 앞단은 같은 클릭으로 액터를
    // 고르지 않습니다(에디터 Play 중 패널 버튼을 누르면 선택이 바뀌던 것).
    virtual void setPointerOverWorldUi(bool /*over*/) {}
};

// 프로젝트별 경로 등 실행 파일(main.cpp)이 앞단 라이브러리에 넘기는 설정(E1a, 2026-11-23).
// 라이브러리는 HARUKA_PROJECT_DIR 같은 프로젝트 컴파일 정의를 직접 보지 않습니다.
struct EngineFrontendConfig {
    std::string projectDir;
    std::string engineContentDir;
};

// 실행 파일마다 하나씩 정의합니다(에디터: editor/EditorFrontend.cpp, 게임: game/GameFrontend.cpp).
std::unique_ptr<IEngineFrontend> createEngineFrontend(const EngineFrontendConfig& config = {});
