#pragma once
#include "../HWidget.hpp"
#include "../HFontAtlas.hpp"
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>

// ── SNumberField ──────────────────────────────────────────────────────────────
// UE5 스타일 숫자 필드:
//   - LMB 좌우 드래그(3px 이상 이동) = 값 스크럽
//   - 클릭(이동 없이 업)            = 인라인 텍스트 입력 모드
// 입력 모드 중 키 입력은 EditorApp이 focusedField()로 라우팅한다.
// Enter 또는 다른 곳 클릭으로 확정(commitEdit).
class SNumberField : public HWidget {
public:
    using GetterFn  = std::function<float()>;
    using SetterFn  = std::function<void(float)>;
    using CallbackFn = std::function<void()>;

    ~SNumberField() override {
        if (sFocused_ == this) sFocused_ = nullptr;   // rebuild로 파괴될 때 dangling 방지
    }

    std::shared_ptr<SNumberField> get(GetterFn fn)        { getter_     = std::move(fn); return self(); }
    std::shared_ptr<SNumberField> set(SetterFn fn)        { setter_     = std::move(fn); return self(); }
    std::shared_ptr<SNumberField> step(float s)           { step_       = s;             return self(); }
    std::shared_ptr<SNumberField> label(std::string l)    { label_      = std::move(l);  return self(); }
    std::shared_ptr<SNumberField> labelColor(glm::vec4 c) { labelColor_ = c;             return self(); }
    std::shared_ptr<SNumberField> fontSize(float f)       { fontSize_   = f;             return self(); }
    std::shared_ptr<SNumberField> onDragBegin(CallbackFn fn) { beginFn_ = std::move(fn); return self(); }
    std::shared_ptr<SNumberField> onDragEnd  (CallbackFn fn) { endFn_   = std::move(fn); return self(); }

    // ── 인라인 편집 (EditorApp이 키 입력을 라우팅) ────────────────────────────
    static SNumberField* focusedField() { return sFocused_; }

    const std::string& editBuffer() const { return editBuf_; }

    void onChar(unsigned int cp) {
        if ((cp >= '0' && cp <= '9') || cp == '.' || cp == '-' || cp == '+') {
            if (selAll_) { editBuf_.clear(); selAll_ = false; }   // 전체 선택 → 교체
            editBuf_ += (char)cp;
            edited_ = true;
        }
    }
    void onBackspace() {
        if (selAll_) { editBuf_.clear(); selAll_ = false; edited_ = true; return; }
        if (!editBuf_.empty()) { editBuf_.pop_back(); edited_ = true; }
    }
    void selectAll() {
        if (editing_ && !editBuf_.empty()) selAll_ = true;
    }
    void commitEdit() {
        if (!editing_) return;
        editing_ = false;
        selAll_  = false;
        if (sFocused_ == this) sFocused_ = nullptr;
        // 아무것도 입력하지 않았으면 값을 다시 쓰지 않습니다. 버퍼는 소수 셋째 자리로 만든
        // 표시 문자열이라, 들어갔다 나오기만 해도 1.23456 이 1.235 로 잘렸습니다. 판정은 글자
        // 비교가 아니라 입력 여부로 합니다. 플레이 중 값이 바뀐 칸에 처음과 같은 글자를 쳐서
        // 확정하면, 글자는 같아도 사용자가 그 값을 원한 것입니다.
        if (!edited_) return;
        char* end = nullptr;
        float v = std::strtof(editBuf_.c_str(), &end);
        if (end && end != editBuf_.c_str() && setter_) {
            setter_(v);
            float endVal = getter_ ? getter_() : v;
            if (endFn_ && std::abs(endVal - editStartVal_) > 1e-6f)
                endFn_();
        }
    }

    glm::vec2 computeDesiredSize() override { return { 70.f, fontSize_ * 1.6f }; }
    void onArrangeChildren(const HGeometry& a) override { cachedGeo_ = a; }

