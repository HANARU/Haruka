#include "AnimStateMachine.hpp"

namespace hanim {

int StateMachine::addState(const std::string& name, const std::string& clip, bool loop, float speed) {
    states_.push_back({ name, clip, loop, speed });
    return (int)states_.size() - 1;
}

void StateMachine::addTransition(int from, int to, float blend, std::vector<Condition> conditions) {
    if (to < 0 || to >= (int)states_.size() || from < -1 || from >= (int)states_.size()) return;
    transitions_.push_back({ from, to, blend < 0.f ? 0.f : blend, std::move(conditions) });
}

int StateMachine::findState(const std::string& name) const {
    for (size_t i = 0; i < states_.size(); ++i) if (states_[i].name == name) return (int)i;
    return -1;
}

bool StateMachine::getBool(const std::string& p) const {
    const auto it = bools_.find(p);
    return it != bools_.end() && it->second;
}

float StateMachine::getFloat(const std::string& p) const {
    const auto it = floats_.find(p);
    return it != floats_.end() ? it->second : 0.f;
}

bool StateMachine::triggerSet(const std::string& p) const {
    const auto it = triggers_.find(p);
    return it != triggers_.end() && it->second;
}

void StateMachine::reset(int startState) {
    start_   = startState;
    current_ = -1;
}

bool StateMachine::met(const Transition& t, bool clipEnded) const {
    for (const Condition& c : t.conditions) {
        switch (c.kind) {
            case Condition::BoolTrue:     if (!getBool(c.param)) return false; break;
            case Condition::BoolFalse:    if (getBool(c.param)) return false; break;
            case Condition::FloatGreater: if (!(getFloat(c.param) > c.value)) return false; break;
            case Condition::FloatLess:    if (!(getFloat(c.param) < c.value)) return false; break;
            case Condition::Trigger:      if (!triggerSet(c.param)) return false; break;
            case Condition::ClipEnd:      if (!clipEnded) return false; break;
        }
    }
    return true;
}

StateMachine::Step StateMachine::update(bool clipEnded) {
    if (states_.empty()) return {};
    if (current_ < 0) {
        current_ = (start_ >= 0 && start_ < (int)states_.size()) ? start_ : 0;
        return { current_, 0.f };
    }
    for (const Transition& t : transitions_) {
        if (t.from != -1 && t.from != current_) continue;
        if (t.from == -1 && t.to == current_) continue;
        if (!met(t, clipEnded)) continue;
        for (const Condition& c : t.conditions)
            if (c.kind == Condition::Trigger) triggers_[c.param] = false;   // 쓴 트리거는 지움
        current_ = t.to;
        return { t.to, t.blend };
    }
    return {};
}

} // namespace hanim
