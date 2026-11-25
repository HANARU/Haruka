// 떠 있는 도크 창(다중 OS 창 M3a, 2026-11-22, Docs/Editor_MultiWindow_Design.md 3절 M3).
//
// 프레임 흐름:
//   이벤트 펌프(엔진 beginLoop) → 에디터 tick → handleInput(메인 창 입력, 도크 배치가 바뀔 수 있음) → syncFloatWindows →
//   렌더(메인 · 보조 창 목록, 보조 창 문맥은 렌더러가 그 창 크기로 배치) → 다음 프레임.
// syncFloatWindows 가 (1) OS 닫기 단추가 눌린 창을 D4 대로 닫고(안의 탭을 모두 닫음 → 도크가 위젯 트리를 다시 만들고 저장),
// (2) 도크의 떠 있는 창 목록과 OS 창 · 렌더러 대상을 맞춥니다(빠진 창: 렌더러 대상 → OS 창 순으로 없앰, 새 창: 작업 영역 안으로
// 끼운 자리에 만듦). 메인 창이 최소화된 프레임에는 tick · handleInput 대신 tickWhileMinimized 가 같은 함수를 부릅니다.
#include "EditorApp.hpp"
#include "../platform/glfw/GlfwPlatform.hpp"
#include "../renderer/Renderer.hpp"

static constinit HLogCategory LogEditor{ "Editor", ELogVerbosity::Log };

void EditorApp::initFloatWindows() {
    Renderer* r = ctx_.renderer;
    EditorFloatWindows::Ops ops;
    ops.createWindow = [](int w, int h, const std::string& title, bool hidden) {
        return createAuxWindow(w, h, title, hidden);
    };
    ops.addTarget    = [r](IWindow* win) { return r ? r->addWindowTarget(win) : 0u; };
    ops.setUi        = [r](uint32_t id, HSlateContext* c) { if (r) r->setWindowUi(id, c); };
    ops.removeTarget = [r](uint32_t id) { if (r) r->removeWindowTarget(id); };
    ops.workAreas    = [] { return GlfwPlatform::monitorWorkAreas(); };
    // 떠 있는 창 문맥의 팝업 층(H2a): 그 창에서 팝업이 열리면 다른 창의 팝업을 닫습니다(메인 문맥과 같은 훅).
    ops.setupContext = [this](int floatId, HSlateContext& c) {
        c.popups().setPushHook([this, floatId](HPopupLayer::Handle) { dismissPopupsExcept(floatId); });
        c.popups().setSingleRoot(true);   // 창 안 팝업도 한 번에 하나(H2b, 메인과 같음)
    };
    floatWindows_.init(std::move(ops), floatsHidden_);
    // 시험 대본(--ui-float-input, M3b): 숨은 실행에서 떠 있는 창의 누름 · 끌기 · 휠을 흉내 냅니다.
    floatScript_.clear();
    floatScriptNext_ = 0;
    mainScripted_ = false;
    if (!uiFloatInput_.empty()) {
        std::string bad;
        if (!hwininput::parseScript(uiFloatInput_, floatScript_, bad))
            HLOG(LogEditor, Warning) << "--ui-float-input: cannot read step '" << bad << "' (later steps ignored)";
        using K = hwininput::ScriptStep::Kind;
        for (const hwininput::ScriptStep& st : floatScript_)
            if (st.window == 0 && st.kind != K::Place && st.kind != K::Wait && st.kind != K::Focus && st.kind != K::Unfocus &&
                st.kind != K::Key)
                mainScripted_ = true;
        HLOG(LogEditor, Log) << "--ui-float-input: " << floatScript_.size() << " step(s) from frame " << kFloatScriptStartFrame
                             << (mainScripted_ ? " (main window pointer scripted)" : "");
    }
}

