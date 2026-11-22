#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

// 떠 있는 창을 모니터 작업 영역 안으로 끼우는 순수 계산(다중 OS 창 M3a, 2026-11-22, Docs/Editor_MultiWindow_Design.md 4절 8).
//
// 배치 파일에 남은 창 자리는 그때의 모니터 기준이라, 모니터를 빼거나 해상도를 바꾼 뒤 실행하면 화면 밖에 창이 생길 수 있습니다.
// OS 를 부르지 않으므로 단위 시험(window_platform)이 그대로 확인합니다. 작업 영역은 GlfwPlatform::monitorWorkAreas 가 줍니다.
namespace hwinplace {

// 화면 좌표의 사각형(픽셀). 창이면 내용 영역(제목 줄 · 테두리 제외, glfwGetWindowPos · glfwGetWindowSize 와 같은 기준)입니다.
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

// 제목 줄 높이로 잡는 값입니다. 창의 내용 영역 위에 제목 줄이 있으므로, 이 높이만큼 위가 보여야 끌어서 옮길 수 있습니다.
// 정확한 값은 OS · 배율마다 다르지만, 끼우기는 "일부라도 보이게"가 목적이라 넉넉한 근삿값이면 됩니다.
inline constexpr int kTitleBarPx = 32;
// 제목 줄이 이 폭 이상 보이면 사용자가 둔 자리를 그대로 둡니다(두 모니터에 걸친 창을 옮기지 않음).
inline constexpr int kMinVisiblePx = 64;

inline int overlap1d(int a0, int a1, int b0, int b1) { return std::max(0, std::min(a1, b1) - std::max(a0, b0)); }

// 창 r 을 작업 영역 areas 가운데 하나에 끼웁니다.
//   - 영역이 없으면(모니터 정보 없음) 그대로 돌려줍니다.
//   - 제목 줄 띠(r 바로 위 kTitleBarPx)가 어느 영역에든 세로로 다 들어가고 가로로 kMinVisiblePx(창이 더 좁으면 창 폭) 이상
//     보이면 자리를 그대로 둡니다. 크기가 그 영역보다 크면 줄이고 그 영역 안으로 옮깁니다.
//   - 아니면 제목 줄 띠와 가장 많이 겹치는 영역, 겹치는 곳이 없으면 창 가운데에서 가장 가까운 영역을 골라 그 안에 창 전체(제목
//     줄 포함)가 들어가게 옮기고, 넘치는 크기는 줄입니다.
inline Rect clampToWorkAreas(Rect r, const std::vector<Rect>& areas, int titleBar = kTitleBarPx,
                             int minVisible = kMinVisiblePx) {
    if (areas.empty()) return r;
    r.w = std::max(r.w, 1);
    r.h = std::max(r.h, 1);
    const int t0 = r.y - titleBar, t1 = r.y;   // 제목 줄 띠의 위 · 아래
    const int needW = std::min(minVisible, r.w);

    auto fitSize = [&](Rect& c, const Rect& a) {
        c.w = std::min(c.w, std::max(1, a.w));
        c.h = std::min(c.h, std::max(1, a.h - titleBar));
    };

    // 1) 제목 줄이 충분히 보이는 영역이 있으면 자리는 그대로.
    for (const Rect& a : areas) {
        const bool vertIn = t0 >= a.y && t1 <= a.y + a.h;
        if (vertIn && overlap1d(r.x, r.x + r.w, a.x, a.x + a.w) >= needW) {
            Rect c = r;
            fitSize(c, a);
            // 줄였다면 그 영역보다 컸던 창이므로 영역 안으로도 옮깁니다(줄인 창이 다른 모니터로 넘치지 않게).
            if (c.w != r.w || c.h != r.h) {
                c.x = std::clamp(c.x, a.x, std::max(a.x, a.x + a.w - c.w));
                c.y = std::clamp(c.y, a.y + titleBar, std::max(a.y + titleBar, a.y + a.h - c.h));
            }
            return c;
        }
    }

    // 2) 옮길 영역 고르기.
    size_t best = 0;
    int64_t bestOverlap = 0;
    for (size_t i = 0; i < areas.size(); ++i) {
        const Rect& a = areas[i];
        const int64_t ov = (int64_t)overlap1d(r.x, r.x + r.w, a.x, a.x + a.w) * overlap1d(t0, t1, a.y, a.y + a.h);
        if (ov > bestOverlap) { bestOverlap = ov; best = i; }
    }
    if (bestOverlap == 0) {
        const int64_t cx = (int64_t)r.x + r.w / 2, cy = (int64_t)r.y + r.h / 2;
        int64_t bestDist = INT64_MAX;
        for (size_t i = 0; i < areas.size(); ++i) {
            const Rect& a = areas[i];
            const int64_t dx = cx < a.x ? a.x - cx : (cx > (int64_t)a.x + a.w ? cx - ((int64_t)a.x + a.w) : 0);
            const int64_t dy = cy < a.y ? a.y - cy : (cy > (int64_t)a.y + a.h ? cy - ((int64_t)a.y + a.h) : 0);
            const int64_t d  = dx * dx + dy * dy;
            if (d < bestDist) { bestDist = d; best = i; }
        }
    }

    // 3) 그 영역 안으로(제목 줄 포함) 옮깁니다.
    const Rect& a = areas[best];
    Rect c = r;
    fitSize(c, a);
    c.x = std::clamp(c.x, a.x, std::max(a.x, a.x + a.w - c.w));
    c.y = std::clamp(c.y, a.y + titleBar, std::max(a.y + titleBar, a.y + a.h - c.h));
    return c;
}

}  // namespace hwinplace
