// 에디터 액터 생성 · 삭제 Undo 를 검증합니다(ActorPresenceCmd · ActorSnapshot · World::reassignId, 2026-10-15).
// Vulkan 도, 디바이스도, 창도 필요 없습니다. 씬 직렬화(SceneSerializer) 대신 테스트용 글 형식을 넘겨,
// "원래 ID · 원래 부모 · 원래 순서로 되살리는가" 만 검산합니다. 직렬화 왕복은 scene_components 가 맡습니다.
//
// 실행: build/Release/HarukaActorUndoTest.exe

#include "../src/editor/EditorCommands.hpp"
#include "../src/core/World.hpp"

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

// 한 줄에 액터 하나: 이름 · 세트 안 부모 인덱스 · 로컬 위치.
std::string fakeWrite(const std::vector<const Actor*>& ordered, const std::vector<int>& parentIndex) {
    std::ostringstream os;
    for (size_t i = 0; i < ordered.size(); ++i) {
        const glm::vec3 p = ordered[i]->transform().position();
        os << ordered[i]->name << ' ' << parentIndex[i] << ' ' << p.x << ' ' << p.y << ' ' << p.z << '\n';
    }
    return os.str();
}

SnapshotReader fakeReader(World& w) {
    return [&w](const std::string& text) {
        std::vector<Actor*> out;
        std::vector<int>    parentOf;
        std::istringstream  is(text);
        std::string name;
        int   parent;
        float x, y, z;
        while (is >> name >> parent >> x >> y >> z) {
            Actor* a = w.spawnActor(name);
            a->setLocalPosition({ x, y, z });
            out.push_back(a);
            parentOf.push_back(parent);
        }
        for (size_t i = 0; i < out.size(); ++i)
            if (parentOf[i] >= 0) out[i]->attachTo(out[parentOf[i]]);
        return out;
    };
}

std::unique_ptr<ActorPresenceCmd> makeCmd(ActorPresenceCmd::Kind k, World& w, std::vector<uint64_t> ids,
                                          Actor** restoredOut = nullptr) {
    return std::make_unique<ActorPresenceCmd>(k, w, std::move(ids), fakeWrite, fakeReader(w),
                                              [restoredOut](Actor* a) { if (restoredOut) *restoredOut = a; });
}

bool idsAscending(const World& w) {
    uint64_t prev = 0;
    for (const auto& a : w.getActors()) {
        if (a->id <= prev) return false;
        prev = a->id;
    }
    return true;
}

std::vector<std::string> childNames(const Actor* a) {
    std::vector<std::string> n;
    for (const Actor* c : a->getChildren()) n.push_back(c->name);
    return n;
}

} // namespace

