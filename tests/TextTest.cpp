// 글자 체계 시험(2026-11-19, Docs/Text_System.md): UTF-8 · 글자 경계 · 한글 NFC · 대소문자 무시(T1-1 ~ 4), 편집 상태(T1-6),
// 입력 처리기(T1-8, 가짜 창), IME 조합(T2), 표시 글 HText(T3).
#include "../src/core/Utf8.hpp"
#include "../src/ui/slate/HTextEdit.hpp"
#include "../src/editor/TextInputController.hpp"
#include "../src/editor/PropertyTextEdit.hpp"
#include "../src/editor/DetailsSearch.hpp"
#include "../src/ui/slate/HSlateContext.hpp"
#include "../src/ui/slate/widgets/SSearchBox.hpp"
#include "../src/ui/slate/widgets/SEditableText.hpp"
#include "../src/platform/IWindow.hpp"
#include "../src/core/HText.hpp"
#include "../src/core/HTextGather.hpp"
#include "../src/core/ProjectSettings.hpp"
#include <fstream>
#include <iterator>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

static int g_fail = 0, g_pass = 0;
static void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  [PASS] %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  [FAIL] %s\n", what.c_str()); }
}

// 소스는 /utf-8 이므로 u8 없이 그대로 UTF-8 바이트입니다.
static const std::string kHan   = "\xED\x95\x9C";                 // 한
static const std::string kGeul  = "\xEA\xB8\x80";                 // 글
static const std::string kEAcute = "e\xCC\x81";                   // e + U+0301
static const std::string kThumb = "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD";   // 👍🏽 (U+1F44D U+1F3FD)
static const std::string kFlagKR = "\xF0\x9F\x87\xB0\xF0\x9F\x87\xB7";  // 🇰🇷
static const std::string kFamily = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";  // 👨‍👩‍👧
static const std::string kJamoHan = "\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB";   // ᄒ ᅡ ᆫ (NFD 한)

// ── T1-1 ~ 4 ─────────────────────────────────────────────────────────────────────────────────
static void testUtf8() {
    std::printf("utf8\n");
    size_t i = 0;
    const std::string s = "A" + kHan + "!";
    check(utf8::next(s, i) == 'A' && utf8::next(s, i) == 0xD55C && utf8::next(s, i) == '!' && i == s.size(),
          "next: A 한 ! 를 코드 포인트로 읽음");
    check(utf8::next(s, i) == 0 && i == s.size(), "next: 끝에서는 0, 자리 그대로");
    const std::string bad = "\xFF" "a" "\xE2\x82";
    size_t j = 0;
    const uint32_t b0 = utf8::next(bad, j), b1 = utf8::next(bad, j), b2 = utf8::next(bad, j);
    check(b0 == '?' && b1 == 'a' && b2 == '?', "next: 깨진 바이트는 '?' 하나씩");
    check(!utf8::isValid(bad) && utf8::isValid(s) && utf8::isValid("a?b"), "isValid: 깨진 글 · 올바른 글 · 진짜 '?'");
    std::string enc;
    for (uint32_t cp : { 0x41u, 0xE9u, 0xD55Cu, 0x1F44Du }) utf8::append(enc, cp);
    size_t k = 0;
    check(utf8::next(enc, k) == 0x41 && utf8::next(enc, k) == 0xE9 && utf8::next(enc, k) == 0xD55C &&
          utf8::next(enc, k) == 0x1F44D && k == enc.size(), "append: 1 ~ 4 바이트 왕복");
    check(utf8::encode(0xD800) == "?", "append: 대리 쌍은 '?'");
    check(utf8::prevStart(s, s.size() - 1) == 1, "prevStart: 한글 앞 시작");
    check(utf8::floorBoundary(s, 2) == 1 && utf8::floorBoundary(s, 4) == 4, "floorBoundary: 글자 중간을 피함");
    check(utf8::codepointCount(s) == 3, "codepointCount");
    check(!utf8::isPrintable(0x1B) && !utf8::isPrintable(0x7F) && !utf8::isPrintable(0x85) && !utf8::isPrintable(0xFFFF) &&
          utf8::isPrintable(' ') && utf8::isPrintable(0xD55C) && utf8::isPrintable(0x1F44D), "isPrintable: 제어 · 비문자 거절");
    check(utf8::sanitize("a\r\nb\tc\x01" "d", true) == "a b cd", "sanitize: 한 줄(CR LF → 공백, 탭 → 공백, 제어 문자 뺌)");
    check(utf8::sanitize("a\r\nb\rc", false) == "a\nb\nc", "sanitize: 여러 줄은 줄바꿈 하나로");
    check(utf8::sanitize("\xFF" "x", true) == "?x", "sanitize: 깨진 바이트는 '?'");
}

static void testGraphemes() {
    std::printf("grapheme\n");
    check(utf8::graphemeCount(kEAcute) == 1, "e + 결합 악센트 = 한 글자");
    check(utf8::graphemeCount(kThumb) == 1, "이모지 + 피부색 = 한 글자");
    check(utf8::graphemeCount(kFlagKR) == 1, "국기(지역 표시 둘) = 한 글자");
    check(utf8::graphemeCount(kFlagKR + kFlagKR) == 2, "국기 둘 = 두 글자(짝으로 끊음)");
    check(utf8::graphemeCount(kFamily) == 1, "ZWJ 로 이은 가족 이모지 = 한 글자");
    check(utf8::graphemeCount(kJamoHan) == 1, "조합형 자모 ᄒ ᅡ ᆫ = 한 글자");
    check(utf8::graphemeCount("a\r\nb") == 3, "CR LF = 한 글자");
    check(utf8::graphemeCount(kHan + kGeul + "ab") == 4, "한 글 a b = 네 글자");
    check(utf8::graphemeCount(std::string("a") + "\xEF\xB8\x8F") == 1, "변형 선택자는 앞 글자에 붙음");
    const std::string mix = "a" + kEAcute + kHan;
    check(utf8::nextGrapheme(mix, 1) == 1 + kEAcute.size(), "nextGrapheme: 결합 문자까지");
    check(utf8::prevGrapheme(mix, mix.size()) == 1 + kEAcute.size(), "prevGrapheme: 한글 한 글자");
    check(utf8::prevGrapheme(mix, 1 + kEAcute.size()) == 1, "prevGrapheme: e + 악센트를 한 번에");
    check(utf8::snapGrapheme(mix, 2) == 1, "snapGrapheme: 결합 문자 안이면 글자 시작으로");
}

static void testWordsNfcCase() {
    std::printf("word · nfc · case\n");
    const std::string s = "Hello, " + kHan + kGeul + " world";
    // "Hello" "," " " "한글" " " "world"
    check(utf8::nextWord(s, 0) == 5, "nextWord: 단어 끝(문장 부호 앞)");
    check(utf8::nextWord(s, 5) == 7, "nextWord: 문장 부호 뒤 공백 건넘");
    check(utf8::nextWord(s, 7) == 7 + 6 + 1, "nextWord: 한글 단어와 공백");
    check(utf8::prevWord(s, s.size()) == s.size() - 5, "prevWord: world 시작");
    check(utf8::prevWord(s, s.size() - 5) == 7, "prevWord: 공백 건너 한글 시작");
    check(utf8::composeHangul(kJamoHan) == kHan, "composeHangul: ᄒ ᅡ ᆫ → 한");
    check(utf8::composeHangul("\xE1\x84\x80\xE1\x85\xA1") == "\xEA\xB0\x80", "composeHangul: ᄀ ᅡ → 가");
    check(utf8::composeHangul("\xEA\xB0\x80\xE1\x86\xA8") == "\xEA\xB0\x81", "composeHangul: 가 + ᆨ → 각");
    check(utf8::composeHangul("ab" + kHan) == "ab" + kHan, "composeHangul: 완성형 · ASCII 그대로");
    check(utf8::asciiLower("AbC" + kHan) == "abc" + kHan, "asciiLower: ASCII 만 접음");
    check(utf8::containsIgnoreCase("ProbeSphere_" + kHan, "sphere_" + kHan), "containsIgnoreCase: 한글 섞인 이름");
    check(!utf8::containsIgnoreCase("Table", "chair") && utf8::containsIgnoreCase("x", ""), "containsIgnoreCase: 없음 · 빈 바늘");
    // 음수 char(UTF-8 바이트)가 섞여도 정의된 동작: 바이트는 바뀌지 않음
    const std::string raw = "\xEC\x94\xAC";
    check(utf8::asciiLower(raw) == raw, "asciiLower: UTF-8 바이트는 그대로");
}

