#include "DetailsPanel.hpp"
#include "EditorCommands.hpp"
#include "ActorPicker.hpp"
#include "DetailsSearch.hpp"
#include "../core/HText.hpp"
#include "../scene/World.hpp"
#include "../scene/Actor.hpp"
#include "../scene/Component.hpp"
#include "../scene/ComponentRegistry.hpp"
#include "../sceneio/PrefabManager.hpp"
#include "../sceneio/PrefabRules.hpp"
#include "../sceneio/PrefabLink.hpp"
#include "PrefabCommands.hpp"
#include "../core/FileUtil.hpp"
#include "../core/ProjectPaths.hpp"
#define GLM_FORCE_RADIANS
#include <glm/gtc/quaternion.hpp>
#include <cstdint>

namespace {

glm::vec3 quatToEulerDeg(const glm::quat& q) {
    return glm::degrees(glm::vec3(glm::pitch(q), glm::yaw(q), glm::roll(q)));
}

std::string rowKey(const void* owner, const std::string& label) {
    return std::to_string((uintptr_t)owner) + "|" + label;
}

} // namespace

void DetailsPanel::init(Deps deps) {
    deps_ = std::move(deps);
    PropertyRowKit::Deps kd;
    kd.ui              = deps_.ui;
    kd.columns         = cols_;
    kd.openCombo       = [this](const std::shared_ptr<HWidget>& a, std::vector<EditorMenuEntry> e, int hi) {
        if (deps_.openCombo) deps_.openCombo(a, std::move(e), hi);
    };
    kd.isComboOpen     = [this](const HWidget* w) { return deps_.isComboOpen && deps_.isComboOpen(w); };
    kd.closeMenu       = [this] { if (deps_.closeMenu) deps_.closeMenu(); };
    kd.focusText       = [this](const std::shared_ptr<HWidget>& w) { if (deps_.focusText) deps_.focusText(w); };
    kd.openAssetPicker = [this](const std::shared_ptr<HWidget>& a, const std::string& t, const std::string& c,
                                std::function<void(const std::string&)> cb) {
        if (deps_.openAssetPicker) deps_.openAssetPicker(a, t, c, std::move(cb));
    };
    kd.browseToAsset   = [this](const std::string& p) { if (deps_.browseToAsset) deps_.browseToAsset(p); };
    kd.openColorPicker = [this](const std::shared_ptr<HWidget>& a, ColorPickerPopup::Target t) {
        if (deps_.openColorPicker) deps_.openColorPicker(a, std::move(t));
    };
    kd.pickActor       = [this](const std::string& title, uint64_t cur, std::function<void(uint64_t)> cb) {
        if (deps_.pickActor) deps_.pickActor(title, cur, std::move(cb));
    };
    kd.actorLabel      = [this](uint64_t g) {
        const std::string none    = HTEXT("Editor.Details", "ActorRef.None", "None").toString();
        const std::string missing = HTEXT("Editor.Details", "ActorRef.Missing", "(missing)").toString();
        return (deps_.ctx && deps_.ctx->world) ? hpick::labelFor(*deps_.ctx->world, g, none, missing) : none;
    };
    kd.onChanged       = [this] { dirty_ = true; };
    rowKit_.init(std::move(kd));
}

std::shared_ptr<HWidget> DetailsPanel::build() {
    // 컴포넌트가 많으면 패널보다 길어지므로 스크롤 상자에 담습니다(DEF-56·57). 예전에는 넘친
    // 칸이 아래 패널 위에 그려졌고, 그 칸은 눌리지도 않았습니다.
    list_   = HNew(SVerticalBox);
    scroll_ = HNew(SScrollBox)->style(EditorUiKit::scrollBoxStyle(s(1.f)))->wheelStep(s(48.f))->content(list_);
    // 속성 검색 칸(H3b, UE 의 디테일 위 검색 칸). 목록 밖이라 목록을 다시 만들어도 그대로이고(포커스 유지), 글이 바뀌면 목록을 다시
    // 만듭니다(맞는 행만). 검색어는 선택이 바뀌어도 남습니다.
    auto search = HReuse(searchKeep_);
    search->focusable(true)
        ->style(EditorUiKit::searchBoxStyle(s(1.f)))
        ->placeholder(HTEXT("Editor.Details", "SearchHint", "Search Details").toString())
        ->edit(&searchEdit_)
        ->focused([this] { return searchFocused_; })
        ->onClick([this] { if (deps_.focusText) deps_.focusText(searchKeep_); })
        ->onEdited([this] { if (searchEdit_.text != shownSearch_) dirty_ = true; })
        ->fontSize(s(12.f));
    auto body = HNew(SVerticalBox)
        ->addSlot(HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ s(6.f), s(4.f) })->content(search),
                  SVerticalBox::ESizeRule::Fixed, s(32.f))
        ->addSlot(HNew(SBorder)->background(EditorUiKit::kBorder), SVerticalBox::ESizeRule::Fixed, 1.f)
        ->addSlot(scroll_, SVerticalBox::ESizeRule::Fill);
    return HNew(SBorder)->background(EditorUiKit::kBgPanel)->content(body);
}

TextInputField DetailsPanel::searchField() {
    TextInputField f{
        searchEdit_,
        searchFocused_,
        [this](glm::vec2 p) { return searchKeep_ && searchKeep_->cachedGeometry().contains(p) && searchKeep_->cachedGeometry().size.x > 0.f; },
        [this] { if (searchEdit_.text != shownSearch_) dirty_ = true; },
    };
    // Esc = 지우고 나감(UE 와 같음). Enter 는 나가기만 합니다(검색어 그대로).
    f.onCancel = [this] { searchEdit_.setText(""); dirty_ = true; };
    return f;
}

bool DetailsPanel::searching() const { return hdsearch::active(shownSearch_); }

bool DetailsPanel::rowMatches(const std::string& shown, const std::string& english) const {
    return hdsearch::matches(shownSearch_, shown, english);
}

void DetailsPanel::flushRebuild() {
    if (!dirty_) return;
    dirty_ = false;
    rebuild();
}

void DetailsPanel::onActorDestroyed(Actor* a) {
    if (actor_ == a) {
        actor_ = nullptr;
        dirty_ = true;
    }
}

