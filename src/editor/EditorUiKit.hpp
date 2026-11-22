#pragma once
#include "EditorTheme.hpp"
#include "../core/HLog.hpp"
#include "../ui/slate/HSlateContext.hpp"
#include "../ui/slate/HWidget.hpp"
#include "../ui/slate/widgets/SButton.hpp"
#include "../ui/slate/widgets/SCheckBox.hpp"
#include "../ui/slate/widgets/SCollapseSection.hpp"
#include "../ui/slate/widgets/SNumberField.hpp"
#include "../ui/slate/widgets/SPropertyRow.hpp"
#include "../ui/slate/widgets/SScrollBox.hpp"
#include "../ui/slate/widgets/SSearchBox.hpp"
#include "../ui/slate/widgets/STabBar.hpp"
#include "../ui/slate/widgets/SToggleButton.hpp"
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <string>

class HFontAtlas;

// 에디터 패널들이 함께 쓰는 배율 · 테마 색 · 위젯 도우미(S6a).
class EditorUiKit {
public:
    static constexpr glm::vec4 kBgPanel   = EditorTheme::bgPanel;
    static constexpr glm::vec4 kBgHeader  = EditorTheme::bgHeader;
    static constexpr glm::vec4 kBgToolbar = EditorTheme::bgWindow;
    static constexpr glm::vec4 kBorder    = EditorTheme::lineDivider;
    static constexpr glm::vec4 kTextHdr   = EditorTheme::textPrimary;
    static constexpr glm::vec4 kTextBody  = EditorTheme::textPrimary;
    static constexpr glm::vec4 kTextDim   = EditorTheme::textSecondary;
    static constexpr glm::vec4 kSelected  = EditorTheme::selectRow;
    static constexpr glm::vec4 kAccent    = EditorTheme::accent;

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

    // 아이콘 단추(D3). label 이 비면 아이콘만, 있으면 아이콘 + 간격 + 글자입니다. 아이콘을 그릴 수 없으면(아이콘 파일 없음)
    // 아이콘 자리에 fallback 글자를 그립니다. iconPx · fontSize · pad 는 1x 값이고 배율은 여기서 곱합니다.
    std::shared_ptr<SButton> iconButton(const std::string& icon, float iconPx, glm::vec4 iconColor,
                                        const std::string& label, float fontSize, glm::vec4 textColor,
                                        HPadding pad, glm::vec4 normal, glm::vec4 hovered,
                                        std::function<void()> onClick, const std::string& fallback = "") const;
    // 아이콘 하나(배율을 곱한 크기, 대신할 글자 포함).
    std::shared_ptr<HWidget> icon(const std::string& name, float iconPx, glm::vec4 color,
                                  const std::string& fallback = "", float fallbackFont = 12.f) const;

    // 열거 콤보 단추(H3a, UE 의 SComboBox 닫힌 모양): 왼쪽에 지금 값, 오른쪽에 아래 화살표. 폭은 놓인 자리를 채웁니다.
    std::shared_ptr<SButton> comboButton(const std::string& text, float fontSize, std::function<void()> onClick) const;
    // 기본값 되돌리기 화살표(H3a, UE 의 노란 되돌리기 화살표). visible 이 거짓이면 숨김(자리는 행이 늘 비워 둠).
    std::shared_ptr<SButton> resetArrow(std::function<bool()> visible, std::function<void()> onClick) const;

    // 폭을 넘는 텍스트를 ".." 로 줄입니다(이분 탐색 + 폰트 실측).
    std::string ellipsizeText(const std::string& text, float maxPx, float fontPx) const;
    static float       textWidth(const std::string& t, float px);
    static std::string toDisplayAscii(std::string s);
    static glm::vec4   logColor(ELogVerbosity v);