// ── 창을 넘는 탭 끌기(다중 OS 창 M4, 2026-11-22) ──────────────────────────────────────────────────────────
//
// EditorDock 은 OS 창을 모르므로 놓을 후보 창의 화면 자리(내용 영역) · 앞뒤 순서를 여기서 줍니다. 앞뒤 순서는 포커스를 받은 순서
// (hdockdrag::FrontOrder, 매 프레임 readFloatPointers 가 갱신)이고, 에디터가 창을 앞으로 가져올 때도 맨 앞으로 올립니다(숨은 실행은
// OS 포커스가 없어 이 경로만). 최소화된 창은 후보가 아닙니다.
void EditorApp::fillDockWindowDeps(EditorDock::Deps& d) {
    d.dropWindows    = [this] { return dropWindows(); };
    d.clampFloatRect = [](hdockdrag::ScreenRect& r) {
        const hwinplace::Rect c = hwinplace::clampToWorkAreas({ r.x, r.y, r.w, r.h }, GlfwPlatform::monitorWorkAreas());
        r = { c.x, c.y, c.w, c.h };
    };
}

std::vector<hdockdrag::DropWindow> EditorApp::dropWindows() const {
    std::vector<hdockdrag::DropWindow> out;
    for (int id : windowIds()) {
        IWindow* w = windowById(id);
        int x = 0, y = 0, ww = 0, wh = 0;
        if (!w || !w->getWindowPos(x, y)) continue;   // 창 자리를 모르면(헤드리스 창) 후보가 아님
        w->getWindowSize(ww, wh);
        out.push_back({ id, { x, y, ww, wh }, frontOrder_.front(id), !w->isIconified() });
    }
    return out;
}

void EditorApp::raiseWindow(int winId) {
    // 숨은 창은 show · focus 가 아무 일도 하지 않으므로(GlfwWindow), 앞뒤 순서는 여기서 직접 올립니다.
    if (winId == 0) {
        if (ctx_.window) ctx_.window->focus();
    } else {
        floatWindows_.raise(winId);
    }
    frontOrder_.bump(winId);
}

void EditorApp::updateFrontOrder() {
    const std::vector<int> ids = windowIds();
    frontOrder_.retain(ids);
    frontOrder_.note(0, (ctx_.window && ctx_.window->isFocused()) || mainSimFocused_);
    for (const auto& w : floatWindows_.windows())
        if (w->window) frontOrder_.note(w->floatId, w->window->isFocused() || w->simFocused);
}

void EditorApp::logWindowRects(const char* why) const {
    for (const hdockdrag::DropWindow& d : dropWindows())
        HLOG(LogEditor, Log) << "--ui-float-input: " << why << ": window " << d.id << " at " << d.rect.x << "," << d.rect.y << " size "
                             << d.rect.w << "x" << d.rect.h << " front " << d.front;
}

void EditorApp::syncFloatWindows() {
    // 1) OS 닫기 단추(D4). 닫을 수 없는 패널이 있어 거부되면 창을 그대로 둡니다(Viewport 는 떠 있는 창에 들어가지 못하므로 지금은
    //    생기지 않는 경우).
    for (int id : floatWindows_.takeCloseRequests()) {
        if (dock_.closeFloat(id)) HLOG(LogEditor, Log) << "Floating window " << id << ": closed by the window's close button";
        else                      floatWindows_.keepOpen(id);
    }
    // 2) 도크가 원하는 창 목록과 맞춥니다.
    std::vector<EditorFloatWindows::Desired> want;
    want.reserve(dock_.floats().size());
    for (const DockFloat& f : dock_.floats())
        want.push_back({ f.id, { f.x, f.y, f.w, f.h }, dock_.floatTitle(f.id), dock_.floatRoot(f.id) });
    floatWindows_.setTooltipStyle(EditorUiKit::tooltipStyle(uiScale_));
    floatWindows_.sync(want);
}

void EditorApp::tickWhileMinimized(float /*dt*/) {
    // 메인이 최소화되면 엔진이 tick · handleInput 을 부르지 않습니다(0 크기 배치를 하지 않음, DEF-106). 떠 있는 창은 계속 보이므로
    // 그 창의 닫기 단추와 생성 · 파괴만 처리합니다. 패널 내용 갱신(로그 줄 · 아웃라이너 등)은 메인이 돌아올 때까지 멈춥니다.
    if (ctx_.world) syncFloatWindows();
}

