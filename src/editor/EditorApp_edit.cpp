// 에디터 편집 기본기(2026-10-15): 다중 선택, 여러 액터 삭제, 복사 · 붙여넣기 · 복제, 부모 바꾸기(월드 자세 유지),
// 선택 대상으로 화면 맞추기, 기즈모로 여러 액터 함께 움직이기, 아웃라이너에서 끌어 놓아 부모 바꾸기.
// 모든 편집은 Undo 커맨드로 쌓습니다(EditorCommands.hpp, 액터를 ID 로 기억).
#include "EditorApp.hpp"
#include "EditorCommands.hpp"
#include "../core/World.hpp"
#include "../core/Actor.hpp"
#include "../components/CameraComponent.hpp"
#include "../assets/SceneSerializer.hpp"
#include "../platform/IWindow.hpp"

#include <algorithm>
#include <cfloat>
#include <functional>
#include <set>

static constinit HLogCategory LogEditorEdit{ "Editor", ELogVerbosity::Log };

// ── 선택 ─────────────────────────────────────────────────────────────────────────
void EditorApp::selectOnly(Actor* a) {
    if (a == editorCamActor_) a = nullptr;
    ctx_.selection.clear();
    if (a) ctx_.selection.push_back(a);
    ctx_.selected = a;
}

void EditorApp::toggleSelect(Actor* a) {
    if (!a || a == editorCamActor_) return;
    auto& s = ctx_.selection;
    if (auto it = std::find(s.begin(), s.end(), a); it != s.end()) s.erase(it);
    else                                                              s.push_back(a);
    ctx_.selected = s.empty() ? nullptr : s.back();
}

void EditorApp::addSelect(Actor* a) {
    if (!a || a == editorCamActor_) return;
    auto& s = ctx_.selection;
    s.erase(std::remove(s.begin(), s.end(), a), s.end());
    s.push_back(a);
    ctx_.selected = a;
}

void EditorApp::selectRange(Actor* a) {
    if (!a) return;
    if (!ctx_.selected) { selectOnly(a); return; }
    int from = -1, to = -1;
    for (int i = 0; i < (int)outlinerRows_.size(); ++i) {
        if (outlinerRows_[i].id == ctx_.selected->id) from = i;
        if (outlinerRows_[i].id == a->id)             to   = i;
    }
    if (from < 0 || to < 0) { addSelect(a); return; }
    std::vector<Actor*> range;
    const int step = to >= from ? 1 : -1;
    for (int i = from;; i += step) {
        if (Actor* r = ctx_.world->findById(outlinerRows_[(size_t)i].id))
            if (r != editorCamActor_) range.push_back(r);
        if (i == to) break;
    }
    ctx_.selection = std::move(range);   // 기준(주 선택)부터 a 까지, a 가 마지막
    ctx_.selected  = ctx_.selection.empty() ? nullptr : ctx_.selection.back();
}

bool EditorApp::isSelected(const Actor* a) const {
    return a && std::find(ctx_.selection.begin(), ctx_.selection.end(), a) != ctx_.selection.end();
}

std::vector<Actor*> EditorApp::selectionRoots() const {
    std::vector<Actor*> roots;
    for (Actor* a : ctx_.selection) {
        if (!a || a == editorCamActor_) continue;
        bool ancestorSelected = false;
        for (Actor* p = a->getParent(); p && !ancestorSelected; p = p->getParent())
            ancestorSelected = isSelected(p);
        if (!ancestorSelected) roots.push_back(a);
    }
    return roots;
}

void EditorApp::deleteSelection() {
    const std::vector<Actor*> roots = selectionRoots();
    if (roots.empty() || !ctx_.world || !ctx_.assets) return;
    gizmo_.cancel();
    groupDrag_.clear();
    std::vector<uint64_t> ids;
    for (Actor* r : roots) ids.push_back(r->id);
    selectOnly(nullptr);
    renaming_ = false;
    auto cmd = makePresenceCmd(false, std::move(ids));
    cmd->redo();
    cmdStack_.push(std::move(cmd));
    outlinerDirty_ = true;
}

