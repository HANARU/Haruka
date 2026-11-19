#include "DetailsPanel.hpp"
#include "EditorCommands.hpp"
#include "../scene/World.hpp"
#include "../scene/Actor.hpp"
#include "../scene/Component.hpp"
#include "../scene/ComponentRegistry.hpp"
#include "../sceneio/PrefabManager.hpp"
#include "../sceneio/PrefabRules.hpp"
#define GLM_FORCE_RADIANS
#include <glm/gtc/quaternion.hpp>

namespace {

constexpr glm::vec4 kRed  { 0.9f, 0.3f, 0.3f, 1.f };
constexpr glm::vec4 kGreen{ 0.3f, 0.9f, 0.3f, 1.f };
constexpr glm::vec4 kBlue { 0.3f, 0.6f, 1.0f, 1.f };

glm::vec3 quatToEulerDeg(const glm::quat& q) {
    return glm::degrees(glm::vec3(glm::pitch(q), glm::yaw(q), glm::roll(q)));
}

// 속성의 단위 종류(core)를 숫자 칸의 단위 종류(UI)로 옮깁니다(DEF-46). 두 계층이 서로를 모르게 둡니다.
ENumberUnit toNumberUnit(PropUnit u) {
    switch (u) {
        case PropUnit::Offset: return ENumberUnit::Offset;
        case PropUnit::Size:   return ENumberUnit::Size;
        case PropUnit::Angle:  return ENumberUnit::Angle;
        case PropUnit::Ratio:  return ENumberUnit::Ratio;
        default:               return ENumberUnit::Scalar;
    }
}

} // namespace

void DetailsPanel::init(Deps deps) {
    deps_ = std::move(deps);
}

std::shared_ptr<HWidget> DetailsPanel::build() {
    // 컴포넌트가 많으면 패널보다 길어지므로 스크롤 상자에 담습니다(DEF-56·57). 예전에는 넘친
    // 칸이 아래 패널 위에 그려졌고, 그 칸은 눌리지도 않았습니다.
    list_   = HNew(SVerticalBox);
    scroll_ = HNew(SScrollBox)->wheelStep(s(48.f))->barWidth(s(4.f))->content(list_);
    return HNew(SBorder)->background(EditorUiKit::kBgPanel)->content(scroll_);
}

void DetailsPanel::flushRebuild() {
    if (!dirty_) return;
    dirty_ = false;
    rebuild();
}

void DetailsPanel::checkWidthChange() {
    // 디테일 패널 폭이 바뀌면 컴포넌트 추가 버튼의 줄 나눔을 다시 합니다(DEF-109, 4px 양자화).
    if (scroll_ && deps_.ctx && deps_.ctx->selected && addCompWidthSig_ >= 0 &&
        (int)(scroll_->cachedGeometry().size.x / 4.f) != addCompWidthSig_)
        dirty_ = true;
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

void DetailsPanel::addNumRow(const char* lbl, glm::vec4 lc,
                             std::function<float()>     getter,
                             std::function<void(float)> setter,
                             float step, ENumberUnit unit,
                             std::function<void()> bFn,
                             std::function<void()> eFn) {
    list_->addSlot(
        HNew(SHorizontalBox)
            ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(12.f))
            ->addSlot(
                HNew(SNumberField)
                    ->label(lbl)->labelColor(lc)->fontSize(s(12.f))
                    ->get(std::move(getter))->set(std::move(setter))->step(step)->unit(unit)
                    ->onDragBegin(std::move(bFn))->onDragEnd(std::move(eFn)),
                SHorizontalBox::ESizeRule::Fill)
            ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(8.f)),
        SVerticalBox::ESizeRule::Fixed, s(26.f));
}

void DetailsPanel::addEnumRow(const char* lbl,
                              const std::vector<const char*>& opts,
                              std::function<int()>    getI,
                              std::function<void(int)> setI) {
    int         idx = getI();
    const char* cur = (idx >= 0 && idx < (int)opts.size()) ? opts[idx] : "?";
    list_->addSlot(
        HNew(SHorizontalBox)
            ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(12.f))
            ->addSlot(
                HNew(STextBlock)->text(lbl)->fontSize(s(12.f))->color(EditorUiKit::kTextBody)
                    ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                SHorizontalBox::ESizeRule::Fill)
            ->addSlot(
                deps_.ui->textButton(cur, 12.f, EditorUiKit::kTextBody, {5.f, 2.f},
                                     {0.22f, 0.22f, 0.28f, 1.f}, {0.35f, 0.35f, 0.40f, 1.f},
                                     [this, opts, getI, setI]{
                                         setI((getI() + 1) % (int)opts.size());
                                         dirty_ = true;
                                     }),
                SHorizontalBox::ESizeRule::Fixed, s(88.f))
            ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(8.f)),
        SVerticalBox::ESizeRule::Fixed, s(26.f));
}