// --ui-float-cycle: 실행 중에 보조 창을 지웠다가 다시 만드는 경로를 숨은 실행으로 확인합니다(M2 의 --ui-aux-probe=cycle 을 대신함).
// 60 프레임째에 모든 떠 있는 창에 OS 닫기 단추를 누른 것처럼 하고(D4 경로: 탭 닫기 → 프레임 끝에 렌더러 대상 · OS 창 없앰),
// 90 프레임째에 배치 파일을 다시 읽습니다(배치 다시 읽기 경로: 떠 있는 창을 새로 만듦). --ui-layout 과 함께 씁니다.
void EditorApp::tickFloatCycle() {
    if (!uiFloatCycle_) return;
    if (dumpFrameCount_ == 60) {
        for (const auto& w : floatWindows_.windows())
            if (w->window) w->window->setShouldClose(true);
        HLOG(LogEditor, Log) << "--ui-float-cycle: closing " << floatWindows_.windows().size() << " floating window(s)";
    }
    if (dumpFrameCount_ == 90) {
        if (dock_.loadLayout()) {
            dock_.rebuildArea();
            HLOG(LogEditor, Log) << "--ui-float-cycle: layout reloaded (" << dock_.floats().size() << " floating window(s))";
        } else {
            HLOG(LogEditor, Warning) << "--ui-float-cycle: layout reload failed";
        }
    }
}

// ── 창별 입력(다중 OS 창 M3b, 2026-11-22) ──────────────────────────────────────────────────────────────
//
// 프레임 안 순서(handleInput):
//   readFloatPointers(떠 있는 창마다 커서 · 버튼 · 휠을 읽음, 패널 입력 창 맞춤) → 메인 handleInputEvents(예전 순서 그대로, 떠 있는
//   창에 있는 패널의 판정은 건너뜀) → handleFloatInputs(떠 있는 창마다 메인과 같은 순서) → 지연 리빌드 소진 → syncFloatWindows.
// 키 · 글자 · 단축키는 tick 의 textInput_.tickInput · tickShortcuts 가 키보드 창에서 읽습니다.

int EditorApp::keyboardWindowId() const {
    std::vector<hwininput::WindowFocus> v;
    v.reserve(floatWindows_.windows().size());
    for (const auto& w : floatWindows_.windows()) {
        if (!w->window) continue;
        v.push_back({ w->floatId, w->window->isFocused() || w->simFocused, w->window->isIconified() });
    }
    return hwininput::keyboardWindow(v);
}

IWindow* EditorApp::windowById(int id) const {
    if (id == 0) return ctx_.window;
    const EditorWindow* w = floatWindows_.find(id);
    return w ? w->window : nullptr;
}

std::vector<int> EditorApp::windowIds() const {
    std::vector<int> ids{ 0 };
    for (const auto& w : floatWindows_.windows())
        if (w->window) ids.push_back(w->floatId);
    return ids;
}

int EditorApp::windowOfFocusTarget(EFocusTarget t) const {
    switch (t) {
        case EFocusTarget::Rename:
        case EFocusTarget::Search:        return dock_.floatOfPanel("Outliner");
        case EFocusTarget::LogSearch:
        case EFocusTarget::Console:
        case EFocusTarget::ContentSearch: return dock_.floatOfPanel("Bottom");
        case EFocusTarget::NumberField:   return dock_.floatOfPanel("Details");
        // Add Component 팝업은 디테일이 있는 창의 팝업 층에 뜹니다(H2a). 그 창 키보드 규칙(fieldTakesKeys)을 따릅니다.
        case EFocusTarget::Picker:        return dock_.floatOfPanel("Details");
        // 이름표 없는 글 칸은 지금 디테일의 글 칸 하나입니다(H3a).
        case EFocusTarget::TextField:     return dock_.floatOfPanel("Details");
        // 대화상자는 메인 루트에 있습니다(D5).
        default:                          return 0;
    }
}

