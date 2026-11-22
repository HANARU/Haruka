#include "DockTree.hpp"
#include <algorithm>
#include <cctype>

DockNode* dockFindParent(DockNode* cur, DockNode* target, int& outIdx) {
    if (!cur || cur->isLeaf()) return nullptr;
    for (size_t i = 0; i < cur->children.size(); ++i) {
        if (cur->children[i].get() == target) { outIdx = (int)i; return cur; }
        if (DockNode* p = dockFindParent(cur->children[i].get(), target, outIdx)) return p;
    }
    return nullptr;
}

void dockNormalize(DockNode* n) {
    if (!n || n->ratios.empty()) return;
    float sum = 0.f;
    for (float r : n->ratios) sum += r;
    if (sum <= 1e-6f) {
        for (float& r : n->ratios) r = 1.f / (float)n->ratios.size();
        return;
    }
    for (float& r : n->ratios) r /= sum;
}

void dockSerialize(const DockNode* n, std::ostringstream& o) {
    if (!n) { o << "L 0 0\n"; return; }
    if (n->isLeaf()) {
        o << "L " << n->activeTab << ' ' << n->panelIds.size();
        for (const auto& id : n->panelIds) o << ' ' << id;
        o << '\n';
    } else {
        o << "S " << (n->split == DockNode::Split::Vertical ? 'V' : 'H')
          << ' ' << n->children.size();
        for (size_t i = 0; i < n->children.size(); ++i)
            o << ' ' << ((i < n->ratios.size()) ? n->ratios[i] : 0.f);
        o << '\n';
        for (const auto& c : n->children) dockSerialize(c.get(), o);
    }
}

std::unique_ptr<DockNode> dockParse(std::istringstream& in, int depth, bool& ok) {
    if (!ok) return nullptr;
    if (depth > 32) { ok = false; return nullptr; }
    std::string tag;
    if (!(in >> tag)) { ok = false; return nullptr; }
    if (tag == "L") {
        int active = 0, count = 0;
        if (!(in >> active >> count) || count < 0 || count > 64) { ok = false; return nullptr; }
        std::vector<std::string> ids;
        for (int i = 0; i < count; ++i) {
            std::string id;
            if (!(in >> id)) { ok = false; return nullptr; }
            ids.push_back(std::move(id));
        }
        return DockNode::makeLeaf(std::move(ids), active);
    }
    if (tag == "S") {
        std::string orient;
        int count = 0;
        if (!(in >> orient >> count) || (orient != "H" && orient != "V")
            || count < 1 || count > 64) { ok = false; return nullptr; }
        std::vector<float> ratios(count, 0.f);
        for (int i = 0; i < count; ++i)
            if (!(in >> ratios[i])) { ok = false; return nullptr; }
        std::vector<std::unique_ptr<DockNode>> kids;
        for (int i = 0; i < count; ++i) {
            auto c = dockParse(in, depth + 1, ok);
            if (!ok) return nullptr;
            kids.push_back(std::move(c));
        }
        return DockNode::makeSplit(orient == "V" ? DockNode::Split::Vertical
                                                 : DockNode::Split::Horizontal,
                                   std::move(kids), std::move(ratios));
    }
    ok = false;
    return nullptr;
}

std::unique_ptr<DockNode> dockPrune(std::unique_ptr<DockNode> n,
                                    const std::function<bool(const std::string&)>& valid,
                                    std::unordered_set<std::string>& seen) {
    if (!n) return nullptr;
    if (n->isLeaf()) {
        std::string activeId;
        if (n->activeTab >= 0 && n->activeTab < (int)n->panelIds.size())
            activeId = n->panelIds[n->activeTab];
        std::vector<std::string> kept;
        for (auto& id : n->panelIds)
            if (valid(id) && seen.insert(id).second) kept.push_back(id);
        if (kept.empty()) return nullptr;
        n->panelIds  = std::move(kept);
        n->activeTab = 0;
        for (int i = 0; i < (int)n->panelIds.size(); ++i)
            if (n->panelIds[i] == activeId) { n->activeTab = i; break; }
        return n;
    }
    std::vector<std::unique_ptr<DockNode>> kids;
    std::vector<float>                     ratios;
    for (size_t i = 0; i < n->children.size(); ++i) {
        auto c = dockPrune(std::move(n->children[i]), valid, seen);
        if (c) {
            kids.push_back(std::move(c));
            ratios.push_back(i < n->ratios.size() ? n->ratios[i] : 1.f);
        }
    }
    if (kids.empty())     return nullptr;
    if (kids.size() == 1) return std::move(kids[0]);
    n->children = std::move(kids);
    n->ratios   = std::move(ratios);
    dockNormalize(n.get());
    return n;
}