void DetailsPanel::addBoolRow(const char* lbl,
                              std::function<bool()>    getB,
                              std::function<void(bool)> setB) {
    bool v = getB();
    list_->addSlot(
        HNew(SHorizontalBox)
            ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(12.f))
            ->addSlot(
                HNew(STextBlock)->text(lbl)->fontSize(s(12.f))->color(EditorUiKit::kTextBody)
                    ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                SHorizontalBox::ESizeRule::Fill)
            ->addSlot(
                deps_.ui->textButton(v ? "Yes" : "No", 12.f, EditorUiKit::kTextBody, {5.f, 2.f},
                                     v ? glm::vec4{0.18f,0.50f,0.22f,1.f}
                                       : glm::vec4{0.38f,0.18f,0.18f,1.f},
                                     {0.35f, 0.35f, 0.40f, 1.f},
                                     [this, getB, setB]{ setB(!getB()); dirty_ = true; }),
                SHorizontalBox::ESizeRule::Fixed, s(44.f))
            ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(8.f)),
        SVerticalBox::ESizeRule::Fixed, s(26.f));
}

void DetailsPanel::rebuild() {
    if (!list_ || !deps_.ctx) return;
    list_->clearSlots();
    // 같은 액터를 다시 그릴 때(컴포넌트 추가·값 토글)는 스크롤 위치를 지키고, 다른 액터로
    // 바뀔 때만 맨 위로 돌립니다.
    if (scroll_ && deps_.ctx->selected != actor_) scroll_->scrollToTop();
    actor_ = deps_.ctx->selected;

    list_->addSlot(
        HNew(SBorder)->background({0,0,0,0})->padding({s(8.f), s(8.f)})
            ->content(HNew(STextBlock)
                ->text(AttrStr([this]() -> std::string {
                    return deps_.ctx->selected ? deps_.ctx->selected->name : "(No Selection)";
                }))
                ->fontSize(s(16.f))->color(deps_.ctx->selected ? EditorUiKit::kTextBody : EditorUiKit::kTextDim)
                ->hAlign(EHAlign::Left)),
        SVerticalBox::ESizeRule::Fixed, s(46.f));

    if (!deps_.ctx->selected) return;

    // 여러 액터를 고른 경우 디테일은 주 선택(마지막에 고른 것)의 값을 보여 주고, 바꾼 칸은 같은 컴포넌트(타입 · 순번)를
    // 가진 다른 선택 액터에도 같은 값으로 넣습니다(2026-11-17, UE 의 다중 선택 편집). 트랜스폼은 바꾼 성분 하나만 넣어
    // 나머지 성분은 액터마다 그대로입니다. 한 번의 편집은 한 번의 Undo 입니다.
    if (deps_.ctx->selection.size() > 1)
        list_->addSlot(
            HNew(SBorder)->background({0.07f, 0.20f, 0.33f, 1.f})->padding({s(8.f), s(3.f)})
                ->content(HNew(STextBlock)
                    ->text(std::to_string(deps_.ctx->selection.size()) + " actors selected (edits apply to all)")
                    ->fontSize(s(12.f))->color(EditorUiKit::kTextBody)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
            SVerticalBox::ESizeRule::Fixed, s(24.f));

    list_->addSlot(HNew(SBorder)->background(EditorUiKit::kBorder), SVerticalBox::ESizeRule::Fixed, 1.f);

    Actor& sel = *deps_.ctx->selected;
    buildTransformSection(sel);
    buildComponentsSection(sel);
    buildAddComponentSection(sel);
    buildSavePrefabRow(sel);
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

    auto txBody = HNew(SVerticalBox);
    // 단위 종류가 끌기 감도와 표시 자릿수를 정합니다(DEF-46): 위치는 거리, 회전은 각도, 크기는 Size.
    auto addTxRow = [&](const char* lbl, glm::vec4 lc,
                        std::function<float()> getter, std::function<void(float)> setter,
                        float step, ENumberUnit unit) {
        txBody->addSlot(
            HNew(SHorizontalBox)
                ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(12.f))
                ->addSlot(HNew(SNumberField)
                    ->label(lbl)->labelColor(lc)->fontSize(s(12.f))
                    ->get(std::move(getter))->set(std::move(setter))->step(step)->unit(unit)
                    ->onDragBegin(txBegin)->onDragEnd(txEnd),
                    SHorizontalBox::ESizeRule::Fill)
                ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(8.f)),
            SVerticalBox::ESizeRule::Fixed, s(26.f));
    };

    txBody->addSlot(deps_.ui->sectionHeader("LOCATION"), SVerticalBox::ESizeRule::Fixed, s(22.f));
    // 성분별 세터는 값을 읽어 한 성분만 바꾸고 세터로 되돌려 넣습니다(필드 직접 쓰기 불가).
    auto posSetter = [selPtr, others](int axis) {
        return [selPtr, others, axis](float v) {
            for (Actor* a : others) { glm::vec3 p = a->transform().position(); p[axis] = v; a->setLocalPosition(p); }
            glm::vec3 p = selPtr->transform().position(); p[axis] = v; selPtr->setLocalPosition(p);
        };
    };
    addTxRow("X", kRed,   [selPtr]{ return selPtr->transform().position().x; }, posSetter(0), 0.01f, ENumberUnit::Offset);
    addTxRow("Y", kGreen, [selPtr]{ return selPtr->transform().position().y; }, posSetter(1), 0.01f, ENumberUnit::Offset);
    addTxRow("Z", kBlue,  [selPtr]{ return selPtr->transform().position().z; }, posSetter(2), 0.01f, ENumberUnit::Offset);
    txBody->addSlot(deps_.ui->spacer(), SVerticalBox::ESizeRule::Fixed, s(4.f));

    txBody->addSlot(deps_.ui->sectionHeader("ROTATION"), SVerticalBox::ESizeRule::Fixed, s(22.f));
    // 다른 액터는 자기 오일러 각에서 그 성분만 바꿉니다.
    auto setEuler = [this, selPtr, others](int axis, float v) {
        (*deps_.cachedEuler)[axis] = v;
        selPtr->setLocalRotation(glm::quat(glm::radians(*deps_.cachedEuler)));
        for (Actor* a : others) {
            glm::vec3 e = quatToEulerDeg(a->transform().rotation());
            e[axis] = v;
            a->setLocalRotation(glm::quat(glm::radians(e)));
        }
    };
    addTxRow("X", kRed,   [this]{ return deps_.cachedEuler->x; }, [setEuler](float v){ setEuler(0, v); }, 0.5f, ENumberUnit::Angle);
    addTxRow("Y", kGreen, [this]{ return deps_.cachedEuler->y; }, [setEuler](float v){ setEuler(1, v); }, 0.5f, ENumberUnit::Angle);
    addTxRow("Z", kBlue,  [this]{ return deps_.cachedEuler->z; }, [setEuler](float v){ setEuler(2, v); }, 0.5f, ENumberUnit::Angle);
    txBody->addSlot(deps_.ui->spacer(), SVerticalBox::ESizeRule::Fixed, s(4.f));

    txBody->addSlot(deps_.ui->sectionHeader("SCALE"), SVerticalBox::ESizeRule::Fixed, s(22.f));
    auto sclSetter = [selPtr, others](int axis) {
        return [selPtr, others, axis](float v) {
            for (Actor* a : others) { glm::vec3 s2 = a->transform().scale(); s2[axis] = v; a->setLocalScale(s2); }
            glm::vec3 s2 = selPtr->transform().scale(); s2[axis] = v; selPtr->setLocalScale(s2);
        };
    };
    addTxRow("X", kRed,   [selPtr]{ return selPtr->transform().scale().x; }, sclSetter(0), 0.01f, ENumberUnit::Size);
    addTxRow("Y", kGreen, [selPtr]{ return selPtr->transform().scale().y; }, sclSetter(1), 0.01f, ENumberUnit::Size);
    addTxRow("Z", kBlue,  [selPtr]{ return selPtr->transform().scale().z; }, sclSetter(2), 0.01f, ENumberUnit::Size);

    // 섹션은 리빌드마다 새로 만들어지므로 접힘 상태를 에디터가 기억합니다. 예전에는
    // expanded(true) 고정이라, 접어 두어도 선택을 바꾸면 다시 펼쳐졌습니다(DEF-90).
    list_->addSlot(
        HNew(SCollapseSection)->title("TRANSFORM")->body(txBody)->expanded(txExpanded_)
            ->onToggle([this](bool open) { txExpanded_ = open; })
            ->headerHeight(s(28.f))->fontSize(s(13.f)),
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

    bool en = comp.isEnabled();
    list_->addSlot(
        HNew(SBorder)->background(EditorUiKit::kBgHeader)->padding({s(4.f), s(3.f)})
            ->content(
                HNew(SHorizontalBox)
                    ->addSlot(
                        HNew(STextBlock)->text(comp.typeName())->fontSize(s(12.f))
                            ->color(EditorUiKit::kTextHdr)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                        SHorizontalBox::ESizeRule::Fill)
                    ->addSlot(
                        deps_.ui->textButton(en ? "On" : "Off", 12.f, EditorUiKit::kTextBody, {5.f, 2.f},
                                             en ? glm::vec4{0.18f,0.50f,0.22f,1.f}
                                                : glm::vec4{0.38f,0.18f,0.18f,1.f},
                                             {0.35f, 0.35f, 0.40f, 1.f},
                                             [this, selPtr, compPtr]{
                                                 const bool before = compPtr->isEnabled();
                                                 compPtr->setEnabled(!before);
                                                 deps_.cmdStack->push(std::make_unique<ComponentEnabledCmd>(*deps_.ctx->world, selPtr, compPtr, before, !before));
                                                 dirty_ = true;
                                             }),
                        SHorizontalBox::ESizeRule::Fixed, s(42.f))
                    ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(4.f))
                    ->addSlot(
                        deps_.ui->textButton("x", 12.f, {0.9f,0.4f,0.4f,1.f}, {5.f, 2.f},
                                             {0.28f, 0.12f, 0.12f, 1.f}, {0.44f, 0.16f, 0.16f, 1.f},
                                             removeFn ? removeFn : std::function<void()>([]{})),
                        SHorizontalBox::ESizeRule::Fixed, s(26.f))),
        SVerticalBox::ESizeRule::Fixed, s(28.f));
}

