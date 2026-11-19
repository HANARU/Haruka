#pragma once
// 디테일 패널의 액터 참조 칸(참조 체계 R-7, Docs/Object_References.md)이 쓰는 목록 · 표시 이름.
// 화면 없이 검산합니다(HarukaRefTest).

#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../core/Guid.hpp"
#include "../scene/Actor.hpp"
#include "../scene/World.hpp"

namespace hpick {

struct Entry {
    std::string label;   // 목록에 보이는 글(같은 이름이면 " #2" 처럼 구분)
    uint64_t    guid = 0;   // 0 = None
};

// 첫 줄은 None, 그다음은 아웃라이너 순서(루트는 월드 순서, 자식은 붙인 순서, 전위)입니다. skip(에디터 카메라)과 그 자손은 뺍니다.
inline std::vector<Entry> buildList(const World& world, const Actor* skip, const std::string& noneLabel) {
    // 1) 아웃라이너 순서로 모읍니다.
    std::vector<const Actor*> order, stack;
    const auto& actors = world.getActors();
    for (auto it = actors.rbegin(); it != actors.rend(); ++it)
        if (!(*it)->getParent()) stack.push_back(it->get());
    while (!stack.empty()) {
        const Actor* a = stack.back();
        stack.pop_back();
        if (a == skip) continue;
        order.push_back(a);
        const auto& kids = a->getChildren();
        for (auto k = kids.rbegin(); k != kids.rend(); ++k) stack.push_back(*k);
    }
    // 2) 글을 붙입니다. 목록 대화상자는 고른 글로 항목을 찾으므로 글이 겹치면 안 됩니다. 이름이 처음 나오면 이름 그대로,
    //    다시 나오면 " #2" 부터 붙이되 다른 액터의 실제 이름(예: "Cube #2")이나 이미 쓴 글은 건너뜁니다. None 과 같은
    //    이름의 액터는 " #2" 부터 붙습니다.
    std::unordered_set<std::string> realNames, used{ noneLabel };
    for (const Actor* a : order) realNames.insert(a->name);
    std::vector<Entry> out;
    out.push_back({ noneLabel, 0 });
    for (const Actor* a : order) {
        std::string label = a->name;
        for (int n = 2; used.count(label); ++n) {
            label = a->name + " #" + std::to_string(n);
            while (realNames.count(label) || used.count(label)) label = a->name + " #" + std::to_string(++n);
        }
        used.insert(label);
        out.push_back({ label, a->guid().value });
    }
    return out;
}

// 칸에 보일 글: 비었으면 noneLabel, 찾으면 대상 이름, 못 찾으면 missingLabel.
inline std::string labelFor(const World& world, uint64_t guid, const std::string& noneLabel, const std::string& missingLabel) {
    if (guid == 0) return noneLabel;
    const Actor* a = world.findByGuid(HGuid{ guid });
    return a ? a->name : missingLabel;
}

} // namespace hpick