// ── T1-6 편집 상태 ──────────────────────────────────────────────────────────────────────────────
static void testEditState() {
    std::printf("edit state\n");
    HTextEditState st;
    st.insert("ab");
    st.insert(kHan);
    st.insert(kThumb);
    check(st.text == "ab" + kHan + kThumb && st.caret == st.text.size(), "insert: 끝에 붙고 커서는 끝");
    check(st.backspace() && st.text == "ab" + kHan, "backspace: 이모지 + 피부색을 한 번에");
    check(st.backspace() && st.text == "ab", "backspace: 한글 한 글자(3 바이트)를 한 번에");
    st.setText("x" + kEAcute + "y");
    st.home(false);
    st.moveRight(false, false);
    st.moveRight(false, false);
    check(st.caret == 1 + kEAcute.size(), "moveRight: 결합 문자를 한 글자로 건넘");
    check(st.del() && st.text == "x" + kEAcute, "del: 오른쪽 한 글자");
    st.setText("hello world", true);
    check(st.isAllSelected() && st.selectedText() == "hello world", "setText(selectAll)");
    st.insert(kHan);
    check(st.text == kHan && !st.hasSelection(), "insert: 선택을 바꿈");
    st.setText("one two three");
    st.moveLeft(true, false);
    check(st.caret == 8, "Ctrl+왼쪽: 단어 시작");
    st.moveLeft(true, true);
    check(st.selectedText() == "two ", "Ctrl+Shift+왼쪽: 단어 선택");
    st.moveRight(false, false);
    check(!st.hasSelection() && st.caret == 8, "오른쪽: 선택을 끝으로 접음");
    check(st.backspace(true) && st.text == "one three", "Ctrl+Backspace: 단어 지움");
    st.end(false);
    st.home(true);
    check(st.isAllSelected(), "Shift+Home: 처음까지 선택");
    check(st.cut() == "one three" && st.text.empty(), "cut: 선택 글을 돌려주고 지움");
    // 한 줄 칸 · 최대 길이
    HTextEditState lim;
    lim.maxBytes = 7;
    lim.insert("ab" + kHan + kHan);   // 2 + 3 + 3 = 8 > 7 → 한 하나만
    check(lim.text == "ab" + kHan, "maxBytes: 글자 경계에서 자름");
    check(!lim.insert("c") || lim.text.size() <= 7, "maxBytes: 넘치는 넣기는 거절");
    lim.maxBytes = 0;
    lim.setText("a\nb");
    check(lim.text == "a b", "한 줄 칸: 줄바꿈을 공백으로");
    check(!lim.insertCodepoint(0x08) && !lim.insertCodepoint('\n') && lim.insertCodepoint(0xAC00), "insertCodepoint: 제어 문자 거절");
    // 결합 문자를 앞 글자에 붙여 넣으면 커서가 글자 안에 남지 않음
    HTextEditState cm;
    cm.insert("e");
    cm.insert("\xCC\x81");
    check(cm.text == kEAcute && cm.caret == cm.text.size(), "결합 문자 넣기: 커서는 글자 뒤");
    cm.setCaret(1);
    check(cm.caret == 0, "setCaret: 글자 안이면 시작으로");
    // 조합 글(T2)
    HTextEditState ime;
    ime.setText("ab");
    ime.setCaret(1);
    ime.setComposition(kHan, kHan.size());
    check(ime.displayText() == "a" + kHan + "b" && ime.displayCaret() == 1 + kHan.size(), "조합 글: 커서 자리에 끼워 보임");
    check(ime.text == "ab", "조합 글: 확정 전에는 글을 바꾸지 않음");
    ime.setText("abc", true);
    ime.setComposition(kGeul, 0);
    check(ime.displayText() == kGeul && ime.compositionBegin() == 0 && ime.compositionEnd() == kGeul.size(),
          "조합 글: 선택 자리를 덮어 보임");
}

// ── T1-8 · T2 가짜 창 ──────────────────────────────────────────────────────────────────────────
class FakeWindow : public IWindow {
public:
    std::map<int, bool> down;
    std::map<int, int>  presses;
    std::vector<unsigned int> chars;
    std::string clip;
    std::string comp;
    size_t      compCaret = 0;
    bool        imeOn = false;
    int         imeToggles = 0;
    float       rect[4] = { 0, 0, 0, 0 };

    void tap(int key, int n = 1) { presses[key] += n; down[key] = true; }
    void endFrame() { presses.clear(); for (auto& kv : down) kv.second = false; }

    void init() override {}
    void cleanup() override {}
    bool shouldClose() const override { return false; }
    void setShouldClose(bool) override {}
    void pollEvents() override {}
    void waitEvents() override {}
    void getFramebufferSize(int& w, int& h) const override { w = 800; h = 600; }
    void getWindowSize(int& w, int& h) const override { w = 800; h = 600; }
    void setTitle(const std::string&) override {}
    bool wasResized() const override { return false; }
    void resetResizedFlag() override {}
    bool providesSurface() const override { return false; }
    VkSurfaceKHR createSurface(VkInstance) override { return VK_NULL_HANDLE; }
    std::vector<const char*> getRequiredInstanceExtensions() const override { return {}; }
    void  getMouseDelta(float& dx, float& dy) override { dx = dy = 0.f; }
    void  getRightMouseDelta(float& dx, float& dy) override { dx = dy = 0.f; }
    float getScrollDelta() override { return 0.f; }
    bool  isKeyDown(int key) const override { auto it = down.find(key); return it != down.end() && it->second; }
    int   keyPressCount(int key) const override { auto it = presses.find(key); return it == presses.end() ? 0 : it->second; }
    bool  isMouseButtonDown(int) const override { return false; }
    void  getMousePosition(float& x, float& y) const override { x = y = -1.f; }
    void  activateCameraMode() override {}
    void  deactivateCameraMode() override {}
    bool  isCameraModeActive() const override { return false; }
    void  drainCharQueue(std::vector<unsigned int>& out) override { out = std::move(chars); chars.clear(); }
    std::string getClipboardText() const override { return clip; }
    void  setClipboardText(const std::string& t) override { clip = t; }
    void  setCursorShape(int) override {}
    void  setTextInputEnabled(bool on) override { if (on != imeOn) ++imeToggles; imeOn = on; }
    bool  isTextInputEnabled() const override { return imeOn; }
    bool  getComposition(std::string& t, size_t& c) const override {
        if (!imeOn || comp.empty()) return false;
        t = comp; c = compCaret; return true;
    }
    void  setTextInputRect(float x, float y, float w, float h) override { rect[0] = x; rect[1] = y; rect[2] = w; rect[3] = h; }
};

static void typeUtf8(FakeWindow& w, const std::string& s) {
    for (size_t i = 0; i < s.size();) w.chars.push_back(utf8::next(s, i));
}