    void paint(HPaintContext& ctx, const HGeometry& geo) override {
        if (!isVisible()) return;

        glm::vec4 bg = editing_    ? glm::vec4{0.10f, 0.22f, 0.40f, 1.f}
                     : isDragging_ ? glm::vec4{0.15f, 0.35f, 0.65f, 1.f}
                     : isHovered_  ? glm::vec4{0.30f, 0.30f, 0.34f, 1.f}
                                   : glm::vec4{0.18f, 0.18f, 0.20f, 1.f};
        ctx.drawRect(geo, bg);
        if (editing_)
            ctx.drawBorder(geo, {0.07f, 0.47f, 0.79f, 1.f}, 1.f);

        // 라벨 폭은 실측 — "FovY"처럼 긴 라벨이 값과 겹치지 않도록
        float labelW = 0.f;
        if (!label_.empty() && ctx.fonts())
            labelW = ctx.fonts()->measureText(label_, fontSize_) + 8.f;

        if (!label_.empty()) {
            HGeometry lg;
            lg.position = { geo.position.x + 4.f, geo.position.y };
            lg.size     = { labelW, geo.size.y };
            lg.scale    = geo.scale;
            ctx.drawText(lg, label_, fontSize_, labelColor_);
        }

        // Value text (편집 중에는 입력 버퍼 + 커서)
        HGeometry vg;
        vg.position = { geo.position.x + 4.f + labelW, geo.position.y };
        vg.size     = { geo.size.x - labelW - 8.f, geo.size.y };
        vg.scale    = geo.scale;
        if (editing_) {
            if (selAll_ && !editBuf_.empty()) {
                // 전체 선택 하이라이트 (커서 없음)
                float tw = ctx.fonts() ? ctx.fonts()->measureText(editBuf_, fontSize_) : 0.f;
                HGeometry sg = vg;
                sg.size.x = std::min(tw + 2.f, vg.size.x);
                ctx.drawRect(sg, {0.20f, 0.45f, 0.85f, 1.f});
                ctx.drawText(vg, editBuf_, fontSize_, {1.f, 1.f, 1.f, 1.f});
            } else {
                ctx.drawText(vg, editBuf_ + "|", fontSize_, {1.f, 1.f, 1.f, 1.f});
            }
        } else {
            char buf[20];
            formatValue(getter_ ? getter_() : 0.f, buf, sizeof(buf));
            ctx.drawText(vg, buf, fontSize_, {0.88f, 0.88f, 0.88f, 1.f});
        }

        // 호버 시 드래그 어포던스 (◀ ▶ — 좌우 드래그로 값 변경)
        if (!editing_ && (isHovered_ || isDragging_)) {
            float hintW = ctx.fonts() ? ctx.fonts()->measureText("< >", fontSize_) : 18.f;
            HGeometry hg;
            hg.position = { geo.position.x + geo.size.x - hintW - 5.f, geo.position.y };
            hg.size     = { hintW, geo.size.y };
            hg.scale    = geo.scale;
            ctx.drawText(hg, "< >", fontSize_, {0.55f, 0.65f, 0.80f, 1.f});
        }
    }

