// 커맨드 스택이 죽은 대상을 참조하는 커맨드를 걷어내는지 검증합니다.
// Vulkan 도, 디바이스도, 창도, World 도 필요 없습니다. HCommandStack 만 컴파일합니다.
//
// 이 테스트가 존재하는 이유: 프로퍼티 스냅샷 커맨드는 값 접근자가 **컴포넌트 원시 포인터**를
// 캡처합니다. 그 대상이 사라지는 경로가 둘인데(액터 파괴 / 컴포넌트만 제거) 한동안 앞의 것만
// 막고 있었고, 뒤의 경로로 들어가면 다음 undo 가 해제된 메모리에 썼습니다. 액터가 살아남기
// 때문에 onActorDestroyed 가 발화하지 않아 purge(Actor*) 가 이 경우를 잡지 못합니다.
// 두 경로가 각각 정확히 자기 몫만 걷어내는지를 여기서 검산합니다.
//
// 실행: build/Release/HarukaCommandTest.exe

#include "../src/editor/HCommandStack.hpp"

#include <cstdio>
#include <string>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

// 실행 횟수를 세는 관찰용 커맨드. 액터와 컴포넌트 중 무엇을 참조하는지 지정합니다.
// 실제 Actor/Component 를 링크하지 않으려고 주소만 쓰므로, 대상은 더미 정수로 둡니다.
struct Probe : ICommand {
    const void* actorKey = nullptr;
    const void* compKey  = nullptr;
    int undos = 0, redos = 0;

    void undo() override { ++undos; }
    void redo() override { ++redos; }
    bool refersTo(const Actor* a) const override {
        return actorKey && actorKey == static_cast<const void*>(a);
    }
    bool refersToComponent(const Component* c) const override {
        return compKey && compKey == static_cast<const void*>(c);
    }
};

// 더미 대상. 주소만 비교하므로 정의는 필요 없습니다.
struct FakeActor { int pad; };
struct FakeComp  { int pad; };

std::unique_ptr<Probe> makeProbe(const void* actorKey, const void* compKey) {
    auto p = std::make_unique<Probe>();
    p->actorKey = actorKey;
    p->compKey  = compKey;
    return p;
}

} // namespace

int main() {
    std::printf("== HCommandStack purge ==\n");

    FakeActor actorA{}, actorB{};
    FakeComp  compA{},  compB{};

    std::printf("\n== 1. 액터 파괴는 그 액터 커맨드만 걷어낸다 ==\n");
    {
        HCommandStack st;
        st.push(makeProbe(&actorA, &compA));
        st.push(makeProbe(&actorB, &compB));

        st.purge(reinterpret_cast<const Actor*>(&actorA));
        check(st.canUndo(), "무관한 커맨드는 남는다");

        st.purge(reinterpret_cast<const Actor*>(&actorB));
        check(!st.canUndo(), "둘 다 걷어내면 스택이 빈다");
    }

    std::printf("\n== 2. 컴포넌트만 제거되는 경로 (액터는 살아 있음) ==\n");
    {
        HCommandStack st;
        st.push(makeProbe(&actorA, &compA));

        // 액터는 파괴되지 않았으므로 purge(Actor*) 는 아무것도 하지 않아야 합니다.
        st.purge(reinterpret_cast<const Actor*>(&actorB));
        check(st.canUndo(), "다른 액터 purge 로는 걷히지 않는다");

        // 컴포넌트 제거 경로가 이 커맨드를 걷어내야 합니다. 이것이 없으면
        // 다음 undo 가 해제된 컴포넌트를 만집니다.
        st.purgeComponent(reinterpret_cast<const Component*>(&compA));
        check(!st.canUndo(), "컴포넌트 purge 로 걷힌다");
    }

    std::printf("\n== 3. 컴포넌트 purge 는 다른 컴포넌트를 건드리지 않는다 ==\n");
    {
        HCommandStack st;
        st.push(makeProbe(&actorA, &compA));
        st.push(makeProbe(&actorA, &compB));

        st.purgeComponent(reinterpret_cast<const Component*>(&compA));
        check(st.canUndo(), "다른 컴포넌트 커맨드는 남는다");

        st.undo();
        check(st.canRedo(), "남은 커맨드는 정상 undo 된다");

        // redo 스택에 있는 것도 걷어내야 합니다. undo 한 뒤 컴포넌트를 지우고
        // redo 하면 같은 방식으로 해제된 메모리를 만지기 때문입니다.
        st.purgeComponent(reinterpret_cast<const Component*>(&compB));
        check(!st.canRedo(), "redo 스택에서도 걷힌다");
    }

    std::printf("\n== 4. 대상을 지정하지 않은 커맨드는 어느 purge 로도 걷히지 않는다 ==\n");
    {
        HCommandStack st;
        st.push(makeProbe(nullptr, nullptr));

        st.purge(reinterpret_cast<const Actor*>(&actorA));
        st.purgeComponent(reinterpret_cast<const Component*>(&compA));
        check(st.canUndo(), "무관한 커맨드는 보존된다");
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
