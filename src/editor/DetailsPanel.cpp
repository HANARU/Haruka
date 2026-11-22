#include "DetailsPanel.hpp"
#include "EditorCommands.hpp"
#include "ActorPicker.hpp"
#include "AssetPickerModel.hpp"
#include "ContentBrowserPanel.hpp"
#include "DetailsSearch.hpp"
#include "../math/Color.hpp"
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

constexpr glm::vec4 kRed   = EditorTheme::axisX;
constexpr glm::vec4 kGreen = EditorTheme::axisY;
constexpr glm::vec4 kBlue  = EditorTheme::axisZ;

// 속성 행 크기(1x, H3a).
constexpr float kRowIndent = 12.f;   // 이름 열 왼쪽 여백(예전 행의 왼쪽 여백과 같음)
constexpr float kResetW    = 18.f;   // 되돌리기 화살표 자리
constexpr float kRightPad  = 6.f;    // 되돌리기 자리 오른쪽
constexpr float kNameFont  = 12.f;

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

// sRGB 로 저장한 색 성분을 선형으로(견본은 렌더러에 선형 값을 넘깁니다, EditorTheme 과 같은 규칙). 정본은 math/Color.hpp(H3b).
float srgbToLinear(float c) { return hcolor::srgbToLinear(c); }

// 성분 띠 색: X · Y · Z(R · G · B) = 축 색, W(A) = 보조 글자 색.
glm::vec4 componentColor(int i) {
    switch (i) {
        case 0:  return kRed;
        case 1:  return kGreen;
        case 2:  return kBlue;
        default: return EditorTheme::textSecondary;
    }
}

std::string rowKey(const void* owner, const std::string& label) {
    return std::to_string((uintptr_t)owner) + "|" + label;
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

// ── 속성 행(H3a) ───────────────────────────────────────────────────────────────────────────────────────────────
// 모든 종류가 같은 틀(SPropertyRow)입니다: 이름 열 | 값 열 | 되돌리기 자리. 예전에는 종류마다 가로 상자 · 여백 · 고정 폭을 손으로
// 다시 만들었습니다(조사 B10). 이름은 이름 열 폭에 맞춰 ".." 로 줄이고, 줄였으면 툴팁으로 전체 이름을 보입니다.
std::shared_ptr<SPropertyRow> DetailsPanel::addPropRow(const std::string& name, glm::vec4 nameColor, std::shared_ptr<HWidget> value,
                                                       std::shared_ptr<RowOps> ops, SVerticalBox* into) {
    auto text = HNew(STextBlock)->fontSize(s(kNameFont))->color(nameColor)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center);
    std::weak_ptr<STextBlock> weakText = text;
    const EditorUiKit* ui = deps_.ui;
    const float font = s(kNameFont);
    text->text(Attr<std::string>(std::function<std::string()>([weakText, ui, name, font]() -> std::string {
        auto t = weakText.lock();
        const float w = t ? t->cachedGeometry().size.x : 0.f;
        return (ui && w > 1.f) ? ui->ellipsizeText(name, w, font) : name;
    })));
    text->tooltip.bind([weakText, ui, name, font]() -> std::string {
        auto t = weakText.lock();
        const float w = t ? t->cachedGeometry().size.x : 0.f;
        return (ui && w > 1.f && EditorUiKit::textWidth(name, font) > w) ? name : std::string();
    });

    std::shared_ptr<HWidget> reset;
    if (ops && ops->reset) {
        auto resetFn = ops->reset;
        auto canFn   = ops->canReset;
        auto b = deps_.ui->resetArrow([canFn] { return canFn && canFn(); }, [this, resetFn] { resetFn(); dirty_ = true; });
        b->setTooltip(HTEXT("Editor.Details", "Tip.ResetToDefault", "Reset to Default"));
        reset = b;
    }
    auto row = HNew(SPropertyRow)
        ->columns(cols_)->style(EditorUiKit::propertyRowStyle(s(1.f)))
        ->indent(s(kRowIndent))->resetWidth(s(kResetW))->rightPad(s(kRightPad))->valueVPad(s(1.f))->height(s(EditorTheme::rowH))
        ->name(text)->value(std::move(value))->reset(std::move(reset));
    if (ops) rowHits_.push_back({ row, ops });
    (into ? into : list_.get())->addSlot(row, SVerticalBox::ESizeRule::Fixed, s(EditorTheme::rowH));
    return row;
}