// ── 복사 · 붙여넣기 · 복제 ────────────────────────────────────────────────────────
// 루트마다 서브트리를 전위 순서(부모가 앞)로 모아 씬 형식 글 하나로 만듭니다.
static std::string makeClip(const std::vector<Actor*>& roots) {
    std::vector<const Actor*> set;
    std::vector<int>          parents;
    std::vector<uint64_t>     rootParents;
    std::function<void(const Actor*, int)> walk = [&](const Actor* a, int parent) {
        const int me = (int)set.size();
        set.push_back(a);
        parents.push_back(parent);
        for (const Actor* c : a->getChildren()) walk(c, me);
    };
    for (Actor* r : roots) {
        rootParents.push_back(r->getParent() ? r->getParent()->id : 0);
        walk(r, -1);
    }
    return hclip::wrap(SceneSerializer::writeActorSet(set, parents), rootParents);
}

std::vector<Actor*> EditorApp::spawnActorSet(const std::string& clip) {
    std::string           setText;
    std::vector<uint64_t> rootParents;
    if (!ctx_.world || !ctx_.assets || !hclip::unwrap(clip, setText, rootParents)) return {};
    std::set<std::string> names;
    for (const auto& ap : ctx_.world->getActors()) names.insert(ap->name);
    const std::vector<Actor*> slots = SceneSerializer::readScene(setText, *ctx_.world, *ctx_.assets);
    std::vector<Actor*> roots;
    for (Actor* a : slots)
        if (a && !a->getParent()) roots.push_back(a);
    for (size_t i = 0; i < roots.size(); ++i) {
        Actor* r = roots[i];
        r->name = hclip::nextCopyName(r->name, names);
        names.insert(r->name);
        // 원래 부모가 이 씬에 있으면 그 아래, 같은 로컬 트랜스폼으로 둡니다(UE · Unity 처럼 원래 자리).
        if (i < rootParents.size() && rootParents[i])
            if (Actor* p = ctx_.world->findById(rootParents[i]); p && p != editorCamActor_) r->attachTo(p);
    }
    return roots;
}

void EditorApp::copySelection() {
    const std::vector<Actor*> roots = selectionRoots();
    if (roots.empty() || !ctx_.window) return;
    ctx_.window->setClipboardText(makeClip(roots));
    HLOG(LogEditorEdit, Log) << "Copied " << roots.size() << (roots.size() == 1 ? " actor" : " actors");
}

void EditorApp::pasteClipboard() {
    if (!ctx_.window) return;
    const std::vector<Actor*> roots = spawnActorSet(ctx_.window->getClipboardText());
    if (roots.empty()) { HLOG(LogEditorEdit, Warning) << "Paste: the clipboard has no Haruka actors"; return; }
    recordActorCreation(roots);
    selectOnly(nullptr);
    for (Actor* r : roots) { addSelect(r); revealActor(r); }
    outlinerRevealSel_ = true;
    outlinerDirty_     = true;
    HLOG(LogEditorEdit, Log) << "Pasted " << roots.size() << (roots.size() == 1 ? " actor" : " actors");
}

void EditorApp::duplicateSelection() {
    const std::vector<Actor*> src = selectionRoots();
    if (src.empty()) return;
    const std::vector<Actor*> roots = spawnActorSet(makeClip(src));
    if (roots.empty()) return;
    recordActorCreation(roots);
    selectOnly(nullptr);
    for (Actor* r : roots) { addSelect(r); revealActor(r); }
    outlinerRevealSel_ = true;
    outlinerDirty_     = true;
    HLOG(LogEditorEdit, Log) << "Duplicated " << roots.size() << (roots.size() == 1 ? " actor" : " actors");
}

// ── 부모 바꾸기 ───────────────────────────────────────────────────────────────────
static std::unique_ptr<ReparentCmd> makeReparent(World& world, Actor* child, Actor* newParent, int index,
                                                 const Actor* editorCam) {
    if (!child || child == editorCam || newParent == editorCam) return nullptr;
    for (Actor* p = newParent; p; p = p->getParent())
        if (p == child) return nullptr;   // 자기 자신이나 자손 아래로는 옮기지 않습니다(사이클)
    Actor* oldParent = child->getParent();
    if (oldParent == newParent && index < 0) return nullptr;
    return std::make_unique<ReparentCmd>(world, child->id,
                                         oldParent ? oldParent->id : 0, child->siblingIndex(), child->transform(),
                                         newParent ? newParent->id : 0, index, hxform::localUnder(*child, newParent));
}

bool EditorApp::reparentUndoable(Actor* child, Actor* newParent, int index) {
    if (!ctx_.world) return false;
    auto cmd = makeReparent(*ctx_.world, child, newParent, index, editorCamActor_);
    if (!cmd) return false;
    cmd->redo();
    cmdStack_.push(std::move(cmd));
    if (newParent) outlinerCollapsed_.erase(newParent->id);
    revealActor(child);
    outlinerRevealSel_ = true;
    outlinerDirty_     = true;
    detailsDirty_      = true;   // 로컬 트랜스폼이 바뀜
    return true;
}