bool dockContains(const DockNode* n, const std::string& id) {
    if (!n) return false;
    if (n->isLeaf()) {
        for (const auto& p : n->panelIds) if (p == id) return true;
        return false;
    }
    for (const auto& c : n->children) if (dockContains(c.get(), id)) return true;
    return false;
}

DockNode* dockFindLeaf(DockNode* n, const std::string& id) {
    if (!n) return nullptr;
    if (n->isLeaf()) {
        for (const auto& p : n->panelIds) if (p == id) return n;
        return nullptr;
    }
    for (auto& c : n->children)
        if (DockNode* f = dockFindLeaf(c.get(), id)) return f;
    return nullptr;
}

void dockRemoveTab(DockNode* leaf, int idx) {
    leaf->panelIds.erase(leaf->panelIds.begin() + idx);
    if (idx < leaf->activeTab) --leaf->activeTab;
    leaf->activeTab = std::clamp(leaf->activeTab, 0, std::max(0, (int)leaf->panelIds.size() - 1));
}

bool dockPanelIsActive(const DockNode* n, const std::string& id) {
    if (!n) return false;
    if (n->isLeaf()) {
        return n->activeTab >= 0 && n->activeTab < (int)n->panelIds.size()
            && n->panelIds[n->activeTab] == id;
    }
    for (const auto& c : n->children) if (dockPanelIsActive(c.get(), id)) return true;
    return false;
}

void dockCollapseEmptyLeaf(std::unique_ptr<DockNode>& root, DockNode* leaf) {
    int idx = -1;
    DockNode* parent = dockFindParent(root.get(), leaf, idx);
    if (!parent) return;
    parent->children.erase(parent->children.begin() + idx);
    if (idx < (int)parent->ratios.size()) parent->ratios.erase(parent->ratios.begin() + idx);
    dockNormalize(parent);

    if (parent->children.size() == 1) {
        int pidx = -1;
        DockNode* grand = dockFindParent(root.get(), parent, pidx);
        std::unique_ptr<DockNode> only = std::move(parent->children[0]);
        if (grand) grand->children[pidx] = std::move(only);
        else       root = std::move(only);
    }
}

// target 옆에 node 를 분할로 붙입니다. 탭 하나(dockAttachSplit)와 떠 있는 창 트리 통째
// (dockMergeFloatInto)가 같은 규칙을 쓰게 하려고 노드를 받는 판으로 나눴습니다.
static void dockAttachNode(std::unique_ptr<DockNode>& root, DockNode* target,
                           std::unique_ptr<DockNode> newLeaf, EDockZone zone, float share) {
    if (!target || !root || !newLeaf) return;
    const bool horiz  = (zone == EDockZone::Left || zone == EDockZone::Right);
    const bool before = (zone == EDockZone::Left || zone == EDockZone::Top);
    const DockNode::Split want = horiz ? DockNode::Split::Horizontal : DockNode::Split::Vertical;

    int pidx = -1;
    DockNode* parent = dockFindParent(root.get(), target, pidx);
    if (!parent && target != root.get()) return;
    if (!target->isLeaf() && target->split == want) {
        for (float& r : target->ratios) r *= (1.f - share);
        const size_t at = before ? 0 : target->children.size();
        target->children.insert(target->children.begin() + at, std::move(newLeaf));
        target->ratios.insert(target->ratios.begin() + std::min(at, target->ratios.size()), share);
        dockNormalize(target);
        return;
    }
    if (parent && parent->split == want) {
        const int   insertAt = before ? pidx : pidx + 1;
        const float cur   = (pidx < (int)parent->ratios.size())
                          ? parent->ratios[pidx]
                          : 1.f / (float)std::max<size_t>(1, parent->children.size());
        const float given = cur * share;
        if (pidx < (int)parent->ratios.size()) parent->ratios[pidx] = cur - given;
        parent->children.insert(parent->children.begin() + insertAt, std::move(newLeaf));
        parent->ratios.insert(parent->ratios.begin() + insertAt, given);
        dockNormalize(parent);
    } else {
        std::unique_ptr<DockNode> targetOwned =
            parent ? std::move(parent->children[pidx]) : std::move(root);
        std::vector<std::unique_ptr<DockNode>> kids;
        std::vector<float>                     ratios;
        if (before) { kids.push_back(std::move(newLeaf));     kids.push_back(std::move(targetOwned)); ratios = { share, 1.f - share }; }
        else        { kids.push_back(std::move(targetOwned)); kids.push_back(std::move(newLeaf));     ratios = { 1.f - share, share }; }
        auto split = DockNode::makeSplit(want, std::move(kids), std::move(ratios));
        if (parent) parent->children[pidx] = std::move(split);
        else        root = std::move(split);
    }
}

