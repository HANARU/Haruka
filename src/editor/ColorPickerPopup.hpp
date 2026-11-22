#pragma once
// ── 색 선택기 팝업 (H3b, 2026-11-22) ────────────────────────────────────────────────────────────────────────────
// 디테일 패널 색 칸의 견본을 누르면 견본 아래에 열립니다(UE 의 색 선택기 축소판).
//   [채도 · 명도 판][색상 막대][알파 막대(4성분)] | [이전 · 새 색 견본][R G B (A)][H S V][16진수(sRGB)]
//                                                                    [확인][취소]
//   - 끄는 동안 값이 곧바로 들어갑니다(뷰포트에 바로 보임). Undo 는 열린 동안 한 묶음(디테일이 연 세션): 확인 · 바깥 누름 · 견본
//     다시 누름 = 확정(Undo 한 단계, 바뀐 것이 없으면 기록 없음), Esc · 취소 = 원래 값으로(기록 없음). 이전 색 견본을 누르면 원래 값.
//   - 값 · 색 공간 계산은 ColorPickerModel.hpp(hcolorpick::State).
//   - 팝업은 디테일이 있는 창 문맥의 팝업 층에 뜹니다(떠 있는 창 포함). 대상(액터 · 컴포넌트)이 사라지면 조용히 닫습니다.

#include "ColorPickerModel.hpp"
#include "EditorUiKit.hpp"
#include "TextInputController.hpp"
#include "../ui/slate/HSlate.hpp"
#include "../ui/slate/HTextEdit.hpp"
#include "../ui/slate/widgets/SColorPicker.hpp"

#include <functional>
#include <memory>
#include <string>

class ColorPickerPopup {
public:
    // 디테일 패널이 열 때 주는 대상. get · set 은 저장 값(성분 넷, RGB 면 a 무시). set 은 함께 고른 액터에도 넣습니다.
    struct Target {
        std::string                            title;        // 행 이름
        int                                    components = 3;
        bool                                   srgb = false;
        std::function<glm::vec4()>             get;
        std::function<void(const glm::vec4&)>  set;
        std::function<void()>                  commit;       // 세션 확정(Undo 한 단계)
        std::function<void()>                  cancel;       // 세션 취소(원래 값)
        std::function<bool()>                  alive;        // 대상이 아직 있는가(없으면 조용히 닫음)
    };
    struct Deps {
        const EditorUiKit*                                    ui = nullptr;
        std::function<HSlateContext*()>                       popupContext;     // 디테일이 있는 창 문맥
        std::function<void(const std::shared_ptr<HWidget>&)>  focus;            // 16진수 칸 포커스(setFocusWidget)
        std::function<void()>                                 releaseFocusQuiet;
        std::function<void()>                                 clearFocus;
    };

    void init(Deps deps) { deps_ = std::move(deps); }
    void resetPopup();   // 위젯 트리를 다시 만들 때: 확정하고 닫음

    void open(const std::shared_ptr<HWidget>& anchor, Target target);
    // 같은 견본으로 열려 있으면 확정하고 닫습니다(토글).
    void toggle(const std::shared_ptr<HWidget>& anchor, Target target);
    void confirm();   // 확정 + 닫기
    void cancel();    // 원래 값 + 닫기
    bool isOpen() const { return open_; }
    bool isOpenFor(const HWidget* anchor) const { return open_ && anchor && anchor_.lock().get() == anchor; }
    HSlateContext* popupContext() const;
    // 매 프레임: 대상 확인 · 바깥에서 바뀐 값 따라가기 · Esc(escEdge = 디테일 창 키보드의 Esc 엣지, 글 칸 포커스가 없을 때).
    void tick(bool escEdge);
    void flushRebuild();

    TextInputField hexField();
    std::shared_ptr<HWidget> hexWidget() const { return hexKeep_; }

    // 덤프 · 시험
    const hcolorpick::State& state() const { return state_; }
    void setPlane(float u, float v) { state_.setPlane(u, v); apply(); }
    void setHue(float h)            { state_.setHue(h); apply(); }
    void setAlpha(float a)          { state_.setAlpha(a); apply(); }
    bool setHex(const std::string& t) { const bool ok = state_.setHex(t); if (ok) apply(); return ok; }

private:
    float s(float x) const { return deps_.ui->s(x); }
    void apply();              // state_.value → 대상(세터가 잘랐으면 다시 읽음)
    void finish(bool commit);  // 확정 · 취소 + 닫기
    void closeQuiet();         // 세션 함수 없이 닫음(대상이 사라짐)
    void rebuildPanel();
    void dismissPopup();
    std::shared_ptr<HWidget> numberRow(const char* label, glm::vec4 bar, std::function<float()> get, std::function<void(float)> set,
                                       float step, ENumberUnit unit);

    Deps   deps_;
    Target target_;
    hcolorpick::State state_;
    bool   open_ = false;
    bool   panelDirty_ = false;
    bool   dragging_ = false;
    std::weak_ptr<HWidget> anchor_;

    HTextEditState hexEdit_;
    bool           hexFocused_ = false;
    bool           hexEscaped_ = false;
    std::shared_ptr<SEditableText> hexKeep_ = HNew(SEditableText)->focusable(true);
    std::shared_ptr<HWidget>       hexHit_;   // 16진수 칸 자리(바깥 누름 판정)

    HSlateContext*      ctx_ = nullptr;
    HPopupLayer::Handle handle_ = 0;
    uint64_t            serial_ = 0;
};