// ── 화면 맞추기(F) ────────────────────────────────────────────────────────────────
void EditorApp::frameSelection() {
    if (!editorCamActor_ || !editorCam_ || ctx_.selection.empty()) return;
    glm::vec3 lo(FLT_MAX), hi(-FLT_MAX);
    for (Actor* a : ctx_.selection) {
        glm::vec3 l(0.f), h(0.f);
        bool has = false;
        for (const auto& c : a->getComponents()) {
            glm::vec3 cl, ch;
            if (c->localBounds(cl, ch)) {
                if (!has) { l = cl; h = ch; has = true; }
                else      { l = glm::min(l, cl); h = glm::max(h, ch); }
            }
        }
        // 몸체가 없는 액터(라이트 · 카메라 · 빈 액터)는 1m 상자로 봅니다.
        if (!has) { l = glm::vec3(-0.5f); h = glm::vec3(0.5f); }
        const glm::mat4 wm = a->worldMatrix();
        for (int k = 0; k < 8; ++k) {
            const glm::vec3 corner{ (k & 1) ? h.x : l.x, (k & 2) ? h.y : l.y, (k & 4) ? h.z : l.z };
            const glm::vec3 w = glm::vec3(wm * glm::vec4(corner, 1.f));
            lo = glm::min(lo, w);
            hi = glm::max(hi, w);
        }
    }
    const glm::vec3     center = (lo + hi) * 0.5f;
    const float         radius = std::max(glm::length(hi - lo) * 0.5f, 0.1f);
    const HViewportRect vr     = viewportFbRect();
    const float         aspect = vr.height > 0.f ? vr.width / vr.height : 1.f;
    const float         dist   = hframe::fitDistance(radius, glm::radians(editorCam_->fovY), aspect) * 1.1f;
    focusFrom_ = editorCamActor_->worldPosition();
    focusTo_   = center - editorCam_->forward() * dist;
    focusT_    = 0.f;
    focusAnim_ = true;
}

// ── 기즈모로 여러 액터 함께 움직이기 ─────────────────────────────────────────────
void EditorApp::beginGroupDrag() {
    groupDrag_.clear();
    Actor* p = ctx_.selected;
    if (!p) return;
    const std::vector<Actor*> roots = selectionRoots();
    // 주 선택의 조상도 선택되어 있으면(주 선택이 루트가 아님) 주 선택만 움직입니다. 조상을 함께 움직이면 주 선택이 두 번 움직입니다.
    if (std::find(roots.begin(), roots.end(), p) == roots.end()) return;
    const Transform& sl     = gizmo_.dragStartTransform();
    const Actor*     parent = p->getParent();
    groupPivotStart_.pos   = parent ? glm::vec3(parent->worldMatrix() * glm::vec4(sl.position(), 1.f)) : sl.position();
    groupPivotStart_.rot   = parent ? parent->worldRotation() * sl.rotation() : sl.rotation();
    groupPivotStart_.scale = parent ? parent->worldScale() * sl.scale() : sl.scale();
    for (Actor* r : roots)
        if (r != p) groupDrag_.push_back({ r->id, hxform::worldPoseOf(*r), r->transform() });
}

void EditorApp::updateGroupDrag() {
    if (groupDrag_.empty() || !ctx_.selected || !ctx_.world) return;
    const hxform::WorldPose now = hxform::worldPoseOf(*ctx_.selected);
    for (const GroupMember& m : groupDrag_) {
        Actor* a = ctx_.world->findById(m.id);
        if (!a) continue;
        const hxform::WorldPose w = hxform::followPivot(groupPivotStart_, now, m.start);
        a->setLocalTransform(hxform::localForWorld(a->getParent(), w.pos, w.rot, w.scale));
    }
}