void DetailsPanel::buildComponentProp(Actor& /*sel*/, Component& /*comp*/, const PropDesc& pd,
                                      const std::function<void()>& compBegin,
                                      const std::function<void()>& compEnd,
                                      const std::function<void(const std::function<void()>&)>& commitClick,
                                      const std::vector<Component*>& peers) {
    // 다른 액터에 같은 칸 값을 넣습니다(칸 이름 · 종류로 찾음).
    auto toPeers = [peers](hcmd::PropValue e) { for (Component* pc : peers) hcmd::setProp(*pc, e); };
    const std::string lbl = pd.label ? pd.label : "";

    if (pd.kind == PropDesc::Float)
        addNumRow(pd.label, pd.color,
            [pd]{ return pd.getF(); },
            [pd, lbl, toPeers](float v){
                pd.setF(v);
                hcmd::PropValue e; e.label = lbl; e.kind = PropDesc::Float; e.f = v;
                toPeers(e);
            },
            pd.step, toNumberUnit(pd.unit), compBegin, compEnd);
    else if (pd.kind == PropDesc::Toggle)
        addBoolRow(pd.label, pd.getB,
                   [commitClick, toPeers, lbl, setB = pd.setB](bool v){ commitClick([&]{
                       setB(v);
                       hcmd::PropValue e; e.label = lbl; e.kind = PropDesc::Toggle; e.b = v;
                       toPeers(e);
                   }); });
    else if (pd.kind == PropDesc::Enum)
        addEnumRow(pd.label, pd.options, pd.getI,
                   [commitClick, toPeers, lbl, setI = pd.setI](int v){ commitClick([&]{
                       setI(v);
                       hcmd::PropValue e; e.label = lbl; e.kind = PropDesc::Enum; e.i = v;
                       toPeers(e);
                   }); });
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
        auto commitClick = [comp, peers, captureAll, pushChanges](const std::function<void()>& change) {
            std::vector<hcmd::PropValues> before;
            captureAll(before);
            change();
            comp->onPropsCommitted();
            for (Component* pc : peers) pc->onPropsCommitted();
            pushChanges(before);
        };

        for (auto& pd : *props)
            buildComponentProp(sel, *comp, pd, compBegin, compEnd, commitClick, peers);

        std::string info = comp->readOnlyInfo();
        if (!info.empty())
            list_->addSlot(
                HNew(SBorder)->background({0,0,0,0})->padding({s(16.f), s(1.f)})
                    ->content(HNew(STextBlock)->text(info)->fontSize(s(12.f))
                        ->color(EditorUiKit::kTextDim)->hAlign(EHAlign::Left)),
                SVerticalBox::ESizeRule::Fixed, s(18.f));
    }
}