static void testController() {
    std::printf("controller\n");
    FakeWindow  fw;
    IWindow*    wp = &fw;
    HTextEditState searchEdit, renameEdit, logEdit, dialogEdit;
    bool searchFocused = false, renameFocused = false, logFocused = false;
    int  changed = 0, committed = 0, deletedActors = 0;
    std::string actorName = "Table";

    TextInputController tc;
    TextInputController::Deps d;
    d.window = &wp;
    // 글 칸 포커스의 정본은 HSlate 포커스 위젯입니다(H1a-2). 칸마다 포커스 받는 위젯 하나를 등록합니다.
    HSlateContext slate;
    auto searchW = HNew(SSearchBox);
    auto renameW = HNew(SEditableText);
    auto logW    = HNew(SSearchBox);
    searchW->focusable(true);
    renameW->focusable(true);
    logW->focusable(true);
    d.slate = &slate;
    d.textFields = {
        { EFocusTarget::Search, [&] { return std::shared_ptr<HWidget>(searchW); }, [&] {
            return TextInputField{ searchEdit, searchFocused, [](glm::vec2) { return false; }, [&] { ++changed; } };
        } },
        { EFocusTarget::Rename, [&] { return std::shared_ptr<HWidget>(renameW); }, [&] {
            return TextInputField{ renameEdit, renameFocused, [](glm::vec2) { return false; }, [&] { ++changed; },
                                   [&] { renameEdit.setText(actorName, true); return true; },
                                   [&] { actorName = renameEdit.text; ++committed; } };
        } },
        { EFocusTarget::LogSearch, [&] { return std::shared_ptr<HWidget>(logW); }, [&] {
            return TextInputField{ logEdit, logFocused, [](glm::vec2) { return false; }, [&] { ++changed; } };
        } },
    };
    d.dialogEdit = [&]() -> HTextEditState* { return &dialogEdit; };
    d.onDeleteSelection = [&] { ++deletedActors; };
    tc.init(d);

    // 포커스 없음: 글자는 버리고 IME 는 꺼짐
    typeUtf8(fw, "abc");
    tc.tickInput();
    fw.endFrame();
    check(searchEdit.text.empty() && !fw.imeOn, "포커스 없음: 글자 버림 · IME 끔");

    tc.setFocus(EFocusTarget::Search);
    check(searchFocused && fw.imeOn, "검색 포커스: IME 켬(T2-3)");
    check(slate.focusedWidget() == searchW && tc.currentFocus() == EFocusTarget::Search, "검색 포커스: HSlate 포커스 위젯이 검색 칸(H1a-2)");
    typeUtf8(fw, "Probe " + kHan + kGeul);
    tc.tickInput();
    fw.endFrame();
    check(searchEdit.text == "Probe " + kHan + kGeul, "한글 · ASCII 글자가 모두 들어옴(ASCII 필터 없음, T1-8)");

    fw.tap(HInput::KEY_BACKSPACE);
    tc.tickInput();
    fw.endFrame();
    check(searchEdit.text == "Probe " + kHan, "Backspace: 한글 한 글자");

    fw.tap(HInput::KEY_LEFT, 2);
    tc.tickInput();
    fw.endFrame();
    check(searchEdit.caret == 5, "왼쪽 화살표 두 번(자동 반복 수)");
    fw.down[HInput::KEY_LEFT_SHIFT] = true;
    fw.tap(HInput::KEY_HOME);
    tc.tickInput();
    fw.endFrame();
    check(searchEdit.selectedText() == "Probe", "Shift+Home: 선택");
    fw.down[HInput::KEY_LEFT_CTRL] = true;
    fw.tap(HInput::KEY_C);
    tc.tickInput();
    fw.endFrame();
    check(fw.clip == "Probe", "Ctrl+C: 선택만 복사");
    fw.down[HInput::KEY_LEFT_CTRL] = true;
    fw.tap(HInput::KEY_X);
    tc.tickInput();
    fw.endFrame();
    check(fw.clip == "Probe" && searchEdit.text == " " + kHan, "Ctrl+X: 잘라내기");
    fw.clip = "\xEC\x94\xAC\r\n" "A";   // "씬\r\nA"
    fw.down[HInput::KEY_LEFT_CTRL] = true;
    fw.tap(HInput::KEY_V);
    tc.tickInput();
    fw.endFrame();
    check(searchEdit.text == "\xEC\x94\xAC A " + kHan, "Ctrl+V: UTF-8 붙여넣기, 줄바꿈은 공백");
    fw.tap(HInput::KEY_END);
    tc.tickInput();
    fw.endFrame();
    fw.tap(HInput::KEY_DELETE);
    tc.tickInput();
    fw.endFrame();
    check(deletedActors == 0, "편집 중 Delete 는 액터를 지우지 않음");
    fw.tap(HInput::KEY_HOME);
    tc.tickInput();
    fw.endFrame();
    fw.tap(HInput::KEY_DELETE);
    tc.tickInput();
    fw.endFrame();
    check(searchEdit.text == " A " + kHan, "Home 뒤 Delete: 첫 글자(씬)");

    // IME 조합(T2-4): 조합 중 키는 칸에 먹지 않고, 확정 글자는 들어옴
    fw.comp = "\xE3\x84\xB1";   // ㄱ
    fw.compCaret = fw.comp.size();
    tc.tickInput();
    fw.endFrame();
    check(searchEdit.composition == fw.comp && searchEdit.text == " A " + kHan, "IME 조합 글을 상태에 둠");
    fw.comp = "\xEA\xB0\x80";   // 가
    fw.tap(HInput::KEY_BACKSPACE);   // 실제로는 IME 가 먹는 키(GLFW 가 버림)이지만 섞여 와도 무시돼야 함
    tc.tickInput();
    fw.endFrame();
    check(searchEdit.text == " A " + kHan && searchEdit.composition == fw.comp, "조합 중 Backspace 는 칸 글을 지우지 않음");
    fw.comp.clear();
    fw.chars.push_back(0xAC00);   // 확정 '가'
    tc.tickInput();
    fw.endFrame();
    check(searchEdit.text == "\xEA\xB0\x80 A " + kHan && !searchEdit.isComposing(), "확정 글자가 커서 자리에 들어가고 조합 끝");
    // 조합 중 Esc 는 IME 몫(포커스 유지)
    fw.comp = "\xE3\x84\xB4";
    tc.tickInput();
    fw.endFrame();
    fw.tap(HInput::KEY_ESCAPE);
    tc.tickInput();
    fw.endFrame();
    check(searchFocused, "조합 중 Esc 는 칸을 닫지 않음");
    fw.comp.clear();
    tc.tickInput();
    fw.endFrame();
    fw.tap(HInput::KEY_ESCAPE);
    tc.tickInput();
    fw.endFrame();
    check(!searchFocused && !fw.imeOn, "조합이 없으면 Esc 가 칸을 닫고 IME 를 끔");

    // 이름 바꾸기: 전체 선택으로 시작, 한글로 바꾸고 Enter
    tc.setFocus(EFocusTarget::Rename);
    check(renameFocused && renameEdit.isAllSelected(), "이름 바꾸기: 전체 선택으로 시작");
    typeUtf8(fw, kHan + "Table");
    fw.tap(HInput::KEY_ENTER);
    tc.tickInput();
    fw.endFrame();
    check(committed == 1 && actorName == kHan + "Table" && !renameFocused, "이름 바꾸기: 한글 이름으로 확정");

    // 포커스가 바뀔 때 눌린 Backspace 는 뗄 때까지 막음(DEF-58 규칙)
    fw.down[HInput::KEY_BACKSPACE] = true;
    tc.setFocus(EFocusTarget::LogSearch);
    logEdit.setText("abc");
    fw.down[HInput::KEY_BACKSPACE] = true;
    fw.presses[HInput::KEY_BACKSPACE] = 3;
    tc.tickInput();
    fw.endFrame();
    check(logEdit.text == "abc", "포커스 전환 때 눌려 있던 Backspace 는 막음");
    tc.tickInput();   // 뗀 프레임
    fw.endFrame();
    fw.tap(HInput::KEY_BACKSPACE);
    tc.tickInput();
    fw.endFrame();
    check(logEdit.text == "ab", "뗐다 다시 누르면 지움");

    // IME 후보 창 자리: 그린 커서 사각형을 창에 알림
    logEdit.caretRect = { 10.f, 20.f, 1.f, 14.f };
    tc.tickInput();
    fw.endFrame();
    check(fw.rect[0] == 10.f && fw.rect[1] == 20.f && fw.rect[3] == 14.f, "후보 창 자리 = 커서 사각형(T2-5)");

    // applyEdit 직접(창 없음)
    HTextEditState st;
    TextInputController::KeyEdges k;
    check(TextInputController::applyEdit(st, { 'a', 0xD55C, 0x1B, 'b' }, k, nullptr) && st.text == "a" + kHan + "b",
          "applyEdit: 제어 문자(ESC)는 거르고 나머지 넣음");
}


