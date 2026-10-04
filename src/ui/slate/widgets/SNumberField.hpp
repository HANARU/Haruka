#pragma once
#include "../HWidget.hpp"
#include "../HFontAtlas.hpp"
#include <algorithm>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>

// 숫자 칸의 단위 종류. 끌기 감도와 표시 자릿수를 정합니다(DEF-46). 에디터가 PropUnit 에서 옮겨 줍니다.
enum class ENumberUnit { Scalar, Offset, Size, Angle, Ratio };

// ── SNumberField ──────────────────────────────────────────────────────────────
// UE5 스타일 숫자 필드:
//   - LMB 좌우 드래그(3px 이상 이동) = 값 스크럽. Shift 는 10배 빠르게, Ctrl 은 0.1배로 정밀하게(UE 식)
//   - 클릭(이동 없이 업)            = 인라인 텍스트 입력 모드
// 입력 모드 중 키 입력은 EditorApp이 focusedField()로 라우팅한다.
// Enter 또는 다른 곳 클릭으로 확정(commitEdit).
class SNumberField : public HWidget {
public:
    using GetterFn  = std::function<float()>;
    using SetterFn  = std::function<void(float)>;
    using CallbackFn = std::function<void()>;

    static constexpr float kDeadzonePx = 3.f;    // 클릭과 끌기를 가르는 거리
    static constexpr float kShiftMul   = 10.f;   // UE 식: Shift 는 빠르게
    static constexpr float kCtrlMul    = 0.1f;   //        Ctrl 은 정밀하게

    // 고정 소수점으로 쓰고 끝의 0 을 지웁니다(소수 한 자리는 남김). 반올림하면 0 이 되는 값은 0 으로
    // 둬 "-0.0" 이 나오지 않습니다(DEF-88). float 의 유효 숫자는 7자리 남짓이라 그 이상은 쓰지 않고,
    // 입력 필터가 'e' 를 받지 않으므로 지수 표기도 쓰지 않습니다.
    static std::string formatTrimmed(float v, int maxDecimals) {
        const float a = std::abs(v);
        const int intDigits = a >= 1.f ? (int)std::floor(std::log10(a)) + 1 : 1;
        const int dec = std::clamp(7 - intDigits, 1, std::max(1, maxDecimals));
        if (a < 0.5f * std::pow(10.f, -(float)dec)) v = 0.f;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*f", dec, v);
        std::string s(buf);
        const size_t dot = s.find('.');
        if (dot != std::string::npos) {
            size_t end = s.size();
            while (end > dot + 2 && s[end - 1] == '0') --end;
            s.resize(end);
        }
        return s;
    }

    ~SNumberField() override {
        if (sFocused_ == this) sFocused_ = nullptr;   // rebuild로 파괴될 때 dangling 방지
    }

    std::shared_ptr<SNumberField> get(GetterFn fn)        { getter_     = std::move(fn); return self(); }
    std::shared_ptr<SNumberField> set(SetterFn fn)        { setter_     = std::move(fn); return self(); }
    std::shared_ptr<SNumberField> step(float s)           { step_       = s;             return self(); }
    std::shared_ptr<SNumberField> unit(ENumberUnit u)     { unit_       = u;             return self(); }
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
            if (endFn_ && valueChanged(editStartVal_, endVal))
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
            ctx.drawText(vg, formatTrimmed(getter_ ? getter_() : 0.f, displayDecimals()),
                         fontSize_, {0.88f, 0.88f, 0.88f, 1.f});
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
            lastX_        = e.position.x;
            dragStartVal_ = getter_ ? getter_() : 0.f;
            dragStep_     = dragStepFor(dragStartVal_);   // 끌기 동안 고정(Size 는 시작값 기준)
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
            // 이동 없는 클릭 → 인라인 입력 모드 진입. 버퍼는 지금 값으로 채웁니다. 표시보다 자릿수를
            // 넉넉히 둬, 표시에서 줄여 보이던 값도 입력 모드에서는 그대로 보입니다(DEF-46).
            editing_      = true;
            selAll_       = false;
            editStartVal_ = getter_ ? getter_() : dragStartVal_;
            editBuf_  = formatTrimmed(editStartVal_, editDecimals());
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
            // 3px 데드존 — 클릭(입력 모드)과 드래그(스크럽) 구분. 데드존을 지난 지점부터 셉니다.
            // 예전에는 처음 넘는 순간 데드존 3px 까지 한꺼번에 더해졌습니다.
            if (!dragMoved_) {
                const float off = e.position.x - dragStartX_;
                if (std::abs(off) <= kDeadzonePx) return HReply::Unhandled();
                dragMoved_ = true;
                lastX_     = dragStartX_ + (off > 0.f ? kDeadzonePx : -kDeadzonePx);
            }
            // 이동마다 그 변화량만 지금 값에 더합니다. 예전에는 끈 거리 전체에 스텝을 곱해, 도중에
            // 배율이 바뀌면 값이 한꺼번에 뛰었습니다(DEF-46). 수정 키는 이벤트마다 읽고, 세터가
            // 범위로 자른 뒤 되돌아올 때 헛도는 구간도 없습니다.
            const float mul = e.shift ? kShiftMul : (e.ctrl ? kCtrlMul : 1.f);
            const float dx  = e.position.x - lastX_;
            lastX_ = e.position.x;
            if (dx != 0.f) setter_(getter_() + dx * dragStep_ * mul);
        }
        return HReply::Unhandled();
    }