void DetailsPanel::buildAddComponentSection(Actor& sel) {
    Actor* selPtr = &sel;
    list_->addSlot(deps_.ui->spacer(), SVerticalBox::ESizeRule::Fixed, s(8.f));
    list_->addSlot(deps_.ui->sectionHeader("ADD COMPONENT", EditorUiKit::kTextHdr, 13.f),
                   SVerticalBox::ESizeRule::Fixed, s(26.f));

    // 버튼은 패널 폭에 맞춰 여러 줄로 나눕니다(DEF-109). 예전에는 한 줄에 늘어놓아, 기본 창 크기에서 컴포넌트가
    // 없는 액터는 + Audio 가 잘리고 + RigidBody 는 보이지 않아 누를 수 없었습니다. 폭은 직전 배치의 디테일 스크롤
    // 상자 폭에서 스크롤 막대 자리를 늘 뺀 값이라, 막대가 생기고 없어져도 줄 나눔이 바뀌지 않습니다. 패널 폭이
    // 바뀌면 tick 이 디테일을 다시 만듭니다(addCompWidthSig_).
    const float addLeft  = s(8.f), addGap = s(4.f);
    const float addAvail = scroll_ ? scroll_->cachedGeometry().size.x - s(14.f) : 0.f;
    addCompWidthSig_ = scroll_ ? (int)(scroll_->cachedGeometry().size.x / 4.f) : -1;
    std::shared_ptr<SHorizontalBox> addRow;
    float addX = 0.f;
    auto flushAddRow = [&] {
        if (addRow) list_->addSlot(addRow, SVerticalBox::ESizeRule::Fixed, s(34.f));
        addRow.reset();
    };
    auto addCompBtn = [&](const std::string& label, std::function<void()> fn) {
        const float w = EditorUiKit::textWidth(label, s(12.f)) + s(7.f) * 2.f;
        if (addRow && addAvail > 0.f && addX + w > addAvail) flushAddRow();
        if (!addRow) {
            addRow = HNew(SHorizontalBox);
            addRow->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, addLeft);
            addX = addLeft;
        }
        addRow->addSlot(
            deps_.ui->textButton(label, 12.f, EditorUiKit::kAccent, {7.f, 4.f},
                                 {0.14f, 0.14f, 0.18f, 1.f}, {0.26f, 0.26f, 0.32f, 1.f}, std::move(fn)),
            SHorizontalBox::ESizeRule::Auto);
        addRow->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, addGap);
        addX += w + addGap;
    };

    // 컴포넌트 추가 버튼은 ComponentRegistry 에 등록된 타입에서 만듭니다(DEF-22). 예전에는 다섯 종을 손으로
    // 나열해, Layer 2 컴포넌트는 에디터에서 붙일 수 없었습니다. 순서는 등록 순서이고, 이미 붙어 있는 타입과
    // 버튼 라벨이 없는 타입은 건너뜁니다.
    for (const auto& entry : ComponentRegistry::get().entries()) {
        if (entry.editorLabel.empty() || entry.find(sel)) continue;
        auto add = entry.add;
        addCompBtn("+ " + entry.editorLabel, [this, selPtr, add]{
            if (Component* c = add(*selPtr))
                deps_.cmdStack->push(std::make_unique<ComponentPresenceCmd>(ComponentPresenceCmd::Kind::Add, *deps_.ctx->world, selPtr, c,
                                                                            ComponentLoadContext{ deps_.ctx->assets },
                                                                            [this] { dirty_ = true; }));
            dirty_ = true;
        });
    }
    if (!addRow) addRow = HNew(SHorizontalBox);   // 붙일 것이 없어도 예전처럼 빈 줄 하나를 둡니다
    flushAddRow();
}

void DetailsPanel::buildSavePrefabRow(Actor& sel) {
    Actor* selPtr = &sel;
    list_->addSlot(deps_.ui->spacer(), SVerticalBox::ESizeRule::Fixed, s(4.f));
    list_->addSlot(
        HNew(SBorder)->background({0,0,0,0})->padding({s(8.f), s(2.f)})
            ->content(
                deps_.ui->textButton("Save as Prefab", 12.f, EditorUiKit::kTextDim, {7.f, 4.f},
                                     {0.14f, 0.14f, 0.18f, 1.f}, {0.26f, 0.26f, 0.32f, 1.f},
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
                                         PrefabManager::save(selPtr, p);
                                         if (deps_.onPrefabSaved) deps_.onPrefabSaved();
                                     })),
        SVerticalBox::ESizeRule::Fixed, s(32.f));
}
