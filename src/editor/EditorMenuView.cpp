#include "EditorMenuView.hpp"
#include "EditorTheme.hpp"

#include <algorithm>

namespace {
// 1x 크기(배율은 s() 로 곱함). 상단 드롭다운 · 오른쪽 메뉴 · 하위 메뉴가 모두 같은 모양입니다(H2b).
constexpr float kRowH     = 26.f;   // 명령 · 하위 메뉴 행
constexpr float kHeadH    = 24.f;   // 소제목 행
constexpr float kSepH     = 9.f;    // 구분선 행
constexpr float kInset    = 4.f;    // 패널 안쪽 여백(위아래 · 좌우)
constexpr float kPadX     = 8.f;    // 행 안 좌우 여백
constexpr float kMarkW    = 22.f;   // 체크 · 아이콘 칸
constexpr float kArrowW   = 14.f;   // 하위 메뉴 화살표 칸
constexpr float kScGap    = 28.f;   // 글과 단축키 사이 최소 간격
constexpr float kFont     = 13.f;
constexpr float kScFont   = 12.f;
constexpr float kHeadFont = 11.f;

// 메뉴 패널: 안의 누름은 모두 메뉴 몫입니다. 꺼진 항목 · 소제목 · 구분선 · 여백을 눌러도 메뉴가 닫히지 않고(UE 와 같음) 밑으로도
// 새지 않습니다. 팝업 층은 내용이 받지 않은 누름을 Consume 배경 누름으로 보아 드롭다운을 닫기 때문입니다(H2a 의 예전 캐처 규칙).
class MenuPanelBox : public SBox {
public:
    HReply onMouseButton(const HPointerEvent& e) override {
        const HReply r = SBox::onMouseButton(e);
        if (!r.handled && cachedGeometry().contains(e.position)) return HReply::Handled();
        return r;
    }
};
}  // namespace

float EditorMenuView::rowHeight(const EditorMenuRow& r) const {
    switch (r.kind) {
        case hmenu::Kind::Heading:   return s(kHeadH);
        case hmenu::Kind::Separator: return s(kSepH);
        default:                     return s(kRowH);
    }
}

int EditorMenuView::levelOf(uint64_t token) const {
    for (size_t i = 0; i < levels_.size(); ++i)
        if (levels_[i].token == token) return (int)i;
    return -1;
}

bool EditorMenuView::shown() const {
    return ctx_ && !levels_.empty() && levels_.front().handle && ctx_->popups().isOpen(levels_.front().handle);
}

