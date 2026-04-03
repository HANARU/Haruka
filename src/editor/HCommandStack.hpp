#pragma once
#include <deque>
#include <memory>
#include <algorithm>

class Actor;

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void undo() = 0;
    virtual void redo() = 0;
    // 이 커맨드가 해당 액터를 참조하는지 여부입니다. 액터가 파괴될 때 스택에서
    // 걷어내기 위한 것으로, 기본은 false(액터와 무관한 커맨드)입니다.
    virtual bool refersTo(const Actor* /*actor*/) const { return false; }
};

class HCommandStack {
    std::deque<std::unique_ptr<ICommand>> undo_;
    std::deque<std::unique_ptr<ICommand>> redo_;
    static constexpr size_t kMax = 64;
public:
    void push(std::unique_ptr<ICommand> cmd) {
        redo_.clear();
        undo_.push_back(std::move(cmd));
        if (undo_.size() > kMax) undo_.pop_front();
    }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    void undo() {
        if (undo_.empty()) return;
        auto c = std::move(undo_.back()); undo_.pop_back();
        c->undo();
        redo_.push_back(std::move(c));
    }
    void redo() {
        if (redo_.empty()) return;
        auto c = std::move(redo_.back()); redo_.pop_back();
        c->redo();
        undo_.push_back(std::move(c));
    }
    void clear() { undo_.clear(); redo_.clear(); }

    // 파괴되는 액터를 참조하는 커맨드를 undo/redo 양쪽에서 제거합니다.
    // onActorDestroyed 구독이 호출하여, 어떤 파괴 경로든 댕글링 커맨드를 남기지
    // 않도록 구조적으로 보장합니다. 각 호출부가 clear() 를 기억해야 했던 규율을
    // 대체하며, 무관한 undo 이력은 보존합니다(전체 clear 보다 정밀함).
    void purge(const Actor* actor) {
        auto drop = [actor](const std::unique_ptr<ICommand>& c) {
            return c->refersTo(actor);
        };
        undo_.erase(std::remove_if(undo_.begin(), undo_.end(), drop), undo_.end());
        redo_.erase(std::remove_if(redo_.begin(), redo_.end(), drop), redo_.end());
    }
};
