#include "OutlinerPanel.hpp"
#include "../core/Utf8.hpp"
#include "../ui/slate/widgets/SIcon.hpp"
#include "EditorCommands.hpp"
#include "../components/LightComponent.hpp"
#include "../input/HInputSystem.hpp"
#include "../platform/IWindow.hpp"
#include "../scene/Actor.hpp"
#include "../scene/World.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>

void OutlinerPanel::init(Deps deps) {
    deps_ = std::move(deps);
}

std::shared_ptr<HWidget> OutlinerPanel::build() {
    using VR = SVerticalBox;
    using BR = SBorder;

    list_      = HNew(VR);
    searchBox_ = HNew(SSearchBox);
    auto outlinerSearch = searchBox_
        ->style(EditorUiKit::searchBoxStyle(s(1.f)))
        ->placeholder(HTEXT("Editor.Outliner", "SearchHint", "Search...").toString())
        ->edit(&searchEdit_)
        ->focused([this]() -> bool { return searchFocused_; })
        ->onClick([this]{ if (deps_.onSearchClicked) deps_.onSearchClicked(); })
        ->onEdited([this] { dirty_ = true; })
        ->fontSize(s(12.f));

    return HNew(BR)
        ->background(EditorUiKit::kBgPanel)
        ->content(HNew(VR)
            ->addSlot(HNew(BR)->background(EditorTheme::transparent)->padding(HPadding{s(4.f), s(3.f)})
                ->content(outlinerSearch), VR::ESizeRule::Fixed, s(34.f))
            // 목록은 보이는 행만 위젯으로 만드는 창 기반 가상 스크롤이라, 스크롤 상자 대신
            // 휠로 시작 행을 옮기고 다시 만듭니다(DEF-57). 좌버튼을 누른 동안에는 굴려도 움직이지
            // 않습니다. 그동안은 목록 리빌드가 보류되어(DEF-83) 뗄 때 한꺼번에 튀기 때문입니다.
            ->addSlot(HNew(BR)->background(EditorTheme::transparent)->content(list_)
                ->onWheel([this](float d) {
                    if (deps_.ctx->window && deps_.ctx->window->isMouseButtonDown(HInput::MOUSE_BUTTON_LEFT))
                        return true;
                    scrollTop_ = std::max(0, scrollTop_ + (d > 0.f ? -3 : 3));
                    dirty_     = true;
                    return true;
                }), VR::ESizeRule::Fill)
            // 액터 개수 줄. list_ 바깥이라 rebuild 의 clearSlots 와 무관하게
            // 남습니다. 문자열은 rebuild 가 캐시해 두고 여기서는 읽기만 합니다
            // (Attr 은 computeDesiredSize/paint 에서 매 프레임 여러 번 평가됩니다).
            ->addSlot(HNew(BR)->background(EditorUiKit::kBgHeader)->padding(HPadding{s(8.f), s(2.f)})
                ->content(HNew(STextBlock)
                    ->text(AttrStr([this]() -> std::string { return footer_; }))
                    ->fontSize(s(11.f))->color(EditorUiKit::kTextDim)
                    ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
                VR::ESizeRule::Fixed, s(kFooterH)));
}

std::shared_ptr<SDockOverlay> OutlinerPanel::resetDropOverlay() {
    dropOverlay_ = HNew(SDockOverlay);
    return dropOverlay_;
}

void OutlinerPanel::revealActor(Actor* a) {
    if (!a) return;
    for (Actor* p = a->getParent(); p; p = p->getParent())
        collapsed_.erase(p->id);
}

bool OutlinerPanel::flushRebuild() {
    if (!dirty_ || pressHeld_) return false;   // 누르는 동안은 미룹니다(DEF-83)
    rebuild();
    dirty_ = false;
    return true;
}

void OutlinerPanel::checkLayoutSize() {
    // 스플리터·도킹으로 패널 크기가 바뀌면 표시 가능한 행 수와 말줄임 폭이 달라집니다.
    // 4px 로 양자화해 드래그 중 매 프레임 리빌드가 되지 않게 합니다.
    if (list_) {
        const HGeometry& lg = list_->cachedGeometry();
        const glm::ivec2 sig{ (int)(lg.size.x / 4.f), (int)(lg.size.y / 4.f) };
        if (sig != layoutSig_) dirty_ = true;
    }
}

void OutlinerPanel::onActorDestroyed(Actor* a) {
    if (!a) return;
    collapsed_.erase(a->id);
    if (deps_.ctx->selected == a) {
        renaming_ = false;
        renameEdit_.clear();
    }
    dirty_ = true;
}

void OutlinerPanel::resetForWorld() {
    collapsed_.clear();
    rows_.clear();
    scrollTop_ = 0;
    dirty_     = true;
    actorCounter_ = 0;
    if (!deps_.ctx || !deps_.ctx->world) return;
    for (auto& ap : deps_.ctx->world->getActors()) {
        const std::string& nm = ap->name;
        if (nm.size() > 6 && nm.substr(0, 6) == "Actor_") {
            try { actorCounter_ = std::max(actorCounter_, std::stoi(nm.substr(6)) + 1); }
            catch (...) {}
        }
    }
}

void OutlinerPanel::setPressHeldIfContains(glm::vec2 p) {
    if (list_ && list_->cachedGeometry().contains(p))
        pressHeld_ = true;
}

TextInputField OutlinerPanel::searchField() {
    return TextInputField{
        searchEdit_,
        searchFocused_,
        [this](glm::vec2 p) { return searchBox_ && searchBox_->cachedGeometry().contains(p); },
        [this] { dirty_ = true; },
        {},
        {}
    };
}

TextInputField OutlinerPanel::renameField() {
    return TextInputField{
        renameEdit_,
        renaming_,
        [this](glm::vec2 p) { return list_ && list_->cachedGeometry().contains(p); },
        [this] { dirty_ = true; },
        [this]() -> bool {
            Actor* sel = deps_.ctx ? deps_.ctx->selected : nullptr;
            if (!sel || (deps_.isEditorCam && deps_.isEditorCam(sel))) return false;
            renameEdit_.setText(sel->name, true);
            return true;
        },
        [this] {
            Actor* sel = deps_.ctx ? deps_.ctx->selected : nullptr;
            // 빈 이름은 적용하지 않되 편집은 끝냅니다. 예전에는 버퍼가 비면 Enter 가
            // 아무 일도 하지 않아 확정도 탈출도 못 하는 상태에 갇혔습니다.
            const std::string& newName = renameEdit_.text;
            if (!newName.empty() && sel && sel->name != newName) {
                deps_.cmdStack->push(std::make_unique<RenameActorCmd>(*deps_.ctx->world, sel, sel->name, newName));
                sel->name = newName;
            }
        }
    };
}

// Type 라벨은 컴포넌트에서 유도하되, 추가 순서에 흔들리지 않도록 고정 우선순위로
// 하나를 고릅니다. 색은 컴포넌트 형별 아이콘 색(ComponentVisualizers)을 재사용해서,
// 아이콘 아틀라스 없이도 타입이 눈에 구분되게 합니다(타입 아이콘은 설계상 보류).
std::string OutlinerPanel::typeLabel(const Actor* a, glm::vec4& outColor) const {
    outColor = EditorUiKit::kTextDim;
    if (!a) return "Actor";

    int         bestRank     = 99;
    int         contributors = 0;
    std::string label;
    glm::vec4   color = EditorUiKit::kTextDim;

    for (const auto& comp : a->getComponents()) {
        const std::string tn = comp->typeName();
        int         rank = 6;
        std::string lbl;
        if (tn == "StaticMeshComponent")     { rank = 1; lbl = "Mesh"; }
        else if (tn == "LightComponent")     {
            rank = 2;
            const auto* lc = static_cast<const LightComponent*>(comp.get());
            lbl = (lc->lightType == LightType::Point) ? "PointLight"
                : (lc->lightType == LightType::Spot)  ? "SpotLight"
                                                      : "DirLight";
        }
        else if (tn == "CameraComponent")    { rank = 3; lbl = "Camera"; }
        else if (tn == "AudioComponent")     { rank = 4; lbl = "Audio";  }
        else if (tn == "RigidBodyComponent") { rank = 5; lbl = "Body";   }
        else {
            lbl = tn;
            const std::string suffix = "Component";
            if (lbl.size() > suffix.size() &&
                lbl.compare(lbl.size() - suffix.size(), suffix.size(), suffix) == 0)
                lbl.erase(lbl.size() - suffix.size());
        }
        ++contributors;
        if (rank < bestRank) {
            bestRank = rank;
            label    = lbl;
            glm::vec4 ic;
            color    = (deps_.visualizers && deps_.visualizers->icon(*comp, ic)) ? ic : EditorUiKit::kTextDim;
        }
    }
    if (label.empty()) return "Actor";
    outColor = color;
    return contributors > 1 ? label + "+" : label;   // 다른 컴포넌트도 있다는 표시
}

// 루트부터 전위 순회하며 화면에 그릴 행을 평탄화합니다. 필터가 걸리면 매치도 아니고
// 매치 자손도 없는 서브트리는 out 을 되감아 통째로 버립니다(단일 패스, 순서 보존).
void OutlinerPanel::flatten(const std::string& filt, std::vector<Row>& out) const {
    out.clear();
    if (!deps_.ctx || !deps_.ctx->world) return;
    const bool filtering = !filt.empty();

    auto matches = [&](const Actor* a) {
        return utf8::containsIgnoreCase(a->name, filt);
    };

    std::function<bool(Actor*, int)> visit = [&](Actor* a, int depth) -> bool {
        if (depth > 64) return false;              // 병리적 깊이·사이클 안전 브레이크
        const size_t mine = out.size();
        out.push_back({ a->id, a, depth, false, true, false, !a->activeInHierarchy() });

        std::vector<Actor*> kids = a->getChildren();
        std::sort(kids.begin(), kids.end(),
                  [](const Actor* x, const Actor* y) { return x->id < y->id; });

        // 필터 중에는 접힘을 무시합니다. 그러지 않으면 매치된 자손이 접힌 조상 아래
        // 숨어 검색이 사실상 회귀합니다.
        const bool open = filtering ||
                          collapsed_.find(a->id) == collapsed_.end();
        bool keptChild = false;
        if (open)
            for (Actor* k : kids) keptChild = visit(k, depth + 1) || keptChild;

        const bool selfMatch = !filtering || matches(a);
        if (!selfMatch && !keptChild) { out.resize(mine); return false; }

        out[mine].hasKids  = filtering ? keptChild : !kids.empty();
        out[mine].expanded = open;
        out[mine].dimPath  = filtering && !selfMatch;   // 경로로만 남은 조상
        return true;
    };

    // 루트 판정에서 에디터 카메라는 완전히 제외합니다(rebuild 주석 참고).
    std::vector<Actor*> roots;
    for (const auto& ap : deps_.ctx->world->getActors()) {
        Actor* a = ap.get();
        if (deps_.isEditorCam && deps_.isEditorCam(a)) continue;
        Actor* p = a->getParent();
        if (!p || (deps_.isEditorCam && deps_.isEditorCam(p))) roots.push_back(a);
    }
    // getActors 는 스폰 순서고 getChildren 은 attachTo 순서라 둘 다 표시 순서의
    // 진리원이 못 됩니다(씬 로드는 부모가 자식보다 뒤에 올 수 있음). id 로 정렬합니다.
    std::sort(roots.begin(), roots.end(),
              [](const Actor* x, const Actor* y) { return x->id < y->id; });
    for (Actor* r : roots) visit(r, 0);
}

// 행 하나를 조립합니다. 슬롯은 SHorizontalBox 가 penX 를 단조 누적하므로 서로 겹치지
// 않고, 그래서 화살표·눈·삭제는 이름 버튼의 자식이 아니라 반드시 형제여야 합니다
// (SButton 은 좌클릭을 자식에게 넘기지 않고 항상 Handled 로 끊습니다).
void OutlinerPanel::addRow(const Row& r, float listW) {
    Actor* const   a         = r.actor;
    const uint64_t id        = r.id;
    const bool     sel       = deps_.isSelected && deps_.isSelected(a);
    // 이름이 바뀌는 것은 주 선택 하나이므로 이름 칸도 그 행에만 보입니다. 예전에는 선택된 모든 행이 주 선택의 이름
    // 칸을 보여 주었습니다(DEF-134).
    const bool     ren       = a == deps_.ctx->selected && renaming_;
    const bool     filtering = !searchEdit_.text.empty();

    // 폭 사다리. 고정폭 합이 패널 폭을 넘으면 이름 Fill 슬롯이 0 이 되어 그리기는
    // 넘쳐 보여도 히트테스트가 죽습니다(행 선택 자체가 불가능해짐). 들여쓰기 클램프 →
    // Type 컬럼 강등 → 들여쓰기 제거 순으로 폭을 반납합니다.
    float indent = std::min((float)r.depth * s(kIndent), std::max(0.f, listW * 0.35f));
    float typeW  = s(kTypeW);
    float fixed  = s(4.f) + indent + s(kArrowW) + typeW + s(kEyeW) + s(kDelW);
    if (listW - fixed < s(kMinLabelW)) { fixed -= typeW;  typeW  = 0.f; }
    if (listW - fixed < s(kMinLabelW)) { fixed -= indent; indent = 0.f; }
    const float nameW = std::max(s(kMinLabelW), listW - fixed) - s(18.f);   // 버튼 padding 보정

    // Ctrl+클릭은 선택에 넣고 빼기, Shift+클릭은 주 선택부터 이 행까지(2026-10-15). 하나만 선택된 행을 다시 누르면
    // 선택이 풀리는 것은 개편 전부터의 동작이라 그대로 둡니다.
    auto selectRow = [this, id] {
        renaming_ = false;
        Actor* t = deps_.ctx->world->findById(id);
        if (!t) return;
        const bool ctrl  = deps_.ctx->window && deps_.ctx->window->isCtrlDown();
        const bool shift = deps_.ctx->window && deps_.ctx->window->isShiftDown();
        if (ctrl)       { if (deps_.onToggleSelect) deps_.onToggleSelect(t); }
        else if (shift) { if (deps_.onSelectRange)  deps_.onSelectRange(t); }
        else if (deps_.ctx->selection.size() == 1 && deps_.ctx->selected == t) {
            if (deps_.onSelectOnly) deps_.onSelectOnly(nullptr);
        } else {
            if (deps_.onSelectOnly) deps_.onSelectOnly(t);
        }
        dirty_ = true;   // 여러 행의 강조가 바뀔 수 있음
    };

    glm::vec4   typeCol;
    std::string typeStr = typeLabel(a, typeCol);
    const glm::vec4 nameCol = sel ? EditorTheme::textOnAccent
                                  : (r.dimPath || r.dimOff) ? EditorTheme::textDisabled : EditorUiKit::kTextBody;
    std::shared_ptr<HWidget> nameWidget;
    if (ren) {
        // 이름 바꾸기 칸(글자 체계 T1): 커서 · 선택 · IME 조합 글을 제자리에 그립니다(예전에는 글 뒤에 '|' 를 붙임).
        SEditableText::Style es;
        es.text        = nameCol;
        es.composition = nameCol;
        es.selection   = sel ? EditorTheme::accentHover : EditorTheme::accent;
        es.caret       = nameCol;
        es.caretWidth  = std::max(1.f, std::floor(s(1.f)));
        nameWidget = HNew(SEditableText)->state(&renameEdit_)->focused([this] { return renaming_; })
                         ->fontSize(s(14.f))->textHeight(s(18.f))->style(es);
    } else {
        nameWidget = HNew(STextBlock)->text(deps_.ui->ellipsizeText(a->name, nameW, s(14.f)))->fontSize(s(14.f))
                         ->color(nameCol)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center);
    }

    const char* arrowGlyph = (!r.hasKids || filtering) ? "" : (r.expanded ? "v" : ">");
    auto smallBtn = [&] {
        return HNew(SToggleButton)->style(EditorUiKit::toggleButtonStyle(s(1.f)))
                 ->padding(HPadding{0.f, 0.f})
                 ->isActive([] { return false; })          // 강제 파란 액센트 바 회피
                 ->normalColor(EditorTheme::transparent)
                 ->hoveredColor(EditorTheme::ctlHover);
    };

    // 펼침 · 눈 · 삭제는 아이콘(D3)이고, 아이콘 파일이 없으면 예전 글자(v · > · O · o · - · x)입니다.
    const bool icons = deps_.ui->fonts && deps_.ui->fonts->hasIcon("eye");
    std::shared_ptr<HWidget> arrowContent;
    if (icons && *arrowGlyph)
        arrowContent = HNew(SIcon)->icon(std::string(r.expanded ? "chevron-down" : "chevron-right"))
                           ->size(s(14.f))->color(EditorUiKit::kTextDim);
    else
        arrowContent = HNew(STextBlock)->text(arrowGlyph)->fontSize(s(12.f))->color(EditorUiKit::kTextDim)
                           ->hAlign(EHAlign::Center)->vAlign(EVAlign::Center);
    auto arrowBtn = smallBtn()
        ->content(arrowContent)
        ->onClick((r.hasKids && !filtering)
            ? std::function<void()>([this, id] {
                  if (collapsed_.count(id)) collapsed_.erase(id);
                  else                      collapsed_.insert(id);
                  dirty_ = true;   // 콜백 안에서 리빌드하면 자기 파괴가 됩니다
              })
            : std::function<void()>(selectRow));
    if (r.hasKids && !filtering)
        arrowBtn->setTooltip(r.expanded ? HTEXT("Editor.Outliner", "Tip.Collapse", "Collapse Children")
                                        : HTEXT("Editor.Outliner", "Tip.Expand", "Expand Children"));

    std::shared_ptr<HWidget> eyeContent;
    if (icons) {
        // 켜짐 = eye, 꺼짐 = eye-off(흐림), 자신은 켜졌지만 부모가 꺼짐 = eye(흐림).
        eyeContent = HNew(SIcon)
            ->icon(AttrStr([this, id]() -> std::string {
                Actor* t = deps_.ctx->world ? deps_.ctx->world->findById(id) : nullptr;
                return (t && t->activeSelf()) ? "eye" : "eye-off";
            }))
            ->size(s(14.f))
            ->color(Attr<glm::vec4>([this, id]() -> glm::vec4 {
                Actor* t = deps_.ctx->world ? deps_.ctx->world->findById(id) : nullptr;
                return (t && t->activeInHierarchy()) ? EditorUiKit::kTextDim : EditorTheme::textDisabled;
            }));
    } else {
        eyeContent = HNew(STextBlock)
            ->text(AttrStr([this, id]() -> std::string {
                Actor* t = deps_.ctx->world ? deps_.ctx->world->findById(id) : nullptr;
                if (!t) return "-";
                if (!t->activeSelf())          return "-";
                return t->activeInHierarchy() ? "O" : "o";
            }))
            ->fontSize(s(12.f))->color(r.dimOff ? EditorTheme::textDisabled : EditorUiKit::kTextBody)
            ->hAlign(EHAlign::Center)->vAlign(EVAlign::Center);
    }
    auto eyeBtn = smallBtn()
        ->content(eyeContent)
        ->onClick([this, id] {
            Actor* t = deps_.ctx->world ? deps_.ctx->world->findById(id) : nullptr;
            if (!t || (deps_.isEditorCam && deps_.isEditorCam(t))) return;
            const bool before = t->activeSelf();
            t->setActive(!before);
            deps_.cmdStack->push(std::make_unique<SetActiveCmd>(*deps_.ctx->world, t, before, !before));
            dirty_ = true;
        });
    eyeBtn->setTooltip(HTEXT("Editor.Outliner", "Tip.Visibility", "Toggle Visibility"));

    auto row = HNew(SHorizontalBox)
        ->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, s(4.f) + indent)
        ->addSlot(arrowBtn, SHorizontalBox::ESizeRule::Fixed, s(kArrowW))
        ->addSlot(
            HNew(SButton)
                ->style(EditorUiKit::buttonStyle())
                ->cornerRadius(0.f)
                ->normalColor (EditorTheme::transparent)
                ->hoveredColor(sel ? EditorTheme::selectRowHover : EditorTheme::ctlHover)
                ->pressedColor(EditorTheme::accentPressed)
                ->padding({s(EditorTheme::space8), s(EditorTheme::space2)})
                ->hAlign(EHAlign::Fill)
                ->onClick(selectRow)
                ->content(nameWidget),
            SHorizontalBox::ESizeRule::Fill);

    if (typeW > 0.f)
        row->addSlot(
            smallBtn()
                ->content(HNew(STextBlock)
                    ->text(deps_.ui->ellipsizeText(typeStr, typeW - s(6.f), s(11.f)))
                    ->fontSize(s(11.f))->color(r.dimOff ? EditorTheme::textDisabled : typeCol)
                    ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center))
                ->onClick(selectRow),
            SHorizontalBox::ESizeRule::Fixed, typeW);

    auto delBtn = deps_.ui->iconButton("x", 12.f, EditorTheme::error, "", 12.f, EditorTheme::error, {5.f, 3.f},
                                       EditorTheme::transparent, EditorTheme::errorHover,
                                       [this, id] {
                                           Actor* t = deps_.ctx->world ? deps_.ctx->world->findById(id) : nullptr;
                                           if (!t || (deps_.isEditorCam && deps_.isEditorCam(t))) return;
                                           if (deps_.ctx->selected == t) {
                                               renaming_ = false;
                                               if (deps_.onCancelGizmo) deps_.onCancelGizmo();
                                           }
                                           if (deps_.onDeleteActor) deps_.onDeleteActor(t);
                                       }, "x");
    delBtn->setTooltip(HTEXT("Editor.Outliner", "Tip.Delete", "Delete Actor"));
    row->addSlot(eyeBtn, SHorizontalBox::ESizeRule::Fixed, s(kEyeW))
       ->addSlot(delBtn, SHorizontalBox::ESizeRule::Fixed, s(kDelW));

    auto rowBorder = HNew(SBorder)->background(sel ? EditorUiKit::kSelected : EditorTheme::transparent)
        ->content(row);
    rowHits_.push_back({ id, rowBorder });
    list_->addSlot(rowBorder, SVerticalBox::ESizeRule::Fixed, s(kRowH));
}