std::shared_ptr<HWidget> DetailsPanel::numberField(std::function<float()> getter, std::function<void(float)> setter, float step,
                                                   ENumberUnit unit, glm::vec4 barColor, std::function<void()> bFn,
                                                   std::function<void()> eFn) {
    auto f = HNew(SNumberField)
        ->style(EditorUiKit::numberFieldStyle(s(1.f)))->fontSize(s(12.f))
        ->get(std::move(getter))->set(std::move(setter))->step(step)->unit(unit)
        ->onDragBegin(std::move(bFn))->onDragEnd(std::move(eFn))->clipText(true);
    if (barColor.a > 0.f) f->axisBar(barColor, s(3.f));
    return f;
}

std::shared_ptr<SVectorField> DetailsPanel::vectorField(const PropDesc& pd, std::function<float(int)> get,
                                                        std::function<void(int, float)> set, std::function<void()> bFn,
                                                        std::function<void()> eFn) {
    const int n = std::clamp(pd.components, 1, 4);
    std::vector<std::string> labels;
    std::vector<glm::vec4> colors;
    for (int i = 0; i < n; ++i) { labels.push_back(""); colors.push_back(componentColor(i)); }
    auto v = HNew(SVectorField)
        ->components(n)->style(EditorUiKit::numberFieldStyle(s(1.f)))->fontSize(s(12.f))
        ->gap(s(EditorTheme::space2))->barWidth(s(3.f))->step(pd.step)->unit(toNumberUnit(pd.unit))
        ->labels(std::move(labels))->colors(std::move(colors))
        ->get(std::move(get))->set(std::move(set))
        ->onDragBegin([bFn](int) { if (bFn) bFn(); })
        ->onDragEnd([eFn](int) { if (eFn) eFn(); });
    // 성분 이름은 띠 색으로 보이고(UE5 와 같음), 툴팁으로도 알립니다.
    for (int i = 0; i < n; ++i)
        if (auto f = v->field(i)) f->tooltip.set(hprop::componentName(pd, i));
    return v;
}

// 색 칸(H3a): [견본][성분 칸 셋(넷)]. 견본을 누르면 색 선택기(H3b)가 견본 아래에 열립니다(다시 누르면 확정하고 닫음).
std::shared_ptr<HWidget> DetailsPanel::colorField(const PropDesc& pd, std::function<float(int)> get, std::function<void(int, float)> set,
                                                  std::function<void()> bFn, std::function<void()> eFn,
                                                  std::function<void(const std::shared_ptr<HWidget>&)> openPicker,
                                                  const std::shared_ptr<RowOps>& ops) {
    const bool srgb = pd.srgb;
    const int n = std::clamp(pd.components, 3, 4);
    auto swatch = HNew(SColorSwatch)
        ->color([get, srgb, n]() -> glm::vec4 {
            glm::vec4 c{ get(0), get(1), get(2), n > 3 ? get(3) : 1.f };
            if (srgb) { c.r = srgbToLinear(c.r); c.g = srgbToLinear(c.g); c.b = srgbToLinear(c.b); }
            return c;
        })
        ->border(EditorTheme::borderPopup, EditorTheme::borderThin)->backdrop(EditorTheme::bgInput)
        ->cornerRadius(s(EditorTheme::radiusInput))->size({ s(28.f), s(18.f) });
    swatch->setTooltip(HTEXT("Editor.Details", "Tip.ColorPicker", "Open the color picker"));
    std::weak_ptr<SColorSwatch> weak = swatch;   // 견본이 자기 자신을 붙잡지 않게
    if (openPicker) {
        swatch->onClick([weak, openPicker] { if (auto w = weak.lock()) openPicker(w); });
        if (ops) ops->openColor = [weak, openPicker](bool) { if (auto w = weak.lock()) openPicker(w); };
    }
    using HR = SHorizontalBox;
    return HNew(SHorizontalBox)
        ->addSlot(swatch, HR::ESizeRule::Fixed, s(28.f))
        ->addSlot(deps_.ui->spacer(), HR::ESizeRule::Fixed, s(EditorTheme::space4))
        ->addSlot(vectorField(pd, std::move(get), std::move(set), std::move(bFn), std::move(eFn)), HR::ESizeRule::Fill);
}