private:
    GetterFn    getter_;
    SetterFn    setter_;
    float       step_       = 0.005f;
    ENumberUnit unit_       = ENumberUnit::Scalar;
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
    float lastX_        = 0.f;     // 직전 이동 이벤트의 x (이동마다 변화량만 더함)
    float dragStep_     = 0.005f;  // 이번 끌기의 px 당 변화량

    // 인라인 편집 상태
    bool        editing_      = false;
    bool        selAll_       = false;
    std::string editBuf_;
    bool        edited_ = false;  // 입력 모드에 들어온 뒤 글자를 넣거나 지웠는가
    float       editStartVal_ = 0.f;

    // 단위 종류별 px 당 변화량(DEF-46). Size 는 끌기 시작값의 1% 이고, 0 근처에서 너무 느려지지
    // 않게 그 속성 step 의 1/10 을 최솟값으로 둡니다.
    float dragStepFor(float v0) const {
        switch (unit_) {
            case ENumberUnit::Offset: return 0.01f;
            case ENumberUnit::Size:   return std::max(std::abs(v0) * 0.01f, step_ * 0.1f);
            case ENumberUnit::Angle:  return 0.5f;
            case ENumberUnit::Ratio:  return 0.005f;
            default:                  return step_;
        }
    }
    // 표시 자릿수. 1mm 미만 거리도 보이게 하되, 회전은 둘째 자리로 둬 오일러 변환의 잡음이
    // 보이지 않게 합니다(DEF-88).
    int displayDecimals() const {
        switch (unit_) {
            case ENumberUnit::Offset:
            case ENumberUnit::Size:  return 5;
            case ENumberUnit::Angle: return 2;
            default:                 return 4;
        }
    }
    int editDecimals() const { return unit_ == ENumberUnit::Angle ? 3 : 6; }

    // 값이 실제로 바뀌었는가. 예전 기준은 절대값 1e-6 이라, 아주 작은 값(1µm 아래)의 편집은
    // undo 기록이 남지 않았습니다. 이제 값 크기에 비례한 기준으로 봅니다(float 잡음은 걸러냄).
    static bool valueChanged(float a, float b) {
        return a != b && std::abs(a - b) > 1e-6f * std::max(std::abs(a), std::abs(b));
    }

    // 스크럽 드래그의 끝. 값이 실제로 바뀌었을 때만 끝 콜백(undo 기록)을 부릅니다.
    void finishDrag() {
        float endVal = getter_ ? getter_() : 0.f;
        if (endFn_ && valueChanged(dragStartVal_, endVal))
            endFn_();
    }

    // 단일 에디터 창 전제의 전역 포커스 (메인 스레드 전용)
    inline static SNumberField* sFocused_ = nullptr;

    std::shared_ptr<SNumberField> self() {
        return std::static_pointer_cast<SNumberField>(shared_from_this());
    }
};
