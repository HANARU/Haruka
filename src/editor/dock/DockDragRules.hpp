#pragma once
#include "DockTree.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

// 탭 끌기 상태 기계의 순수 규칙(다중 OS 창 M4, 2026-11-22, Docs/Editor_MultiWindow_Design.md 3절 M4).
//
// 탭 끌기는 누른 창(원래 창)의 커서 좌표로 돌지만, 놓을 자리는 화면 좌표로 고릅니다. OS 가 누른 창에 커서를 잡아 두므로 끄는 동안
// 원래 창 밖의 좌표도 계속 들어오고, 그 좌표에 원래 창 내용 영역의 화면 자리를 더하면 화면 좌표가 됩니다. 결정 D1(모든 창이 메인
// 배율 하나)이라 배율은 곱하지 않습니다. 위젯 · 플랫폼 · glm 을 모르므로 dock_tree 시험이 그대로 부릅니다.
namespace hdockdrag {

// 화면 좌표의 사각형(픽셀, 창이면 내용 영역 — 제목 줄 · 테두리 제외).
struct ScreenRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const {
        return w > 0 && h > 0 && px >= (float)x && py >= (float)y && px < (float)(x + w) && py < (float)(y + h);
    }
};

// 놓을 후보 창 하나. front 가 클수록 앞(위)에 있는 창입니다(FrontOrder). usable 이 거짓이면(최소화 · 창 없음) 후보가 아닙니다.
struct DropWindow {
    int        id     = 0;   // 0 = 메인, 1.. = 떠 있는 창 번호
    ScreenRect rect;
    uint64_t   front  = 0;
    bool       usable = true;
};

// 커서 아래 창들 가운데 가장 앞의 창 번호. 어느 창 위도 아니면 -1 입니다. 끄는 중인 원래 창도 후보입니다(같은 창 재도킹).
// front 가 같으면 목록 순서로 처음 것입니다.
inline int pickDropWindow(float sx, float sy, const std::vector<DropWindow>& wins) {
    int best = -1;
    uint64_t bestFront = 0;
    bool found = false;
    for (const DropWindow& w : wins) {
        if (!w.usable || !w.rect.contains(sx, sy)) continue;
        if (!found || w.front > bestFront) {
            best = w.id;
            bestFront = w.front;
            found = true;
        }
    }
    return best;
}

inline const DropWindow* findWindow(const std::vector<DropWindow>& wins, int id) {
    for (const DropWindow& w : wins)
        if (w.id == id) return &w;
    return nullptr;
}

// 창의 앞뒤 순서(Z 순서 대안). GLFW 에는 Z 순서 API 가 없으므로 "마지막으로 포커스를 받은 순서"로 대신합니다:
//   - 처음 보는 창은 맨 앞(새 창은 OS 에서도 위에 뜸),
//   - 포커스가 새로 온 창(직전 프레임에 포커스가 없었음)은 맨 앞,
//   - 에디터가 창을 앞으로 가져올 때(bump) 맨 앞(숨은 실행은 OS 포커스가 없어 이 경로만 씀).
// Windows 에서 떠 있는 창은 메인에 딸린 창이 아니라, 메인을 누르면 메인이 떠 있는 창 위로 올라오는 것까지 이 규칙과 같습니다.
// 다른 프로그램의 창이 위를 덮는 경우는 알 수 없습니다.
class FrontOrder {
public:
    void note(int id, bool focused) {
        Entry* e = find(id);
        if (!e) {
            entries_.push_back({ id, ++next_, focused });
            return;
        }
        if (focused && !e->focused) e->front = ++next_;
        e->focused = focused;
    }
    void bump(int id) {
        if (Entry* e = find(id)) e->front = ++next_;
        else entries_.push_back({ id, ++next_, false });
    }
    // 목록에 없는 창을 잊습니다(같은 번호가 다시 생기면 새 창으로 맨 앞).
    void retain(const std::vector<int>& ids) {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                      [&](const Entry& e) { return std::find(ids.begin(), ids.end(), e.id) == ids.end(); }),
                       entries_.end());
    }
    uint64_t front(int id) const {
        for (const Entry& e : entries_)
            if (e.id == id) return e.front;
        return 0;
    }

private:
    struct Entry {
        int      id;
        uint64_t front;
        bool     focused;
    };
    Entry* find(int id) {
        for (Entry& e : entries_)
            if (e.id == id) return &e;
        return nullptr;
    }
    std::vector<Entry> entries_;
    uint64_t           next_ = 0;
};

// 리프 안 커서 자리 → 도킹 영역(가장자리 28% = 분할, 가운데 = 탭)과 미리보기 사각형(리프 좌표와 같은 좌표계).
struct ZoneRect {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
};
inline EDockZone zoneInLeaf(float px, float py, const ZoneRect& leaf, ZoneRect& highlight) {
    const float rx = (px - leaf.x) / std::max(1.f, leaf.w);
    const float ry = (py - leaf.y) / std::max(1.f, leaf.h);
    const float edge = 0.28f;
    highlight = leaf;
    if (rx < edge)       { highlight.w *= 0.5f; return EDockZone::Left; }
    if (rx > 1.f - edge) { highlight.x += leaf.w * 0.5f; highlight.w *= 0.5f; return EDockZone::Right; }
    if (ry < edge)       { highlight.h *= 0.5f; return EDockZone::Top; }
    if (ry > 1.f - edge) { highlight.y += leaf.h * 0.5f; highlight.h *= 0.5f; return EDockZone::Bottom; }
    return EDockZone::Center;
}