void EditorApp::commitGizmoDrag(const Transform& before, const Transform& after) {
    if (!ctx_.world || !ctx_.selected) return;
    updateGroupDrag();   // 다른 액터를 주 선택의 마지막 자세에 맞춤
    auto composite = std::make_unique<CompositeCmd>();
    composite->add(std::make_unique<SetTransformCmd>(*ctx_.world, ctx_.selected, before, after));
    for (const GroupMember& m : groupDrag_) {
        Actor* a = ctx_.world->findById(m.id);
        if (!a) continue;
        composite->add(std::make_unique<SetTransformCmd>(*ctx_.world, a, m.startLocal, a->transform()));
        a->notifyTransformCommitted();   // 주 선택은 기즈모가 알립니다
    }
    if (composite->cmds.size() == 1) cmdStack_.push(std::move(composite->cmds.front()));
    else                             cmdStack_.push(std::move(composite));
    groupDrag_.clear();
}

// ── 아웃라이너에서 끌어 놓아 부모 바꾸기 ─────────────────────────────────────────
// 행을 누르고 6px 넘게 끌면 끌기입니다. 다른 행 위에서 놓으면 그 액터의 자식으로, 목록의 빈 곳에서 놓으면 루트로
// 옮깁니다. 끈 행이 선택에 들어 있으면 선택된 루트 전부를, 아니면 그 행 하나를 옮깁니다(UE 아웃라이너와 같음).
void EditorApp::tickOutlinerDrag(float mx, float my, bool leftDown) {
    const glm::vec2 m{ mx, my };
    auto rowAt = [&](const glm::vec2& p) -> uint64_t {
        for (const OutlinerRowHit& h : outlinerRowHits_)
            if (h.widget && h.widget->cachedGeometry().contains(p)) return h.id;
        return 0;
    };
    auto hideDrop = [&] { if (outlinerDropOverlay_) outlinerDropOverlay_->active = false; };

    if (leftDown && !prevLeftDown_) {
        outDragId_     = 0;
        outDragActive_ = false;
        hideDrop();
        if (modal_ || !outlinerVisible_ || !outlinerList_ || !outlinerList_->cachedGeometry().contains(m)) return;
        if (const uint64_t id = rowAt(m)) { outDragId_ = id; outDragStart_ = m; }
        return;
    }
    if (leftDown && outDragId_) {
        if (!outDragActive_ && glm::length(m - outDragStart_) > s(6.f)) outDragActive_ = true;
        if (!outDragActive_ || !outlinerDropOverlay_) return;
        const bool inList = outlinerList_ && outlinerList_->cachedGeometry().contains(m);
        const uint64_t target = inList ? rowAt(m) : 0;
        outlinerDropOverlay_->active = inList;
        if (target && target != outDragId_) {
            for (const OutlinerRowHit& h : outlinerRowHits_)
                if (h.id == target) outlinerDropOverlay_->rect = h.widget->cachedGeometry();
        } else if (inList) {
            // 루트로: 목록 아래쪽에 얇은 띠
            const HGeometry& lg = outlinerList_->cachedGeometry();
            HGeometry band = lg;
            band.position.y = lg.position.y + lg.size.y - s(6.f);
            band.size.y     = s(6.f);
            outlinerDropOverlay_->rect = band;
        }
        return;
    }
    if (!leftDown && outDragId_) {
        const uint64_t dragId = outDragId_;
        const bool     active = outDragActive_;
        outDragId_     = 0;
        outDragActive_ = false;
        hideDrop();
        if (!active || !ctx_.world) return;
        if (!outlinerList_ || !outlinerList_->cachedGeometry().contains(m)) return;   // 목록 밖에서 놓으면 취소
        Actor* dragged = ctx_.world->findById(dragId);
        if (!dragged) return;
        const uint64_t targetId = rowAt(m);
        if (targetId == dragId) return;
        Actor* parent = targetId ? ctx_.world->findById(targetId) : nullptr;
        const std::vector<Actor*> movers = isSelected(dragged) ? selectionRoots() : std::vector<Actor*>{ dragged };
        auto composite = std::make_unique<CompositeCmd>();
        for (Actor* a : movers) {
            auto cmd = makeReparent(*ctx_.world, a, parent, -1, editorCamActor_);
            if (!cmd) continue;
            cmd->redo();
            composite->add(std::move(cmd));
        }
        if (composite->empty()) {
            HLOG(LogEditorEdit, Warning) << "Cannot move '" << dragged->name << "' there (an actor cannot go under itself or its child)";
            return;
        }
        if (composite->cmds.size() == 1) cmdStack_.push(std::move(composite->cmds.front()));
        else                             cmdStack_.push(std::move(composite));
        if (parent) outlinerCollapsed_.erase(parent->id);
        for (Actor* a : movers) revealActor(a);
        outlinerDirty_ = true;
        detailsDirty_  = true;
    }
}