// 열거 콤보(H3a): 누르면 단추 아래에 선택 목록(지금 값에 체크 · 강조)이 열립니다. ↑ ↓ · Enter · Esc · 바깥 누름은 메뉴 규칙
// (EditorMenuView)이고, 팝업은 디테일이 있는 창의 팝업 층입니다. 다시 누르면 닫습니다. 예전에는 누를 때마다 다음 값으로 돌았습니다.
std::shared_ptr<HWidget> DetailsPanel::comboField(const PropDesc& pd, std::function<void(int)> choose, const std::shared_ptr<RowOps>& ops) {
    const int idx = pd.getI ? pd.getI() : -1;
    const std::vector<const char*> opts = pd.options;
    const char* cur = (idx >= 0 && idx < (int)opts.size() && opts[(size_t)idx]) ? opts[(size_t)idx] : "?";
    auto btn = deps_.ui->comboButton(cur, 12.f, {});
    std::weak_ptr<SButton> weak = btn;   // 단추가 자기 자신을 붙잡지 않게
    auto getI = pd.getI;
    auto open = [this, weak, opts, getI, choose] {
        auto b = weak.lock();
        if (!b || !deps_.openCombo) return;
        if (deps_.isComboOpen && deps_.isComboOpen(b.get())) { if (deps_.closeMenu) deps_.closeMenu(); return; }
        const int now = getI ? getI() : -1;
        std::vector<EditorMenuEntry> entries;
        for (int i = 0; i < (int)opts.size(); ++i) {
            EditorMenuEntry e = EditorMenuEntry::act(opts[(size_t)i] ? opts[(size_t)i] : "", [choose, i] { choose(i); });
            e.checked = [getI, i] { return getI && getI() == i; };
            entries.push_back(std::move(e));
        }
        deps_.openCombo(b, std::move(entries), now);
    };
    btn->onClick(open);
    if (ops) ops->openCombo = open;
    return btn;
}

// 액터 참조 칸(참조 체계 R-7): 대상 이름(비었으면 None, 못 찾으면 (missing))을 보이는 단추. 누르면 액터 목록에서 고릅니다.
std::shared_ptr<HWidget> DetailsPanel::actorRefField(const PropDesc& pd, std::function<void(uint64_t)> choose) {
    const std::string none    = HTEXT("Editor.Details", "ActorRef.None", "None").toString();
    const std::string missing = HTEXT("Editor.Details", "ActorRef.Missing", "(missing)").toString();
    auto getG = pd.getG;
    const std::string cur   = (deps_.ctx && deps_.ctx->world && getG) ? hpick::labelFor(*deps_.ctx->world, getG(), none, missing) : none;
    const std::string title = pd.label ? pd.label : "";
    auto b = deps_.ui->textButton(cur, 12.f, EditorUiKit::kTextBody, { 6.f, 0.f }, EditorTheme::ctlNormal, EditorTheme::ctlHover,
                                  [this, title, getG, choose] {
                                      if (!deps_.pickActor || !getG) return;
                                      deps_.pickActor(title, getG(), [this, choose](uint64_t g) {
                                          choose(g);
                                          dirty_ = true;
                                      });
                                  });
    b->hAlign(EHAlign::Left);
    return b;
}

std::shared_ptr<HWidget> DetailsPanel::checkField(std::function<bool()> getB, std::function<void(bool)> setB) {
    auto box = HNew(SCheckBox)
        ->style(EditorUiKit::checkBoxStyle(s(1.f)))
        ->checked(getB)
        ->onClick([this, getB, setB] { setB(!getB()); dirty_ = true; });
    return HNew(SBox)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)->content(box);
}