void OutlinerPanel::rebuild() {
    if (!list_ || !deps_.ctx || !deps_.ctx->world) return;
    list_->clearSlots();
    rowHits_.clear();

    // + Add Empty Actor button
    list_->addSlot(
        HNew(SBorder)->background(EditorTheme::transparent)->padding({s(6.f), s(4.f)})
            ->content(
                deps_.ui->iconButton("plus", 14.f, EditorTheme::accentHover, HTEXT("Editor.Outliner", "AddEmpty", "Add Empty").toString().c_str(), 13.f, EditorTheme::accentHover,
                                     {6.f, 4.f}, EditorTheme::ctlNormal, EditorTheme::ctlHover,
                                     [this]{
                                         std::string nm = nextActorName();
                                         Actor* a = deps_.ctx->world->spawnActor(nm);
                                         if (deps_.onSelectOnly) deps_.onSelectOnly(a);
                                         if (deps_.onRecordCreation) deps_.onRecordCreation({ deps_.ctx->selected });
                                     }, "+")),
        SVerticalBox::ESizeRule::Fixed, s(kHeaderH));

    // Case-insensitive search filter
    // 대소문자 무시 검색(ASCII 만 접음, 한글 등은 그대로 비교, 글자 체계 T1-4).
    const std::string filter = utf8::asciiLower(searchEdit_.text);
    flatten(filter, rows_);

    const HGeometry& lg = list_->cachedGeometry();
    const float seedW = deps_.seedWidth ? *deps_.seedWidth : 290.f;
    const float listW = (lg.size.x >= 1.f) ? lg.size.x : s(seedW);
    const float listH = (lg.size.y >= 1.f) ? lg.size.y : s(320.f);
    layoutSig_ = { (int)(lg.size.x / 4.f), (int)(lg.size.y / 4.f) };

    rowCap_ = std::max(0, (int)((listH - s(kHeaderH)) / s(kRowH)));
    const int total = (int)rows_.size();

    if (filter != filterPrev_) { scrollTop_ = 0; filterPrev_ = filter; }

    int top = std::clamp(scrollTop_, 0, std::max(0, total - rowCap_));

    if (revealSel_ && deps_.ctx->selected) {     // 선택 행을 창 안으로 끌어옵니다
        for (int i = 0; i < total; ++i) {
            if (rows_[i].actor != deps_.ctx->selected) continue;
            if (i < top)                 top = i;
            else if (i >= top + rowCap_) top = i - rowCap_ + 1;
            break;
        }
    }
    revealSel_ = false;
    scrollTop_ = top;

    const int end = std::min(top + rowCap_, total);
    for (int i = top; i < end; ++i) addRow(rows_[i], listW);

    int actorTotal = 0;
    for (const auto& ap : deps_.ctx->world->getActors())
        if (!deps_.isEditorCam || !deps_.isEditorCam(ap.get())) ++actorTotal;

    char buf[96];
    const bool windowed = total > rowCap_;
    if (!filter.empty() && windowed)
        std::snprintf(buf, sizeof buf, "%d of %d actors  (%d-%d)",
                      total, actorTotal, top + 1, end);
    else if (!filter.empty())
        std::snprintf(buf, sizeof buf, "%d of %d actors", total, actorTotal);
    else if (windowed)
        std::snprintf(buf, sizeof buf, "%d actors  (%d-%d)", actorTotal, top + 1, end);
    else
        std::snprintf(buf, sizeof buf, "%d actor%s", actorTotal, actorTotal == 1 ? "" : "s");
    footer_ = buf;
}

