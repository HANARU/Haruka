#pragma once
#include "EditorUiKit.hpp"
#include "EditorContext.hpp"
#include "TextInputController.hpp"
#include "../core/HLog.hpp"
#include "../ui/slate/HSlate.hpp"
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <string>

// 상단 메인 툴바(Play/Pause/Stop · 씬 버튼 · VRAM 표시)(S6e).
class EditorToolbar {
public:
    struct Deps {
        const EditorUiKit*   ui  = nullptr;
        const EditorContext* ctx = nullptr;
        std::function<bool()> isPlaying;
        std::function<bool()> isPaused;
        std::function<std::string()> playTooltip;
        std::function<void()> onPlay;
        std::function<void()> onPause;
        std::function<void()> onStop;
        std::function<void()> onSave;
        std::function<void()> onOpen;
        std::function<void()> onRefresh;
    };

    void init(Deps deps);
    std::shared_ptr<SBorder> build();
    void rebuild();
    void markDirty() { dirty_ = true; }
    void flushRebuild();
    bool playButtonContains(glm::vec2 p) const;

private:
    float s(float x) const { return deps_.ui->s(x); }

    Deps deps_;
    std::shared_ptr<SHorizontalBox> box_;
    std::shared_ptr<SButton>        playButton_;
    bool dirty_ = false;
};

// 하단 상태바(최근 로그 · 열린 씬 · 키보드 포커스 표시)(S6e).
class StatusBarPanel {
public:
    struct Deps {
        const EditorUiKit*            ui       = nullptr;
        bool                          dumpMode = false;
        std::function<EFocusTarget()> currentFocus;
        std::function<std::string()>  sceneTitle;
    };

    void init(Deps deps);
    std::shared_ptr<SBorder> build();
    void tick(bool logChanged);
    void updateMessage();

private:
    float s(float x) const { return deps_.ui->s(x); }
    std::string focusText() const;
    static std::string formatLogLine(const HLogRecord& rec);

    Deps deps_;
    std::shared_ptr<SHorizontalBox> box_;
    std::string msg_       = "Ready";
    glm::vec4   msgColor_  = EditorUiKit::kTextDim;
    int         narrow_    = 0;
    glm::ivec2  sizeSig_   { 0, 0 };
};