// 글 칸(H3a). 편집 중이 아닐 때는 글을 보이는 칸이고, 누르면 그 자리에서 편집합니다(키보드 포커스 = H1a-2 글 칸 체계). Enter = 확정
// (Undo 한 단계), Esc = 취소, 다른 곳 누름 = 취소(아웃라이너 이름 바꾸기 칸과 같은 규칙).
std::shared_ptr<HWidget> DetailsPanel::stringValue(const std::string& key, std::function<std::string()> get,
                                                   std::function<void(const std::string&)> commit, const std::shared_ptr<RowOps>& ops) {
    auto begin = [this, key, get, commit] {
        str_.prepare(key, get, commit);
        if (deps_.focusText) deps_.focusText(strText_);
    };
    if (ops) ops->beginEdit = begin;
    if (str_.editing(key)) {
        strShown_ = true;
        SEditableText::Style es;
        es.text        = EditorUiKit::kTextBody;
        es.composition = EditorUiKit::kTextBody;
        es.selection   = EditorTheme::accent;
        es.caret       = EditorUiKit::kTextBody;
        es.caretWidth  = std::max(1.f, std::floor(s(1.f)));
        auto edit = HReuse(strText_)->focusable(true)->state(&str_.edit())->focused([this] { return str_.focused(); })
                        ->fontSize(s(12.f))->textHeight(s(16.f))->style(es);
        str_.rebind(get, commit);   // 다시 만들 때 새 세터(같은 칸)
        return HNew(SBorder)->background(EditorTheme::bgInput)->borderColor(EditorTheme::accent)
            ->borderThickness(EditorTheme::borderThin)->cornerRadius(s(EditorTheme::radiusInput))
            ->padding(HPadding{ s(5.f), 0.f })->vAlign(EVAlign::Center)->content(edit);
    }
    const std::string now = get ? get() : std::string();
    auto b = HNew(SButton)
        ->style(EditorUiKit::buttonStyle(s(1.f)))->cornerRadius(s(EditorTheme::radiusInput))
        ->normalColor(EditorTheme::bgInput)->hoveredColor(EditorTheme::ctlNormal)->pressedColor(EditorTheme::bgInput)
        ->padding(HPadding{ s(5.f), 0.f })->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)
        ->content(HNew(STextBlock)->text(now)->fontSize(s(12.f))->color(EditorUiKit::kTextBody))
        ->onClick(begin);
    return b;
}

TextInputField DetailsPanel::stringField() {
    // 포커스가 바뀌면(들어감 · 나감 · 확정 · 취소) 행을 다시 만듭니다(보기 ↔ 편집 모양). 글자는 칸이 상태를 바로 그리므로 다시 만들지 않습니다.
    return str_.field([this] { if (str_.focused() != strShown_ || !str_.focused()) dirty_ = true; },
                      [this](glm::vec2 p) { return strShown_ && strText_ && strText_->cachedGeometry().contains(p); });
}