    // EditorTheme 기반 공용 위젯 스타일 묶음. scale 은 s(1.f)(1x=1.f).
    static constexpr SButton::Style buttonStyle(float scale = 1.f) {
        return { EditorTheme::ctlNormal, EditorTheme::ctlHover, EditorTheme::ctlPressed,
                 EditorTheme::radiusControl * scale, EditorTheme::textDisabled };
    }
    static constexpr SToggleButton::Style toggleButtonStyle(float scale = 1.f) {
        return { EditorTheme::ctlNormal, EditorTheme::ctlHover, EditorTheme::selectRow, EditorTheme::accentHover,
                 EditorTheme::radiusControl * scale, EditorTheme::textDisabled };
    }
    static constexpr STabBar::Style tabBarStyle(float scale = 1.f) {
        return {
            EditorTheme::bgWindow, EditorTheme::lineDivider, EditorTheme::bgPanel, EditorTheme::bgHeader,
            EditorTheme::transparent, EditorTheme::accent, EditorTheme::textPrimary, EditorTheme::textSecondary,
            EditorTheme::radiusControl * scale
        };
    }
    static constexpr SSearchBox::Style searchBoxStyle(float scale = 1.f) {
        return {
            EditorTheme::bgInput, EditorTheme::accent, EditorTheme::borderPopup, EditorTheme::lineDivider,
            EditorTheme::textDisabled, EditorTheme::textDisabled, EditorTheme::accent,
            EditorTheme::textPrimary, EditorTheme::textSecondary,
            EditorTheme::radiusInput * scale, EditorTheme::borderThin, "search"
        };
    }
    static constexpr SNumberField::Style numberFieldStyle(float scale = 1.f) {
        return {
            EditorTheme::bgInput, EditorTheme::selectRow, EditorTheme::ctlNormal, EditorTheme::bgInput,
            EditorTheme::accent, EditorTheme::accent, EditorTheme::textOnAccent,
            EditorTheme::textPrimary, EditorTheme::accentHover, EditorTheme::textPrimary,
            EditorTheme::lineDivider, EditorTheme::borderPopup, EditorTheme::borderThin,
            EditorTheme::radiusInput * scale
        };
    }
    // 디테일 속성 행(H3a): hover 바탕 · 열 나눔선 · 끌기 강조 · 끌기 자리 · 이름 열과 값 열 사이.
    static constexpr SPropertyRow::Style propertyRowStyle(float scale = 1.f) {
        return { EditorTheme::propertyRowHover, EditorTheme::withAlpha(EditorTheme::lineDivider, 0.6f), EditorTheme::accentHover,
                 4.f * scale, EditorTheme::space8 * scale };
    }
    static constexpr SCollapseSection::Style collapseSectionStyle() {
        return {
            EditorTheme::ctlNormal, EditorTheme::bgHeader, EditorTheme::lineDivider,
            EditorTheme::textSecondary, EditorTheme::textPrimary, "chevron-down", "chevron-right", true
        };
    }
    static constexpr SScrollBox::Style scrollBoxStyle(float scale = 1.f) {
        return {
            EditorTheme::transparent, EditorTheme::ctlPressed, EditorTheme::textDisabled,
            6.f * scale, 3.f * scale
        };
    }
    static constexpr SCheckBox::Style checkBoxStyle(float scale = 1.f) {
        return {
            EditorTheme::bgInput, EditorTheme::borderPopup, EditorTheme::accent,
            EditorTheme::accentHover, EditorTheme::textOnAccent,
            EditorTheme::space16 * scale, 3.f * scale, EditorTheme::borderThin, "check",
            EditorTheme::textDisabled
        };
    }
    static constexpr HSlateContext::TooltipStyle tooltipStyle(float scale = 1.f) {
        return {
            EditorTheme::bgPopup, EditorTheme::borderPopup, EditorTheme::textPrimary,
            12.f * scale, EditorTheme::radiusControl * scale, EditorTheme::borderThin,
            EditorTheme::space8 * scale, EditorTheme::space4 * scale,
            { 12.f * scale, 16.f * scale },
            { 0.f, 2.f * scale }, 6.f * scale, EditorTheme::shadowColor, 0.5f
        };
    }
};