std::shared_ptr<HWidget> EditorMenuView::buildPanel(Level& lv, float minWidth) {
    const EditorUiKit& u = *ui_;
    bool anySub = false, anyMark = false;
    float maxLabel = 0.f, maxSc = 0.f, maxHead = 0.f;
    for (const EditorMenuRow& r : lv.rows) {
        if (r.kind == hmenu::Kind::Heading) { maxHead = std::max(maxHead, EditorUiKit::textWidth(r.label, s(kHeadFont))); continue; }
        if (r.kind == hmenu::Kind::Separator) continue;
        anySub  = anySub || r.kind == hmenu::Kind::SubMenu;
        anyMark = anyMark || r.hasCheck || !r.icon.empty();
        maxLabel = std::max(maxLabel, EditorUiKit::textWidth(r.label, s(kFont)));
        if (!r.shortcut.empty()) maxSc = std::max(maxSc, EditorUiKit::textWidth(r.shortcut, s(kScFont)));
    }
    // 체크 · 아이콘 칸은 그 단계에 체크 · 아이콘이 있는 행이 하나라도 있을 때만 둡니다(오른쪽 누름 메뉴처럼 글뿐이면 좁게).
    const float markW = anyMark ? s(kMarkW) : s(4.f);
    float need = s(kInset) * 2.f + s(kPadX) * 2.f + markW + maxLabel + s(4.f);
    if (maxSc > 0.f) need += s(kScGap) + maxSc;
    if (anySub) need += s(kArrowW);
    need = std::max(need, s(kInset) * 2.f + s(kPadX) * 2.f + maxHead + s(4.f));
    const float W = std::max(s(minWidth), need);

    auto list = HNew(SVerticalBox);
    lv.buttons.assign(lv.rows.size(), nullptr);
    lv.rowTop.assign(lv.rows.size(), 0.f);
    float y = s(kInset);
    const uint64_t token = lv.token;
    for (size_t i = 0; i < lv.rows.size(); ++i) {
        const EditorMenuRow& r = lv.rows[i];
        const float h = rowHeight(r);
        lv.rowTop[i] = y;
        y += h;
        if (r.kind == hmenu::Kind::Heading) {
            list->addSlot(HNew(SBorder)->background(EditorTheme::transparent)
                              ->padding(HPadding{ s(kPadX), s(6.f), s(kPadX), 0.f })
                              ->content(HNew(STextBlock)->text(r.label)->fontSize(s(kHeadFont))
                                            ->color(EditorUiKit::kTextDim)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
                          SVerticalBox::ESizeRule::Fixed, h);
            continue;
        }
        if (r.kind == hmenu::Kind::Separator) {
            list->addSlot(HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ s(kPadX), s(4.f) })
                              ->content(HNew(SBorder)->background(EditorTheme::lineDivider)),
                          SVerticalBox::ESizeRule::Fixed, h);
            continue;
        }
        // 체크 · 아이콘 칸(C1): 켜진 체크 항목은 체크 표시, 그 외(꺼진 체크 항목 포함)는 아이콘이 있으면 아이콘을 그립니다.
        std::shared_ptr<HWidget> mark = u.spacer();
        if (r.hasCheck && r.checked) {
            mark = HNew(SBox)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)
                       ->content(u.icon("check", 14.f, EditorUiKit::kAccent, "v", 12.f));
        } else if (!r.icon.empty()) {
            mark = HNew(SBox)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)
                       ->content(u.icon(r.icon, 14.f, EditorUiKit::kTextDim));
        }
        auto content = HNew(SHorizontalBox);
        content->addSlot(mark, SHorizontalBox::ESizeRule::Fixed, markW);
        content->addSlot(HNew(STextBlock)->text(r.label)->fontSize(s(kFont))->color(EditorUiKit::kTextBody)
                             ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                         SHorizontalBox::ESizeRule::Fill);
        if (!r.shortcut.empty())
            content->addSlot(HNew(STextBlock)->text(r.shortcut)->fontSize(s(kScFont))->color(EditorUiKit::kTextDim)
                                 ->hAlign(EHAlign::Right)->vAlign(EVAlign::Center),
                             SHorizontalBox::ESizeRule::Auto);
        if (anySub) {
            std::shared_ptr<HWidget> arrow = u.spacer();
            if (r.kind == hmenu::Kind::SubMenu)
                arrow = HNew(SBox)->hAlign(EHAlign::Right)->vAlign(EVAlign::Center)
                            ->content(u.icon("chevron-right", 12.f, EditorUiKit::kTextDim, ">", 12.f));
            content->addSlot(arrow, SHorizontalBox::ESizeRule::Fixed, s(kArrowW));
        }
        const int row = (int)i;
        auto btn = HNew(SButton)
            ->style(EditorUiKit::buttonStyle(s(1.f)))->cornerRadius(s(3.f))
            ->normalColor(EditorTheme::transparent)->hoveredColor(EditorTheme::selectRow)
            ->pressedColor(EditorTheme::accentPressed)
            ->padding(HPadding{ s(kPadX), 0.f })->hAlign(EHAlign::Fill)
            ->onClick([this, token, row] {
                const int l = levelOf(token);
                if (l >= 0) activate(l, row);
            })
            ->content(content);
        btn->enabled.set(r.enabled);
        lv.buttons[i] = btn;
        list->addSlot(btn, SVerticalBox::ESizeRule::Fixed, h);
    }
    const float H = y + s(kInset);
    lv.size = { W, H };
    lv.panel = HNew(MenuPanelBox)->widthOverride(W)->heightOverride(H)->content(
        HNew(SBorder)
            ->background(EditorTheme::bgPopup)
            ->borderColor(EditorTheme::borderPopup)->borderThickness(EditorTheme::borderThin)
            ->cornerRadius(s(EditorTheme::radiusPopup))
            ->shadow({ 0.f, s(EditorTheme::shadowOffset.y) }, s(EditorTheme::shadowBlur), EditorTheme::shadowColor)
            ->padding(HPadding{ s(kInset), s(kInset) })
            ->content(list));
    return lv.panel;
}

