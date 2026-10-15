#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Actor;
class World;

// 지운 액터 서브트리를 되살리는 데 필요한 것(에디터 생성 · 삭제 Undo, 2026-10-15).
// Unity 의 Undo 처럼 지울 때 상태를 글로 떠 두고, 되살릴 때 원래 ID 로 다시 만듭니다. 액터 객체를
// 월드 밖에 살려 두는 방식(UE · Godot)은 물리 · 오디오 · 선택이 모두 "월드 밖 액터"를 알아야 해서 쓰지
// 않았습니다. 되살린 액터는 새 객체이므로, 지우기 전의 포인터를 붙잡은 커맨드는 지울 때 걷힙니다(purge).
struct ActorSnapshot {
    std::string           text;          // 서브트리 블록들. 블록 순서 = ids 순서, 0 번이 루트
    std::vector<uint64_t> ids;           // 블록마다 원래 액터 ID
    uint64_t              parentId = 0;  // 루트의 원래 부모 ID(0 = 월드 루트)
};

// 서브트리를 글로 쓰는 함수. ordered[i] 의 부모가 세트 안에 있으면 parentIndex[i] 가 그 인덱스, 아니면 -1.
using SnapshotWriter = std::function<std::string(const std::vector<const Actor*>& ordered,
                                                 const std::vector<int>&          parentIndex)>;
// 글에서 블록마다 한 슬롯씩 액터를 만들고(실패면 nullptr) 세트 안의 부모까지 붙이는 함수.
using SnapshotReader = std::function<std::vector<Actor*>(const std::string& text)>;

namespace hsnap {

// 루트와 자손을 전위 순회 순서로 모읍니다(0 번이 루트, 자식은 붙인 순서).
std::vector<const Actor*> collectSubtree(const Actor* root);

ActorSnapshot capture(const Actor* root, const SnapshotWriter& write);

// 원래 ID 와 원래 부모로 되살리고 루트를 돌려줍니다. 원래 ID 중 하나라도 다른 액터가 쓰고 있거나
// 루트를 만들지 못하면 아무것도 남기지 않고 nullptr 입니다. 원래 부모가 사라졌으면 월드 루트에 둡니다.
Actor* restore(const ActorSnapshot& snap, World& world, const SnapshotReader& read);

} // namespace hsnap