bool EditorApp::blocksGameInput() const {
    return hwininput::blocksGameInput(playState_ != EPlayState::Editor, keyboardWindowId());
}

void EditorApp::bringPanelForward(const char* panelId, bool visible) {
    // 떠 있는 창의 패널: showPanel 이 그 창 안의 탭을 활성으로 하고 창을 앞으로 가져옵니다(onRaiseFloat → raise, 숨은 창은 그대로).
    if (dock_.floatOfPanel(panelId) != 0 || !visible) dock_.showPanel(panelId);
}

void EditorApp::readFloatPointers() {
    // 시험 대본(--ui-float-input): 시작 프레임부터 한 프레임에 한 단계.
    for (const auto& w : floatWindows_.windows()) w->simSample.scroll = 0.f;
    if (floatScriptNext_ < floatScript_.size() && dumpFrameCount_ >= kFloatScriptStartFrame &&
        dumpFrameCount_ - kFloatScriptStartFrame >= floatScriptNext_) {
        if (floatScriptNext_ == 0) logWindowRects("start");
        const hwininput::ScriptStep& st = floatScript_[floatScriptNext_++];
        if (st.kind == hwininput::ScriptStep::Kind::Key) {
            // 메뉴 키(H2b): 다음 tick 의 tickMenuKeys 가 그 창의 열린 메뉴에 넣습니다.
            hmenu::Key k;
            if (hmenu::parseKey(st.key, k)) scriptMenuKeys_.push_back({ st.window, k });
            HLOG(LogEditor, Log) << "--ui-float-input: frame " << dumpFrameCount_ << " window " << st.window << " key " << st.key;
        } else if (st.kind == hwininput::ScriptStep::Kind::Place) {
            // 창 자리 옮기기(M4): 창 사이 끌기를 정해진 화면 자리에서 흉내 냅니다(숨은 창도 자리는 바뀜, M3a 확인).
            if (IWindow* pw = windowById(st.window)) {
                pw->setWindowPos((int)st.pos.x, (int)st.pos.y);
                logWindowRects("placed");
            } else {
                HLOG(LogEditor, Warning) << "--ui-float-input: no window " << st.window << " (step " << floatScriptNext_ << ")";
            }
        } else if (st.kind != hwininput::ScriptStep::Kind::Wait && st.window == 0) {
            hwininput::applyStep(st, mainSim_, mainSimFocused_);
            HLOG(LogEditor, Log) << "--ui-float-input: frame " << dumpFrameCount_ << " window 0 step " << floatScriptNext_ << " at "
                                 << st.pos.x << "," << st.pos.y << " left=" << mainSim_.left << " focus=" << mainSimFocused_;
        } else if (st.kind != hwininput::ScriptStep::Kind::Wait) {
            if (EditorWindow* w = floatWindows_.find(st.window)) {
                hwininput::applyStep(st, w->simSample, w->simFocused);
                HLOG(LogEditor, Log) << "--ui-float-input: frame " << dumpFrameCount_ << " window " << st.window << " step "
                                     << floatScriptNext_ << " at " << st.pos.x << "," << st.pos.y
                                     << " left=" << w->simSample.left << " right=" << w->simSample.right
                                     << " wheel=" << w->simSample.scroll << " focus=" << w->simFocused;
            } else {
                HLOG(LogEditor, Warning) << "--ui-float-input: no floating window " << st.window << " (step " << floatScriptNext_ << ")";
            }
        }
    }
    for (const auto& w : floatWindows_.windows()) {
        hwininput::PointerSample s;
        if (w->window) {
            // 휠은 읽으면 비워지므로 대본을 쓰는 실행에서도 읽어 버립니다(쌓이지 않게).
            const float osScroll = w->window->getScrollDelta();
            if (!floatScript_.empty()) {
                s = w->simSample;
            } else {
                w->window->getMousePosition(s.pos.x, s.pos.y);
                s.left   = w->window->isMouseButtonDown(HInput::MOUSE_BUTTON_LEFT);
                s.right  = w->window->isMouseButtonDown(HInput::MOUSE_BUTTON_RIGHT);
                s.scroll = osScroll;
            }
        }
        w->sample = s;
    }
    // 시험 대본 실행에서는 키보드 창이 바뀔 때 남깁니다(대본의 focus · unfocus 확인, D7 판정의 입력).
    if (!floatScript_.empty()) {
        const int kb = keyboardWindowId();
        if (kb != loggedKeyboardWin_) {
            loggedKeyboardWin_ = kb;
            HLOG(LogEditor, Log) << "--ui-float-input: keyboard window " << kb << (blocksGameInput() ? " (game input blocked)" : "");
        }
    }
    // 창 앞뒤 순서(M4): 처음 보는 창 · 포커스가 새로 온 창을 맨 앞으로.
    updateFrontOrder();
    // 아웃라이너의 수정자 키 · 좌버튼 · 끌어 놓기 미리보기는 그 패널이 있는 창 것으로.
    const int ow = dock_.floatOfPanel("Outliner");
    outlinerPanel_.setPointerWindow(ow ? windowById(ow) : nullptr);
    outlinerPanel_.useDropOverlay(ow ? dock_.floatDropOverlay(ow) : nullptr);
}