HPopupLayer::Spec EditorMenuView::levelSpec(Level& lv, uint64_t token) {
    HPopupLayer::Spec sp;
    sp.content = lv.panel;
    sp.size    = lv.size;
    sp.onDismissed = [this, token] {
        // 층이 닫음(배경 누름 · 한 번에 하나 · 다른 창 팝업 닫기 · 부모와 함께). 소유자가 닫은 단계는 이미 목록에 없어 무시됩니다.
        const int l = levelOf(token);
        if (l < 0) return;
        levels_.resize((size_t)l);
        if (l == 0) {
            ctx_ = nullptr;
            if (onRootDismissed_) onRootDismissed_();
        }
    };
    return sp;
}

bool EditorMenuView::show(HSlateContext* ctx, std::vector<EditorMenuEntry> entries, const RootSpec& spec, int initialHighlight) {
    if (!ctx || !ui_) { close(); return false; }
    Level lv;
    lv.entries = std::move(entries);
    lv.rows    = hmenu::resolve(lv.entries, reg_);
    if (lv.rows.empty()) { close(); return false; }
    spec_    = spec;
    lv.token = ++nextToken_;
    buildPanel(lv, spec.minWidth);
    const uint64_t token = lv.token;

    HPopupLayer::Spec sp = levelSpec(lv, token);
    sp.anchor           = spec.anchor;
    sp.placement        = spec.placement;
    sp.outside          = spec.outside;
    sp.closeOnLeftOutside  = spec.closeOnLeft;
    sp.closeOnRightOutside = spec.closeOnRight;
    sp.closeOnEscape    = false;   // Esc 는 메뉴 키 처리(한 단계씩)가 맡습니다
    sp.backdropInsetTop = spec.backdropInsetTop;
    sp.anchorWidget     = spec.anchorWidget;

    if (shown() && ctx == ctx_) {
        closeFrom(1);
        const HPopupLayer::Handle h = levels_.front().handle;
        lv.handle = h;
        levels_.front() = std::move(lv);
        ctx_->popups().update(h, std::move(sp));   // 옛 onDismissed 는 버려짐(새 토큰)
    } else {
        close();
        ctx_ = ctx;
        levels_.push_back(std::move(lv));
        const HPopupLayer::Handle h = ctx->pushPopup(std::move(sp));
        if (!h || levelOf(token) != 0) {
            levels_.clear();
            ctx_ = nullptr;
            return false;
        }
        levels_.front().handle = h;
    }
    lastHover_ = { -1e9f, -1e9f };
    if (initialHighlight >= 0) {
        const std::vector<EditorMenuRow>& rows = levels_.front().rows;
        const bool exact = initialHighlight < (int)rows.size() && hmenu::selectable(rows[(size_t)initialHighlight]);
        setHighlight(0, exact ? initialHighlight : hmenu::firstSelectable(rows));
    }
    return true;
}

void EditorMenuView::close() {
    HSlateContext* c = ctx_;
    const HPopupLayer::Handle h = levels_.empty() ? 0 : levels_.front().handle;
    levels_.clear();
    ctx_ = nullptr;
    if (c && h) c->dismissPopup(h);   // 하위 메뉴도 층이 함께 닫음. 알림은 토큰이 없어 무시됨
}

void EditorMenuView::closeFrom(int level) {
    if (level < 0 || level >= (int)levels_.size()) return;
    const HPopupLayer::Handle h = levels_[(size_t)level].handle;
    levels_.resize((size_t)level);
    if (ctx_ && h) ctx_->dismissPopup(h);
}

