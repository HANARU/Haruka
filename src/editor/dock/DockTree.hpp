#pragma once
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

class HWidget;

// ── DockNode ──────────────────────────────────────────────────────────────────
// 에디터 도크 트리의 한 노드입니다. children 이 비면 **리프(탭 스택)**, 아니면 **분할 노드**.
// 분할 비율은 children 과 1:1 인 ratios(합 ≈ 1)로 두고, 실제 배치는 SHorizontalBox/
// SVerticalBox 의 Fill 가중치로 매핑됩니다(가중치 = 비율). 트리 자체는 위젯과 독립이라
// 위젯을 리빌드해도(스플리터 드래그·재도킹) 노드 포인터는 안정적입니다.
struct DockNode {
    enum class Split { Horizontal, Vertical };   // H = 자식 좌우, V = 자식 상하

    Split                                  split = Split::Horizontal;
    std::vector<std::unique_ptr<DockNode>> children;   // 분할 노드일 때
    std::vector<float>                     ratios;      // children 과 1:1, 합 ≈ 1

    std::vector<std::string>               panelIds;    // 리프의 탭 목록
    int                                    activeTab = 0;

    bool isLeaf() const { return children.empty(); }

    static std::unique_ptr<DockNode> makeLeaf(std::vector<std::string> ids, int active = 0) {
        auto n = std::make_unique<DockNode>();
        n->panelIds  = std::move(ids);
        n->activeTab = active;
        return n;
    }
    static std::unique_ptr<DockNode> makeSplit(Split s,
            std::vector<std::unique_ptr<DockNode>> kids, std::vector<float> r) {
        auto n = std::make_unique<DockNode>();
        n->split    = s;
        n->children = std::move(kids);
        n->ratios   = std::move(r);
        return n;
    }
};

// ── PanelRegistry ─────────────────────────────────────────────────────────────
// id → {제목, 위젯, closable}. 패널 위젯은 EditorApp 이 1회 생성해 보관합니다(패널은
// 싱글턴이라 재도킹 시 같은 인스턴스가 이동). **신규 위젯도 add() 한 줄이면 도킹 대상**이
// 됩니다 — 사용자가 말한 "위에 위젯을 디자인"하는 확장성 seam.
struct PanelDef {
    std::string              title;
    std::shared_ptr<HWidget> widget;
    bool                     closable = true;
};

class PanelRegistry {
public:
    void add(const std::string& id, const std::string& title,
             std::shared_ptr<HWidget> widget, bool closable = true) {
        if (!defs_.count(id)) order_.push_back(id);
        defs_[id] = { std::move(title), std::move(widget), closable };
    }
    const PanelDef* find(const std::string& id) const {
        auto it = defs_.find(id);
        return it == defs_.end() ? nullptr : &it->second;
    }
    const std::vector<std::string>& order() const { return order_; }

private:
    std::unordered_map<std::string, PanelDef> defs_;
    std::vector<std::string>                  order_;
};
