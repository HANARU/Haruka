#include "EditorFloatWindows.hpp"
#include "../core/HLog.hpp"
#include <algorithm>

static constinit HLogCategory LogEditorWindows{ "Editor", ELogVerbosity::Log };

EditorWindow* EditorFloatWindows::find(int floatId) {
    for (auto& w : wins_)
        if (w->floatId == floatId) return w.get();
    return nullptr;
}

const EditorWindow* EditorFloatWindows::find(int floatId) const {
    for (const auto& w : wins_)
        if (w->floatId == floatId) return w.get();
    return nullptr;
}

void EditorFloatWindows::destroy(EditorWindow& w) {
    // 순서(Renderer::removeWindowTarget 주석): 렌더러 대상(장치 대기 → 프레임버퍼 · 세마포어 → 스왑체인 → 표면) → OS 창.
    // 표면이 창보다 먼저 사라져야 합니다. 문맥의 루트는 그 뒤에 놓습니다(렌더러가 더는 문맥을 읽지 않음).
    if (w.target && ops_.removeTarget) ops_.removeTarget(w.target);
    w.target = 0;
    if (w.owned) {
        w.owned->cleanup();
        w.owned.reset();
    }
    w.window = nullptr;
    // 그 창 팝업 층의 팝업을 닫습니다(H2a). 소유자(Add Component · 로그 · 오른쪽 메뉴)가 onDismissed 로 상태를 비워, 없어진 문맥을
    // 가리키지 않게 합니다.
    if (w.ctx) w.ctx->dismissAllPopups();
    if (w.ctx) w.ctx->setRootWidget(nullptr);
    w.root.reset();
}

void EditorFloatWindows::sync(const std::vector<Desired>& want) {
    // 1) 원하는 목록에서 빠진 창을 없앱니다.
    for (auto it = wins_.begin(); it != wins_.end();) {
        const int id = (*it)->floatId;
        const bool keep = std::any_of(want.begin(), want.end(), [id](const Desired& d) { return d.floatId == id; });
        if (keep) { ++it; continue; }
        destroy(**it);
        HLOG(LogEditorWindows, Log) << "Floating window " << id << " closed";
        it = wins_.erase(it);
    }
    // 2) 새 번호는 만들고, 남은 창은 제목 · 루트를 맞춥니다.
    for (const Desired& d : want) {
        if (EditorWindow* w = find(d.floatId)) {
            if (w->root != d.root) {
                w->root = d.root;
                w->ctx->setRootWidget(d.root);
            }
            if (w->title != d.title && w->window) {
                w->title = d.title;
                w->window->setTitle(d.title);
            }
            if (hasTooltip_) w->ctx->setTooltipStyle(tooltip_);
            continue;
        }
        if (std::find(failedIds_.begin(), failedIds_.end(), d.floatId) != failedIds_.end()) continue;
        if (!ops_.createWindow || !d.root) continue;

        const std::vector<hwinplace::Rect> areas = ops_.workAreas ? ops_.workAreas() : std::vector<hwinplace::Rect>{};
        const hwinplace::Rect r = hwinplace::clampToWorkAreas(d.rect, areas);
        auto w = std::make_unique<EditorWindow>();
        w->floatId = d.floatId;
        w->owned   = ops_.createWindow(r.w, r.h, d.title, hidden_);
        if (!w->owned) {
            failedIds_.push_back(d.floatId);
            HLOG(LogEditorWindows, Warning) << "Floating window " << d.floatId << " not created (no window)";
            continue;
        }
        w->window = w->owned.get();
        w->window->setWindowPos(r.x, r.y);
        w->target = ops_.addTarget ? ops_.addTarget(w->window) : 0;
        if (w->target == 0) {
            // 렌더러가 받지 못하면(표시 큐 · 형식) 비어 있는 창을 남기지 않습니다.
            failedIds_.push_back(d.floatId);
            HLOG(LogEditorWindows, Warning) << "Floating window " << d.floatId << " not created (renderer refused it)";
            destroy(*w);
            continue;
        }
        w->ctx = std::make_unique<HSlateContext>();
        // 에디터 문맥과 같이 누르면 포커스를 켜지 않습니다(H1a-2). 이번 단계에서 떠 있는 창은 입력을 받지 않지만, M3b 가 같은 규칙에서
        // 시작하게 맞춰 둡니다.
        w->ctx->setClickToFocus(false);
        if (hasTooltip_) w->ctx->setTooltipStyle(tooltip_);
        w->root  = d.root;
        w->title = d.title;
        w->ctx->setRootWidget(d.root);
        if (ops_.setupContext) ops_.setupContext(d.floatId, *w->ctx);
        if (ops_.setUi) ops_.setUi(w->target, w->ctx.get());
        HLOG(LogEditorWindows, Log) << "Floating window " << d.floatId << " opened: " << r.w << "x" << r.h << " at "
                                    << r.x << "," << r.y << (hidden_ ? " (hidden)" : "");
        wins_.push_back(std::move(w));
    }
}

std::vector<int> EditorFloatWindows::takeCloseRequests() {
    std::vector<int> out;
    for (auto& w : wins_) {
        if (!w->window || w->closeReported || !w->window->shouldClose()) continue;
        w->closeReported = true;
        out.push_back(w->floatId);
    }
    return out;
}

void EditorFloatWindows::keepOpen(int floatId) {
    if (EditorWindow* w = find(floatId)) {
        w->closeReported = false;
        if (w->window) w->window->setShouldClose(false);
    }
}

void EditorFloatWindows::destroyAll() {
    for (auto& w : wins_) destroy(*w);
    wins_.clear();
}

bool EditorFloatWindows::readRect(int floatId, hwinplace::Rect& out) const {
    const EditorWindow* w = find(floatId);
    if (!w || !w->window) return false;
    // 최소화된 창은 자리 · 크기가 의미 없는 값(Windows 는 -32000)이라 읽지 않습니다(배치 파일의 값을 그대로 둠).
    if (w->window->isIconified()) return false;
    int x = 0, y = 0, ww = 0, hh = 0;
    if (!w->window->getWindowPos(x, y)) return false;
    w->window->getWindowSize(ww, hh);
    if (ww <= 0 || hh <= 0) return false;
    out = { x, y, ww, hh };
    return true;
}

void EditorFloatWindows::raise(int floatId) {
    if (EditorWindow* w = find(floatId); w && w->window) {
        w->window->show(true);
        w->window->focus();
    }
}