    HReply onMouseButton(const HPointerEvent& e) override {
        if (e.button != 0) return HReply::Unhandled();
        if (e.down) {
            // 입력 중인 칸을 다시 누르면 아무것도 하지 않습니다. 예전에는 입력 모드에 새로
            // 들어가면서 버퍼를 지금 값으로 되돌려, 치던 글자가 사라졌습니다.
            if (editing_) return HReply::Handled();
            isDragging_   = true;
            dragMoved_    = false;
            dragStartX_   = e.position.x;
            dragStartVal_ = getter_ ? getter_() : 0.f;
            if (beginFn_) beginFn_();
            return HReply::Handled().captureMouse(this);
        }
        // 뗌은 내 위에서 시작한 누름의 짝일 때만 받습니다. 다른 곳에서 누르고 여기서 떼면
        // 캡처가 없으니 트리 경로로 들어오는데, 예전에는 그대로 입력 모드에 들어가 낡은
        // 시작값(새로 만든 칸이면 0)으로 버퍼를 채웠습니다. 그 값이 확정되면 원래 값이
        // 사라지고, 시작값과 같아 보여 undo 기록도 남지 않았습니다(DEF-77).
        if (!isDragging_) return HReply::Unhandled();
        isDragging_ = false;
        if (!dragMoved_) {
            // 이동 없는 클릭 → 인라인 입력 모드 진입. 버퍼는 지금 값으로 채웁니다.
            editing_      = true;
            selAll_       = false;
            editStartVal_ = getter_ ? getter_() : dragStartVal_;
            char buf[20];
            formatValue(editStartVal_, buf, sizeof(buf));
            editBuf_  = buf;
            edited_   = false;
            sFocused_ = this;
            return HReply::Handled().releaseMouse();
        }
        finishDrag();
        return HReply::Handled().releaseMouse();
    }

    HReply onMouseMove(const HPointerEvent& e) override {
        isHovered_ = cachedGeo_.contains(e.position);
        // 뗌을 받지 못한 채 버튼이 떨어졌습니다(뗌이 다른 처리에 소비된 경우). 드래그만
        // 마무리하고, 어디서 뗐는지 모르므로 입력 모드에는 들어가지 않습니다.
        if (isDragging_ && !e.down) {
            isDragging_ = false;
            if (dragMoved_) finishDrag();
            return HReply::Handled().releaseMouse();
        }
        if (isDragging_ && setter_ && getter_) {
            // 3px 데드존 — 클릭(입력 모드)과 드래그(스크럽) 구분
            if (!dragMoved_ && std::abs(e.position.x - dragStartX_) > 3.f)
                dragMoved_ = true;
            if (dragMoved_) {
                float delta = (e.position.x - dragStartX_) * step_;
                setter_(dragStartVal_ + delta);
            }
        }
        return HReply::Unhandled();
    }

private:
    GetterFn    getter_;
    SetterFn    setter_;
    float       step_       = 0.005f;
    float       fontSize_   = 10.f;
    std::string label_;
    glm::vec4   labelColor_ = {0.85f, 0.85f, 0.85f, 1.f};

    CallbackFn  beginFn_;
    CallbackFn  endFn_;

    bool  isDragging_   = false;
    bool  dragMoved_    = false;
    bool  isHovered_    = false;
    float dragStartX_   = 0.f;
    float dragStartVal_ = 0.f;

    // 인라인 편집 상태
    bool        editing_      = false;
    bool        selAll_       = false;
    std::string editBuf_;
    bool        edited_ = false;  // 입력 모드에 들어온 뒤 글자를 넣거나 지웠는가
    float       editStartVal_ = 0.f;

    // 표시용 문자열(소수 셋째 자리). 반올림하면 0 이 되는 음수는 0 으로 둡니다. 그대로 찍으면
    // 회전 없는 액터의 오일러 값처럼 -0.0001 이 "-0.000" 으로 보입니다(DEF-88).
    static void formatValue(float v, char* buf, size_t n) {
        if (v > -0.0005f && v < 0.0005f) v = 0.f;
        std::snprintf(buf, n, "%.3f", v);
    }

    // 스크럽 드래그의 끝. 값이 실제로 바뀌었을 때만 끝 콜백(undo 기록)을 부릅니다.
    void finishDrag() {
        float endVal = getter_ ? getter_() : 0.f;
        if (endFn_ && std::abs(endVal - dragStartVal_) > 1e-6f)
            endFn_();
    }

    // 단일 에디터 창 전제의 전역 포커스 (메인 스레드 전용)
    inline static SNumberField* sFocused_ = nullptr;

    std::shared_ptr<SNumberField> self() {
        return std::static_pointer_cast<SNumberField>(shared_from_this());
    }
};