void EditorMenuView::setHighlight(int level, int row) {
    if (level < 0 || level >= (int)levels_.size()) return;
    Level& lv = levels_[(size_t)level];
    if (lv.highlight >= 0 && lv.highlight < (int)lv.buttons.size() && lv.buttons[(size_t)lv.highlight])
        lv.buttons[(size_t)lv.highlight]->normalColor(EditorTheme::transparent);
    lv.highlight = (row >= 0 && row < (int)lv.rows.size()) ? row : -1;
    if (lv.highlight >= 0 && lv.buttons[(size_t)lv.highlight])
        lv.buttons[(size_t)lv.highlight]->normalColor(EditorTheme::selectRow);
}

bool EditorMenuView::openSub(int level, int row, bool highlightFirst) {
    if (!shown() || level < 0 || level >= (int)levels_.size()) return false;
    {
        const Level& pl = levels_[(size_t)level];
        if (row < 0 || row >= (int)pl.rows.size()) return false;
        const EditorMenuRow& r = pl.rows[(size_t)row];
        if (r.kind != hmenu::Kind::SubMenu || !hmenu::selectable(r) || r.entry < 0) return false;
    }
    closeFrom(level + 1);
    setHighlight(level, row);
    const Level& pl = levels_[(size_t)level];
    HPopupRect placed;
    if (!ctx_->popups().placedRect(pl.handle, placed)) return false;

    Level sub;
    sub.entries   = pl.entries[(size_t)pl.rows[(size_t)row].entry].children;
    sub.rows      = hmenu::resolve(sub.entries, reg_);
    if (sub.rows.empty()) return false;
    sub.token     = ++nextToken_;
    sub.parentRow = row;
    buildPanel(sub, spec_.minWidth);
    const uint64_t token = sub.token;

    HPopupLayer::Spec sp = levelSpec(sub, token);
    // 하위 메뉴의 첫 행이 부모 행과 같은 높이에 오도록 패널 안쪽 여백만큼 올립니다.
    sp.anchor = { { placed.pos.x, placed.pos.y + pl.rowTop[(size_t)row] - s(kInset) },
                  { pl.size.x, rowHeight(pl.rows[(size_t)row]) + s(kInset) * 2.f } };
    sp.placement.side     = HPopupPlacement::Side::Right;
    sp.placement.align    = HPopupPlacement::Align::Start;
    sp.placement.flip     = HPopupPlacement::Flip::Overflow;
    sp.placement.clamp    = true;
    sp.placement.gap      = -s(2.f);
    sp.placement.insetTop = spec_.subInsetTop;
    sp.outside            = HPopupLayer::EOutside::PassThrough;
    sp.closeOnLeftOutside  = false;   // 바깥 누름은 루트가 닫음(하위도 함께)
    sp.closeOnRightOutside = false;
    sp.closeOnEscape      = false;
    sp.parent             = pl.handle;

    levels_.push_back(std::move(sub));
    const HPopupLayer::Handle h = ctx_->pushPopup(std::move(sp));
    const int l = levelOf(token);
    if (!h || l < 0) {
        if (l >= 0) levels_.resize((size_t)l);
        return false;
    }
    levels_[(size_t)l].handle = h;
    if (highlightFirst) setHighlight(l, hmenu::firstSelectable(levels_[(size_t)l].rows));
    return true;
}

void EditorMenuView::activate(int level, int row) {
    if (level < 0 || level >= (int)levels_.size()) return;
    const Level& lv = levels_[(size_t)level];
    if (row < 0 || row >= (int)lv.rows.size()) return;
    const EditorMenuRow& r = lv.rows[(size_t)row];
    if (!hmenu::selectable(r) || r.entry < 0) return;
    const EditorMenuEntry& e = lv.entries[(size_t)r.entry];
    if (e.kind == hmenu::Kind::SubMenu) { openSub(level, row, false); return; }
    // 닫은 뒤 실행합니다(동작이 새 팝업 · 대화상자를 열어도 메뉴가 남지 않게, 예전 메뉴와 같은 순서).
    const std::string id = e.command;
    std::function<void()> fn = e.action;
    if (requestClose_) requestClose_();
    else               close();
    if (!id.empty()) { if (reg_) reg_->execute(id); }
    else if (fn)     fn();
}