int main() {
    std::printf("== 1. 서브트리를 지우고 원래 ID · 부모 · 위치 · 순서로 되살린다 ==\n");
    {
        World w;
        HCommandStack st;
        w.onActorDestroyed.add([&st](Actor* a) { st.purge(a); });

        Actor* P  = w.spawnActor("P");
        Actor* R  = w.spawnActor("R");
        Actor* C1 = w.spawnActor("C1");
        Actor* C2 = w.spawnActor("C2");
        Actor* G  = w.spawnActor("G");
        Actor* U  = w.spawnActor("U");
        R->attachTo(P);  C1->attachTo(R);  C2->attachTo(R);  G->attachTo(C1);
        R->setLocalPosition({ 1.f, 2.f, 3.f });
        const uint64_t idR = R->id, idC1 = C1->id, idC2 = C2->id, idG = G->id, idU = U->id;

        const std::vector<const Actor*> order = hsnap::collectSubtree(R);
        check(order.size() == 4 && order[0] == R && order[1] == C1 && order[2] == G && order[3] == C2,
              "전위 순회 순서: R, C1, G, C2");
        const ActorSnapshot snap = hsnap::capture(R, fakeWrite);
        check(snap.parentId == P->id && snap.ids.size() == 4 && snap.ids[0] == idR, "스냅샷이 원래 부모 · ID 를 기억한다");

        Actor* restored = nullptr;
        auto cmd = makeCmd(ActorPresenceCmd::Kind::Delete, w, { idR }, &restored);
        cmd->redo();
        st.push(std::move(cmd));
        check(!w.findById(idR) && !w.findById(idC1) && !w.findById(idC2) && !w.findById(idG), "서브트리가 모두 지워진다");
        check(P->getChildren().empty() && w.findById(idU), "부모는 자식을 잃고 무관한 액터는 남는다");
        check(st.canUndo(), "삭제 커맨드는 자기가 지운 액터 때문에 걷히지 않는다");

        st.undo();
        Actor* r = w.findById(idR);
        check(r && r == restored && r->name == "R", "Undo 로 루트가 원래 ID 로 돌아온다");
        check(r && r->getParent() == P, "원래 부모 아래로 돌아온다");
        check(r && r->transform().position() == glm::vec3(1.f, 2.f, 3.f), "로컬 위치가 같다");
        check(r && childNames(r) == std::vector<std::string>{ "C1", "C2" }, "자식이 같은 순서로 돌아온다");
        Actor* g = w.findById(idG);
        check(g && g->getParent() == w.findById(idC1) && w.findById(idC2), "손자까지 원래 ID · 부모로 돌아온다");
        check(idsAscending(w), "World 의 액터 목록이 ID 오름차순을 유지한다");

        st.redo();
        check(!w.findById(idR) && !w.findById(idG), "Redo 로 다시 지워진다");
        st.undo();
        check(w.findById(idR) && w.findById(idG) && w.findById(idR)->getParent() == P, "다시 Undo 해도 같은 자리로 돌아온다");

        Actor* fresh = w.spawnActor("Fresh");
        check(fresh->id > idU && fresh->id > idG, "새로 만든 액터는 되살린 ID 와 겹치지 않는다");
    }

    std::printf("\n== 2. 삭제 전 편집 이력은 걷히고(사용자 결정), 되살린 상태는 지운 순간의 상태다 ==\n");
    {
        World w;
        HCommandStack st;
        w.onActorDestroyed.add([&st](Actor* a) { st.purge(a); });

        Actor* A = w.spawnActor("A");
        const uint64_t idA = A->id;
        Transform before = A->transform();
        A->setLocalPosition({ 5.f, 0.f, 0.f });
        st.push(std::make_unique<SetTransformCmd>(A, before, A->transform()));

        auto cmd = makeCmd(ActorPresenceCmd::Kind::Delete, w, { idA });
        cmd->redo();
        st.push(std::move(cmd));
        st.undo();
        Actor* a = w.findById(idA);
        check(a && a->transform().position().x == 5.f, "지운 순간의 위치로 돌아온다");
        check(!st.canUndo(), "삭제 전의 이동 커맨드는 지울 때 걷혔다");
    }

    std::printf("\n== 3. 생성 커맨드: Undo 는 지우고 Redo 는 같은 ID 로 되살린다 ==\n");
    {
        World w;
        HCommandStack st;
        w.onActorDestroyed.add([&st](Actor* a) { st.purge(a); });

        Actor* m1 = w.spawnActor("Mesh");
        Actor* m2 = w.spawnActor("Mesh_1");
        const uint64_t id1 = m1->id, id2 = m2->id;
        m2->setLocalPosition({ 0.f, 4.f, 0.f });
        st.push(makeCmd(ActorPresenceCmd::Kind::Create, w, { id1, id2 }));

        st.undo();
        check(!w.findById(id1) && !w.findById(id2), "Undo 로 한 번에 만든 액터가 모두 지워진다");
        st.redo();
        check(w.findById(id1) && w.findById(id2), "Redo 로 같은 ID 로 돌아온다");
        check(w.findById(id2) && w.findById(id2)->transform().position().y == 4.f, "지운 순간의 상태로 돌아온다");
        check(idsAscending(w), "ID 오름차순을 유지한다");
    }

    std::printf("\n== 4. 원래 부모가 사라졌으면 월드 루트에 되살린다 ==\n");
    {
        World w;
        Actor* P = w.spawnActor("P");
        Actor* C = w.spawnActor("C");
        C->attachTo(P);
        const uint64_t idC = C->id;

        auto cmd = makeCmd(ActorPresenceCmd::Kind::Delete, w, { idC });
        cmd->redo();
        w.destroyActor(P);   // Undo 커맨드가 아닌 경로로 부모가 사라짐
        cmd->undo();
        Actor* c = w.findById(idC);
        check(c && c->getParent() == nullptr, "부모 없이 루트로 돌아온다");
    }

    std::printf("\n== 5. ID 를 다른 액터와 겹치게 돌려주지 않는다 ==\n");
    {
        World w;
        Actor* a = w.spawnActor("a");
        Actor* b = w.spawnActor("b");
        check(!w.reassignId(a, b->id), "다른 액터가 쓰는 ID 는 거부한다");
        check(!w.reassignId(a, 0), "0 은 거부한다");
        check(w.reassignId(a, a->id), "같은 ID 는 그대로 성공한다");

        // 이미 쓰는 ID 로 되살리려 하면 아무것도 만들지 않습니다.
        ActorSnapshot snap = hsnap::capture(b, fakeWrite);
        const size_t before = w.getActors().size();
        Actor* r = hsnap::restore(snap, w, fakeReader(w));
        check(r == nullptr && w.getActors().size() == before, "쓰고 있는 ID 로는 되살리지 않고 아무것도 남기지 않는다");
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