void dockAttachSplit(std::unique_ptr<DockNode>& root, DockNode* target,
                     const std::string& panelId, EDockZone zone, float share) {
    dockAttachNode(root, target, DockNode::makeLeaf({ panelId }), zone, share);
}

bool dockRedock(std::unique_ptr<DockNode>& root, DockNode* src, int srcIdx,
                const std::string& panelId, DockNode* target, EDockZone zone) {
    if (!src || !target || zone == EDockZone::None) return false;
    if (srcIdx < 0 || srcIdx >= (int)src->panelIds.size()) return false;
    if (src == target && (zone == EDockZone::Center || src->panelIds.size() <= 1)) return false;

    dockRemoveTab(src, srcIdx);

    if (zone == EDockZone::Center) {
        target->panelIds.push_back(panelId);
        target->activeTab = (int)target->panelIds.size() - 1;
    } else {
        dockAttachSplit(root, target, panelId, zone, 0.5f);
    }

    if (src->panelIds.empty())
        dockCollapseEmptyLeaf(root, src);
    return true;
}

bool dockClosePanel(std::unique_ptr<DockNode>& root, DockPanelHome& home,
                    const std::string& id, bool closable) {
    if (!closable || !root) return false;
    DockNode* leaf = dockFindLeaf(root.get(), id);
    if (!leaf) return false;
    if (leaf == root.get() && leaf->panelIds.size() <= 1) return false;

    std::vector<std::string> companions;
    for (const auto& p : leaf->panelIds) if (p != id) companions.push_back(p);
    home[id] = std::move(companions);

    const auto it = std::find(leaf->panelIds.begin(), leaf->panelIds.end(), id);
    dockRemoveTab(leaf, (int)(it - leaf->panelIds.begin()));
    if (leaf->panelIds.empty()) dockCollapseEmptyLeaf(root, leaf);
    return true;
}

bool dockShowPanel(std::unique_ptr<DockNode>& root, DockPanelHome& home,
                   const std::string& id) {
    if (!root) return false;
    if (DockNode* leaf = dockFindLeaf(root.get(), id)) {
        for (int i = 0; i < (int)leaf->panelIds.size(); ++i)
            if (leaf->panelIds[i] == id) leaf->activeTab = i;
    } else {
        DockNode* target = nullptr;
        if (auto h = home.find(id); h != home.end())
            for (const auto& other : h->second)
                if ((target = dockFindLeaf(root.get(), other)) != nullptr) break;
        if (target) {
            target->panelIds.push_back(id);
            target->activeTab = (int)target->panelIds.size() - 1;
        } else {
            DockNode* vp = dockFindLeaf(root.get(), "Viewport");
            if (id == "Bottom" || !vp) dockAttachSplit(root, root.get(), id, EDockZone::Bottom, 0.28f);
            else if (id == "Outliner") dockAttachSplit(root, vp, id, EDockZone::Left,  0.25f);
            else                       dockAttachSplit(root, vp, id, EDockZone::Right, 0.25f);
        }
    }
    return true;
}

std::string dockSerializeLayout(const DockNode* root) {
    std::ostringstream oss;
    oss << "# Haruka Dock Layout v1\n";
    dockSerialize(root, oss);
    return oss.str();
}

std::unique_ptr<DockNode> dockParseLayout(const std::string& text,
                                          const std::function<bool(const std::string&)>& valid) {
    std::istringstream raw(text);
    std::string body, line;
    while (std::getline(raw, line)) {
        if (!line.empty() && line[0] == '#') continue;
        body += line;
        body += '\n';
    }
    std::istringstream in(body);
    bool ok = true;
    auto parsed = dockParse(in, 0, ok);
    if (!ok || !parsed) return nullptr;
    std::unordered_set<std::string> seen;
    auto pruned = dockPrune(std::move(parsed), valid, seen);
    if (!pruned || !dockContains(pruned.get(), "Viewport")) return nullptr;
    return pruned;
}

