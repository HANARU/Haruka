#include "SamplesGameModule.hpp"
#include "components/UIPanelComponent.hpp"
#include "ui/slate/HSlate.hpp"

void SamplesGameModule::onRegister() {
    // 3D UI 패널 "Demo" 내용 등록 (P3b: 엔진에서 게임 모듈로 분리).
    // 에디터는 Play 전에도 씬의 패널을 그리므로 첫 씬을 열기 전에 등록합니다.
    UIPanelComponent::registerContent("Demo", [](UIPanelComponent& panel, float s) {
        auto title = std::make_shared<STextBlock>();
        title->text(panel.paramAttr("title", "Haruka UI Panel"))->fontSize(56.f * s)->color(glm::vec4(1.f));
        auto sub = std::make_shared<STextBlock>();
        sub->text("World-space HSlate panel (3D UI P1)\n1250 px/m, mipmapped, not tone mapped")
           ->fontSize(30.f * s)->color(glm::vec4(0.75f, 0.8f, 0.9f, 1.f));
        auto label = std::make_shared<STextBlock>();
        label->text("Button")->fontSize(32.f * s)->color(glm::vec4(1.f));
        auto button = std::make_shared<SButton>();
        button->content(label)->padding(HPadding(28.f * s, 12.f * s))
              ->onClick(panel.actionCallback("demo.button"))
              ->normalColor (glm::vec4(0.20f, 0.36f, 0.70f, 1.f))
              ->hoveredColor(glm::vec4(0.26f, 0.45f, 0.85f, 1.f))
              ->pressedColor(glm::vec4(0.14f, 0.26f, 0.52f, 1.f));
        auto column = std::make_shared<SVerticalBox>();
        column->addSlot(title)
              ->addSlot(sub,    SVerticalBox::ESizeRule::Auto, 0.f, HPadding(0.f, 20.f * s, 0.f, 0.f))
              ->addSlot(button, SVerticalBox::ESizeRule::Auto, 0.f, HPadding(0.f, 40.f * s, 0.f, 0.f), EHAlign::Left);
        auto root = std::make_shared<SBorder>();
        root->background(glm::vec4(0.07f, 0.08f, 0.11f, 1.f))
            ->borderColor(glm::vec4(0.35f, 0.55f, 0.95f, 1.f))
            ->borderThickness(4.f * s)
            ->padding(HPadding(48.f * s))
            ->content(column);
        return std::shared_ptr<HWidget>(root);
    });
}

void SamplesGameModule::onStart(EngineContext& /*ctx*/) {}

std::unique_ptr<IGameModule> createGameModule() {
    return std::make_unique<SamplesGameModule>();
}