bool EditorApp::contentPointerInMain(float mainX, float mainY, bool mainLeft, glm::vec2& outPos, bool& outLeft) const {
    const int cw = dock_.floatOfPanel("Bottom");
    if (cw == 0) {
        outPos  = { mainX, mainY };
        outLeft = mainLeft;
        return true;
    }
    const EditorWindow* w = floatWindows_.find(cw);
    if (!w || !w->window || !ctx_.window) return false;
    outLeft = w->sample.left;
    int fx = 0, fy = 0, mx = 0, my = 0;
    const glm::vec2 lp = w->sample.pos;
    // 화면 좌표가 없으면 놓지 않습니다. 커서 아래 가장 앞의 창이 메인이 아니면(떠 있는 창 자신 · 메인 앞의 다른 떠 있는 창) 그 뒤에
    // 가려진 메인 뷰포트에 놓지 않습니다(M4: 탭 끌기와 같은 앞뒤 순서 규칙, M3b 는 "자기 창 위면 놓지 않음"만).
    if (!w->window->getWindowPos(fx, fy) || !ctx_.window->getWindowPos(mx, my) ||
        hdockdrag::pickDropWindow(lp.x + (float)fx, lp.y + (float)fy, dropWindows()) != 0) {
        outPos = { -10000.f, -10000.f };
        return true;
    }
    outPos = hwininput::toWindow(lp, { fx, fy }, { mx, my });
    return true;
}

void EditorApp::handleFloatInputs() {
    // 처리 도중 도크 배치가 바뀌어도(탭 닫기 · 재도킹) OS 창 목록은 프레임 끝 syncFloatWindows 에서만 바뀝니다.
    std::vector<EditorWindow*> wins;
    for (const auto& w : floatWindows_.windows()) wins.push_back(w.get());
    for (EditorWindow* w : wins) handleFloatInput(*w);
}

