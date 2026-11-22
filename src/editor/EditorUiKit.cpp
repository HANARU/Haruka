#include "EditorUiKit.hpp"
#include "../ui/slate/HFontAtlas.hpp"
#include "../ui/slate/widgets/SBorder.hpp"
#include "../ui/slate/widgets/STextBlock.hpp"
#include "../ui/slate/widgets/SIcon.hpp"
#include "../ui/slate/widgets/SHorizontalBox.hpp"
#include <utility>

std::shared_ptr<HWidget> EditorUiKit::spacer() const {
    return HNew(SBorder)->background(EditorTheme::transparent);
}

std::shared_ptr<HWidget> EditorUiKit::panelHeader(const std::string& title) const {
    return HNew(SBorder)
        ->background(kBgHeader)->padding(HPadding{s(8.f), s(6.f)})
        ->content(HNew(STextBlock)->text(title)->fontSize(s(13.f))->color(kTextHdr)->bold()
            ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center));
}

std::shared_ptr<HWidget> EditorUiKit::sectionHeader(const std::string& title,
                                                    glm::vec4 color, float fontSize) const {
    return HNew(SBorder)
        ->background(kBgHeader)->padding(HPadding{s(8.f), s(3.f)})
        ->content(HNew(STextBlock)->text(title)->fontSize(s(fontSize))->color(color)->bold()
            ->hAlign(EHAlign::Left));
}

std::shared_ptr<SButton> EditorUiKit::textButton(const std::string& label, float fontSize,
                                                 glm::vec4 textColor, HPadding pad,
                                                 glm::vec4 normal, glm::vec4 hovered,
                                                 std::function<void()> onClick) const {
    return HNew(SButton)
        ->style(buttonStyle(s(1.f)))
        ->content(HNew(STextBlock)->text(label)->fontSize(s(fontSize))->color(textColor))
        ->padding(HPadding{s(pad.left), s(pad.top), s(pad.right), s(pad.bottom)})
        ->normalColor(normal)->hoveredColor(hovered)
        ->onClick(std::move(onClick));
}

std::shared_ptr<HWidget> EditorUiKit::icon(const std::string& name, float iconPx, glm::vec4 color,
                                           const std::string& fallback, float fallbackFont) const {
    auto w = HNew(SIcon)->icon(name)->size(s(iconPx))->color(color);
    if (!fallback.empty()) w->fallback(fallback, s(fallbackFont));
    return w;
}

std::shared_ptr<SButton> EditorUiKit::iconButton(const std::string& iconName, float iconPx, glm::vec4 iconColor,
                                                 const std::string& label, float fontSize, glm::vec4 textColor,
                                                 HPadding pad, glm::vec4 normal, glm::vec4 hovered,
                                                 std::function<void()> onClick, const std::string& fallback) const {
    std::shared_ptr<HWidget> content;
    if (label.empty()) {
        content = icon(iconName, iconPx, iconColor, fallback.empty() ? iconName : fallback, fontSize);
    } else {
        using HR = SHorizontalBox;
        auto row = HNew(SHorizontalBox);
        row->addSlot(icon(iconName, iconPx, iconColor, fallback, fontSize), HR::ESizeRule::Auto, 0.f, {}, EVAlign::Center);
        if (!fallback.empty() || (fonts && fonts->hasIcon(iconName)))
            row->addSlot(spacer(), HR::ESizeRule::Fixed, s(6.f));
        row->addSlot(HNew(STextBlock)->text(label)->fontSize(s(fontSize))->color(textColor)->vAlign(EVAlign::Center),
                     HR::ESizeRule::Auto, 0.f, {}, EVAlign::Center);
        content = row;
    }
    return HNew(SButton)
        ->style(buttonStyle(s(1.f)))
        ->content(content)
        ->padding(HPadding{s(pad.left), s(pad.top), s(pad.right), s(pad.bottom)})
        ->normalColor(normal)->hoveredColor(hovered)
        ->onClick(std::move(onClick));
}

