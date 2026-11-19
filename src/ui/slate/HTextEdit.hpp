#pragma once
#include "../../core/Utf8.hpp"
#include "HFontAtlas.hpp"
#include <algorithm>
#include <string>
#include <string_view>

// ── HTextEditState (2026-11-19, 글자 체계 T1 · T2, Docs/Text_System.md) ─────────────────────────────
// 한 줄 편집 칸의 상태: 글(UTF-8) · 커서 · 기준점(선택 범위 = 기준점 ~ 커서) · IME 조합 글. 커서와 기준점은 늘 글자(grapheme)
// 경계의 바이트 자리입니다. 넣기는 선택을 바꾸고, 지우기 · 이동은 글자 단위(word 면 단어 단위)입니다. 위젯 · 키 처리와 떨어져
// 있어 창 없이 시험합니다. UE 의 FTextLayout + SEditableText 의 편집 부분에 해당합니다(여러 줄 · 실행 취소는 없음).
struct HTextEditState {
    std::string text;
    size_t      caret  = 0;
    size_t      anchor = 0;
    size_t      maxBytes   = 0;      // 0 = 제한 없음
    bool        singleLine = true;
    // IME 조합 중인 글(T2). 비어 있지 않으면 커서 자리에 끼워 그리고, 확정되면 WM_CHAR 로 들어와 insert 됩니다.
    std::string composition;
    size_t      compositionCaret = 0;   // 조합 글 안의 커서(바이트)
    // 마지막으로 그린 커서 사각형(렌더 타겟 px, x · y · 폭 · 높이). 그리는 위젯이 쓰고, IME 후보 창 자리로 씁니다(T2).
    mutable glm::vec4 caretRect{ 0.f, 0.f, 0.f, 0.f };

    bool   hasSelection() const { return caret != anchor; }
    size_t selBegin() const { return std::min(caret, anchor); }
    size_t selEnd()   const { return std::max(caret, anchor); }
    std::string selectedText() const { return text.substr(selBegin(), selEnd() - selBegin()); }
    bool   isAllSelected() const { return !text.empty() && selBegin() == 0 && selEnd() == text.size(); }
    bool   isComposing() const { return !composition.empty(); }

    // 글을 통째로 바꿉니다(칸을 열 때). 커서는 끝, selectAll 이면 전체 선택.
    void setText(std::string_view t, bool selectAll = false) {
        text   = utf8::sanitize(t, singleLine);
        clampToMax();
        caret  = text.size();
        anchor = selectAll ? 0 : caret;
        composition.clear();
        compositionCaret = 0;
    }
    void clear() { setText({}); }
    void selectAll() { anchor = 0; caret = text.size(); }
    void clearSelection() { anchor = caret; }

    // 커서를 pos 로(글자 경계로 내림). extend 면 기준점은 그대로 두어 선택을 늘립니다.
    void setCaret(size_t pos, bool extend = false) {
        caret = utf8::snapGrapheme(text, std::min(pos, text.size()));
        if (!extend) anchor = caret;
    }

    // UTF-8 글을 넣습니다(선택이 있으면 바꿈). 줄바꿈 · 제어 문자는 고치고, 최대 길이를 넘으면 글자 경계에서 자릅니다.
    // 바뀌었으면 참.
    bool insert(std::string_view utf8Text) {
        std::string add = utf8::sanitize(utf8Text, singleLine);
        if (add.empty() && !hasSelection()) return false;
        const size_t b = selBegin(), e = selEnd();
        if (maxBytes) {
            const size_t room = maxBytes > text.size() - (e - b) ? maxBytes - (text.size() - (e - b)) : 0;
            if (add.size() > room) {
                size_t cut = 0;
                while (cut < add.size()) {
                    const size_t n = utf8::nextGrapheme(add, cut);
                    if (n > room) break;
                    cut = n;
                }
                add.resize(cut);
            }
            if (add.empty() && b == e) return false;
        }
        text.replace(b, e - b, add);
        // 넣은 글이 앞 글자와 합쳐질 수 있으므로(결합 문자) 커서를 경계로 맞춥니다.
        caret  = utf8::snapGrapheme(text, b + add.size());
        if (caret < b + add.size()) caret = std::min(utf8::nextGrapheme(text, caret), text.size());
        anchor = caret;
        return true;
    }
    bool insertCodepoint(uint32_t cp) {
        if (!utf8::isPrintable(cp)) return false;
        return insert(utf8::encode(cp));
    }

