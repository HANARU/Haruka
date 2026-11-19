#pragma once
#include "../core/HLog.hpp"
#include "../ui/slate/HWidget.hpp"
#include "../ui/slate/widgets/SButton.hpp"
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <string>

class HFontAtlas;

// 에디터 패널들이 함께 쓰는 배율 · 테마 색 · 위젯 도우미(S6a).
class EditorUiKit {
public:
    static constexpr glm::vec4 kBgPanel   { 0.105f, 0.105f, 0.11f,  1.f };
    static constexpr glm::vec4 kBgHeader  { 0.078f, 0.078f, 0.082f, 1.f };
    static constexpr glm::vec4 kBgToolbar { 0.12f,  0.12f,  0.13f,  1.f };
    static constexpr glm::vec4 kBorder    { 0.055f, 0.055f, 0.060f, 1.f };
    static constexpr glm::vec4 kTextHdr   { 0.85f,  0.85f,  0.87f,  1.f };
    static constexpr glm::vec4 kTextBody  { 0.88f,  0.88f,  0.90f,  1.f };
    static constexpr glm::vec4 kTextDim   { 0.42f,  0.42f,  0.45f,  1.f };
    static constexpr glm::vec4 kSelected  { 0.071f, 0.467f, 0.792f, 0.50f };
    static constexpr glm::vec4 kAccent    { 0.071f, 0.467f, 0.792f, 1.f };

    float       uiScale = 1.f;
    HFontAtlas* fonts   = nullptr;

    float s(float x) const { return x * uiScale; }

    std::shared_ptr<HWidget> spacer() const;
    std::shared_ptr<HWidget> panelHeader(const std::string& title) const;
    std::shared_ptr<HWidget> sectionHeader(const std::string& title,
                                           glm::vec4 color = kTextHdr,
                                           float fontSize = 12.f) const;
    std::shared_ptr<SButton> textButton(const std::string& label, float fontSize,
                                        glm::vec4 textColor, HPadding pad,
                                        glm::vec4 normal, glm::vec4 hovered,
                                        std::function<void()> onClick) const;

    // 폭을 넘는 텍스트를 ".." 로 줄입니다(이분 탐색 + 폰트 실측).
    std::string ellipsizeText(const std::string& text, float maxPx, float fontPx) const;
    static float       textWidth(const std::string& t, float px);
    static std::string toDisplayAscii(std::string s);
    static glm::vec4   logColor(ELogVerbosity v);
};
