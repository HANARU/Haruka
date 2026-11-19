#include "EditorUiKit.hpp"
#include "../ui/slate/HFontAtlas.hpp"
#include "../ui/slate/widgets/SBorder.hpp"
#include "../ui/slate/widgets/STextBlock.hpp"
#include <utility>

std::shared_ptr<HWidget> EditorUiKit::spacer() const {
    return HNew(SBorder)->background({0.f, 0.f, 0.f, 0.f});
}

std::shared_ptr<HWidget> EditorUiKit::panelHeader(const std::string& title) const {
    return HNew(SBorder)
        ->background(kBgHeader)->padding(HPadding{s(8.f), s(6.f)})
        ->content(HNew(STextBlock)->text(title)->fontSize(s(13.f))->color(kTextHdr)
            ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center));
}

std::shared_ptr<HWidget> EditorUiKit::sectionHeader(const std::string& title,
                                                    glm::vec4 color, float fontSize) const {
    return HNew(SBorder)
        ->background(kBgHeader)->padding(HPadding{s(8.f), s(3.f)})
        ->content(HNew(STextBlock)->text(title)->fontSize(s(fontSize))->color(color)
            ->hAlign(EHAlign::Left));
}

std::shared_ptr<SButton> EditorUiKit::textButton(const std::string& label, float fontSize,
                                                 glm::vec4 textColor, HPadding pad,
                                                 glm::vec4 normal, glm::vec4 hovered,
                                                 std::function<void()> onClick) const {
    return HNew(SButton)
        ->content(HNew(STextBlock)->text(label)->fontSize(s(fontSize))->color(textColor))
        ->padding(HPadding{s(pad.left), s(pad.top), s(pad.right), s(pad.bottom)})
        ->normalColor(normal)->hoveredColor(hovered)
        ->onClick(std::move(onClick));
}

std::string EditorUiKit::ellipsizeText(const std::string& text, float maxPx, float fontPx) const {
    if (text.empty() || maxPx <= 1.f || !fonts) return text;
    if (fonts->measureText(text, fontPx) <= maxPx) return text;
    size_t lo = 1, hi = text.size() - 1, best = 0;
    while (lo <= hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (fonts->measureText(text.substr(0, mid) + "..", fontPx) <= maxPx) { best = mid; lo = mid + 1; }
        else                                                                   { hi = mid - 1; }
    }
    return best ? text.substr(0, best) + ".." : text.substr(0, 1);
}

float EditorUiKit::textWidth(const std::string& t, float px) {
    if (const HFontAtlas* f = HFontAtlas::metrics()) return f->measureText(t, px);
    return (float)t.size() * px * 0.6f;
}

std::string EditorUiKit::toDisplayAscii(std::string s) {
    for (char& c : s)
        if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';
    return s;
}

glm::vec4 EditorUiKit::logColor(ELogVerbosity v) {
    switch (v) {
        case ELogVerbosity::Error:   return { 0.90f, 0.35f, 0.35f, 1.f };
        case ELogVerbosity::Warning: return { 0.90f, 0.75f, 0.30f, 1.f };
        case ELogVerbosity::Verbose: return { 0.32f, 0.32f, 0.35f, 1.f };
        default:                     return kTextDim;
    }
}