// ── H1a-2 글 칸 포커스 = HSlate 포커스 위젯 ──────────────────────────────────────────────────────
static void testFocusWidgets() {
    std::printf("focus widgets (H1a-2)\n");
    FakeWindow fw;
    IWindow*   wp = &fw;
    HSlateContext slate;
    HTextEditState aEdit, conEdit, genEdit, pickEdit;
    bool aFoc = false, conFoc = false, genFoc = false, pickFoc = false;
    int  aChanged = 0, conCommits = 0, genBegins = 0, pickCancels = 0, pickCommits = 0;
    int  deleted = 0, copied = 0, menusClosed = 0;
    bool genAllow = true;
    EFocusTarget focusSeenInCancel = EFocusTarget::Search;
    auto aW = HNew(SSearchBox), conW = HNew(SSearchBox), pickW = HNew(SSearchBox);
    auto genW = HNew(SEditableText);
    auto loose = HNew(SSearchBox);   // 등록하지 않은 포커스 위젯
    aW->focusable(true); conW->focusable(true); pickW->focusable(true); genW->focusable(true); loose->focusable(true);

    TextInputController tc;
    TextInputController::Deps d;
    d.window = &wp;
    d.slate  = &slate;
    d.onDeleteSelection = [&] { ++deleted; };
    d.onCopySelection   = [&] { ++copied; };
    d.onCloseMenus      = [&] { ++menusClosed; };
    d.textFields = {
        { EFocusTarget::Search, [&] { return std::shared_ptr<HWidget>(aW); }, [&] {
            return TextInputField{ aEdit, aFoc, [](glm::vec2) { return false; }, [&] { ++aChanged; } };
        } },
        { EFocusTarget::Console, [&] { return std::shared_ptr<HWidget>(conW); }, [&] {
            TextInputField f{ conEdit, conFoc, [](glm::vec2) { return false; }, [] {} };
            f.onCommit = [&] { ++conCommits; };
            f.keepFocusOnCommit = true;
            f.selectAllOnFocus  = false;
            return f;
        } },
        { EFocusTarget::Picker, [&] { return std::shared_ptr<HWidget>(pickW); }, [&] {
            TextInputField f{ pickEdit, pickFoc, [](glm::vec2) { return false; }, [] {} };
            f.onCommit = [&] { ++pickCommits; };
            f.onCancel = [&] { ++pickCancels; focusSeenInCancel = tc.currentFocus(); };
            return f;
        } },
        // 이름표 없는 새 글 칸(TextField): 열거값 · Deps 줄을 더하지 않고 등록 한 줄로 붙음
        { EFocusTarget::TextField, [&] { return std::shared_ptr<HWidget>(genW); }, [&] {
            TextInputField f{ genEdit, genFoc, [](glm::vec2) { return false; }, [] {} };
            f.onBegin = [&] { ++genBegins; return genAllow; };
            return f;
        } },
    };
    tc.init(d);
    auto step = [&] { tc.tickInput(); fw.endFrame(); };

    // 1) 대상 종류 계산 · 정본
    check(tc.currentFocus() == EFocusTarget::None && !slate.focusedWidget(), "처음: 포커스 없음");
    aEdit.setText("abc");
    fw.chars = { 'x', 'y' };
    tc.setFocus(EFocusTarget::Search);
    check(slate.focusedWidget() == aW && tc.currentFocus() == EFocusTarget::Search && aFoc,
          "setFocus(Search): HSlate 포커스 = 검색 위젯, 이름표 Search, 거울 값 켬");
    check(fw.chars.empty() && aEdit.text == "abc" && aEdit.isAllSelected() && fw.imeOn && menusClosed == 1,
          "들어갈 때: 문자 큐 비움 · 전체 선택 · IME 켬 · 메뉴 닫음");
    check(tc.focusedTextEdit() == &aEdit, "focusedTextEdit = 포커스 칸의 편집 상태");

    // 2) 키 엣지 다시 심기: Enter 를 누른 채 콘솔에 들어가면 그 Enter 는 확정하지 않음
    fw.down[HInput::KEY_ENTER] = true;
    tc.setFocus(EFocusTarget::Console);
    check(!aFoc && conFoc && slate.focusedWidget() == conW && !aEdit.hasSelection(), "칸 이동: 이전 칸 정리 · 새 칸");
    fw.down[HInput::KEY_ENTER] = true;
    step();
    check(conCommits == 0 && conFoc, "눌린 채 들어온 Enter 는 엣지가 아님(키 엣지 다시 심기)");
    step();
    fw.tap(HInput::KEY_ENTER);
    step();
    check(conCommits == 1 && conFoc && slate.focusedWidget() == conW, "콘솔 Enter: 확정 · 포커스 유지");
    conEdit.setText("hist");
    tc.setFocus(EFocusTarget::Search);
    tc.setFocus(EFocusTarget::Console);
    check(conEdit.caret == conEdit.text.size() && !conEdit.hasSelection(), "selectAllOnFocus=false: 콘솔은 끝에 커서");

    // 3) 단축키: 글 칸 포커스면 Delete · Ctrl+C 가 에디터로 가지 않고, 놓으면 감
    fw.tap(HInput::KEY_DELETE);
    step();
    fw.down[HInput::KEY_LEFT_CTRL] = true;
    fw.tap(HInput::KEY_C);
    step();
    check(deleted == 0 && copied == 0, "글 칸 포커스: Delete · Ctrl+C 는 칸 몫(에디터 단축키 아님)");
    tc.setFocus(EFocusTarget::None);
    check(!slate.focusedWidget() && !conFoc && !fw.imeOn && tc.currentFocus() == EFocusTarget::None, "None: HSlate 포커스 · IME 끔");
    fw.tap(HInput::KEY_DELETE);
    step();
    fw.down[HInput::KEY_LEFT_CTRL] = true;
    fw.tap(HInput::KEY_C);
    step();
    check(deleted == 1 && copied == 1, "포커스 없음: Delete · Ctrl+C 가 에디터로 감");

    // 4) 이름표 없는 칸: 위젯으로 포커스
    check(tc.setFocusWidget(genW) && tc.currentFocus() == EFocusTarget::TextField && genFoc && fw.imeOn && genBegins == 1,
          "setFocusWidget: 이름표 없는 칸(TextField)도 같은 규칙");
    check(!tc.setFocusWidget(loose) && tc.currentFocus() == EFocusTarget::TextField, "등록 안 된 위젯은 setFocusWidget 거절");
    genAllow = false;
    tc.setFocus(EFocusTarget::None);
    check(!tc.setFocusWidget(genW) || (tc.currentFocus() == EFocusTarget::None && !slate.focusedWidget() && !genFoc),
          "onBegin 거절: 들어가지 않고 HSlate 포커스도 없음");
    genAllow = true;

    // 5) 바깥에서 HSlate 포커스를 직접 옮김(훅 경로) → 컨트롤러가 같은 부수 효과를 냄
    fw.chars = { 'q' };
    slate.setFocus(aW);
    check(aFoc && tc.currentFocus() == EFocusTarget::Search && fw.chars.empty() && aEdit.isAllSelected() && fw.imeOn,
          "훅: 위젯이 포커스를 받으면 컨트롤러가 들어감(문자 큐 · 전체 선택 · IME)");
    slate.setFocus(conW);
    check(!aFoc && conFoc && tc.currentFocus() == EFocusTarget::Console, "훅: 위젯 사이 이동 — 이전 칸 정리");
    slate.setFocus(loose);
    check(!conFoc && tc.currentFocus() == EFocusTarget::None && !fw.imeOn, "훅: 등록 안 된 위젯으로 가면 글 칸 포커스 없음 · IME 끔");
    slate.setFocus(aW);
    slate.clearFocus();
    check(!aFoc && tc.currentFocus() == EFocusTarget::None && !fw.imeOn, "훅: 포커스 해제 = 칸을 닫음(확정 없음)");
    genAllow = false;
    slate.setFocus(genW);
    check(!genFoc && !slate.focusedWidget() && tc.currentFocus() == EFocusTarget::None, "훅: onBegin 거절이면 HSlate 포커스를 되돌림");
    genAllow = true;

    // 6) 패널이 거울 값을 직접 끔(액터 삭제 · 행 누름 · 팝업 호스트 재설정): 부수 효과 없이 포커스 없음(예전과 같음)
    tc.setFocus(EFocusTarget::Search);
    const int toggles = fw.imeToggles;
    aFoc = false;
    check(tc.currentFocus() == EFocusTarget::None && fw.imeToggles == toggles && slate.focusedWidget() == aW,
          "거울 값 끔: 곧바로 None, IME 그대로(HSlate 포커스는 다음 전환이 덮음)");
    tc.setFocus(EFocusTarget::Search);
    check(aFoc && tc.currentFocus() == EFocusTarget::Search, "다시 들어갈 수 있음");

    // 7) 루트 교체: preserveFocusAcross 안에서는 확정 · 취소 · 문자 큐 · IME 변화 없음
    slate.setRootWidget(HNew(SBox));
    tc.setFocus(EFocusTarget::Picker);
    pickEdit.setText("tra");
    fw.chars = { 'n' };
    const int t0 = fw.imeToggles, cm0 = pickCommits, cc0 = pickCancels, mc0 = menusClosed;
    tc.preserveFocusAcross([&] { slate.setRootWidget(HNew(SBox)); });
    check(pickFoc && slate.focusedWidget() == pickW && tc.currentFocus() == EFocusTarget::Picker,
          "루트 교체: 같은 위젯에 포커스 유지");
    check(pickCommits == cm0 && pickCancels == cc0 && fw.imeToggles == t0 && fw.chars.size() == 1 && menusClosed == mc0 &&
          pickEdit.text == "tra", "루트 교체: 확정 · 취소 · IME · 문자 큐 · 메뉴 변화 없음");
    fw.chars.clear();
    // 감싸지 않은 루트 교체는 포커스를 풂(확정 · 취소는 여전히 없음)
    slate.setRootWidget(HNew(SBox));
    check(!pickFoc && tc.currentFocus() == EFocusTarget::None && pickCommits == cm0 && pickCancels == cc0,
          "감싸지 않은 루트 교체: 포커스만 풀림(확정 · 취소 없음)");

    // 8) Esc: 포커스를 먼저 푼 뒤 onCancel(팝업 닫기)
    tc.setFocus(EFocusTarget::Picker);
    fw.tap(HInput::KEY_ESCAPE);
    step();
    check(pickCancels == cc0 + 1 && focusSeenInCancel == EFocusTarget::None && !slate.focusedWidget() && !pickFoc,
          "Esc: 포커스를 푼 뒤 onCancel");
    // 9) Enter(keepFocusOnCommit 아님): 확정 후 포커스 없음
    tc.setFocus(EFocusTarget::Picker);
    step();
    fw.tap(HInput::KEY_ENTER);
    step();
    check(pickCommits == cm0 + 1 && !pickFoc && !slate.focusedWidget(), "Enter: 확정 후 포커스 해제");
    (void)aChanged;
}

