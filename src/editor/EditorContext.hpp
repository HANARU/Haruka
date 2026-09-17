#pragma once

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
    Actor*        selected = nullptr;
    AssetManager* assets   = nullptr;
    Renderer*     renderer = nullptr;
    IWindow*      window   = nullptr;
};
