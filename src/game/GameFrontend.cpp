// 게임 실행 파일(HarukaGame)의 엔진 앞단(2026-10-19, 런타임 분리 DEF-12). 에디터 실행 파일은 이 파일 대신
// editor/EditorFrontend.cpp 를 링크합니다.
//
// 시작 씬을 열고 처음부터 Play 상태로 돕니다. 에디터의 Play 와 같은 경로(World::setPlaying, 엔진의 BeginPlay 통지)를
// 쓰므로 게임 코드는 두 실행 형태를 구별하지 않습니다. 실행 규칙은 2026-10-19 사용자 결정입니다(policy()).
#include "../core/IEngineFrontend.hpp"
#include "../core/World.hpp"
#include "../core/Actor.hpp"
#include "../core/HLog.hpp"
#include "../core/ProjectSettings.hpp"
#include "../vfs/Vfs.hpp"
#include "../components/CameraComponent.hpp"
#include "../assets/SceneSerializer.hpp"
#include "../input/HInputSystem.hpp"
#include "../platform/IWindow.hpp"

#include <filesystem>
#include <vector>

static constinit HLogCategory LogGame{ "Game", ELogVerbosity::Log };

namespace {

class GameFrontend final : public IEngineFrontend {
public:
    Policy policy() const override {
        Policy p;
        p.startXrFromProject = true;   // VR 시작은 project.ini 의 xr.startInVr, 명령줄 --xr · --no-xr 이 덮어씀
        p.pauseOnXrFocusLoss = true;   // 헤드셋을 벗거나 시스템 메뉴를 열면 일시정지
        p.quitOnXrExit       = true;   // 대시보드에서 종료하면 앱도 끝남
        p.retryXrSession     = true;   // 헤드셋 연결이 끊겼다 돌아오면 다시 엶
        p.userDataFolder     = true;   // 설정 · 로그 · 캐시는 %LOCALAPPDATA%
        p.mountGamePacks     = true;   // 셰이더 · 에셋은 실행 폴더의 game.hpak(있으면)
        p.allowHeadless      = true;   // --headless 로 창 없이(자동 확인 · Quest 와 같은 경로)
        return p;
    }

    void init(Renderer& /*renderer*/, World& world, AssetManager& assets, IWindow& window) override {
        world_  = &world;
        assets_ = &assets;
        window_ = &window;
    }

    void openInitialScene(const std::string& startScene) override {
        std::string text;
        if (project_settings::isSceneLogicalPath(startScene) && hvfs::readText(startScene, text)) {
            SceneSerializer::readScene(text, *world_, *assets_);
            HLOG(LogGame, Log) << "Scene opened: " << startScene << " (" << world_->getActors().size() << " actors)";
        } else {
            HLOG(LogGame, Error) << "Start scene not found: " << startScene << " (set startScene in "
                                 << ProjectSettings::kPath << "); starting empty";
        }
        sceneName_ = std::filesystem::path(startScene).stem().string();
        ensureCamera();
        world_->setPlaying(true);
    }

    void cleanup() override {}

    bool isPlaying() const override { return true; }    // 일시정지(헤드셋 초점)는 엔진이 정책으로 겹칩니다
    bool isPaused()  const override { return false; }
    bool requestClose() override { return true; }       // 즉시 종료(2026-10-19 결정)

    void tick(float /*dt*/) override {
        // 오른쪽 버튼을 누른 채 끈 양을 게임의 시선 축(LookX · LookY)으로 넘깁니다. 에디터 Play 와 같은 조작입니다.
        float rx = 0.f, ry = 0.f, lx = 0.f, ly = 0.f;
        window_->getRightMouseDelta(rx, ry);
        window_->getMouseDelta(lx, ly);   // 쌓이지 않게 비움(DEF-79)
        HInputSystem::get().setLookDelta(rx, ry);
        // 받는 곳이 없는 문자 입력은 버립니다. 쌓아 두면 나중에 입력창이 생겼을 때 한꺼번에 쏟아집니다.
        std::vector<unsigned int> junk;
        window_->drainCharQueue(junk);
    }

    void handleInput(float, float, bool, bool, float, float, float) override {}
    bool inViewport(float, float, float, float) const override { return true; }   // 창 전체가 게임 화면
    bool isTextInputActive() const override { return false; }
    std::string windowTitle() const override { return "Haruka  |  " + sceneName_; }
    bool headsetAudioInVr() const override { return true; }   // 장치 설정 화면은 나중(2026-10-19 결정)

private:
    // 씬에 켜진 primary 카메라가 없으면 기본 카메라를 놓습니다(UE 가 플레이어 없이 시작하면 기본 폰을 놓는 것과 같은
    // 자리). 위치와 각도는 에디터가 처음 여는 시점과 같아, 에디터에서 본 첫 화면이 그대로 보입니다.
    void ensureCamera() {
        if (world_->getActiveCamera()) return;
        Actor* a = world_->spawnActor("Default Camera");
        a->setLocalPosition({ 0.f, 0.3f, 2.5f });
        a->setLocalRotation(CameraComponent::yawPitchRotation(0.f, 0.15f));
        a->addComponent<CameraComponent>();
        HLOG(LogGame, Warning) << "No camera in the scene; using a default camera at (0, 0.3, 2.5)";
    }

    World*        world_  = nullptr;
    AssetManager* assets_ = nullptr;
    IWindow*      window_ = nullptr;
    std::string   sceneName_;
};

} // namespace

std::unique_ptr<IEngineFrontend> createEngineFrontend() {
    return std::make_unique<GameFrontend>();
}