// ── T3 표시 글 ───────────────────────────────────────────────────────────────────────────────────
static void testHText() {
    std::printf("htext\n");
    htext::clearTables();
    htext::setCulture("en");
    const HText lit = HText::fromString("Sun " + kHan);
    check(lit.toString() == "Sun " + kHan && !lit.isLocalized(), "fromString: 원문 그대로");
    const HText save = HTEXT("T.Test", "Save", "Save");
    check(save.toString() == "Save" && save.isLocalized() && save.ns() == "T.Test" && save.key() == "Save", "HTEXT: 표가 없으면 원문");
    HText copy = save;
    check(copy.identicalTo(save) && HText().isEmpty(), "복사는 같은 데이터 · 빈 값");

    const char* po =
        "# comment\n"
        "msgid \"\"\n"
        "msgstr \"\"\n"
        "\"Language: ko\\n\"\n"
        "\n"
        "msgctxt \"T.Test,Save\"\n"
        "msgid \"Save\"\n"
        "msgstr \"\xEC\xA0\x80\xEC\x9E\xA5\"\n"          // 저장
        "\n"
        "msgctxt \"T.Test,Multi\"\n"
        "msgid \"\"\n"
        "\"Line one\\n\"\n"
        "\"Line \\\"two\\\"\"\n"
        "msgstr \"\"\n"
        "\"\xEC\xB2\xAB \xEC\xA4\x84\\n\"\n"             // 첫 줄\n
        "\"\xEB\x91\x98\xEC\xA7\xB8 \\\"\xEC\xA4\x84\\\"\"\n"   // 둘째 "줄"
        "\n"
        "msgctxt \"T.Test,Old\"\n"
        "msgid \"Old source\"\n"
        "msgstr \"\xEC\x98\x9B\"\n"                       // 옛
        "\n"
        "msgctxt \"T.Test,Empty\"\n"
        "msgid \"Empty\"\n"
        "msgstr \"\"\n"
        "\n"
        "msgctxt \"broken-no-comma\"\n"
        "msgid \"x\"\n"
        "msgstr \"y\"\n"
        "\n"
        "msgctxt \"T.Test,Fmt\"\n"
        "msgid \"{0} of {1}\"\n"
        "msgstr \"{1} \xEC\xA4\x91 {0}\"\n";            // {1} 중 {0}
    std::vector<std::string> errs;
    const size_t n = htext::loadPo(po, "ko", htext::Layer::Engine, &errs);
    check(n == 5 && errs.size() == 1, "loadPo: 항목 5 · 고장 1 (" + std::to_string(n) + ", " + std::to_string(errs.size()) + ")");
    check(save.toString() == "Save", "다른 문화권 표는 지금 문화권(en)에 쓰지 않음");
    const uint32_t r0 = htext::revision();
    htext::setCulture("ko");
    check(htext::revision() != r0, "setCulture: 판 번호가 오름");
    check(save.toString() == "\xEC\xA0\x80\xEC\x9E\xA5", "ko: 번역(저장), 이미 만든 HText 도 다시 풀림");
    check(HTEXT("T.Test", "Multi", "Line one\nLine \"two\"").toString() == "\xEC\xB2\xAB \xEC\xA4\x84\n\xEB\x91\x98\xEC\xA7\xB8 \"\xEC\xA4\x84\"",
          "여러 줄 · 이스케이프");
    check(HTEXT("T.Test", "Old", "New source").toString() == "New source", "원문이 바뀐 낡은 번역은 쓰지 않음");
    check(HTEXT("T.Test", "Empty", "Empty").toString() == "Empty", "빈 번역은 원문");
    check(HTEXT("T.Test", "Nope", "Nope").toString() == "Nope", "표에 없는 키는 원문");
    // 층: 프로젝트(Game)가 엔진을 이김
    htext::loadPo("msgctxt \"T.Test,Save\"\nmsgid \"Save\"\nmsgstr \"GAME\"\n", "ko", htext::Layer::Game);
    check(save.toString() == "GAME", "Game 층이 Engine 층을 이김");

    // 서식
    const HText fmt = HText::format(HTEXT("T.Test", "Fmt", "{0} of {1}"), { 3, HText::fromString("X") });
    check(fmt.toString() == "X \xEC\xA4\x91 3", "format: 번역된 틀의 어순({1} 중 {0})");
    htext::setCulture("en");
    check(fmt.toString() == "3 of X", "format: 문화권을 바꾸면 다시 풂");
    check(HText::format(HText::fromString("{{a}} {0} {2}"), { "b" }).toString() == "{a} b {2}", "format: 중괄호 이스케이프 · 없는 인자는 그대로");
    check(HText::formatNamed(HText::fromString("{Name} has {Count} items"), { { "Name", "Box" }, { "Count", 1234 } }).toString() ==
          "Box has 1,234 items", "formatNamed: 이름 인자 · 정수 묶음");
    check(htext::formatInteger(-1234567, "en") == "-1,234,567" && htext::formatInteger(0, "ko") == "0", "정수: 묶음 · 음수 · 0");
    check(htext::formatNumber(1234.5, 2, "ko") == "1,234.5" && htext::formatNumber(1234.5, 2, "de") == "1.234,5",
          "실수: ko 1,234.5 · de 1.234,5");
    check(htext::formatNumber(1234.5, 2, "fr") == "1\xC2\xA0" "234,5", "실수: fr 은 줄 바꿈 없는 공백");
    check(htext::formatNumber(-0.001, 2, "en") == "0" && htext::formatNumber(2.0, 3, "en") == "2", "실수: -0 은 0, 끝 0 뺌");
    check(HText::asNumber(0.25).toString() == "0.25" && HText::asInteger(1000).toString() == "1,000", "asNumber · asInteger");
    htext::setCulture("de");
    check(HText::asInteger(1000).toString() == "1.000", "asInteger: 문화권을 따름");
    htext::setCulture("en");
    check(htext::poEscape("a\"b\\c\n") == "a\\\"b\\\\c\\n" && htext::poUnescape(htext::poEscape("x\t\"y\"\n")) == "x\t\"y\"\n",
          "poEscape · poUnescape 왕복");
    htext::clearTables();
}

