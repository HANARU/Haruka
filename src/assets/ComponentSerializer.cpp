#include "ComponentSerializer.hpp"

#include "../core/Actor.hpp"
#include "../core/ComponentRegistry.hpp"
#include "../core/SceneLog.hpp"
#include "../core/UnknownComponent.hpp"

#include <set>

namespace ComponentSerializer {

void write(std::ostream& os, const Actor& actor) {
    auto& reg = ComponentRegistry::get();
    for (const auto& c : actor.getComponents()) {
        // 등록되지 않은 타입의 자리표시자는 원래 이름과 값을 그대로 다시 씁니다(enabled 도 원문에 있음).
        if (auto* u = dynamic_cast<const UnknownComponent*>(c.get())) {
            ComponentFieldWriter w;
            u->writeFields(w);
            os << "COMPONENT:" << u->originalType << "\n";
            for (const auto& kv : w.fields()) os << kv.first << "=" << kv.second << "\n";
            os << "END_COMPONENT\n";
            continue;
        }
        const char* type = c->typeName();
        if (!reg.find(type)) {
            static std::set<std::string> warned;
            if (warned.insert(type).second)
                HLOG(LogScene, Warning) << "component type '" << type
                    << "' is not registered in ComponentRegistry and is not saved";
            continue;
        }
        ComponentFieldWriter w;
        c->writeFields(w);
        os << "COMPONENT:" << type << "\n";
        os << "enabled=" << (c->isEnabled() ? 1 : 0) << "\n";
        for (const auto& kv : w.fields()) os << kv.first << "=" << kv.second << "\n";
        os << "END_COMPONENT\n";
    }
}

bool BlockParser::feed(const std::string& line, const std::string& actorName) {
    if (line.rfind("COMPONENT:", 0) == 0) {
        if (open_)
            HLOG(LogScene, Warning) << "actor '" << actorName << "': COMPONENT block '"
                                    << blocks_.back().type << "' has no END_COMPONENT";
        blocks_.push_back({ line.substr(10), {} });
        open_ = true;
        return true;
    }
    if (line == "END_COMPONENT") {
        open_ = false;
        return true;
    }
    if (!open_) return false;
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
        HLOG(LogScene, Warning) << "actor '" << actorName << "': line '" << line
                                << "' in COMPONENT block '" << blocks_.back().type << "' has no '='; skipped";
        return true;
    }
    blocks_.back().fields.emplace_back(line.substr(0, eq), line.substr(eq + 1));
    return true;
}

int spawn(Actor& actor, const std::vector<Block>& blocks, const ComponentLoadContext& ctx) {
    auto& reg = ComponentRegistry::get();
    int made = 0;
    for (const auto& b : blocks) {
        const ComponentRegistry::Entry* e = reg.find(b.type);
        if (!e) {
            HLOG(LogScene, Warning) << "actor '" << actor.name << "': component type '" << b.type
                << "' is not registered; kept as is so saving does not lose it";
            auto* u = actor.addComponent<UnknownComponent>();
            u->originalType = b.type;
            u->rawFields    = b.fields;
            ++made;
            continue;
        }
        Component* c = e->add(actor);
        if (!c) continue;
        bool enabled = true;
        for (const auto& [key, value] : b.fields) {
            if (key == "enabled") {
                if (!fieldparse::toBool(value, enabled)) {
                    c->reportBadField(key, value);
                    enabled = true;
                }
                continue;
            }
            if (!c->readField(key, value))
                HLOG(LogScene, Warning) << "actor '" << actor.name << "': " << e->name
                                        << " has no field '" << key << "'; skipped";
        }
        c->onFieldsLoaded(ctx);
        c->setEnabled(enabled);
        ++made;
    }
    return made;
}

} // namespace ComponentSerializer