// ── 떠 있는 창 (다중 OS 창 M1) ────────────────────────────────────────────────

namespace {

// 메인 트리에 반드시 남아야 하는 패널입니다(dockParseLayout · dockShowPanel 과 같은 이름).
const char* const kMainRequiredPanel = "Viewport";

void dockCollectPanels(const DockNode* n, std::vector<std::string>& out) {
    if (!n) return;
    if (n->isLeaf()) {
        for (const auto& p : n->panelIds) out.push_back(p);
        return;
    }
    for (const auto& c : n->children) dockCollectPanels(c.get(), out);
}

const DockNode* dockFirstLeaf(const DockNode* n) {
    while (n && !n->isLeaf()) n = n->children.empty() ? nullptr : n->children[0].get();
    return n;
}

// 예전 기록을 앞에 두고 others 를 중복 없이 뒤에 붙입니다. 예전 기록(메인에서 뗄 때 남긴 동료)이
// 메인의 원래 자리를 가리키므로, 다시 열 때 그 자리를 먼저 찾게 하려는 순서입니다.
void dockRecordHome(DockPanelHome& home, const std::string& id,
                    const std::vector<std::string>& others) {
    std::vector<std::string> list;
    if (auto it = home.find(id); it != home.end()) list = it->second;
    for (const auto& o : others)
        if (o != id && std::find(list.begin(), list.end(), o) == list.end()) list.push_back(o);
    home[id] = std::move(list);
}

bool dockTabMatches(const DockNode* leaf, int idx, const std::string& panelId) {
    return leaf && leaf->isLeaf() && idx >= 0 && idx < (int)leaf->panelIds.size()
        && leaf->panelIds[idx] == panelId;
}

bool dockIsFLine(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i >= line.size() || line[i] != 'F') return false;
    return i + 1 == line.size() || std::isspace((unsigned char)line[i + 1]);
}

} // namespace

bool dockTreeEmpty(const DockNode* root) {
    return !root || (root->isLeaf() && root->panelIds.empty());
}

bool dockOwns(const DockNode* root, const DockNode* node) {
    if (!root || !node) return false;
    if (root == node) return true;
    int idx = -1;
    return dockFindParent(const_cast<DockNode*>(root), const_cast<DockNode*>(node), idx) != nullptr;
}

int dockNextFloatId(const std::vector<DockFloat>& floats) {
    int next = 1;
    for (const auto& f : floats) next = std::max(next, f.id + 1);
    return next;
}

bool dockContainsAll(const DockNode* mainRoot, const std::vector<DockFloat>& floats,
                     const std::string& id) {
    if (dockContains(mainRoot, id)) return true;
    for (const auto& f : floats) if (dockContains(f.root.get(), id)) return true;
    return false;
}

DockLocation dockFindLeafAll(DockNode* mainRoot, std::vector<DockFloat>& floats,
                             const std::string& id) {
    if (DockNode* l = dockFindLeaf(mainRoot, id)) return { l, -1 };
    for (int i = 0; i < (int)floats.size(); ++i)
        if (DockNode* l = dockFindLeaf(floats[i].root.get(), id)) return { l, i };
    return {};
}

bool dockMoveToFloat(std::unique_ptr<DockNode>& srcRoot, bool srcIsMain, DockNode* src, int srcIdx,
                     const std::string& panelId, bool floatable, DockPanelHome& home,
                     DockFloat& out, bool& srcEmptied) {
    srcEmptied = false;
    if (!floatable || !dockTabMatches(src, srcIdx, panelId)) return false;
    if (!dockOwns(srcRoot.get(), src)) return false;
    if (srcIsMain && panelId == kMainRequiredPanel) return false;

    if (srcIsMain) {
        std::vector<std::string> companions;
        for (const auto& p : src->panelIds) if (p != panelId) companions.push_back(p);
        home[panelId] = std::move(companions);
    }

    dockRemoveTab(src, srcIdx);
    if (src->panelIds.empty()) dockCollapseEmptyLeaf(srcRoot, src);
    srcEmptied = dockTreeEmpty(srcRoot.get());

    out = DockFloat{};
    out.root = DockNode::makeLeaf({ panelId });
    return true;
}

