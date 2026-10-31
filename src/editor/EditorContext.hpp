#pragma once
#include <vector>

class World;
class Actor;
class AssetManager;
class Renderer;
class IWindow;

// ── Editor shared state ────────────────────────────────────────────────────────
// 로그는 여기 두지 않습니다. 코어 로그 체계(core/HLog.hpp)가 링 버퍼를 소유하고,
// 에디터는 HLog::latestSeq / copySince 로 새 레코드만 읽어 갑니다.
struct EditorContext {
    World*        world    = nullptr;
    // 주 선택(마지막에 고른 액터). 디테일 패널 · 기즈모 · 리네임이 이것을 씁니다.
    Actor*        selected = nullptr;
    // 모든 선택(2026-10-15). 마지막 요소가 selected 입니다. 액터가 파괴되면 에디터가 여기서 뺍니다.
    std::vector<Actor*> selection;
    AssetManager* assets   = nullptr;
    Renderer*     renderer = nullptr;
    IWindow*      window   = nullptr;
};