bool DetailsPanel::scrollContains(const glm::vec2& p) const {
    return !scroll_ || scroll_->cachedGeometry().contains(p);
}

const DetailsPanel::RowOps* DetailsPanel::assetRowAt(glm::vec2 p) const {
    if (!scroll_ || !scrollContains(p)) return nullptr;
    for (const auto& [w, ops] : rowKit_.rowHits()) {
        auto row = w.lock();
        if (!row || !ops || !ops->setAsset || !row->cachedGeometry().contains(p)) continue;
        return ops.get();
    }
    return nullptr;
}

bool DetailsPanel::canDropAsset(glm::vec2 p, const std::string& path) const {
    const RowOps* r = assetRowAt(p);
    return r && r->acceptsAsset && r->acceptsAsset(path);
}

bool DetailsPanel::dropAsset(glm::vec2 p, const std::string& path) {
    const RowOps* r = assetRowAt(p);
    if (!r || !r->acceptsAsset || !r->acceptsAsset(path) || !r->setAsset) return false;
    const auto set = r->setAsset;   // 넣으면 다시 만들어 r 이 사라질 수 있음
    set(path);
    return true;
}

void DetailsPanel::setDropHover(glm::vec2 p, const std::string& path, bool active) {
    const RowOps* r = active ? assetRowAt(p) : nullptr;
    const std::string row = r ? r->label : std::string();
    const bool ok = r && r->acceptsAsset && r->acceptsAsset(path);
    if (row == dropHoverRow_ && ok == dropHoverOk_) return;
    dropHoverRow_ = row;
    dropHoverOk_  = ok;
    dirty_ = true;
}

std::vector<EditorMenuEntry> DetailsPanel::rowMenuEntries() const {
    return { EditorMenuEntry::cmd("Details.ResetToDefault"), EditorMenuEntry::separator(),
             EditorMenuEntry::cmd("Details.CopyValue"), EditorMenuEntry::cmd("Details.PasteValue") };
}

bool DetailsPanel::openRowMenuAt(glm::vec2 p) {
    if (!deps_.openMenu || !scrollContains(p)) return false;
    auto ops = rowKit_.hitTestRow(p);
    if (!ops) return false;
    menuRow_ = std::move(ops);
    deps_.openMenu(p, rowMenuEntries());
    return true;
}

const DetailsPanel::RowOps* DetailsPanel::findRow(const std::string& label) const {
    return rowKit_.findRow(label);
}

bool DetailsPanel::dumpSetValue(const std::string& label, const std::string& text) {
    const RowOps* r = findRow(label);
    if (!r || !r->paste) return false;
    const bool ok = r->paste(text);
    dirty_ = true;
    return ok;
}

bool DetailsPanel::dumpOpenCombo(const std::string& label) {
    const RowOps* r = findRow(label);
    if (!r || !r->openCombo) return false;
    r->openCombo();
    return true;
}

bool DetailsPanel::dumpOpenRowMenu(const std::string& label) {
    for (const auto& [w, ops] : rowKit_.rowHits()) {
        auto row = w.lock();
        if (!row || !ops || ops->label != label) continue;
        menuRow_ = ops;
        if (!deps_.openMenu) return false;
        const HGeometry& g = row->cachedGeometry();
        deps_.openMenu(g.position + glm::vec2(g.size.x * 0.5f, g.size.y * 0.5f), rowMenuEntries());
        return true;
    }
    return false;
}

bool DetailsPanel::dumpOpenAssetPicker(const std::string& label) {
    const RowOps* r = findRow(label);
    if (!r || !r->openPicker) return false;
    r->openPicker();
    return true;
}

bool DetailsPanel::dumpOpenColorPicker(const std::string& label, bool rgbaDemo) {
    const RowOps* r = findRow(label);
    if (!r || !r->openColor) return false;
    if (!rgbaDemo) { r->openColor(false); return true; }
    // 네 성분 시험(실제 RGBA 속성이 아직 없음): 그 행의 견본 아래에 시험 값으로 엽니다. Undo · 컴포넌트와 무관.
    auto hold = deps_.openColorPicker;
    deps_.openColorPicker = [this, hold](const std::shared_ptr<HWidget>& anchor, ColorPickerPopup::Target) {
        ColorPickerPopup::Target t;
        t.title      = "RGBA (test)";
        t.components = 4;
        t.srgb       = false;
        t.get        = [this] { return demoColor_; };
        t.set        = [this](const glm::vec4& v) { demoColor_ = v; };
        if (hold) hold(anchor, std::move(t));
    };
    r->openColor(true);
    deps_.openColorPicker = hold;
    return true;
}

bool DetailsPanel::dumpDropAsset(const std::string& label, const std::string& path) {
    for (const auto& [w, ops] : rowKit_.rowHits()) {
        auto row = w.lock();
        if (!row || !ops || ops->label != label) continue;
        const HGeometry& g = row->cachedGeometry();
        return dropAsset(g.position + g.size * 0.5f, path);
    }
    return false;
}

bool DetailsPanel::dumpBeginEdit(const std::string& label) {
    const RowOps* r = findRow(label);
    if (!r || !r->beginEdit) return false;
    r->beginEdit();
    return true;
}