bool dockRedockAcross(std::unique_ptr<DockNode>& srcRoot, bool srcIsMain, DockNode* src, int srcIdx,
                      const std::string& panelId,
                      std::unique_ptr<DockNode>& dstRoot, bool dstIsMain, DockNode* target,
                      EDockZone zone, bool floatable, bool& srcEmptied) {
    srcEmptied = false;
    if (&srcRoot == &dstRoot) return dockRedock(srcRoot, src, srcIdx, panelId, target, zone);
    if (!target || zone == EDockZone::None) return false;
    if (!dockTabMatches(src, srcIdx, panelId)) return false;
    if (!dockOwns(srcRoot.get(), src) || !dockOwns(dstRoot.get(), target)) return false;
    if (zone == EDockZone::Center && !target->isLeaf()) return false;
    if (!dstIsMain && !floatable) return false;
    if (srcIsMain && !dstIsMain && panelId == kMainRequiredPanel) return false;

    dockRemoveTab(src, srcIdx);
    if (zone == EDockZone::Center) {
        target->panelIds.push_back(panelId);
        target->activeTab = (int)target->panelIds.size() - 1;
    } else {
        dockAttachSplit(dstRoot, target, panelId, zone, 0.5f);
    }
    if (src->panelIds.empty()) dockCollapseEmptyLeaf(srcRoot, src);
    srcEmptied = dockTreeEmpty(srcRoot.get());
    return true;
}

bool dockMergeFloatInto(DockFloat& fl, std::unique_ptr<DockNode>& dstRoot, bool dstIsMain,
                        DockNode* target, EDockZone zone, const DockPanelPredicate& floatable) {
    if (dockTreeEmpty(fl.root.get()) || !target || zone == EDockZone::None) return false;
    if (&dstRoot == &fl.root || dockOwns(fl.root.get(), target)) return false;
    if (!dockOwns(dstRoot.get(), target)) return false;
    if (zone == EDockZone::Center && !target->isLeaf()) return false;

    std::vector<std::string> panels;
    dockCollectPanels(fl.root.get(), panels);
    if (!dstIsMain && floatable)
        for (const auto& p : panels) if (!floatable(p)) return false;

    if (zone == EDockZone::Center) {
        std::string activeId;
        if (const DockNode* first = dockFirstLeaf(fl.root.get()))
            if (first->activeTab >= 0 && first->activeTab < (int)first->panelIds.size())
                activeId = first->panelIds[first->activeTab];
        for (const auto& p : panels) {
            target->panelIds.push_back(p);
            if (p == activeId) target->activeTab = (int)target->panelIds.size() - 1;
        }
        fl.root.reset();
    } else {
        // 위에서 target 이 dstRoot 에 속함을 확인했으므로 dockAttachNode 가 일찍 돌아가 트리를 잃는 일은 없습니다.
        dockAttachNode(dstRoot, target, std::move(fl.root), zone, 0.5f);
        fl.root.reset();
    }
    return true;
}

bool dockCloseFloat(DockFloat& fl, DockPanelHome& home, const DockPanelPredicate& closable,
                    std::vector<std::string>* closed) {
    std::vector<std::string> panels;
    dockCollectPanels(fl.root.get(), panels);
    if (closable)
        for (const auto& p : panels) if (!closable(p)) return false;
    for (const auto& p : panels) dockRecordHome(home, p, panels);
    if (closed) closed->insert(closed->end(), panels.begin(), panels.end());
    fl.root.reset();
    return true;
}

bool dockClosePanelInFloat(DockFloat& fl, DockPanelHome& home, const std::string& id,
                           bool closable, bool& emptied) {
    emptied = false;
    if (!closable || !fl.root) return false;
    DockNode* leaf = dockFindLeaf(fl.root.get(), id);
    if (!leaf) return false;

    dockRecordHome(home, id, leaf->panelIds);
    const auto it = std::find(leaf->panelIds.begin(), leaf->panelIds.end(), id);
    dockRemoveTab(leaf, (int)(it - leaf->panelIds.begin()));
    if (leaf->panelIds.empty()) dockCollapseEmptyLeaf(fl.root, leaf);
    emptied = dockTreeEmpty(fl.root.get());
    return true;
}