std::shared_ptr<SButton> EditorUiKit::comboButton(const std::string& text, float fontSize, std::function<void()> onClick) const {
    using HR = SHorizontalBox;
    auto row = HNew(SHorizontalBox);
    row->addSlot(HNew(STextBlock)->text(text)->fontSize(s(fontSize))->color(kTextBody)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                 HR::ESizeRule::Fill);
    row->addSlot(icon("chevron-down", 12.f, kTextDim, "v", 11.f), HR::ESizeRule::Auto, 0.f, {}, EVAlign::Center);
    return HNew(SButton)
        ->style(buttonStyle(s(1.f)))
        ->content(row)
        ->padding(HPadding{ s(6.f), 0.f, s(4.f), 0.f })
        ->hAlign(EHAlign::Fill)->vAlign(EVAlign::Fill)
        ->normalColor(EditorTheme::ctlNormal)->hoveredColor(EditorTheme::ctlHover)
        ->onClick(std::move(onClick));
}

std::shared_ptr<SButton> EditorUiKit::resetArrow(std::function<bool()> visible, std::function<void()> onClick) const {
    auto b = HNew(SButton)
        ->style(buttonStyle(s(1.f)))
        ->content(icon("rotate-ccw", 12.f, EditorTheme::resetArrow, "<", 11.f))
        ->padding(HPadding{ 0.f, 0.f })
        ->normalColor(EditorTheme::transparent)->hoveredColor(EditorTheme::ctlHover)
        ->cornerRadius(s(EditorTheme::radiusControl))
        ->onClick(std::move(onClick));
    if (visible) b->visibility.bind([visible] { return visible() ? EVisibility::Visible : EVisibility::Hidden; });
    return b;
}

std::string EditorUiKit::ellipsizeText(const std::string& text, float maxPx, float fontPx) const {
    if (text.empty() || maxPx <= 1.f || !fonts) return text;
    if (fonts->measureText(text, fontPx) <= maxPx) return text;
    size_t lo = 1, hi = text.size() - 1, best = 0;
    while (lo <= hi) {
        const size_t mid = lo + (hi - lo) / 2;
        // UTF-8 글자 중간에서 자르지 않습니다(D4, 한글 이름).
        const size_t cut = HFontAtlas::utf8Floor(text, mid);
        if (cut > 0 && fonts->measureText(text.substr(0, cut) + "..", fontPx) <= maxPx) { best = cut; lo = mid + 1; }
        else                                                                              { hi = mid - 1; }
    }
    if (best) return text.substr(0, best) + "..";
    size_t first = 0;
    HFontAtlas::nextCodepoint(text, first);   // 첫 글자 하나(여러 바이트일 수 있음)
    return text.substr(0, first);
}

float EditorUiKit::textWidth(const std::string& t, float px) {
    if (const HFontAtlas* f = HFontAtlas::metrics()) return f->measureText(t, px);
    return (float)t.size() * px * 0.6f;
}

std::string EditorUiKit::toDisplayAscii(std::string s) {
    // 이름은 예전 그대로지만, 이제 올바른 UTF-8(한글 등)은 남기고 제어 문자 · 깨진 바이트만 '?' 로 바꿉니다(D4).
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const size_t   start = i;
        const uint32_t cp    = HFontAtlas::nextCodepoint(s, i);
        if (cp < 32 || cp == 127) { out.push_back('?'); continue; }
        if (cp == '?' && (i - start != 1 || s[start] != '?')) { out.push_back('?'); continue; }   // 깨진 바이트
        out.append(s, start, i - start);
    }
    return out;
}

glm::vec4 EditorUiKit::logColor(ELogVerbosity v) {
    switch (v) {
        case ELogVerbosity::Error:   return EditorTheme::error;
        case ELogVerbosity::Warning: return EditorTheme::warn;
        case ELogVerbosity::Verbose: return EditorTheme::textDisabled;
        default:                     return kTextDim;
    }
}