void DetailsPanel::rebuild() {
    if (!list_ || !deps_.ctx) return;
    dirty_ = false;   // 지금 다시 만듭니다(직접 부른 경우에도 소진 때 한 번 더 만들지 않게)
    // 디테일이 연 메뉴 · 콤보는 다시 만들 때 닫습니다(그 항목이 붙잡은 칸이 낡음, H3a). 행 판정 목록도 새로 만듭니다.
    if (deps_.closeMenu) deps_.closeMenu();
    // 에셋 고르기 팝업 · 색 선택기(H3b)는 같은 액터를 다시 만들 때는 그대로 둡니다(값을 넣는 함수가 대상이 아직 있는지 봄. 글 칸 바깥 누름
    // 확정이 다시 만들기를 부르는 같은 누름으로 연 팝업이 곧바로 닫히지 않게). 고른 액터가 바뀌면 고르기는 닫고 색 선택기는 확정하고 닫습니다.
    if (deps_.ctx->selected != actor_) {
        if (deps_.closeAssetPicker) deps_.closeAssetPicker();
        if (deps_.confirmColorPicker) deps_.confirmColorPicker();
    }
    rowKit_.beginRebuild(list_);
    menuRow_.reset();
    shownSearch_   = searchEdit_.text;
    searchMatches_ = 0;
    list_->clearSlots();
    // 같은 액터를 다시 그릴 때(컴포넌트 추가·값 토글)는 스크롤 위치를 지키고, 다른 액터로
    // 바뀔 때만 맨 위로 돌립니다.
    if (scroll_ && deps_.ctx->selected != actor_) scroll_->scrollToTop();
    actor_ = deps_.ctx->selected;

    list_->addSlot(
        HNew(SBorder)->background(EditorTheme::transparent)->padding({s(8.f), s(8.f)})
            ->content(HNew(STextBlock)
                ->text(AttrStr([this]() -> std::string {
                    return deps_.ctx->selected ? deps_.ctx->selected->name : "(No Selection)";
                }))
                ->fontSize(s(16.f))->color(deps_.ctx->selected ? EditorUiKit::kTextBody : EditorTheme::textDisabled)
                ->bold()->hAlign(EHAlign::Left)),
        SVerticalBox::ESizeRule::Fixed, s(46.f));

    if (deps_.ctx->selected) {
        // 여러 액터를 고른 경우 디테일은 주 선택(마지막에 고른 것)의 값을 보여 주고, 바꾼 칸은 같은 컴포넌트(타입 · 순번)를
        // 가진 다른 선택 액터에도 같은 값으로 넣습니다(2026-11-17, UE 의 다중 선택 편집). 트랜스폼 · 성분 칸은 바꾼 성분 하나만 넣어
        // 나머지 성분은 액터마다 그대로입니다. 한 번의 편집은 한 번의 Undo 입니다.
        if (deps_.ctx->selection.size() > 1)
            list_->addSlot(
                HNew(SBorder)->background(EditorTheme::selectRow)->padding({s(EditorTheme::space8), s(EditorTheme::space2)})
                    ->content(HNew(STextBlock)
                        ->text(std::to_string(deps_.ctx->selection.size()) + " actors selected (edits apply to all)")
                        ->fontSize(s(12.f))->color(EditorUiKit::kTextBody)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
                    SVerticalBox::ESizeRule::Fixed, s(EditorTheme::rowH));

        list_->addSlot(HNew(SBorder)->background(EditorUiKit::kBorder), SVerticalBox::ESizeRule::Fixed, 1.f);

        Actor& sel = *deps_.ctx->selected;
        overrides_.clear();
        if (hprefablink::instanceRootOf(&sel))
            overrides_ = hprefablink::overridePathsOf(sel, hprefablink::loaderFor(deps_.ctx->world));
        buildPrefabSection(sel);
        buildTransformSection(sel);
        buildComponentsSection(sel);
        // 속성 검색(H3b): 맞는 행이 하나도 없으면 알립니다(UE 와 같음).
        if (hdsearch::showNoMatch(searching(), searchMatches_))
            list_->addSlot(
                HNew(SBorder)->background(EditorTheme::transparent)->padding({ s(EditorTheme::space12), 0.f })
                    ->content(HNew(STextBlock)
                        ->text(HText::format(HTEXT("Editor.Details", "SearchNoMatch", "No properties match '{0}'"),
                                             { shownSearch_ }).toString())
                        ->fontSize(s(12.f))->color(EditorUiKit::kTextDim)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
                SVerticalBox::ESizeRule::Fixed, s(EditorTheme::rowH) + s(8.f));
        buildAddComponentSection(sel);
        buildSavePrefabRow(sel);
    }
    // 편집하던 글 칸의 행이 없어졌으면(선택이 바뀜 · 컴포넌트를 지움) 부수 효과 없이 포커스를 놓습니다.
    if (rowKit_.isStringFocused() && !rowKit_.isStringShown()) {
        if (deps_.releaseTextQuiet) deps_.releaseTextQuiet();
        rowKit_.forgetString();
    }
}

void DetailsPanel::buildTransformSection(Actor& sel) {
    // 함께 바꿀 다른 선택 액터들(주 선택 제외). 디테일은 선택이 바뀌면 다시 만들어지므로 여기서 한 번 떠 둡니다.
    Actor* selPtr = &sel;
    std::vector<Actor*> others;
    for (Actor* a : deps_.ctx->selection) if (a && a != selPtr) others.push_back(a);
    auto txBefore = std::make_shared<std::vector<Transform>>();
    auto txBegin = [this, selPtr, others, txBefore]{
        *deps_.undoSnapshot = selPtr->transform();
        txBefore->clear();
        for (Actor* a : others) txBefore->push_back(a->transform());
    };
    auto txEnd = [this, selPtr, others, txBefore]{
        auto comp = std::make_unique<CompositeCmd>();
        comp->add(std::make_unique<SetTransformCmd>(*deps_.ctx->world, selPtr, *deps_.undoSnapshot, selPtr->transform()));
        selPtr->notifyTransformCommitted();   // 물리 바디 등이 편집 결과를 따라오게
        for (size_t i = 0; i < others.size() && i < txBefore->size(); ++i) {
            comp->add(std::make_unique<SetTransformCmd>(*deps_.ctx->world, others[i], (*txBefore)[i], others[i]->transform()));
            others[i]->notifyTransformCommitted();
        }
        if (comp->cmds.size() == 1) deps_.cmdStack->push(std::move(comp->cmds.front()));
        else                        deps_.cmdStack->push(std::move(comp));
    };
    // 클릭 한 번 편집(기본값 되돌리기 · 붙여넣기): 앞뒤 트랜스폼을 떠서 한 번의 Undo 로 쌓습니다. 바뀐 것이 없으면 쌓지 않습니다.
    auto txChange = [this, selPtr, others](const std::function<void()>& change) {
        const Transform b0 = selPtr->transform();
        std::vector<Transform> bs;
        for (Actor* a : others) bs.push_back(a->transform());
        change();
        auto comp = std::make_unique<CompositeCmd>();
        auto same = [](const Transform& x, const Transform& y) {
            return x.position() == y.position() && x.rotation() == y.rotation() && x.scale() == y.scale();
        };
        if (!same(b0, selPtr->transform())) {
            comp->add(std::make_unique<SetTransformCmd>(*deps_.ctx->world, selPtr, b0, selPtr->transform()));
            selPtr->notifyTransformCommitted();
        }
        for (size_t i = 0; i < others.size(); ++i)
            if (!same(bs[i], others[i]->transform())) {
                comp->add(std::make_unique<SetTransformCmd>(*deps_.ctx->world, others[i], bs[i], others[i]->transform()));
                others[i]->notifyTransformCommitted();
            }
        *deps_.cachedEuler = quatToEulerDeg(selPtr->transform().rotation());
        if (comp->empty()) return;
        if (comp->cmds.size() == 1) deps_.cmdStack->push(std::move(comp->cmds.front()));
        else                        deps_.cmdStack->push(std::move(comp));
        dirty_ = true;
    };

    auto txBody = HNew(SVerticalBox);
    // 성분별 세터는 값을 읽어 한 성분만 바꾸고 세터로 되돌려 넣습니다(필드 직접 쓰기 불가). 다른 선택 액터에는 그 성분만 넣습니다.
    auto setPos = [selPtr, others](int axis, float v) {
        for (Actor* a : others) { glm::vec3 p = a->transform().position(); p[axis] = v; a->setLocalPosition(p); }
        glm::vec3 p = selPtr->transform().position(); p[axis] = v; selPtr->setLocalPosition(p);
    };
    // 다른 액터는 자기 오일러 각에서 그 성분만 바꿉니다.
    auto setRot = [this, selPtr, others](int axis, float v) {
        (*deps_.cachedEuler)[axis] = v;
        selPtr->setLocalRotation(glm::quat(glm::radians(*deps_.cachedEuler)));
        for (Actor* a : others) {
            glm::vec3 e = quatToEulerDeg(a->transform().rotation());
            e[axis] = v;
            a->setLocalRotation(glm::quat(glm::radians(e)));
        }
    };
    auto setScl = [selPtr, others](int axis, float v) {
        for (Actor* a : others) { glm::vec3 s2 = a->transform().scale(); s2[axis] = v; a->setLocalScale(s2); }
        glm::vec3 s2 = selPtr->transform().scale(); s2[axis] = v; selPtr->setLocalScale(s2);
    };

    int txMatches = 0;
    // 행 하나: 성분 칸 + 기본값(위치 0 · 회전 0 · 배율 1, UE 의 트랜스폼 되돌리기와 같음). 붙여넣기 · 되돌리기는 모든 선택 액터에 같은 값.
    auto txRow = [&](const char* id, const HText& name, glm::vec4 nameColor, PropUnit unit, float step,
                     std::function<glm::vec3()> get, std::function<void(int, float)> setAxis, glm::vec3 def) {
        PropDesc pd = makePropVec3(id, step, get, [setAxis](const glm::vec3& v) { for (int i = 0; i < 3; ++i) setAxis(i, v[i]); }, unit)
                          .withDefault(def);
        auto ops = std::make_shared<RowOps>();
        ops->label    = name.toString();
        ops->kind     = PropDesc::Vec3;
        ops->canReset = [pd] { return hprop::canReset(pd); };
        ops->reset    = [pd, txChange] { txChange([&] { hprop::resetToDefault(pd); }); };
        ops->copyText = [pd] { return hprop::valueText(pd); };
        ops->paste    = [pd, txChange](const std::string& t) {
            bool ok = false;
            txChange([&] { ok = hprop::setFromText(pd, t); });
            return ok;
        };
        ops->english  = name.source();
        if (!rowMatches(ops->label, ops->english)) return;   // 속성 검색(H3b)
        ++txMatches;
        auto field = rowKit_.vectorField(pd, [get](int i) { return get()[i]; }, setAxis, txBegin, txEnd);
        rowKit_.addPropRow(ops->label, nameColor, field, ops, txBody.get());
    };

    // 프리팹 인스턴스에서 덮어쓴 값은 이름 색으로 알립니다(D9).
    const glm::vec4 posHdr = overridden("POS") ? EditorTheme::prefabOverride : EditorUiKit::kTextBody;
    const glm::vec4 rotHdr = (overridden("ROT") || overridden("QUAT")) ? EditorTheme::prefabOverride : EditorUiKit::kTextBody;
    const glm::vec4 sclHdr = overridden("SCALE") ? EditorTheme::prefabOverride : EditorUiKit::kTextBody;
    // 단위 종류가 끌기 감도와 표시 자릿수를 정합니다(DEF-46): 위치는 거리, 회전은 각도, 크기는 Size.
    txRow("Location", HTEXT("Editor.Details", "Transform.Location", "Location"), posHdr, PropUnit::Offset, 0.01f,
          [selPtr] { return selPtr->transform().position(); }, setPos, glm::vec3(0.f));
    txRow("Rotation", HTEXT("Editor.Details", "Transform.Rotation", "Rotation"), rotHdr, PropUnit::Angle, 0.5f,
          [this] { return *deps_.cachedEuler; }, setRot, glm::vec3(0.f));
    txRow("Scale", HTEXT("Editor.Details", "Transform.Scale", "Scale"), sclHdr, PropUnit::Size, 0.01f,
          [selPtr] { return selPtr->transform().scale(); }, setScl, glm::vec3(1.f));
    txBody->addSlot(deps_.ui->spacer(), SVerticalBox::ESizeRule::Fixed, s(4.f));

    // 섹션은 리빌드마다 새로 만들어지므로 접힘 상태를 에디터가 기억합니다. 예전에는
    // expanded(true) 고정이라, 접어 두어도 선택을 바꾸면 다시 펼쳐졌습니다(DEF-90).
    // 속성 검색 중(H3b): 맞는 행이 있으면 펼치고 없으면 접습니다(기억한 접힘 상태는 그대로, UE 와 같음).
    const bool search = searching();
    searchMatches_ += txMatches;
    const bool expanded = hdsearch::sectionExpanded(search, txMatches, txExpanded_);
    list_->addSlot(
        HNew(SCollapseSection)->style(EditorUiKit::collapseSectionStyle())
            ->title("TRANSFORM")->body(txBody)->expanded(expanded)
            ->onToggle([this, search](bool open) { if (!search) txExpanded_ = open; })
            ->headerHeight(s(EditorTheme::headerH))->fontSize(s(13.f)),
        SVerticalBox::ESizeRule::Auto);
}

void DetailsPanel::buildComponentHeader(Actor& sel, Component& comp) {
    Actor* selPtr = &sel;
    Component* compPtr = &comp;
    // 컴포넌트 제거(Undo 가능, 2026-10-15). 지우는 순간의 값을 떠 두었다가 원래 자리로 되살립니다.
    std::function<void()> removeFn = [this, selPtr, compPtr] {
        auto cmd = std::make_unique<ComponentPresenceCmd>(ComponentPresenceCmd::Kind::Remove, *deps_.ctx->world, selPtr, compPtr,
                                                          ComponentLoadContext{ deps_.ctx->assets }, [this] { dirty_ = true; });
        cmd->redo();
        deps_.cmdStack->push(std::move(cmd));
    };

    auto enBox = HNew(SCheckBox)
        ->style(EditorUiKit::checkBoxStyle(s(1.f)))
        ->checked([compPtr]{ return compPtr->isEnabled(); })
        ->onClick([this, selPtr, compPtr]{
            const bool before = compPtr->isEnabled();
            compPtr->setEnabled(!before);
            deps_.cmdStack->push(std::make_unique<ComponentEnabledCmd>(*deps_.ctx->world, selPtr, compPtr, before, !before));
            dirty_ = true;
        });
    enBox->setTooltip(HTEXT("Editor.Details", "Tip.Enable", "Enable/Disable Component"));
    auto rmBtn = deps_.ui->iconButton("trash-2", 14.f, EditorTheme::error, "", 12.f, EditorTheme::error, {5.f, 2.f},
                                      EditorTheme::errorBg, EditorTheme::errorHover,
                                      removeFn ? removeFn : std::function<void()>([]{}), "x");
    rmBtn->setTooltip(HTEXT("Editor.Details", "Tip.Remove", "Remove Component"));

    list_->addSlot(
        HNew(SBorder)->background(EditorUiKit::kBgHeader)->padding({s(EditorTheme::space4), s(EditorTheme::space2)})
            ->content(
                HNew(SHorizontalBox)
                    ->addSlot(
                        HNew(STextBlock)->text(comp.typeName())->fontSize(s(12.f))
                            ->color(overridden(std::string(comp.typeName()) + "[" + std::to_string(hcmd::ordinalOf(sel, &comp)) + "]")
                                        ? EditorTheme::prefabOverride : EditorUiKit::kTextHdr)
                            ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                        SHorizontalBox::ESizeRule::Fill)
                    ->addSlot(enBox, SHorizontalBox::ESizeRule::Fixed, s(EditorTheme::space16))
                    ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(EditorTheme::space4))
                    ->addSlot(rmBtn, SHorizontalBox::ESizeRule::Fixed, s(26.f))),
        SVerticalBox::ESizeRule::Fixed, s(EditorTheme::headerH));
}

void DetailsPanel::buildComponentProp(Actor& sel, Component& comp, const PropDesc& pd,
                                      const std::function<void()>& compBegin,
                                      const std::function<void()>& compEnd,
                                      const std::function<void(const std::function<void()>&)>& commitClick,
                                      const std::vector<Component*>& peers) {
    // 다른 액터에 같은 칸 값을 넣습니다(칸 이름 · 종류로 찾음).
    auto toPeers = [peers](hcmd::PropValue e) { for (Component* pc : peers) hcmd::setProp(*pc, e); };
    const std::string lbl = pd.label ? pd.label : "";
    const PropDesc::Kind kind = pd.kind;
    Actor* selPtr = &sel;
    Component* compPtr = &comp;

    // 속성 검색(H3b): 맞지 않는 행은 만들지 않습니다. 컴포넌트 칸 이름은 아직 번역하지 않으므로 보이는 이름 = 영어 원문입니다.
    if (!rowMatches(lbl, lbl)) return;

    PropertyRowKit::RowParams p;
    p.rowKey      = rowKey(compPtr, lbl);
    p.englishName = lbl;
    p.nameColor   = EditorUiKit::kTextBody;
    p.dropHot     = !dropHoverRow_.empty() && dropHoverRow_ == lbl;
    p.dropOk      = dropHoverOk_;
    p.onDragBegin = compBegin;
    p.onDragEnd   = compEnd;
    p.commit      = commitClick;

    if (pd.hasDefault) {
        p.onReset = [this, selPtr, compPtr, peers, lbl, kind] {
            hcmd::resetPropToDefault(*deps_.ctx->world, selPtr, compPtr, peers, lbl, kind, *deps_.cmdStack);
            dirty_ = true;
        };
    }
    if (hprop::writable(pd)) {
        p.onPaste = [pd, commitClick, peers, lbl, kind](const std::string& t) {
            bool ok = false;
            commitClick([&] {
                ok = hprop::setFromText(pd, t);
                if (ok)
                    for (Component* pc : peers) hcmd::withProp(*pc, lbl, kind, [&](const PropDesc& q) { hprop::setFromText(q, t); });
            });
            return ok;
        };
    }

    p.onSetFloat = [pd, lbl, toPeers](float v) {
        pd.setF(v);
        hcmd::PropValue e; e.label = lbl; e.kind = PropDesc::Float; e.f = v;
        toPeers(e);
    };
    p.onSetBool = [lbl, toPeers, setB = pd.setB](bool v) {
        setB(v);
        hcmd::PropValue e; e.label = lbl; e.kind = PropDesc::Toggle; e.b = v;
        toPeers(e);
    };
    p.onSetEnum = [lbl, toPeers, setI = pd.setI](int v) {
        setI(v);
        hcmd::PropValue e; e.label = lbl; e.kind = PropDesc::Enum; e.i = v;
        toPeers(e);
    };
    p.onSetActor = [lbl, toPeers, setG = pd.setG](uint64_t v) {
        setG(v);
        hcmd::PropValue e; e.label = lbl; e.kind = PropDesc::ActorRef; e.g = v;
        toPeers(e);
    };
    p.onSetString = [lbl, toPeers, setS = pd.setS](const std::string& v) {
        setS(v);
        hcmd::PropValue e; e.label = lbl; e.kind = PropDesc::String; e.s = v;
        toPeers(e);
    };
    p.onSetAsset = [this, commitClick, toPeers, lbl, setS = pd.setS, getS = pd.getS,
                    world = deps_.ctx->world, actorId = selPtr->id, compPtr](const std::string& v) {
        if (!setS || !world) return;
        Actor* owner = world->findById(actorId);
        if (!owner || std::none_of(owner->getComponents().begin(), owner->getComponents().end(),
                                   [compPtr](const auto& c) { return c.get() == compPtr; }))
            return;
        commitClick([&] {
            if (!getS || getS() != v) setS(v);
            hcmd::PropValue e; e.label = lbl; e.kind = PropDesc::AssetPath; e.s = v;
            toPeers(e);
        });
        dirty_ = true;
    };
    auto setVecComp = [pd, peers, lbl, kind](int i, float v) {
        glm::vec4 cur = pd.getV();
        cur[std::clamp(i, 0, 3)] = v;
        pd.setV(cur);
        for (Component* pc : peers) hcmd::setPropComponent(*pc, lbl, kind, i, v);
    };
    p.onSetVecComp = setVecComp;
    p.onOpenColor = [this, world = deps_.ctx->world, stack = deps_.cmdStack, actorId = selPtr->id,
                     selPtr, compPtr, peers, pd, setVecComp, lbl](const std::shared_ptr<HWidget>& anchor) {
        if (!deps_.openColorPicker || !world) return;
        const int n = std::clamp(pd.components, 3, 4);
        auto session = std::make_shared<hcmd::PropSession>(hcmd::beginPropSession(*compPtr, peers));
        ColorPickerPopup::Target t;
        t.title      = lbl;
        t.components = n;
        t.srgb       = pd.srgb;
        t.get        = [pd] { return pd.getV(); };
        t.set        = [setVecComp, n](const glm::vec4& v) { for (int i = 0; i < n; ++i) setVecComp(i, v[i]); };
        t.alive      = [world, actorId, compPtr] {
            Actor* a = world->findById(actorId);
            if (!a) return false;
            for (const auto& c : a->getComponents()) if (c.get() == compPtr) return true;
            return false;
        };
        t.commit = [this, world, stack, selPtr, compPtr, peers, session] {
            hcmd::endPropSession(*world, selPtr, compPtr, peers, *session, *stack);
            dirty_ = true;
        };
        t.cancel = [this, compPtr, peers, session] {
            hcmd::cancelPropSession(compPtr, peers, *session);
            dirty_ = true;
        };
        deps_.openColorPicker(anchor, std::move(t));
    };

    rowKit_.buildRow(pd, p);
}

void DetailsPanel::buildComponentsSection(Actor& sel) {
    Actor* selPtr = &sel;
    for (auto& compPtr : sel.getComponents()) {
        Component* comp = compPtr.get();
        list_->addSlot(deps_.ui->spacer(), SVerticalBox::ESizeRule::Fixed, s(8.f));
        buildComponentHeader(sel, *comp);

        // ── Generic property rows via buildProps() ────────────────────────────
        auto props      = std::make_shared<std::vector<PropDesc>>(comp->buildProps());
        // 함께 바꿀 다른 선택 액터의 같은 컴포넌트(타입 · 순번). 각자의 이전 값을 떠 두었다가 한 커맨드로 묶습니다.
        const std::vector<Component*> peers = hcmd::peerComponents(sel, *comp, deps_.ctx->selection);
        // 에셋 경로 칸의 세터가 에셋을 다시 읽을 곳(H3b). 코드로 만든 컴포넌트(콘텐츠 브라우저로 놓은 메시)도 여기서 받습니다.
        const ComponentLoadContext loadCtx{ deps_.ctx->assets };
        comp->bindLoadContext(loadCtx);
        for (Component* pc : peers) pc->bindLoadContext(loadCtx);
        auto snapBefore = std::make_shared<std::vector<hcmd::PropValues>>();   // [0] = 주 선택, [1..] = peers

        // 숫자 칸은 드래그 · 입력을 시작할 때와 확정할 때의 값을 떠서 한 커맨드로 쌓습니다. 커맨드는 컴포넌트를
        // 포인터가 아니라 액터 ID · 타입 · 순번으로 기억합니다(EditorCommands.hpp). 토글 · 열거도 이제 되돌립니다.
        auto captureAll = [comp, peers](std::vector<hcmd::PropValues>& out) {
            out.clear();
            out.push_back(hcmd::captureProps(*comp));
            for (Component* pc : peers) out.push_back(hcmd::captureProps(*pc));
        };
        auto pushChanges = [this, selPtr, comp, peers](std::vector<hcmd::PropValues>& before) {
            auto group = std::make_unique<CompositeCmd>();
            for (size_t k = 0; k < before.size() && k <= peers.size(); ++k) {
                Component* c = k == 0 ? comp : peers[k - 1];
                hcmd::PropValues after = hcmd::captureProps(*c);
                if (after == before[k]) continue;
                group->add(std::make_unique<ComponentPropsCmd>(*deps_.ctx->world, k == 0 ? selPtr : c->owner, c,
                                                               std::move(before[k]), std::move(after)));
            }
            if (group->cmds.size() == 1)  deps_.cmdStack->push(std::move(group->cmds.front()));
            else if (!group->empty())     deps_.cmdStack->push(std::move(group));
        };
        auto compBegin = [captureAll, snapBefore] { captureAll(*snapBefore); };
        auto compEnd = [comp, peers, snapBefore, pushChanges] {
            pushChanges(*snapBefore);
            comp->onPropsCommitted();
            for (Component* pc : peers) pc->onPropsCommitted();
        };
        // 클릭 한 번 편집(토글 · 열거 · 글 확정 · 붙여넣기): 앞뒤 값을 떠서 한 번의 Undo(hcmd::commitPropChange, H3a 에서 옮김).
        auto commitClick = [this, selPtr, comp, peers](const std::function<void()>& change) {
            hcmd::commitPropChange(*deps_.ctx->world, selPtr, comp, peers, change, *deps_.cmdStack);
        };

        const size_t rowsBefore = rowKit_.rowHits().size();
        for (auto& pd : *props)
            buildComponentProp(sel, *comp, pd, compBegin, compEnd, commitClick, peers);
        // 속성 검색(H3b): 맞는 행이 없는 컴포넌트는 머리 줄만 남깁니다(접힘). 읽기 정보 줄은 검색 중에는 숨깁니다.
        if (searching()) searchMatches_ += (int)(rowKit_.rowHits().size() - rowsBefore);
        if (!hdsearch::showInfoLine(searching())) continue;

        std::string info = comp->readOnlyInfo();
        if (!info.empty())
            list_->addSlot(
                HNew(SBorder)->background(EditorTheme::transparent)->padding({s(16.f), s(1.f)})
                    ->content(HNew(STextBlock)->text(info)->fontSize(s(12.f))
                        ->color(EditorUiKit::kTextDim)->hAlign(EHAlign::Left)),
                // 여러 줄이면 줄 수만큼(H3a 전에는 18px 고정이라 3줄 정보가 위 · 아래 행에 겹쳤습니다).
                SVerticalBox::ESizeRule::Fixed, s(18.f) + s(14.4f) * (float)std::count(info.begin(), info.end(), '\n'));
    }
}

void DetailsPanel::buildAddComponentSection(Actor& sel) {
    Actor* selPtr = &sel;
    list_->addSlot(deps_.ui->spacer(), SVerticalBox::ESizeRule::Fixed, s(EditorTheme::space8));

    // Add Component 단추 하나(2026-11-21, Unity 방식). 누르면 그 아래에 검색 팝업이 열립니다(ComponentPicker). 예전에는
    // 등록된 타입마다 "+ 이름" 단추를 패널 폭에 맞춰 여러 줄로 늘어놓았습니다(DEF-22 · DEF-109). 타입이 24개를 넘어
    // 찾기 어려워 검색 · 분류로 바꿨습니다.
    auto btn = deps_.ui->textButton(HTEXT("Editor.Details", "AddComponent", "Add Component").toString(), 13.f, EditorUiKit::kTextBody,
                                    {12.f, 4.f}, EditorTheme::ctlNormal, EditorTheme::ctlHover, {});
    std::weak_ptr<SButton> weak = btn;   // 단추가 자기 자신을 붙잡지 않게
    btn->onClick([this, selPtr, weak] {
        if (auto b = weak.lock(); b && deps_.onOpenComponentPicker) deps_.onOpenComponentPicker(selPtr, b);
    });
    btn->hAlign(EHAlign::Center);
    addCompBtn_ = btn;
    list_->addSlot(
        HNew(SBorder)->background(EditorTheme::transparent)->padding({s(EditorTheme::space24), 0.f})->content(btn),
        SVerticalBox::ESizeRule::Fixed, s(EditorTheme::tabH) + s(4.f));
}

void DetailsPanel::addComponentUndoable(Actor* a, int entryIndex) {
    const auto& entries = ComponentRegistry::get().entries();
    if (!a || entryIndex < 0 || entryIndex >= (int)entries.size()) return;
    const auto& entry = entries[(size_t)entryIndex];
    if (entry.find(*a)) return;   // 한 액터에 같은 타입은 하나
    if (Component* c = entry.add(*a))
        deps_.cmdStack->push(std::make_unique<ComponentPresenceCmd>(ComponentPresenceCmd::Kind::Add, *deps_.ctx->world, a, c,
                                                                    ComponentLoadContext{ deps_.ctx->assets },
                                                                    [this] { dirty_ = true; }));
    if (deps_.logCat) HLOG(*deps_.logCat, Log) << "Added " << entry.editorLabel << " to " << a->name;
    dirty_ = true;
}

void DetailsPanel::buildSavePrefabRow(Actor& sel) {
    Actor* selPtr = &sel;
    list_->addSlot(deps_.ui->spacer(), SVerticalBox::ESizeRule::Fixed, s(EditorTheme::space4));
    list_->addSlot(
        HNew(SBorder)->background(EditorTheme::transparent)->padding({s(EditorTheme::space8), 0.f})
            ->content(
                deps_.ui->textButton("Save as Prefab", 12.f, EditorUiKit::kTextDim, {7.f, 3.f},
                                     EditorTheme::ctlNormal, EditorTheme::ctlHover,
                                     [this, selPtr]{
                                         // 액터 이름이 파일 이름이 됩니다. 폴더 구분자 · 금지 글자 · '..' 이 있으면 저장하지 않습니다(DEF-140).
                                         std::string why;
                                         if (!hprefab::isValidName(selPtr->name, &why)) {
                                             if (deps_.logCat)
                                                 HLOG(*deps_.logCat, Warning) << "Save as Prefab: '" << selPtr->name << "' cannot be a prefab file name ("
                                                                              << why << "). Rename the actor (F2) and try again.";
                                             return;
                                         }
                                         std::string p = std::string(PrefabManager::kDir) + "/" + selPtr->name + PrefabManager::kExt;
                                         std::string text;
                                         if (PrefabManager::save(selPtr, p, &text) && deps_.onPrefabWritten) deps_.onPrefabWritten(p, text);
                                     })),
        SVerticalBox::ESizeRule::Fixed, s(EditorTheme::tabH));
}

bool DetailsPanel::overridden(const std::string& prefix) const {
    for (const auto& o : overrides_)
        if (o.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
}

void DetailsPanel::buildPrefabSection(Actor& sel) {
    Actor* root = hprefablink::instanceRootOf(&sel);
    const bool missing = !sel.prefabLink.source.empty() && !sel.prefabLink.raw.empty();
    if (!root && !missing) return;
    if (!root) root = &sel;
    World* world = deps_.ctx->world;
    Actor* selPtr = &sel;
    const std::string source = root->prefabLink.source;

    list_->addSlot(deps_.ui->sectionHeader("PREFAB", EditorTheme::prefabInstance), SVerticalBox::ESizeRule::Fixed, s(22.f));
    auto line = [&](const std::string& text, glm::vec4 col) {
        list_->addSlot(
            HNew(SBorder)->background(EditorTheme::transparent)->padding({s(EditorTheme::space12), 0.f})
                ->content(HNew(STextBlock)->text(text)->fontSize(s(12.f))->color(col)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
            SVerticalBox::ESizeRule::Fixed, s(EditorTheme::rowH));
    };
    line(missing ? HText::format(HTEXT("Editor.Details", "Prefab.Missing", "Missing: {0}"), { source }).toString()
                 : HText::format(HTEXT("Editor.Details", "Prefab.Source", "Instance of {0}"), { source }).toString(),
         missing ? EditorTheme::error : EditorUiKit::kTextBody);
    if (missing) {
        line(HTEXT("Editor.Details", "Prefab.MissingKept", "Overrides are kept as written").toString(), EditorUiKit::kTextDim);
        return;
    }
    line(selPtr == root
             ? HText::format(HTEXT("Editor.Details", "Prefab.RootOverrides", "Root, {0} overridden value(s)"), { std::to_string(overrides_.size()) }).toString()
             : HText::format(HTEXT("Editor.Details", "Prefab.Overrides", "{0} overridden value(s)"), { std::to_string(overrides_.size()) }).toString(),
         overrides_.empty() ? EditorUiKit::kTextDim : EditorTheme::prefabOverride);

    // 되돌리기: 이 액터의 값을 원본 값으로(루트는 이름 · 트랜스폼을 지킴, Unity 와 같음). Undo 가능.
    auto revert = [this, world, selPtr, root] {
        std::string base;
        if (!hprefablink::baselineBlock(*selPtr, hprefablink::loaderFor(world), base)) return;
        if (selPtr == root) {
            std::vector<hprefablink::TBlock> b = hprefablink::parseBlocks(base);
            std::vector<hprefablink::TBlock> cur = hprefablink::parseBlocks(hprefablink::actorBlock(*selPtr));
            if (b.empty() || cur.empty()) return;
            b[0].name = cur[0].name;
            for (const char* k : { "POS", "ROT", "QUAT", "SCALE" }) {
                for (auto& kv : b[0].props)
                    if (kv.first == k)
                        for (const auto& ckv : cur[0].props)
                            if (ckv.first == k) kv.second = ckv.second;
            }
            base = hprefablink::writeBlocks(b);
        }
        auto cmd = std::make_unique<ActorStateCmd>(*world, *selPtr, hprefablink::actorBlock(*selPtr), base,
                                                   ComponentLoadContext{ deps_.ctx->assets },
                                                   [this] { dirty_ = true; if (deps_.onPrefabLinksChanged) deps_.onPrefabLinksChanged(); });
        cmd->redo();
        deps_.cmdStack->push(std::move(cmd));
    };
    // 원본에 적용: 이 액터의 덮어쓰기를 원본 프리팹 글에 넣고 저장 → 인스턴스를 다시 펼침(파일 동작이라 Undo 하지 않음).
    auto apply = [this, world, selPtr, root, source] {
        const hprefablink::Loader load = hprefablink::loaderFor(world);
        std::vector<std::string> lines = hprefablink::overrideLinesOf(*selPtr, load);
        if (selPtr == root) {
            // 루트의 이름 · 트랜스폼은 인스턴스마다 다른 값이라 원본에 넣지 않습니다(Unity 의 Apply All 과 같음).
            const std::string pre = "OVERRIDE:" + selPtr->prefabLink.pair.toString() + " ";
            lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const std::string& l) {
                            if (l.rfind(pre, 0) != 0) return false;
                            const std::string path = l.substr(pre.size(), l.find('=') - pre.size());
                            return path == "POS" || path == "ROT" || path == "QUAT" || path == "SCALE" || path == "NAME";
                        }),
                        lines.end());
        }
        if (lines.empty()) return;
        std::string text;
        if (!load(source, text)) return;
        std::vector<std::string> errors;
        const int n = hprefablink::applyToSourceText(text, source, lines, load, &errors);
        if (n <= 0) return;
        std::string err;
        if (!hfile::writeAtomic(hproj::resolve(source), text, &err)) {
            if (deps_.logCat) { HLOG(*deps_.logCat, Error) << "Apply to prefab: cannot write " << source << " (" << err << ")"; }
            return;
        }
        if (deps_.logCat) { HLOG(*deps_.logCat, Log) << "Applied " << n << " override(s) to " << source; }
        if (deps_.onPrefabWritten) deps_.onPrefabWritten(source, text);
    };
    // 링크 끊기: 인스턴스 전체를 보통 액터로(Undo 가능).
    auto unlink = [this, world, root] {
        auto cmd = std::make_unique<PrefabUnlinkCmd>(*world, *root,
                                                     [this] { dirty_ = true; if (deps_.onPrefabLinksChanged) deps_.onPrefabLinksChanged(); });
        cmd->redo();
        deps_.cmdStack->push(std::move(cmd));
    };
    auto btn = [&](const std::string& label, std::function<void()> fn) {
        return deps_.ui->textButton(label, 12.f, EditorUiKit::kTextBody, {7.f, 3.f}, EditorTheme::ctlNormal, EditorTheme::ctlHover,
                                    std::move(fn));
    };
    list_->addSlot(
        HNew(SBorder)->background(EditorTheme::transparent)->padding({s(EditorTheme::space8), 0.f})
            ->content(HNew(SHorizontalBox)
                ->addSlot(btn(HTEXT("Editor.Details", "Prefab.Revert", "Revert").toString(), revert), SHorizontalBox::ESizeRule::Fill)
                ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(EditorTheme::space4))
                ->addSlot(btn(HTEXT("Editor.Details", "Prefab.Apply", "Apply").toString(), apply), SHorizontalBox::ESizeRule::Fill)
                ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(EditorTheme::space4))
                ->addSlot(btn(HTEXT("Editor.Details", "Prefab.Unlink", "Unlink").toString(), unlink), SHorizontalBox::ESizeRule::Fill)),
        SVerticalBox::ESizeRule::Fixed, s(EditorTheme::tabH));
    list_->addSlot(deps_.ui->spacer(), SVerticalBox::ESizeRule::Fixed, s(EditorTheme::space4));
    list_->addSlot(HNew(SBorder)->background(EditorUiKit::kBorder), SVerticalBox::ESizeRule::Fixed, 1.f);
}