void EditorMenuView::hover(const HSlateContext* ctx, glm::vec2 p) {
    if (!shown() || ctx != ctx_) return;
    if (p == lastHover_) return;   // 포인터가 가만히 있으면 키보드 강조를 건드리지 않습니다
    lastHover_ = p;
    for (int l = (int)levels_.size() - 1; l >= 0; --l) {
        Level& lv = levels_[(size_t)l];
        if (!lv.panel || !lv.panel->cachedGeometry().contains(p)) continue;
        int hit = -1;
        for (size_t i = 0; i < lv.buttons.size(); ++i)
            if (lv.buttons[i] && lv.buttons[i]->cachedGeometry().contains(p)) { hit = (int)i; break; }
        if (hit < 0 || !hmenu::selectable(lv.rows[(size_t)hit])) return;   // 소제목 · 구분선 · 꺼진 항목 · 빈 곳: 그대로
        if (lv.highlight != hit) setHighlight(l, hit);
        const bool isSub  = lv.rows[(size_t)hit].kind == hmenu::Kind::SubMenu;
        const bool opened = (int)levels_.size() > l + 1 && levels_[(size_t)l + 1].parentRow == hit;
        if (isSub && !opened) openSub(l, hit, false);
        else if (!isSub)      closeFrom(l + 1);
        return;
    }
}

hmenu::Act EditorMenuView::key(hmenu::Key k, bool inBar) {
    if (!shown()) return hmenu::Act::None;
    const int l = (int)levels_.size() - 1;
    Level& lv = levels_[(size_t)l];
    const EditorMenuRow* hl = (lv.highlight >= 0 && lv.highlight < (int)lv.rows.size()) ? &lv.rows[(size_t)lv.highlight] : nullptr;
    const hmenu::Act a = hmenu::decide(k, (int)levels_.size(), hl, inBar);
    switch (a) {
        case hmenu::Act::MoveUp:   setHighlight(l, hmenu::step(lv.rows, lv.highlight, -1)); break;
        case hmenu::Act::MoveDown: setHighlight(l, hmenu::step(lv.rows, lv.highlight, +1)); break;
        case hmenu::Act::Activate: activate(l, lv.highlight); break;
        case hmenu::Act::OpenSub:  openSub(l, lv.highlight, true); break;
        case hmenu::Act::CloseSub: closeFrom(l); break;
        default: break;
    }
    return a;
}

bool EditorMenuView::activateByMnemonic(char asciiKey) {
    if (!shown() || levels_.empty()) return false;
    const int l = (int)levels_.size() - 1;
    Level& lv = levels_[(size_t)l];
    bool uniqueMatch = false;
    const int row = hmenu::findMnemonicRow(lv.rows, lv.highlight, asciiKey, uniqueMatch);
    if (row < 0) return false;
    if (uniqueMatch) {
        activate(l, row);
    } else {
        setHighlight(l, row);
    }
    return true;
}

bool EditorMenuView::openSubmenuByLabel(const std::string& label) {
    if (!shown()) return false;
    const int l = (int)levels_.size() - 1;
    const std::vector<EditorMenuRow>& rows = levels_[(size_t)l].rows;
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].kind == hmenu::Kind::SubMenu && rows[i].label == label) return openSub(l, (int)i, false);
    return false;
}

bool EditorMenuView::activateByLabel(const std::string& label) {
    if (levels_.empty()) return false;
    const std::vector<EditorMenuRow>& rows = levels_.front().rows;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].label != label || !hmenu::selectable(rows[i])) continue;
        activate(0, (int)i);
        return true;
    }
    return false;
}