    // 선택을 지웁니다. 선택이 없으면 거짓.
    bool deleteSelection() {
        if (!hasSelection()) return false;
        text.erase(selBegin(), selEnd() - selBegin());
        caret = anchor = selBegin();
        return true;
    }
    bool backspace(bool word = false) {
        if (deleteSelection()) return true;
        if (caret == 0) return false;
        const size_t p = word ? utf8::prevWord(text, caret) : utf8::prevGrapheme(text, caret);
        text.erase(p, caret - p);
        caret = anchor = p;
        return true;
    }
    bool del(bool word = false) {
        if (deleteSelection()) return true;
        if (caret >= text.size()) return false;
        const size_t n = word ? utf8::nextWord(text, caret) : utf8::nextGrapheme(text, caret);
        text.erase(caret, n - caret);
        anchor = caret;
        return true;
    }
    // 잘라내기: 선택 글을 돌려주고 지웁니다(선택이 없으면 빈 글).
    std::string cut() {
        std::string s = selectedText();
        deleteSelection();
        return s;
    }

    // 이동. 선택이 있는데 extend 가 아니면 왼쪽 · 오른쪽 화살표는 선택 끝으로 가서 접습니다(Windows 편집 칸과 같음).
    void moveLeft(bool word, bool extend) {
        if (hasSelection() && !extend && !word) { caret = anchor = selBegin(); return; }
        caret = word ? utf8::prevWord(text, caret) : utf8::prevGrapheme(text, caret);
        if (!extend) anchor = caret;
    }
    void moveRight(bool word, bool extend) {
        if (hasSelection() && !extend && !word) { caret = anchor = selEnd(); return; }
        caret = word ? utf8::nextWord(text, caret) : utf8::nextGrapheme(text, caret);
        if (!extend) anchor = caret;
    }
    void home(bool extend) { caret = 0;           if (!extend) anchor = caret; }
    void end(bool extend)  { caret = text.size(); if (!extend) anchor = caret; }

    // 조합 글(T2). 조합 중에는 선택 범위를 그대로 두고(그림에서는 조합 글이 선택 자리를 덮음) 확정 때 insert 가 바꿉니다.
    void setComposition(std::string_view comp, size_t compCaret) {
        composition      = utf8::sanitize(comp, true);
        compositionCaret = std::min(compCaret, composition.size());
    }
    void clearComposition() { composition.clear(); compositionCaret = 0; }

    // ── 그리기용 ───────────────────────────────────────────────────────────────────────
    // 화면에 보일 글: 조합 중이면 선택 자리를 조합 글로 바꿔 끼운 글.
    std::string displayText() const {
        if (!isComposing()) return text;
        std::string d = text;
        d.replace(selBegin(), selEnd() - selBegin(), composition);
        return d;
    }
    // 화면 글에서 커서 자리(바이트).
    size_t displayCaret() const { return isComposing() ? selBegin() + compositionCaret : caret; }
    // 화면 글에서 조합 글의 범위.
    size_t compositionBegin() const { return selBegin(); }
    size_t compositionEnd()   const { return selBegin() + composition.size(); }

    // 화면 글 앞 pos 바이트의 폭(px). 그리기 · 폭 재기와 같은 함수(HFontAtlas::measureText)를 씁니다.
    static float xOf(const HFontAtlas* fonts, std::string_view shown, float size, int face, size_t pos) {
        if (!fonts) return 0.f;
        return fonts->measureText(shown.substr(0, std::min(pos, shown.size())), size, face);
    }
    // 글자 시작에서 x(px)에 가장 가까운 글자 경계(이 상태의 글, 조합 글 제외).
    size_t caretFromX(const HFontAtlas* fonts, float size, int face, float x) const {
        if (!fonts || x <= 0.f) return 0;
        size_t prev  = 0;
        float  prevX = 0.f;
        for (size_t p = 0; p < text.size();) {
            const size_t n  = utf8::nextGrapheme(text, p);
            const float  nx = fonts->measureText(std::string_view(text).substr(0, n), size, face);
            if (x < nx) return (x - prevX) < (nx - x) ? prev : n;
            prev  = n;
            prevX = nx;
            p     = n;
        }
        return text.size();
    }

private:
    void clampToMax() {
        if (maxBytes && text.size() > maxBytes) {
            size_t cut = 0;
            while (cut < text.size()) {
                const size_t n = utf8::nextGrapheme(text, cut);
                if (n > maxBytes) break;
                cut = n;
            }
            text.resize(cut);
        }
    }
};