// 놓을 때 할 일.
enum class DropAction {
    None,           // 아무 일 없음(다른 창의 리프가 아닌 곳 · 통째 끌기를 창 밖에 놓음 · 6px 안 움직인 통째 끌기)
    Activate,       // 끌지 않고 뗌 = 탭 누르기(그 탭을 활성으로)
    RedockSame,     // 같은 창 안 재도킹(dockRedock)
    RedockAcross,   // 다른 창의 리프로(dockRedockAcross)
    NewFloat,       // 어느 창 위도 아닌 곳 → 새 떠 있는 창(dockMoveToFloat)
    MergeFloat,     // 떠 있는 창 통째로 다른 창의 리프에(dockMergeFloatInto)
    Rejected,       // 거부(떼기 금지 패널을 창 밖 · 떠 있는 창에 놓음, D8)
};

struct DropQuery {
    bool dragActive = false;   // 누른 자리에서 6px 넘게 움직였음
    bool mergeDrag  = false;   // 떠 있는 창의 탭 줄 빈 곳을 눌러 창 통째로 끄는 중
    int  srcWin     = 0;       // 원래 창(0 = 메인)
    int  overWin    = -1;      // pickDropWindow 결과(-1 = 어느 창 위도 아님)
    bool overLeaf   = false;   // overWin 의 리프 위(영역이 정해짐)
    bool floatable  = true;    // 끄는 패널(통째 끌기면 그 창의 모든 패널)을 떠 있는 창에 둘 수 있는지
};

inline DropAction decideDrop(const DropQuery& q) {
    if (!q.dragActive) return q.mergeDrag ? DropAction::None : DropAction::Activate;
    if (q.mergeDrag) {
        // 창 자체를 옮기는 일은 OS 제목 표시줄이 합니다. 통째 끌기는 다른 창의 리프에 놓을 때만 뜻이 있습니다.
        if (q.overWin < 0 || q.overWin == q.srcWin || !q.overLeaf) return DropAction::None;
        if (q.overWin != 0 && !q.floatable) return DropAction::Rejected;
        return DropAction::MergeFloat;
    }
    if (q.overWin < 0) return q.floatable ? DropAction::NewFloat : DropAction::Rejected;
    if (q.overWin == q.srcWin) return q.overLeaf ? DropAction::RedockSame : DropAction::None;
    if (!q.overLeaf) return DropAction::None;
    if (q.overWin != 0 && !q.floatable) return DropAction::Rejected;
    return DropAction::RedockAcross;
}

// 끄는 동안의 커서 모양(IWindow::setCursorShape): 0 = 화살표(리프 위 — 미리보기가 그 창에 그려짐 · 놓을 곳 없음),
// 3 = 이동(놓으면 새 창이 생김), 4 = 금지(놓아도 거부). 창 밖에서는 미리보기를 그릴 창이 없으므로 커서 모양이 미리보기입니다.
inline int dragCursor(DropAction a) {
    switch (a) {
        case DropAction::NewFloat: return 3;
        case DropAction::Rejected: return 4;
        default:                   return 0;
    }
}

// 새 떠 있는 창의 자리 · 크기(내용 영역, 화면 좌표). 끈 탭이 새 창의 첫 탭이 되므로(탭 줄 왼쪽 위), 누를 때 탭 안에서 잡은 자리
// (grab)가 커서 아래에 오게 둡니다(UE 와 같음). 크기는 원래 리프 크기를 기준으로 하되 너무 작거나 크지 않게 고치고, 리프 크기를
// 모르면 기본값입니다. 작업 영역 끼우기는 부르는 쪽이 hwinplace::clampToWorkAreas 로 합니다.
inline constexpr int kNewFloatDefaultW = 480;
inline constexpr int kNewFloatDefaultH = 400;
inline constexpr int kNewFloatMinW     = 320;
inline constexpr int kNewFloatMinH     = 240;
inline constexpr int kNewFloatMaxW     = 1280;
inline constexpr int kNewFloatMaxH     = 900;
inline ScreenRect newFloatRect(float sx, float sy, float grabX, float grabY, float leafW, float leafH) {
    ScreenRect r;
    r.w = leafW >= 1.f ? std::clamp((int)leafW, kNewFloatMinW, kNewFloatMaxW) : kNewFloatDefaultW;
    r.h = leafH >= 1.f ? std::clamp((int)leafH, kNewFloatMinH, kNewFloatMaxH) : kNewFloatDefaultH;
    const float gx = std::clamp(grabX, 0.f, (float)r.w - 1.f);
    const float gy = std::clamp(grabY, 0.f, (float)r.h - 1.f);
    r.x = (int)(sx - gx);
    r.y = (int)(sy - gy);
    return r;
}

// 이동 뒤 비어 있는 떠 있는 창 번호(마지막 탭이 빠진 창). 부르는 쪽이 이 창들을 목록에서 빼고, OS 창 · 렌더러 대상은 프레임 끝에
// 지웁니다.
inline std::vector<int> emptyFloats(const std::vector<DockFloat>& floats) {
    std::vector<int> out;
    for (const DockFloat& f : floats)
        if (dockTreeEmpty(f.root.get())) out.push_back(f.id);
    return out;
}

// 새 떠 있는 창 번호. 지금 쓰는 번호 다음(dockNextFloatId)과, 이번 실행에서 마지막으로 준 번호 다음 가운데 큰 값입니다. 같은
// 프레임에 지운 창의 번호를 다시 주면 프레임 끝의 OS 창 맞추기가 옛 창을 새 창으로 여기므로(번호가 같음) 번호를 되쓰지 않습니다.
inline int issueFloatId(const std::vector<DockFloat>& floats, int& lastIssued) {
    const int id = std::max(dockNextFloatId(floats), lastIssued + 1);
    lastIssued = id;
    return id;
}

}  // namespace hdockdrag