std::string dockSerializeLayout(const DockNode* mainRoot, const std::vector<DockFloat>& floats) {
    std::ostringstream oss;
    oss << "# Haruka Dock Layout v1\n";
    dockSerialize(mainRoot, oss);
    bool header = false;
    for (const auto& f : floats) {
        if (dockTreeEmpty(f.root.get())) continue;
        // 떠 있는 창이 없으면 이 주석도 쓰지 않아, F 블록 없는 배치는 예전 빌드와 같은 글이 됩니다.
        if (!header) { oss << "# floating windows\n"; header = true; }
        oss << "F " << f.id << ' ' << f.x << ' ' << f.y << ' ' << f.w << ' ' << f.h
            << ' ' << (f.maximized ? 1 : 0) << '\n';
        dockSerialize(f.root.get(), oss);
    }
    return oss.str();
}

DockLayoutData dockParseLayoutAll(const std::string& text, const DockPanelPredicate& valid,
                                  const DockPanelPredicate& floatable) {
    // 주석 줄을 빼고, F 로 시작하는 줄마다 구간을 나눕니다. 메인 트리 구간은 옛 파서가 읽는 글과
    // 같고(옛 파서는 첫 트리 뒤를 보지 않음), 깨진 F 블록이 메인이나 다른 블록을 끌고 가지 않게
    // 블록마다 따로 읽습니다.
    std::istringstream raw(text);
    std::string mainBody, line;
    std::vector<std::pair<std::string, std::string>> blocks;   // {F 줄, 그 창의 트리 글}
    while (std::getline(raw, line)) {
        if (!line.empty() && line[0] == '#') continue;
        if (dockIsFLine(line))   blocks.push_back({ line, std::string() });
        else if (blocks.empty()) { mainBody += line; mainBody += '\n'; }
        else                     { blocks.back().second += line; blocks.back().second += '\n'; }
    }

    DockLayoutData out;
    std::istringstream in(mainBody);
    bool ok = true;
    auto parsed = dockParse(in, 0, ok);
    if (!ok || !parsed) return out;
    std::unordered_set<std::string> seen;   // 모든 트리가 함께 씀(메인이 먼저라 메인이 이김)
    auto pruned = dockPrune(std::move(parsed), valid, seen);
    if (!pruned || !dockContains(pruned.get(), kMainRequiredPanel)) return out;
    out.main = std::move(pruned);

    const DockPanelPredicate floatValid = [&](const std::string& id) {
        return valid(id) && (!floatable || floatable(id));
    };
    std::vector<DockFloat>  kept;
    std::vector<bool>       needId;
    std::unordered_set<int> usedIds;
    for (auto& [header, body] : blocks) {
        std::istringstream hs(header);
        std::string tag;
        int id = 0, x = 0, y = 0, w = 0, h = 0, maxi = 0;
        if (!(hs >> tag >> id >> x >> y >> w >> h >> maxi) || tag != "F"
            || (maxi != 0 && maxi != 1)) continue;   // 머리 줄이 깨지면 이 블록만 버림
        std::istringstream ts(body);
        bool tok = true;
        auto tree = dockParse(ts, 0, tok);
        if (!tok || !tree) continue;                // 트리가 깨지면 이 블록만 버림(seen 도 건드리지 않음)
        auto ptree = dockPrune(std::move(tree), floatValid, seen);
        if (!ptree) continue;                       // 걸러 낸 뒤 빈 창은 버림

        DockFloat f;
        f.id   = id;
        f.root = std::move(ptree);
        // 위치는 그대로 둡니다. 화면 밖 위치를 모니터 작업 영역 안으로 끼우는 일은 모니터 정보가
        // 필요하므로 M3(창을 실제로 만들 때)에서 합니다.
        f.x = x;
        f.y = y;
        f.w = std::clamp(w, kDockFloatMinW, kDockFloatMaxDim);
        f.h = std::clamp(h, kDockFloatMinH, kDockFloatMaxDim);
        f.maximized = (maxi == 1);
        const bool fresh = id >= 1 && usedIds.insert(id).second;
        needId.push_back(!fresh);
        kept.push_back(std::move(f));
    }
    // 번호가 0 이하거나 겹친 창은 패널을 잃지 않게 새 번호를 줍니다(먼저 나온 창이 번호를 가짐).
    int next = 1;
    for (int u : usedIds) next = std::max(next, u + 1);
    for (size_t i = 0; i < kept.size(); ++i)
        if (needId[i]) kept[i].id = next++;
    out.floats = std::move(kept);
    return out;
}
