#include "DockTree.hpp"
#include <algorithm>

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

void dockAttachSplit(std::unique_ptr<DockNode>& root, DockNode* target,
                     const std::string& panelId, EDockZone zone, float share) {
    if (!target || !root) return;
    const bool horiz  = (zone == EDockZone::Left || zone == EDockZone::Right);
    const bool before = (zone == EDockZone::Left || zone == EDockZone::Top);
    const DockNode::Split want = horiz ? DockNode::Split::Horizontal : DockNode::Split::Vertical;

    int pidx = -1;
    DockNode* parent = dockFindParent(root.get(), target, pidx);
    if (!parent && target != root.get()) return;
    auto newLeaf = DockNode::makeLeaf({ panelId });
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