static void testGatherAndCoverage() {
    std::printf("gather\n");
    htext_gather::Result r;
    std::map<std::string, size_t> idx;
    const std::string src =
        "#define HTEXT(ns, key, source) X\n"
        "// HTEXT(\"Skip\", \"Comment\", \"in a comment\")\n"
        "auto a = HTEXT(\"Ns\", \"K1\", \"Hello \" \"world\");\n"
        "auto b = HTEXT( \"Ns\" ,\n   \"K2\", \"Tab\\there\\n\");\n"
        "auto c = MY_HTEXT(\"Ns\", \"K3\", \"not ours\");\n"
        "auto d = HTEXT(\"Ns\", \"K1\", \"Hello world\");\n"
        "auto e = HTEXT(\"Ns\", \"K1\", \"Different\");\n"
        "auto f = HTEXT(name, \"K4\", \"not literal\");\n";
    htext_gather::gatherText(src, "x.cpp", r, idx);
    check(r.entries.size() == 2, "gather: 리터럴 HTEXT 둘(주석 · 매크로 정의 · 다른 이름 · 리터럴 아님은 건너뜀)");
    check(!r.entries.empty() && r.entries[0].source == "Hello world" && r.entries[0].where == "x.cpp:3", "gather: 이어 붙인 리터럴 · 자리");
    check(r.entries.size() > 1 && r.entries[1].source == "Tab\there\n", "gather: 여러 줄 인자 · 이스케이프");
    check(r.conflicts.size() == 1, "gather: 같은 키 · 다른 원문은 충돌");
    const std::string pot = htext_gather::toPot(r);
    check(pot.find("msgctxt \"Ns,K2\"\nmsgid \"Tab\\there\\n\"\nmsgstr \"\"") != std::string::npos, "toPot: 이스케이프한 항목");

    // T3-8: 소스의 모든 HTEXT 키를 한국어 표가 덮음
    const htext_gather::Result all = htext_gather::gatherDirs({ std::filesystem::path(HARUKA_SOURCE_DIR) });
    check(all.conflicts.empty(), "소스: 같은 키 · 다른 원문 없음");
    check(all.entries.size() >= 80, "소스: 에디터 글 " + std::to_string(all.entries.size()) + " 개를 모음");
    std::ifstream f(std::string(HARUKA_ENGINE_CONTENT_DIR) + "/Localization/ko/Engine.po", std::ios::binary);
    const std::string ko((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<std::string> errs;
    htext::clearTables();
    const size_t n = htext::loadPo(ko, "ko", htext::Layer::Engine, &errs);
    check(n > 0 && errs.empty(), "ko/Engine.po 를 오류 없이 읽음 (" + std::to_string(n) + ")");
    htext::setCulture("ko");
    int missing = 0;
    for (const auto& e : all.entries)
        if (!htext::lookup(e.ns, e.key, e.source)) {
            ++missing;
            std::printf("    missing ko: %s,%s (%s)\n", e.ns.c_str(), e.key.c_str(), e.where.c_str());
        }
    check(missing == 0, "ko 표가 소스 키를 모두 덮음(빠진 키 " + std::to_string(missing) + ")");
    htext::setCulture("en");
    htext::clearTables();
}

// T1-11: 저장 대화상자의 씬 이름(모든 언어의 글자)
static void testSceneNames() {
    std::printf("scene names\n");
    std::string out;
    check(project_settings::normalizeSceneName("  Level_01 ", out) && out == "Level_01", "ASCII 이름 · 앞뒤 공백");
    check(project_settings::normalizeSceneName("maps/" + kHan + kGeul + " 1.hscene", out) && out == "maps/" + kHan + kGeul + " 1",
          "한글 이름 · 폴더 · 확장자 뗌");
    check(project_settings::normalizeSceneName(kJamoHan, out) && out == kHan, "조합형 한글은 완성형으로");
    check(project_settings::normalizeSceneName("\xE6\xBC\xA2\xE5\xAD\x97", out), "한자 이름");
    check(!project_settings::normalizeSceneName("a:b", out) && !project_settings::normalizeSceneName("a*b", out) &&
          !project_settings::normalizeSceneName("a\\b", out), "Windows 금지 문자(: * \\)는 거절");
    check(!project_settings::normalizeSceneName("/a", out) && !project_settings::normalizeSceneName("a//b", out) &&
          !project_settings::normalizeSceneName("", out) && !project_settings::normalizeSceneName("\xFF", out),
          "빈 이름 · 앞 / · // · 깨진 바이트는 거절");
}

// 거울 값 정리(M3a, H1a-2 판단 2): 패널이 거울 값을 직접 끄던 자리가 부르는 releaseQuiet 는 예전 동작(거울 값만 끔)과 같은 결과에
// HSlate 포커스까지 비웁니다. IME · 문자 큐 · 글 칸 상태 · onChanged 는 그대로여야 합니다(setFocus(None) 과 다름).
static void testReleaseQuiet() {
    std::printf("release quiet (M3a)\n");
    FakeWindow fw;
    IWindow*   wp = &fw;
    HSlateContext slate;
    HTextEditState renEdit, srchEdit;
    bool renFoc = false, srchFoc = false;
    int  renChanged = 0;
    auto renW = HNew(SSearchBox), srchW = HNew(SSearchBox);
    renW->focusable(true); srchW->focusable(true);
    TextInputController tc;
    TextInputController::Deps d;
    d.window = &wp;
    d.slate  = &slate;
    d.textFields = {
        { EFocusTarget::Rename, [&] { return std::shared_ptr<HWidget>(renW); }, [&] {
            return TextInputField{ renEdit, renFoc, [](glm::vec2) { return false; }, [&] { ++renChanged; } };
        } },
        { EFocusTarget::Search, [&] { return std::shared_ptr<HWidget>(srchW); }, [&] {
            return TextInputField{ srchEdit, srchFoc, [](glm::vec2) { return false; }, [] {} };
        } },
    };
    tc.init(d);

    renEdit.setText("Table");
    tc.setFocus(EFocusTarget::Rename);
    check(tc.currentFocus() == EFocusTarget::Rename && renFoc && fw.imeOn && slate.focusedWidget() == renW,
          "이름 바꾸기 들어감: 거울 값 · IME · HSlate 포커스");
    const int changedBefore = renChanged, togglesBefore = fw.imeToggles;
    fw.chars = { 'q' };
    tc.releaseQuiet(EFocusTarget::Rename);
    check(!renFoc && tc.currentFocus() == EFocusTarget::None, "releaseQuiet: 거울 값 꺼짐 · 포커스 없음");
    check(!slate.focusedWidget(), "releaseQuiet: HSlate 포커스(정본)도 비움");
    check(fw.imeOn && fw.imeToggles == togglesBefore, "releaseQuiet: IME 는 그대로(예전 동작, 끌지는 사용자 결정)");
    check(fw.chars.size() == 1 && renChanged == changedBefore && renEdit.isAllSelected() && renEdit.text == "Table",
          "releaseQuiet: 문자 큐 · onChanged · 선택 · 글 그대로");

    // 다른 칸이 포커스일 때 부르면 그 칸은 건드리지 않습니다.
    tc.setFocus(EFocusTarget::Search);
    tc.releaseQuiet(EFocusTarget::Rename);
    check(srchFoc && slate.focusedWidget() == srchW && tc.currentFocus() == EFocusTarget::Search,
          "다른 칸 포커스 중 releaseQuiet(Rename): 검색 칸 포커스 유지");
    tc.setFocus(EFocusTarget::None);
}

// M3b: 여러 OS 창. 키는 키보드 창에서 읽고, 칸은 그 칸이 있는 창에 키보드가 있을 때만 받습니다. IME 는 칸이 있는 창만 켭니다.
static void testKeyboardWindow() {
    std::printf("keyboard window (M3b)\n");
    FakeWindow mainW, floatW;   // 0 = 메인, 3 = 떠 있는 창
    IWindow*   wp = &mainW;
    HSlateContext slate;
    HTextEditState srchEdit, conEdit;
    bool srchFoc = false, conFoc = false;
    int  deleted = 0;
    int  kb = 0;              // 지금 키보드 창
    int  outlinerWin = 3;     // 검색 칸(아웃라이너)이 있는 창
    auto srchW = HNew(SSearchBox), conW = HNew(SSearchBox);
    srchW->focusable(true); conW->focusable(true);
    TextInputController tc;
    TextInputController::Deps d;
    d.window = &wp;
    d.slate  = &slate;
    d.onDeleteSelection = [&] { ++deleted; };
    d.textFields = {
        { EFocusTarget::Search, [&] { return std::shared_ptr<HWidget>(srchW); }, [&] {
            return TextInputField{ srchEdit, srchFoc, [](glm::vec2 p) { return p.x >= 0.f && p.x < 100.f && p.y >= 0.f && p.y < 20.f; },
                                   [] {} };
        } },
        { EFocusTarget::Console, [&] { return std::shared_ptr<HWidget>(conW); }, [&] {
            return TextInputField{ conEdit, conFoc, [](glm::vec2) { return true; }, [] {} };
        } },
    };
    d.keyboardWindow = [&] { return kb; };
    d.windowOfFocus  = [&](EFocusTarget t) { return t == EFocusTarget::Search ? outlinerWin : 0; };
    d.windowById     = [&](int id) -> IWindow* { return id == 0 ? (IWindow*)&mainW : id == 3 ? (IWindow*)&floatW : nullptr; };
    d.windowIds      = [] { return std::vector<int>{ 0, 3 }; };
    tc.init(d);
    auto step = [&] { tc.tickInput(); mainW.endFrame(); floatW.endFrame(); };

    // IME 는 칸이 있는 창만.
    tc.setFocus(EFocusTarget::Search);
    check(srchFoc && floatW.imeOn && !mainW.imeOn, "떠 있는 창의 칸에 포커스: IME 는 그 창만 켬");
    check(tc.focusWindow() == 3, "focusWindow = 칸이 있는 창");
    // 좌표 판정은 그 창 좌표로만.
    check(tc.focusWidgetContains(3, 10.f, 10.f) && !tc.focusWidgetContains(0, 10.f, 10.f) && !tc.focusWidgetContains(10.f, 10.f),
          "focusWidgetContains: 칸이 있는 창 좌표만 맞음(메인 좌표는 같은 자리여도 칸 밖)");

    // 키보드가 메인이면 그 칸은 글자를 받지 않음(메인의 글자는 버림).
    mainW.chars = { 'a', 'b' };
    step();
    check(srchEdit.text.empty() && mainW.chars.empty(), "키보드 = 메인: 떠 있는 창의 칸은 받지 않고 메인 글자는 버림");
    mainW.tap(HInput::KEY_ESCAPE);
    step();
    check(tc.currentFocus() == EFocusTarget::Search, "키보드 = 메인: 메인의 Esc 는 다른 창의 칸을 끝내지 않음");

    // 키보드가 떠 있는 창으로 오면 받음.
    kb = 3;
    floatW.chars = { 'h', 'i' };
    step();
    check(srchEdit.text == "hi", "키보드 = 떠 있는 창: 그 창 글자가 칸에 들어감");
    floatW.comp = "x";
    floatW.rect[0] = -1.f;
    srchEdit.caretRect = { 40.f, 5.f, 2.f, 14.f };
    step();
    check(floatW.rect[0] == 40.f && mainW.rect[0] == 0.f, "IME 후보 창 자리는 칸이 있는 창에 맞춤");
    floatW.comp.clear();
    step();

    // 창이 바뀔 때 눌려 있던 키는 엣지로 잡지 않음: 메인에서 Enter 를 누른 채 떠 있는 창으로 → Enter 엣지 없음.
    kb = 0;
    step();
    floatW.down[HInput::KEY_ENTER] = true;
    kb = 3;
    tc.tickInput();   // endFrame 없이(누른 채)
    check(tc.currentFocus() == EFocusTarget::Search, "키보드 창이 바뀌는 순간 이미 눌려 있던 Enter 는 확정으로 안 잡음");
    floatW.down[HInput::KEY_ENTER] = false;
    tc.tickInput();
    floatW.tap(HInput::KEY_ENTER);
    step();
    check(tc.currentFocus() == EFocusTarget::None && !srchFoc, "그다음 새로 누른 Enter 는 확정(포커스 놓음)");
    check(!floatW.imeOn && !mainW.imeOn, "포커스 없음: 모든 창의 IME 끔");

    // 포커스 없음: 키보드 창의 Delete 가 선택 삭제(떠 있는 창에서도 편집 단축키).
    floatW.tap(HInput::KEY_DELETE);
    step();
    check(deleted == 1, "포커스 없음 + 키보드 = 떠 있는 창: 그 창의 Delete 가 선택 삭제");
    mainW.tap(HInput::KEY_DELETE);
    step();
    check(deleted == 1, "키보드가 떠 있는 창이면 메인의 Delete 는 읽지 않음");

    // 메인에 있는 칸(콘솔)은 메인 키보드일 때만.
    tc.setFocus(EFocusTarget::Console);
    check(mainW.imeOn && !floatW.imeOn, "메인 칸: IME 는 메인만");
    floatW.chars = { 'z' };
    step();
    check(conEdit.text.empty() && floatW.chars.empty(), "키보드 = 떠 있는 창: 메인 칸은 받지 않음");
    kb = 0;
    mainW.chars = { 'q' };
    step();
    check(conEdit.text == "q", "키보드 = 메인: 메인 칸이 받음");

    // 패널이 메인으로 돌아오면(창 번호 0) 같은 칸이 메인 키보드로 받음.
    tc.setFocus(EFocusTarget::Search);
    outlinerWin = 0;
    mainW.chars = { 'k' };
    step();
    check(srchEdit.text.size() >= 1 && srchEdit.text.back() == 'k', "칸이 메인으로 오면 메인 키보드로 받음");
    tc.setFocus(EFocusTarget::None);
}

// H3a → H3b: 디테일 글 칸(PropertyTextEdit) — Enter 확정 · Esc 취소 · 바깥(포커스 잃음) 확정(H3b, UE · 숫자 칸과 같음), 같은 값은 확정하지 않음.
static void testPropertyTextEdit() {
    std::printf("property text field (H3a, H3b outside = commit)\n");
    FakeWindow fw;
    IWindow*   wp = &fw;
    HSlateContext slate;
    PropertyTextEdit pe;
    std::string value = "patrol";
    int commits = 0, changes = 0;
    auto w = HNew(SEditableText);
    w->focusable(true);
    auto other = HNew(SSearchBox);
    other->focusable(true);
    HTextEditState otherEdit;
    bool otherFoc = false;
    TextInputController tc;
    TextInputController::Deps d;
    d.window = &wp;
    d.slate  = &slate;
    d.textFields = {
        { EFocusTarget::Search, [&] { return std::shared_ptr<HWidget>(other); }, [&] {
            return TextInputField{ otherEdit, otherFoc, [](glm::vec2) { return false; }, [] {} };
        } },
        { EFocusTarget::TextField, [&] { return std::shared_ptr<HWidget>(w); }, [&] {
            return pe.field([&] { ++changes; }, [](glm::vec2) { return false; });
        } },
    };
    tc.init(d);
    auto step = [&] { tc.tickInput(); fw.endFrame(); };
    auto begin = [&] {
        pe.prepare("row", [&] { return value; }, [&](const std::string& t) { value = t; ++commits; });
        return tc.setFocusWidget(w);
    };
    tc.setFocusWidget(w);
    check(!pe.focused() && tc.currentFocus() == EFocusTarget::None, "편집할 행이 없으면 들어가지 않음(onBegin 거절)");
    check(begin() && pe.focused() && pe.editing("row") && tc.currentFocus() == EFocusTarget::TextField, "행 편집 시작 = TextField 포커스");
    check(pe.edit().text == "patrol" && pe.edit().isAllSelected(), "지금 값 · 전체 선택");
    step();
    fw.chars = { 'g', 'u', 'a', 'r', 'd' };
    step();
    check(pe.edit().text == "guard" && value == "patrol", "치는 동안 값은 그대로");
    fw.tap(HInput::KEY_ENTER);
    step();
    check(value == "guard" && commits == 1 && !pe.focused() && pe.key().empty() && tc.currentFocus() == EFocusTarget::None,
          "Enter = 확정 한 번 · 편집 끝");
    // Esc 취소
    check(begin(), "다시 시작");
    step();
    fw.chars = { 'x' };
    step();
    fw.tap(HInput::KEY_ESCAPE);
    step();
    check(value == "guard" && commits == 1 && !pe.focused() && pe.key().empty(), "Esc = 취소(값 그대로)");
    // 바깥 누름(다른 칸으로 포커스 이동 · 포커스 해제) = 확정(H3b: UE · 숫자 칸과 같음. H3a 에서는 취소였음)
    begin();
    step();
    fw.chars = { 'y' };
    step();
    tc.setFocus(EFocusTarget::Search);
    check(value == "y" && commits == 2 && !pe.focused() && otherFoc && pe.key().empty(), "다른 칸으로 = 확정(한 번)");
    begin();
    step();
    fw.chars = { 'z' };
    step();
    tc.setFocus(EFocusTarget::None);
    check(value == "z" && commits == 3 && !pe.focused(), "포커스 해제(바깥 누름) = 확정");
    // 바깥으로 나가도 같은 값이면 확정하지 않음
    begin();
    step();
    tc.setFocus(EFocusTarget::None);
    check(value == "z" && commits == 3, "같은 값으로 나가면 기록 없음");
    // Esc 는 여전히 취소(나감보다 먼저 표시해 둠) · 다음 편집에 남지 않음
    begin();
    step();
    fw.chars = { 'q' };
    step();
    fw.tap(HInput::KEY_ESCAPE);
    step();
    check(value == "z" && commits == 3 && !pe.focused(), "Esc = 취소(나감 확정 없음)");
    begin();
    step();
    fw.chars = { 'r' };
    step();
    tc.setFocus(EFocusTarget::None);
    check(value == "r" && commits == 4, "Esc 다음 편집의 바깥 누름은 다시 확정");
    // Enter 뒤의 포커스 해제가 한 번 더 확정하지 않음
    begin();
    step();
    fw.chars = { 's' };
    step();
    fw.tap(HInput::KEY_ENTER);
    step();
    check(value == "s" && commits == 5, "Enter 확정은 한 번(뒤따르는 나감은 확정 안 함)");
    // 같은 값 Enter 는 확정하지 않음(Undo 기록 없음)
    begin();
    step();
    fw.tap(HInput::KEY_ENTER);
    step();
    check(commits == 5 && !pe.focused(), "같은 값 Enter 는 확정 안 함");
    check(changes > 0, "포커스가 바뀔 때마다 행 다시 만들기 알림");
    // 조용히 놓기(행이 사라짐)
    begin();
    tc.releaseQuiet(EFocusTarget::TextField);
    pe.forget();
    check(!pe.focused() && tc.currentFocus() == EFocusTarget::None && commits == 5, "행이 사라지면 조용히 놓음(확정 없음)");
}

// H3b: 디테일 속성 검색 — 이름 맞춤 · 문화권(보이는 이름 · 영어 원문) · 묶음 접기.
static void testDetailsSearch() {
    std::printf("details property search (H3b)\n");
    check(!hdsearch::active("") && !hdsearch::active("   ") && hdsearch::active(" a "), "빈 글 · 공백만 = 검색 아님");
    check(hdsearch::matches("", "Intensity", "Intensity"), "빈 검색어 = 모두 맞음");
    check(hdsearch::matches("int", "Intensity", "Intensity") && hdsearch::matches("INT", "Intensity", "Intensity"), "부분 글 · 대소문자 무시");
    check(!hdsearch::matches("radius", "Intensity", "Intensity"), "맞지 않음");
    check(hdsearch::matches("max dist", "Max Distance", "Max Distance") && !hdsearch::matches("max zz", "Max Distance", "Max Distance"),
          "낱말이 모두 맞아야 함(순서 무관)");
    check(hdsearch::matches("dist max", "Max Distance", "Max Distance"), "낱말 순서 무관");
    // 한국어 문화권: 보이는 이름 = 위치, 영어 원문 = Location. 둘 다로 찾음.
    check(hdsearch::matches("위치", "위치", "Location") && hdsearch::matches("loc", "위치", "Location"), "문화권 이름 · 영어 이름 모두");
    check(hdsearch::matches("위 loc", "위치", "Location"), "낱말마다 어느 이름이든");
    check(!hdsearch::matches("회전", "위치", "Location"), "다른 한국어 이름은 맞지 않음");
    check(hdsearch::sectionExpanded(true, 2, false) && !hdsearch::sectionExpanded(true, 0, true), "검색 중: 맞는 행이 있으면 펼침 · 없으면 접음");
    check(hdsearch::sectionExpanded(false, 0, true) && !hdsearch::sectionExpanded(false, 5, false), "검색 아님: 사용자가 정한 접힘 그대로");
    check(!hdsearch::showInfoLine(true) && hdsearch::showInfoLine(false), "검색 중 읽기 정보 줄 숨김");
    check(hdsearch::showNoMatch(true, 0) && !hdsearch::showNoMatch(true, 1) && !hdsearch::showNoMatch(false, 0), "맞는 행 없음 알림");
}

int main() {
    testUtf8();
    testGraphemes();
    testWordsNfcCase();
    testEditState();
    testController();
    testFocusWidgets();
    testReleaseQuiet();
    testKeyboardWindow();
    testPropertyTextEdit();
    testDetailsSearch();
    testHText();
    testGatherAndCoverage();
    testSceneNames();
    std::printf("\n%s (%d passed, %d failed)\n", g_fail ? "FAILED" : "ALL PASSED", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
