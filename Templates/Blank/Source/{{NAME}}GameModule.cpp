#include "runtime/IGameModule.hpp"
#include "core/HLog.hpp"

namespace {
constinit HLogCategory Log{{NAME}}{ "{{NAME}}", ELogVerbosity::Log };
}

class {{NAME}}GameModule final : public IGameModule {
public:
    void onStart(EngineContext& /*ctx*/) override {
        HLOG(Log{{NAME}}, Log) << "{{NAME}} game module started";
    }
};

std::unique_ptr<IGameModule> createGameModule() {
    return std::make_unique<{{NAME}}GameModule>();
}
