#include "SubtitlePanel.hpp"

#include "../core/Subtitles.hpp"
#include "../core/Utf8.hpp"
#include "../ui/slate/HFontAtlas.hpp"
#include "../ui/slate/widgets/SBorder.hpp"
#include "../ui/slate/widgets/STextBlock.hpp"
#include "../ui/slate/widgets/SVerticalBox.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace {
float rowHeight(float fontPx) { return fontPx * 1.2f; }   // STextBlock 한 줄 높이와 같음
float padOf(float fontPx)     { return fontPx * 0.35f; }
float gapOf(float fontPx)     { return fontPx * 0.25f; }
} // namespace

glm::vec2 SubtitlePanel::layoutSize(float layoutWidth, float fontPx, int maxLines) {
    const int   n      = std::max(maxLines, 1);
    const float entryH = kRowsPerLine * rowHeight(fontPx) + 2.f * padOf(fontPx);
    return { layoutWidth, n * entryH + (n - 1) * gapOf(fontPx) };
}

SubtitlePanel::Placement SubtitlePanel::placeXr(const glm::vec3& headPos, const glm::quat& headRot) {
    Placement p;
    const float th = glm::radians(kXrDownDeg);
    p.position       = headPos + headRot * glm::vec3(0.f, -std::sin(th), -std::cos(th)) * kXrDistance;
    // 판 앞면(+Z)이 머리를 향하게: X 축으로 -θ 돌리면 +Z 가 (0, sinθ, cosθ) 가 됩니다.
    p.rotation       = headRot * glm::angleAxis(-th, glm::vec3(1.f, 0.f, 0.f));
    p.layoutWidth    = kXrLayoutWidth;
    p.metersPerPixel = kXrWidth / kXrLayoutWidth;
    return p;
}

SubtitlePanel::Placement SubtitlePanel::placeDesktop(const glm::vec3& camPos, const glm::quat& camRot, float fovYDeg,
                                                     float aspect, float fontScale, int maxLines) {
    Placement p;
    const float fov  = std::clamp(fovYDeg, 1.f, 170.f);
    const float visH = 2.f * std::tan(glm::radians(fov) * 0.5f) * kDeskDistance;
    const float visW = visH * std::max(aspect, 0.1f);
    p.metersPerPixel = kDeskFontFrac * visH / kBaseFontPx;   // 배율 1 의 글자가 화면 높이의 4.5 %
    p.layoutWidth    = std::min(std::floor(kDeskWidthFrac * visW / p.metersPerPixel), (float)kMaxSide);
    const float hM   = layoutSize(p.layoutWidth, kBaseFontPx * fontScale, maxLines).y * p.metersPerPixel;
    const float cy   = -0.5f * visH + kDeskBottomFrac * visH + 0.5f * hM;
    p.position       = camPos + camRot * glm::vec3(0.f, cy, -kDeskDistance);
    p.rotation       = camRot;   // 카메라는 -Z 를 보므로 판 앞면 +Z 가 카메라를 향함
    return p;
}

void SubtitlePanel::update(const SubtitleQueue& queue, const AccessSettings& access) {
    queue_    = &queue;
    maxLines_ = queue.maxLines();
    fontPx_   = kBaseFontPx * std::clamp(access.subtitleSize * access.textScale, 0.5f, kMaxFontScale);
    visible_  = access.subtitles && !queue.visible().empty();
    const bool settingsChanged = access.subtitleSize != builtAccess_.subtitleSize || access.textScale != builtAccess_.textScale
                              || access.subtitleBackground != builtAccess_.subtitleBackground;
    if (queue.revision() != builtRevision_ || settingsChanged) needBuild_ = true;
    access_ = access;
}

void SubtitlePanel::place(const glm::vec3& headPos, const glm::quat& headRot, bool xr, float fovYDeg, float aspect) {
    placement_ = xr ? placeXr(headPos, headRot)
                    : placeDesktop(headPos, headRot, fovYDeg, aspect, fontPx_ / kBaseFontPx, maxLines_);
    layout_  = layoutSize(placement_.layoutWidth, fontPx_, maxLines_);
    texSize_ = { (uint32_t)std::clamp(std::lround(layout_.x), 1L, (long)kMaxSide),
                 (uint32_t)std::clamp(std::lround(layout_.y), 1L, (long)kMaxSide) };
    if (placement_.layoutWidth != builtLayoutWidth_) needBuild_ = true;
    if (visible_ && needBuild_ && queue_) rebuild(*queue_);
}

void SubtitlePanel::rebuild(const SubtitleQueue& queue) {
    const float font = fontPx_;
    const float pad  = padOf(font);
    const float maxW = std::max(layout_.x - 4.f * pad, font);
    const HFontAtlas* atlas = HFontAtlas::metrics();
    auto measure = [&](std::string_view s) {
        if (atlas) return atlas->measureText(s, font);
        size_t i = 0, n = 0;
        while (i < s.size()) { utf8::next(s, i); ++n; }
        return n * font * 0.6f;   // 글꼴이 없는 시험: STextBlock 과 같은 근사
    };
    const glm::vec4 bg(0.f, 0.f, 0.f, access_.subtitleBackground);

    rows_.clear();
    auto root = std::make_shared<SVerticalBox>();
    root->addSlot(std::make_shared<SVerticalBox>(), SVerticalBox::ESizeRule::Fill);   // 줄은 판 아래쪽에 붙음
    const auto& shown = queue.visible();
    for (size_t i = 0; i < shown.size(); ++i) {
        const SubtitleLine& l = shown[i].line;
        std::string text = l.text.toString();
        if (!l.speaker.isEmpty()) text = l.speaker.toString() + ": " + text;
        const std::vector<std::string> rows = haccess::wrapText(text, maxW, kRowsPerLine, measure);
        auto col = std::make_shared<SVerticalBox>();
        for (const std::string& r : rows) {
            auto tb = std::make_shared<STextBlock>();
            tb->text(r)->fontSize(font)->color(glm::vec4(1.f))->hAlign(EHAlign::Center);
            col->addSlot(tb, SVerticalBox::ESizeRule::Auto, 0.f, {}, EHAlign::Center);
            rows_.push_back(r);
        }
        auto border = std::make_shared<SBorder>();
        border->background(bg)->cornerRadius(pad * 0.5f)->padding(HPadding(pad, pad * 0.5f))->content(col);
        root->addSlot(border, SVerticalBox::ESizeRule::Auto, 0.f, HPadding(0.f, i == 0 ? 0.f : gapOf(font), 0.f, 0.f), EHAlign::Center);
    }
    slate_.setRootWidget(std::move(root));
    builtRevision_    = queue.revision();
    builtAccess_      = access_;
    builtLayoutWidth_ = placement_.layoutWidth;
    needBuild_        = false;
    dirty_            = true;
}

HSlateContext& SubtitlePanel::slate() { return slate_; }
