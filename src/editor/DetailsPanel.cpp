#include "DetailsPanel.hpp"
#include "EditorCommands.hpp"
#include "ActorPicker.hpp"
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

namespace {

constexpr glm::vec4 kRed   = EditorTheme::axisX;
constexpr glm::vec4 kGreen = EditorTheme::axisY;
constexpr glm::vec4 kBlue  = EditorTheme::axisZ;

glm::vec4 themePropColor(glm::vec4 c) {
    if (c == PropColor::Red)     return EditorTheme::axisX;
    if (c == PropColor::Green)   return EditorTheme::axisY;
    if (c == PropColor::Blue)    return EditorTheme::axisZ;
    if (c == PropColor::Neutral) return EditorTheme::textSecondary;
    return c;
}

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
    scroll_ = HNew(SScrollBox)->style(EditorUiKit::scrollBoxStyle(s(1.f)))->wheelStep(s(48.f))->content(list_);
    return HNew(SBorder)->background(EditorUiKit::kBgPanel)->content(scroll_);
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

void DetailsPanel::addNumRow(const char* lbl, glm::vec4 lc,
                             std::function<float()>     getter,
                             std::function<void(float)> setter,
                             float step, ENumberUnit unit,
                             std::function<void()> bFn,
                             std::function<void()> eFn) {
    list_->addSlot(
        HNew(SHorizontalBox)
            ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(EditorTheme::space12))
            ->addSlot(
                HNew(SNumberField)
                    ->style(EditorUiKit::numberFieldStyle(s(1.f)))
                    ->label(lbl)->labelColor(lc)->fontSize(s(12.f))
                    ->get(std::move(getter))->set(std::move(setter))->step(step)->unit(unit)
                    ->onDragBegin(std::move(bFn))->onDragEnd(std::move(eFn)),
                SHorizontalBox::ESizeRule::Fill)
            ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(EditorTheme::space8)),
        SVerticalBox::ESizeRule::Fixed, s(EditorTheme::rowH));
}

void DetailsPanel::addEnumRow(const char* lbl,
                              const std::vector<const char*>& opts,
                              std::function<int()>    getI,
                              std::function<void(int)> setI) {
    int         idx = getI();
    const char* cur = (idx >= 0 && idx < (int)opts.size()) ? opts[idx] : "?";
    list_->addSlot(
        HNew(SHorizontalBox)
            ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(EditorTheme::space12))
            ->addSlot(
                HNew(STextBlock)->text(lbl)->fontSize(s(12.f))->color(EditorUiKit::kTextBody)
                    ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                SHorizontalBox::ESizeRule::Fill)
            ->addSlot(
                deps_.ui->textButton(cur, 12.f, EditorUiKit::kTextBody, {5.f, 2.f},
                                     EditorTheme::ctlNormal, EditorTheme::ctlHover,
                                     [this, opts, getI, setI]{
                                         setI((getI() + 1) % (int)opts.size());
                                         dirty_ = true;
                                     }),
                SHorizontalBox::ESizeRule::Fixed, s(88.f))
            ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(EditorTheme::space8)),
        SVerticalBox::ESizeRule::Fixed, s(EditorTheme::rowH));
}

// 액터 참조 칸(참조 체계 R-7): 대상 이름(비었으면 None, 못 찾으면 (missing))을 보이는 단추. 누르면 액터 목록에서 고릅니다.
void DetailsPanel::addActorRefRow(const char* lbl,
                                  std::function<uint64_t()> getG,
                                  std::function<void(uint64_t)> setG) {
    const std::string none    = HTEXT("Editor.Details", "ActorRef.None", "None").toString();
    const std::string missing = HTEXT("Editor.Details", "ActorRef.Missing", "(missing)").toString();
    const std::string cur     = deps_.ctx && deps_.ctx->world ? hpick::labelFor(*deps_.ctx->world, getG(), none, missing) : none;
    const std::string title   = lbl ? lbl : "";
    list_->addSlot(
        HNew(SHorizontalBox)
            ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(EditorTheme::space12))
            ->addSlot(
                HNew(STextBlock)->text(lbl)->fontSize(s(12.f))->color(EditorUiKit::kTextBody)
                    ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                SHorizontalBox::ESizeRule::Fill)
            ->addSlot(
                deps_.ui->textButton(cur, 12.f, EditorUiKit::kTextBody, {5.f, 2.f},
                                     EditorTheme::ctlNormal, EditorTheme::ctlHover,
                                     [this, title, getG, setG]{
                                         if (!deps_.pickActor) return;
                                         deps_.pickActor(title, getG(), [this, setG](uint64_t g) {
                                             setG(g);
                                             dirty_ = true;
                                         });
                                     }),
                SHorizontalBox::ESizeRule::Fixed, s(120.f))
            ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(EditorTheme::space8)),
        SVerticalBox::ESizeRule::Fixed, s(EditorTheme::rowH));
}

