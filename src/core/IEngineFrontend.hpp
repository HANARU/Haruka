#pragma once
#include <memory>
#include <string>

class Renderer;
class World;
class AssetManager;
class IWindow;
class AudioSystem;

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
        // 에셋 루트로 쓸 프로젝트 폴더(그 아래 assets/). 비어 있으면 실행 파일 옆의 assets/ 를 씁니다.
        std::string projectDir;
        // 명령줄에 --xr · --no-xr 이 없을 때 프로젝트 설정 xr.startInVr 로 VR 시작을 정함(게임). 거짓이면 --xr 로만 켬(에디터).
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
    virtual void init(Renderer& renderer, World& world, AssetManager& assets, IWindow& window) = 0;
    virtual void setAudio(AudioSystem* /*audio*/) {}
    // 처음 열 씬. 에디터는 마지막으로 연 씬을 먼저 열고, 게임은 프로젝트 설정의 시작 씬을 엽니다.
    virtual void openInitialScene(const std::string& startScene) = 0;
    virtual void cleanup() = 0;

    // ── 상태 ────────────────────────────────────────────────────────────────────
    virtual bool isPlaying() const = 0;   // 월드 · 게임 모듈 · 동적 모듈이 tick 함
    virtual bool isPaused()  const = 0;   // Play 중이지만 멈춤(isPlaying 은 거짓)
    // 창을 닫아도 되는지. 거짓이면 앞단이 확인을 받은 뒤 창을 다시 닫습니다.
    virtual bool requestClose() = 0;

    // ── 프레임 ──────────────────────────────────────────────────────────────────
    virtual void tick(float dt) = 0;
    virtual void handleInput(float mx, float my, bool ldown, bool lup, float scroll, float winW, float winH) = 0;
    // (mx, my) 가 3D 화면 안인지. 오른쪽 버튼 끌기(카메라 · 시선)를 이 안에서만 시작합니다.
    virtual bool inViewport(float mx, float my, float winW, float winH) const = 0;
    // 텍스트 입력이 키보드를 쥐고 있으면 게임 입력이 키를 무시합니다(DEF-67).
    virtual bool isTextInputActive() const = 0;
    virtual std::string windowTitle() const = 0;   // 창 제목 앞부분(엔진이 FPS 를 덧붙임)
    // XR 세션 동안 소리를 헤드셋 장치로 바꿀지(2026-10-16).
    virtual bool headsetAudioInVr() const = 0;
};

// 실행 파일마다 하나씩 정의합니다(에디터: editor/EditorFrontend.cpp, 게임: game/GameFrontend.cpp).
std::unique_ptr<IEngineFrontend> createEngineFrontend();
