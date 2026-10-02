#include "DefaultGameModule.hpp"
#include "../core/World.hpp"
#include "../assets/SceneSerializer.hpp"
#include "../core/HLog.hpp"

#include <fstream>
#include <iterator>
#include <string>

static constinit HLogCategory LogGame{ "Game", ELogVerbosity::Log };

// ── 시작 씬 ───────────────────────────────────────────────────────────────────
// Primitive_And_Lighting_Design.md 9절의 Lighting_Probe 배치입니다. 예전에는 이 함수가
// 코드로 지었지만, 절차 도형 직렬화(PRIM:)가 붙은 뒤 .hscene 파일로 옮겼습니다(9-4절).
// 이유는 둘입니다. 씬 자체가 직렬화 왕복의 테스트 케이스가 되고, 게임 모듈이 특정
// 디버그 씬에 결합되지 않습니다. 배치를 바꾸려면 에디터에서 고친 뒤 Save 로 만든
// scene.hscene 을 이 파일에 덮어쓰면 됩니다.
//
// 경로는 작업 디렉터리 기준입니다. 빌드가 assets/ 를 실행 파일 옆으로 복사합니다.
static constexpr const char* kStartScene = "assets/scenes/LightingProbe.hscene";

void DefaultGameModule::onStart(EngineContext& ctx) {
    std::ifstream f(kStartScene);
    if (!f) {
        HLOG(LogGame, Error) << "start scene not found: " << kStartScene << " (the world stays empty)";
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const std::vector<Actor*> spawned = SceneSerializer::readScene(text, ctx.world, ctx.assets);
    size_t failed = 0;
    for (const Actor* a : spawned) if (!a) ++failed;
    HLOG(LogGame, Log) << "start scene loaded: " << kStartScene << " (" << spawned.size() - failed
                       << " actors" << (failed ? ", " + std::to_string(failed) + " failed" : "") << ")";
}