// 에셋 경로 칸(H3b, UE 의 에셋 고르기 칸 축소판): [종류 아이콘 · 에셋 이름 · ▾][콘텐츠 브라우저에서 찾기][비우기].
//   - 이름 단추를 누르면 칸 아래에 고르기 팝업(검색 + 그 종류의 에셋 목록, AssetPicker). 다시 누르면 닫음.
//   - 찾기 = 콘텐츠 브라우저를 그 에셋의 폴더로 열고 고름. 비우기 = 빈 경로(None). 값이 비면 둘 다 흐림.
//   - 콘텐츠 브라우저에서 끌어 이 칸에 놓을 수 있습니다(종류가 맞지 않으면 거부, EditorApp 이 dropAsset 을 부름).
//   - 값을 넣는 일은 모두 setPath(Undo 한 단계, 함께 고른 액터 포함)입니다.
std::shared_ptr<HWidget> DetailsPanel::assetPathValue(const PropDesc& pd, std::function<void(const std::string&)> setPath,
                                                      const std::shared_ptr<RowOps>& ops) {
    const EditorUiKit& u = *deps_.ui;
    const std::string path = pd.getS ? pd.getS() : std::string();
    const std::string none = HTEXT("Editor.Details", "ActorRef.None", "None").toString();
    const hcb::AssetKind kind = path.empty() ? hcb::AssetKind::Other : hcb::kindOfExt(hcb::extOf(hcb::nameOf(path)));
    const std::string assetType = pd.assetType ? pd.assetType : "";
    const std::vector<hcb::AssetKind> kinds = hasset::kindsOf(assetType);
    using HR = SHorizontalBox;

    auto row = HNew(SHorizontalBox);
    if (!path.empty())
        row->addSlot(u.icon(hcbui::kindIcon(kind), 13.f, hcbui::kindColor(kind), hcbui::kindFallback(kind), 10.f),
                     HR::ESizeRule::Fixed, s(17.f), {}, EVAlign::Center);
    row->addSlot(HNew(STextBlock)->text(path.empty() ? none : hasset::displayName(path))->fontSize(s(12.f))
                     ->color(path.empty() ? EditorUiKit::kTextDim : EditorUiKit::kTextBody)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                 HR::ESizeRule::Fill);
    row->addSlot(u.icon("chevron-down", 12.f, EditorUiKit::kTextDim, "v", 11.f), HR::ESizeRule::Auto, 0.f, {}, EVAlign::Center);
    auto btn = HNew(SButton)
        ->style(EditorUiKit::buttonStyle(s(1.f)))->content(row)
        ->padding(HPadding{ s(5.f), 0.f, s(4.f), 0.f })->hAlign(EHAlign::Fill)->vAlign(EVAlign::Fill)
        ->normalColor(EditorTheme::bgInput)->hoveredColor(EditorTheme::ctlNormal)->cornerRadius(s(EditorTheme::radiusInput));
    btn->tooltip.set(path.empty() ? HTEXT("Editor.Details", "Tip.AssetPick", "Choose an asset").toString() : path);
    std::weak_ptr<SButton> weak = btn;
    auto getS = pd.getS;
    auto open = [this, weak, assetType, getS, setPath] {
        auto b = weak.lock();
        if (!b || !deps_.openAssetPicker) return;
        deps_.openAssetPicker(b, assetType, getS ? getS() : std::string(), setPath);
    };
    btn->onClick(open);

    auto browse = u.iconButton("search", 12.f, EditorUiKit::kTextBody, "", 12.f, EditorUiKit::kTextBody, { 3.f, 0.f },
                               EditorTheme::ctlNormal, EditorTheme::ctlHover, [this, getS] {
                                   const std::string p = getS ? getS() : std::string();
                                   if (!p.empty() && deps_.browseToAsset) deps_.browseToAsset(p);
                               }, "o");
    browse->setTooltip(HTEXT("Editor.Details", "Tip.AssetBrowse", "Browse to Asset in Content Browser"));
    browse->enabled.set(!path.empty());
    auto clear = u.iconButton("x", 12.f, EditorUiKit::kTextBody, "", 12.f, EditorUiKit::kTextBody, { 3.f, 0.f },
                              EditorTheme::ctlNormal, EditorTheme::ctlHover, [setPath] { if (setPath) setPath(""); }, "x");
    clear->setTooltip(HTEXT("Editor.Details", "Tip.AssetClear", "Clear"));
    clear->enabled.set(!path.empty());

    if (ops) {
        ops->acceptsAsset = [kinds](const std::string& p) { return hasset::acceptsPath(kinds, p); };
        ops->setAsset     = setPath;
        ops->openPicker   = open;
    }
    // 끌어 놓기 강조: 이 행 위를 끄는 중이면 테두리(받음 = 강조색, 안 받음 = 빨강).
    const bool hot = !dropHoverRow_.empty() && ops && dropHoverRow_ == ops->label;
    auto frame = HNew(SBorder)->background(EditorTheme::transparent)
        ->borderColor(hot ? (dropHoverOk_ ? EditorTheme::accent : EditorTheme::error) : EditorTheme::transparent)
        ->borderThickness(hot ? std::max(1.f, s(1.f)) : 0.f)->cornerRadius(s(EditorTheme::radiusInput))
        ->padding(hot ? HPadding{ std::max(1.f, s(1.f)), std::max(1.f, s(1.f)) } : HPadding{ 0.f, 0.f })   // 테두리가 칸에 가리지 않게
        ->content(HNew(SHorizontalBox)
            ->addSlot(btn, HR::ESizeRule::Fill)
            ->addSlot(u.spacer(), HR::ESizeRule::Fixed, s(2.f))
            ->addSlot(browse, HR::ESizeRule::Fixed, s(22.f))
            ->addSlot(u.spacer(), HR::ESizeRule::Fixed, s(2.f))
            ->addSlot(clear, HR::ESizeRule::Fixed, s(22.f)));
    return frame;
}