void EditorApp::handleFloatInput(EditorWindow& w) {
    const int id = w.floatId;
    const hwininput::PointerFrame f = hwininput::makeFrame(w.sample, w.prevLeftDown, w.prevRightDown);
    const bool prevLeft = w.prevLeftDown;
    w.prevLeftDown  = f.left;
    w.prevRightDown = f.right;
    if (!w.window || !w.ctx || !w.ctx->rootWidget()) return;
    // 모달(D5)은 메인에 뜨고 모든 창의 입력을 막습니다. 최소화된 창도 받지 않습니다.
    if (!hwininput::floatTakesInput((bool)modal_, w.window->isIconified())) return;
    int fw = 0, fh = 0;
    w.window->getFramebufferSize(fw, fh);
    if (fw <= 0 || fh <= 0) return;
    // 이벤트 전에 그 창 배치부터(DEF-84 와 같은 이유: 이번 프레임에 다시 만든 위젯도 누름을 받게).
    w.ctx->arrange({ (float)fw, (float)fh });
    const glm::vec2 m = f.pos;

    const bool outlinerHere   = hwininput::panelGetsPointer(dock_.floatOfPanel("Outliner"), id);
    const bool bottomHere     = hwininput::panelGetsPointer(dock_.floatOfPanel("Bottom"), id);
    const bool outlinerActive = dock_.panelActiveIn(id, "Outliner");
    const bool bottomActive   = dock_.panelActiveIn(id, "Bottom");

    // 아웃라이너 눌림 유지 풀기 · 행 끌어 놓기(메인과 같은 자리: 포커스 해제 앞).
    if (outlinerHere) {
        if (!f.left) outlinerPanel_.releasePressHeld();
        outlinerPanel_.tickDrag(m.x, m.y, f.left, prevLeft, false, outlinerActive);
    }
    // 포커스 해제: 다른 곳(다른 창 포함)을 누르면 편집을 끝냅니다.
    if (f.leftPressed && currentFocus() != EFocusTarget::None && !focusWidgetContains(id, m.x, m.y))
        setTextFocus(EFocusTarget::None);
    // 팝업 바깥(H2a): 다른 창(메인 · 다른 떠 있는 창)의 팝업 · 메뉴는 이 창을 누르면 모두 닫고, 이 창 팝업 층은 메인과 같은 규칙
    // (왼쪽 = PassThrough 팝업 바깥, 오른쪽 = 오른쪽 메뉴만)으로 닫습니다. 닫기만 하고 누름은 그대로 흘려보냅니다(DEF-64).
    HPopupLayer& popups = w.ctx->popups();
    if (f.leftPressed || f.rightPressed) {
        dismissPopupsExcept(id);
        menus_.close();   // 열었지만 아직 층에 들어가기 전인 메뉴도
        if (f.leftPressed)  popups.dismissOutside(m, 0);
        if (f.rightPressed) popups.dismissOutside(m, 1);
    }
    // 오른쪽 누름: 콘텐츠 브라우저 위면 그 자리의 메뉴를 이 창 팝업 층에 엽니다(메인과 같음, H2a 전에는 열지 않음).
    if (f.rightPressed && !modal_ && bottomTab_ == 0 && bottomHere && bottomActive) contentPanel_.openContextMenuAt(m);
    // 디테일 속성 행(H3a): 이 창에 디테일이 있으면 그 행의 오른쪽 메뉴를 이 창 팝업 층에 엽니다.
    if (f.rightPressed && !modal_ && hwininput::panelGetsPointer(dock_.floatOfPanel("Details"), id) && dock_.panelActiveIn(id, "Details"))
        detailsPanel_.openRowMenuAt(m);
    if (f.rightPressed && !modal_ && hwininput::panelGetsPointer(dock_.floatOfPanel("ProjectSettings"), id) && dock_.panelActiveIn(id, "ProjectSettings"))
        projectSettingsPanel_.openRowMenuAt(m);
    if (f.rightPressed && !modal_ && hwininput::panelGetsPointer(dock_.floatOfPanel("EditorPreferences"), id) && dock_.panelActiveIn(id, "EditorPreferences"))
        editorPreferencesPanel_.openRowMenuAt(m);
    // Esc: 이 창 키(GLFW 키 상태는 창마다)로 closeOnEscape 팝업을 닫습니다.
    if (!popups.empty() && w.window->isKeyDown(HInput::KEY_ESCAPE)) popups.dismissOnEscape();
    // 콘텐츠 브라우저: 누름 · 끌기 시작(놓기는 메인 처리가 메인 뷰포트 좌표로).
    if (f.leftPressed && bottomTab_ == 0 && bottomHere && bottomActive) contentPanel_.pointerPress(m);
    if (!dumpDrag_ && bottomHere) contentPanel_.pointerMove(m, f.left);

    // 메뉴 hover(H2b): 이 창에 뜬 오른쪽 메뉴의 하위 메뉴 열기 · 닫기, 강조(메인 입력 순서의 "메뉴 hover" 자리).
    contextMenu_.hover(w.ctx.get(), m);
    // 스플리터 · 탭(이 창의 창 상태). 팝업 위(또는 Consume 팝업이 열림)면 스플리터 · 탭 끌기를 시작하지 않습니다(메인의 overPopup 과 같음).
    const bool overPopup = popups.overPopup(m);
    if (dock_.handleSplitterInput(id, m.x, m.y, f.left, prevLeft, overPopup, w.window) ||
        dock_.handleTabInput(id, m.x, m.y, f.left, prevLeft, overPopup, w.window))
        return;

    HPointerEvent e;
    e.position = m;
    e.shift    = w.window->isShiftDown();
    e.ctrl     = w.window->isCtrlDown();
    if (f.scroll != 0.f) {
        e.wheelDelta = f.scroll;
        w.ctx->handlePointerEvent(e);
        e.wheelDelta = 0.f;
    }
    if (f.leftPressed) {
        if (outlinerHere && outlinerActive) outlinerPanel_.setPressHeldIfContains(m);
        e.button = 0; e.down = true;
        w.ctx->handlePointerEvent(e);
    }
    if (f.leftReleased) {
        e.button = 0; e.down = false;
        w.ctx->handlePointerEvent(e);
    }
    e.button = -1;
    e.down   = f.left;
    w.ctx->handlePointerEvent(e);
}

