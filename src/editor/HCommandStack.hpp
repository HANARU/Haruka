#pragma once
#include <deque>
#include <memory>
#include <algorithm>
#include <cstdint>

class Actor;
class Component;

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void undo() = 0;
    virtual void redo() = 0;
    // 이 커맨드가 해당 액터를 참조하는지 여부입니다. 액터가 파괴될 때 스택에서
    // 걷어내기 위한 것으로, 기본은 false(액터와 무관한 커맨드)입니다. 2026-10-15 부터 에디터 커맨드는
    // 액터를 ID 로 찾으므로 재정의하는 것이 없습니다. 포인터를 붙잡는 커맨드를 새로 만들 때만 재정의합니다.
    virtual bool refersTo(const Actor* /*actor*/) const { return false; }
    // 액터는 살아 있는데 **컴포넌트만** 제거되는 경로가 따로 있습니다(디테일 패널의
    // 컴포넌트 x 버튼). 그때는 onActorDestroyed 가 발화하지 않으므로 위의 refersTo
    // 만으로는 걷어낼 수 없어, 컴포넌트를 붙잡고 있는 커맨드가 이쪽을 재정의합니다. 2026-10-15 부터는 컴포넌트를
    // 타입 + 순번으로 찾는 커맨드뿐이라 제거 경로(ComponentPresenceCmd)가 purgeComponent 를 부르지 않습니다.
    // 컴포넌트 포인터를 붙잡는 커맨드를 새로 만들면 그 경로에 purgeComponent 호출도 다시 넣어야 합니다.
    virtual bool refersToComponent(const Component* /*comp*/) const { return false; }
};

class HCommandStack {
    // 커맨드마다 쌓을 때 받은 일련번호를 함께 둡니다. 씬 문서가 "저장한 순간의 상태"를 기억하는 표지로
    // 씁니다(stateToken). 위치(개수)로 기억하면, 되돌린 뒤 다른 편집을 쌓아 같은 개수가 되었을 때
    // 저장한 상태로 잘못 봅니다(Qt QUndoStack 의 clean index 와 같은 문제를 일련번호로 피함).
    struct Entry { std::unique_ptr<ICommand> cmd; uint64_t serial = 0; };
    std::deque<Entry> undo_;
    std::deque<Entry> redo_;
    uint64_t nextSerial_ = 1;
    // undo_ 가 비었을 때의 상태 표지입니다. 처음에는 0, 오래된 커맨드를 밀어내면 그 커맨드의 번호가
    // 됩니다. 밀려난 커맨드의 효과는 남아 있으므로, 전부 되돌려도 처음 상태와 같지 않기 때문입니다.
    uint64_t base_ = 0;
    static constexpr size_t kMax = 64;
public:
    void push(std::unique_ptr<ICommand> cmd) {
        redo_.clear();
        undo_.push_back({ std::move(cmd), nextSerial_++ });
        if (undo_.size() > kMax) { base_ = undo_.front().serial; undo_.pop_front(); }
    }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    void undo() {
        if (undo_.empty()) return;
        Entry e = std::move(undo_.back()); undo_.pop_back();
        e.cmd->undo();
        redo_.push_back(std::move(e));
    }
    void redo() {
        if (redo_.empty()) return;
        Entry e = std::move(redo_.back()); redo_.pop_back();
        e.cmd->redo();
        undo_.push_back(std::move(e));
    }
    // 이력을 비웁니다. 지운 뒤의 상태 표지는 새 번호라, 지우기 전에 기억한 표지와 겹치지 않습니다.
    void clear() { undo_.clear(); redo_.clear(); base_ = nextSerial_++; }

    // 지금 상태의 표지입니다. 같은 값이면 같은 편집 상태입니다(되돌리기 · 다시 하기로 오간 경우 포함).
    uint64_t stateToken() const { return undo_.empty() ? base_ : undo_.back().serial; }

    // 파괴되는 액터를 참조하는 커맨드를 undo/redo 양쪽에서 제거합니다.
    // onActorDestroyed 구독이 호출하여, 어떤 파괴 경로든 댕글링 커맨드를 남기지
    // 않도록 구조적으로 보장합니다. 각 호출부가 clear() 를 기억해야 했던 규율을
    // 대체하며, 무관한 undo 이력은 보존합니다(전체 clear 보다 정밀함).
    void purge(const Actor* actor) {
        auto drop = [actor](const Entry& e) { return e.cmd->refersTo(actor); };
        undo_.erase(std::remove_if(undo_.begin(), undo_.end(), drop), undo_.end());
        redo_.erase(std::remove_if(redo_.begin(), redo_.end(), drop), redo_.end());
    }

    // 제거되는 **컴포넌트**를 참조하는 커맨드를 걷어냅니다. 프로퍼티 스냅샷 커맨드는
    // 값 접근자가 컴포넌트 원시 포인터를 캡처하고 있어, 컴포넌트가 해제된 뒤 undo 하면
    // 해제된 메모리에 씁니다. 액터는 살아남으므로 purge(Actor*) 가 이 경우를 잡지 못합니다.
    void purgeComponent(const Component* comp) {
        auto drop = [comp](const Entry& e) { return e.cmd->refersToComponent(comp); };
        undo_.erase(std::remove_if(undo_.begin(), undo_.end(), drop), undo_.end());
        redo_.erase(std::remove_if(redo_.begin(), redo_.end(), drop), redo_.end());
    }
};