const DetailsPanel::RowOps* DetailsPanel::assetRowAt(glm::vec2 p) const {
    if (!scroll_ || !scrollContains(p)) return nullptr;
    for (const auto& [w, ops] : rowHits_) {
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
    for (const auto& [w, ops] : rowHits_) {
        auto row = w.lock();
        if (!row || !row->cachedGeometry().contains(p)) continue;
        menuRow_ = ops;
        deps_.openMenu(p, rowMenuEntries());
        return true;
    }
    return false;
}

const DetailsPanel::RowOps* DetailsPanel::findRow(const std::string& label) const {
    for (const auto& [w, ops] : rowHits_)
        if (ops && ops->label == label) return ops.get();
    return nullptr;
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
    for (const auto& [w, ops] : rowHits_) {
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
    for (const auto& [w, ops] : rowHits_) {
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
    rowHits_.clear();
    menuRow_.reset();
    strShown_ = false;
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
    if (str_.focused() && !strShown_) {
        if (deps_.releaseTextQuiet) deps_.releaseTextQuiet();
        str_.forget();
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
        auto field = vectorField(pd, [get](int i) { return get()[i]; }, setAxis, txBegin, txEnd);
        addPropRow(ops->label, nameColor, field, ops, txBody.get());
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

    // 행 동작: 되돌리기(이 칸 + 함께 고른 액터의 같은 칸, Undo 한 단계) · 복사 · 붙여넣기(Undo 한 단계).
    auto ops = std::make_shared<RowOps>();
    ops->label   = lbl;
    ops->english = lbl;
    ops->kind    = kind;
    if (pd.hasDefault) {
        ops->canReset = [pd] { return hprop::canReset(pd); };
        ops->reset    = [this, selPtr, compPtr, peers, lbl, kind] {
            hcmd::resetPropToDefault(*deps_.ctx->world, selPtr, compPtr, peers, lbl, kind, *deps_.cmdStack);
            dirty_ = true;
        };
    }
    ops->copyText = [pd] { return hprop::valueText(pd); };
    if (hprop::writable(pd))
        ops->paste = [pd, commitClick, peers, lbl, kind](const std::string& t) {
            bool ok = false;
            commitClick([&] {
                ok = hprop::setFromText(pd, t);
                if (ok)
                    for (Component* pc : peers) hcmd::withProp(*pc, lbl, kind, [&](const PropDesc& q) { hprop::setFromText(q, t); });
            });
            return ok;
        };

    std::shared_ptr<HWidget> value;
    glm::vec4 nameColor = EditorUiKit::kTextBody;
    switch (kind) {
        case PropDesc::Float: {
            // 축 색이 붙은 칸(폭 · 높이처럼 X · Y 를 뜻하는 칸)은 숫자 칸 왼쪽에 그 색 띠를 둡니다.
            const glm::vec4 bar = pd.color == PropColor::Neutral ? glm::vec4(0.f) : themePropColor(pd.color);
            value = numberField([pd] { return pd.getF(); },
                                [pd, lbl, toPeers](float v) {
                                    pd.setF(v);
                                    hcmd::PropValue e; e.label = lbl; e.kind = PropDesc::Float; e.f = v;
                                    toPeers(e);
                                },
                                pd.step, toNumberUnit(pd.unit), bar, compBegin, compEnd);
            break;
        }
        case PropDesc::Toggle:
            value = checkField(pd.getB, [commitClick, toPeers, lbl, setB = pd.setB](bool v) {
                commitClick([&] {
                    setB(v);
                    hcmd::PropValue e; e.label = lbl; e.kind = PropDesc::Toggle; e.b = v;
                    toPeers(e);
                });
            });
            break;
        case PropDesc::Enum:
            value = comboField(pd, [this, commitClick, toPeers, lbl, setI = pd.setI](int v) {
                commitClick([&] {
                    setI(v);
                    hcmd::PropValue e; e.label = lbl; e.kind = PropDesc::Enum; e.i = v;
                    toPeers(e);
                });
                dirty_ = true;
            }, ops);
            break;
        case PropDesc::ActorRef:
            value = actorRefField(pd, [commitClick, toPeers, lbl, setG = pd.setG](uint64_t v) {
                commitClick([&] {
                    setG(v);
                    hcmd::PropValue e; e.label = lbl; e.kind = PropDesc::ActorRef; e.g = v;
                    toPeers(e);
                });
            });
            break;
        case PropDesc::String:
            value = stringValue(rowKey(compPtr, lbl), pd.getS, [commitClick, toPeers, lbl, setS = pd.setS](const std::string& v) {
                commitClick([&] {
                    setS(v);
                    hcmd::PropValue e; e.label = lbl; e.kind = PropDesc::String; e.s = v;
                    toPeers(e);
                });
            }, ops);
            break;
        case PropDesc::AssetPath:
            // 고르기 · 비우기 · 끌어 놓기 · 붙여넣기가 모두 이 한 길(Undo 한 단계, 함께 고른 액터 포함)입니다. 같은 값이면 기록 없음.
            value = assetPathValue(pd, [this, commitClick, toPeers, lbl, setS = pd.setS, getS = pd.getS,
                                        world = deps_.ctx->world, actorId = selPtr->id, compPtr](const std::string& v) {
                if (!setS || !world) return;
                // 고르기 팝업이 열린 동안 컴포넌트가 사라졌으면(Undo · 씬 바꿈) 넣지 않습니다.
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
            }, ops);
            break;
        case PropDesc::Vec3:
        case PropDesc::Color: {
            // 성분 하나를 바꾸면 주 선택은 그 성분만, 함께 고른 액터도 그 성분만 바꿉니다(트랜스폼과 같은 규칙).
            auto get = [pd](int i) { return pd.getV()[std::clamp(i, 0, 3)]; };
            auto set = [pd, peers, lbl, kind](int i, float v) {
                glm::vec4 cur = pd.getV();
                cur[std::clamp(i, 0, 3)] = v;
                pd.setV(cur);
                for (Component* pc : peers) hcmd::setPropComponent(*pc, lbl, kind, i, v);
            };
            if (kind == PropDesc::Color) {
                // 색 선택기(H3b): 연 동안의 편집은 한 세션(hcmd::PropSession)입니다. 확정 = Undo 한 단계, 취소 = 원래 값(기록 없음).
                World* world = deps_.ctx->world;
                HCommandStack* stack = deps_.cmdStack;
                const uint64_t actorId = selPtr->id;
                const int n = std::clamp(pd.components, 3, 4);
                auto openPicker = [this, world, stack, actorId, selPtr, compPtr, peers, pd, set, lbl, n](const std::shared_ptr<HWidget>& anchor) {
                    if (!deps_.openColorPicker || !world) return;
                    auto session = std::make_shared<hcmd::PropSession>(hcmd::beginPropSession(*compPtr, peers));
                    ColorPickerPopup::Target t;
                    t.title      = lbl;
                    t.components = n;
                    t.srgb       = pd.srgb;
                    t.get        = [pd] { return pd.getV(); };
                    t.set        = [set, n](const glm::vec4& v) { for (int i = 0; i < n; ++i) set(i, v[i]); };
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
                value = colorField(pd, get, set, compBegin, compEnd, openPicker, ops);
            } else {
                value = vectorField(pd, get, set, compBegin, compEnd);
            }
            break;
        }
    }
    if (value) addPropRow(lbl, nameColor, value, ops);
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

        const size_t rowsBefore = rowHits_.size();
        for (auto& pd : *props)
            buildComponentProp(sel, *comp, pd, compBegin, compEnd, commitClick, peers);
        // 속성 검색(H3b): 맞는 행이 없는 컴포넌트는 머리 줄만 남깁니다(접힘). 읽기 정보 줄은 검색 중에는 숨깁니다.
        if (searching()) searchMatches_ += (int)(rowHits_.size() - rowsBefore);
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