// ── 창별 팝업 층(H2a, 2026-11-22) ──────────────────────────────────────────────────────────────────────────
//
// 팝업은 그 앵커가 있는 창 문맥의 팝업 층(HSlateContext::popups)에 뜹니다(D5). 창 사이 규칙:
//   - 한 창을 누르면(왼쪽 · 오른쪽) 다른 창의 팝업은 모두 닫힘(handleInputEvents · handleFloatInput 의 "팝업 바깥" 단계).
//   - 한 창에서 팝업이 열리면 다른 창의 팝업은 닫힘(문맥마다 건 pushHook, UE 도 메뉴는 한 번에 하나).
//   - 패널이 다른 창으로 옮겨 가면 옛 창에 남은 그 패널의 팝업은 닫힘(closePopupsLeftBehind, 도크 보임 알림).
//   - 떠 있는 창이 없어지면 그 창 팝업은 닫힘(EditorFloatWindows::destroy 가 문맥을 버리기 전에 dismissAllPopups).
// 모달은 메인에만 뜨고(D5) 열 때 모든 팝업을 닫습니다(openModal).

HSlateContext* EditorApp::contextOfWindow(int id) {
    if (id == 0) return &slateRenderer_.context();
    EditorWindow* w = floatWindows_.find(id);
    return (w && w->window && w->ctx) ? w->ctx.get() : nullptr;
}

HSlateContext* EditorApp::panelPopupContext(const char* panelId) {
    return contextOfWindow(dock_.floatOfPanel(panelId));
}

void EditorApp::dismissPopupsExcept(int winId) {
    for (int id : windowIds()) {
        if (id == winId) continue;
        if (HSlateContext* c = contextOfWindow(id); c && !c->popups().empty()) c->dismissAllPopups();
    }
}

void EditorApp::closePopupsLeftBehind() {
    if (HSlateContext* c = logPanel_.popupContext(); c && c != panelPopupContext("Bottom")) logPanel_.closePopup();
    if (HSlateContext* c = contextMenu_.popupContext(); c && c != panelPopupContext(contextMenu_.panel().c_str())) contextMenu_.close();
    if (HSlateContext* c = componentPicker_.popupContext(); c && c != panelPopupContext("Details")) componentPicker_.close();
    if (HSlateContext* c = assetPicker_.popupContext(); c && c != panelPopupContext("Details")) assetPicker_.close();
    if (HSlateContext* c = colorPicker_.popupContext(); c && c != panelPopupContext("Details")) colorPicker_.confirm();
}