void DetailsPanel::addBoolRow(const char* lbl,
                              std::function<bool()>    getB,
                              std::function<void(bool)> setB) {
    list_->addSlot(
        HNew(SHorizontalBox)
            ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(EditorTheme::space12))
            ->addSlot(
                HNew(STextBlock)->text(lbl)->fontSize(s(12.f))->color(EditorUiKit::kTextBody)
                    ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                SHorizontalBox::ESizeRule::Fill)
            ->addSlot(
                HNew(SCheckBox)
                    ->style(EditorUiKit::checkBoxStyle(s(1.f)))
                    ->checked(getB)
                    ->onClick([this, getB, setB]{ setB(!getB()); dirty_ = true; }),
                SHorizontalBox::ESizeRule::Fixed, s(EditorTheme::space16))
            ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(EditorTheme::space8)),
        SVerticalBox::ESizeRule::Fixed, s(EditorTheme::rowH));
}

void DetailsPanel::rebuild() {
    if (!list_ || !deps_.ctx) return;
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

    if (!deps_.ctx->selected) return;

    // 여러 액터를 고른 경우 디테일은 주 선택(마지막에 고른 것)의 값을 보여 주고, 바꾼 칸은 같은 컴포넌트(타입 · 순번)를
    // 가진 다른 선택 액터에도 같은 값으로 넣습니다(2026-11-17, UE 의 다중 선택 편집). 트랜스폼은 바꾼 성분 하나만 넣어
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
                ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(EditorTheme::space12))
                ->addSlot(HNew(SNumberField)
                    ->style(EditorUiKit::numberFieldStyle(s(1.f)))
                    ->label(lbl)->labelColor(lc)->fontSize(s(12.f))
                    ->get(std::move(getter))->set(std::move(setter))->step(step)->unit(unit)
                    ->onDragBegin(txBegin)->onDragEnd(txEnd),
                    SHorizontalBox::ESizeRule::Fill)
                ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(EditorTheme::space8)),
            SVerticalBox::ESizeRule::Fixed, s(EditorTheme::rowH));
    };

    // 프리팹 인스턴스에서 덮어쓴 값은 머리 글자 색으로 알립니다(D9).
    const glm::vec4 posHdr = overridden("POS") ? EditorTheme::prefabOverride : EditorUiKit::kTextDim;
    const glm::vec4 rotHdr = (overridden("ROT") || overridden("QUAT")) ? EditorTheme::prefabOverride : EditorUiKit::kTextDim;
    const glm::vec4 sclHdr = overridden("SCALE") ? EditorTheme::prefabOverride : EditorUiKit::kTextDim;
    txBody->addSlot(deps_.ui->sectionHeader("LOCATION", posHdr), SVerticalBox::ESizeRule::Fixed, s(22.f));
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

    txBody->addSlot(deps_.ui->sectionHeader("ROTATION", rotHdr), SVerticalBox::ESizeRule::Fixed, s(22.f));
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

    txBody->addSlot(deps_.ui->sectionHeader("SCALE", sclHdr), SVerticalBox::ESizeRule::Fixed, s(22.f));
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
        HNew(SCollapseSection)->style(EditorUiKit::collapseSectionStyle())
            ->title("TRANSFORM")->body(txBody)->expanded(txExpanded_)
            ->onToggle([this](bool open) { txExpanded_ = open; })
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

void DetailsPanel::buildComponentProp(Actor& /*sel*/, Component& /*comp*/, const PropDesc& pd,
                                      const std::function<void()>& compBegin,
                                      const std::function<void()>& compEnd,
                                      const std::function<void(const std::function<void()>&)>& commitClick,
                                      const std::vector<Component*>& peers) {
    // 다른 액터에 같은 칸 값을 넣습니다(칸 이름 · 종류로 찾음).
    auto toPeers = [peers](hcmd::PropValue e) { for (Component* pc : peers) hcmd::setProp(*pc, e); };
    const std::string lbl = pd.label ? pd.label : "";

    if (pd.kind == PropDesc::Float)
        addNumRow(pd.label, themePropColor(pd.color),
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
    else if (pd.kind == PropDesc::ActorRef)
        addActorRefRow(pd.label, pd.getG,
                       [commitClick, toPeers, lbl, setG = pd.setG](uint64_t v){ commitClick([&]{
                           setG(v);
                           hcmd::PropValue e; e.label = lbl; e.kind = PropDesc::ActorRef; e.g = v;
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
                HNew(SBorder)->background(EditorTheme::transparent)->padding({s(16.f), s(1.f)})
                    ->content(HNew(STextBlock)->text(info)->fontSize(s(12.f))
                        ->color(EditorUiKit::kTextDim)->hAlign(EHAlign::Left)),
                SVerticalBox::ESizeRule::Fixed, s(18.f));
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