void OutlinerPanel::cancelDrag() {
    dragId_     = 0;
    dragActive_ = false;
    if (dropOverlay_) dropOverlay_->active = false;
}

void OutlinerPanel::tickDrag(float mx, float my, bool leftDown, bool prevLeftDown, bool modalOpen, bool visible) {
    const glm::vec2 m{ mx, my };
    auto rowAt = [&](const glm::vec2& p) -> uint64_t {
        for (const RowHit& h : rowHits_)
            if (h.widget && h.widget->cachedGeometry().contains(p)) return h.id;
        return 0;
    };
    auto hideDrop = [&] { if (dropOverlay_) dropOverlay_->active = false; };

    if (leftDown && !prevLeftDown) {
        dragId_     = 0;
        dragActive_ = false;
        hideDrop();
        if (modalOpen || !visible || !list_ || !list_->cachedGeometry().contains(m)) return;
        if (const uint64_t id = rowAt(m)) { dragId_ = id; dragStart_ = m; }
        return;
    }
    if (leftDown && dragId_) {
        if (!dragActive_ && glm::length(m - dragStart_) > s(6.f)) dragActive_ = true;
        if (!dragActive_ || !dropOverlay_) return;
        const bool inList = list_ && list_->cachedGeometry().contains(m);
        const uint64_t target = inList ? rowAt(m) : 0;
        dropOverlay_->active = inList;
        if (target && target != dragId_) {
            for (const RowHit& h : rowHits_)
                if (h.id == target) dropOverlay_->rect = h.widget->cachedGeometry();
        } else if (inList) {
            const HGeometry& lg = list_->cachedGeometry();
            HGeometry band = lg;
            band.position.y = lg.position.y + lg.size.y - s(6.f);
            band.size.y     = s(6.f);
            dropOverlay_->rect = band;
        }
        return;
    }
    if (!leftDown && dragId_) {
        const uint64_t dragId = dragId_;
        const bool     active = dragActive_;
        dragId_     = 0;
        dragActive_ = false;
        hideDrop();
        if (!active || !deps_.ctx || !deps_.ctx->world) return;
        if (!list_ || !list_->cachedGeometry().contains(m)) return;   // 목록 밖에서 놓으면 취소
        Actor* dragged = deps_.ctx->world->findById(dragId);
        if (!dragged) return;
        const uint64_t targetId = rowAt(m);
        if (targetId == dragId) return;
        Actor* parent = targetId ? deps_.ctx->world->findById(targetId) : nullptr;
        const std::vector<Actor*> movers = (deps_.isSelected && deps_.isSelected(dragged))
            ? deps_.getSelectionRoots() : std::vector<Actor*>{ dragged };
        auto composite = std::make_unique<CompositeCmd>();
        for (Actor* a : movers) {
            Actor* cam = deps_.editorCamActor ? *deps_.editorCamActor : nullptr;
            auto cmd = hcmd::makeReparent(*deps_.ctx->world, a, parent, -1, cam);
            if (!cmd) continue;
            cmd->redo();
            composite->add(std::move(cmd));
        }
        if (composite->empty()) {
            if (deps_.logCat)
                HLOG(*deps_.logCat, Warning) << "Cannot move '" << dragged->name << "' there (an actor cannot go under itself or its child)";
            return;
        }
        if (composite->cmds.size() == 1) deps_.cmdStack->push(std::move(composite->cmds.front()));
        else                             deps_.cmdStack->push(std::move(composite));
        if (parent) collapsed_.erase(parent->id);
        for (Actor* a : movers) revealActor(a);
        dirty_ = true;
        if (deps_.onDetailsDirty) deps_.onDetailsDirty();
    }
}
