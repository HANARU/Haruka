#include "IGameModule.hpp"

// 게임 소스가 없는 프로젝트가 링크하는 빈 기본 모듈 (P3b, 2026-11-19).
class EmptyGameModule final : public IGameModule {
public:
    void onStart(EngineContext& /*ctx*/) override {}
};

std::unique_ptr<IGameModule> createGameModule() {
    return std::make_unique<EmptyGameModule>();
}
