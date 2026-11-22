#pragma once
#include "../platform/IWindow.hpp"
#include "../platform/WindowPlacement.hpp"
#include "../ui/slate/HSlateContext.hpp"
#include "EditorWindowInput.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// 에디터 창 하나(다중 OS 창 M3a, 2026-11-22, Docs/Editor_MultiWindow_Design.md 3절 M3).
//
// 떠 있는 도크 창(DockFloat) 하나 = OS 창 + HSlate 문맥 + 루트 위젯(EditorDock::floatHost, 탭 줄 + 패널만) + 렌더러 대상.
// 메인 창은 이번 단계에서 이 구조체로 옮기지 않았습니다. 메인 경로(입력 순서 규칙 · 팝업 호스트 · 모달 · 덤프 기준)를 그대로 두려는
// 것이고, M3b 의 창별 입력 분배 때 메인도 같은 단위로 다룰지 다시 봅니다. 떠 있는 창은 아직 입력을 받지 않습니다(보이기만 함).
struct EditorWindow {
    int                            floatId = 0;   // DockFloat::id(1..). 0 은 메인 자리(이번 단계에서는 쓰지 않음)
    IWindow*                       window  = nullptr;
    std::unique_ptr<IWindow>       owned;          // 떠 있는 창은 이 구조체가 소유합니다(메인은 엔진 소유)
    std::unique_ptr<HSlateContext> ctx;            // 렌더러가 포인터를 들고 있으므로 주소가 바뀌지 않게 따로 둠
    std::shared_ptr<HWidget>       root;
    uint32_t                       target  = 0;    // Renderer 창 대상 번호(0 = 없음)
    std::string                    title;
    bool                           closeReported = false;   // OS 닫기 단추를 이미 알렸음(처리는 부른 쪽, D4)

    // ── 창별 입력(M3b) ──
    // 숨은 GLFW 창의 isFocused() 는 OS 값(보통 false)입니다(H0 보고). keyboardWindowId() 는 "포커스를 가진 떠 있는 창이
    // 없으면 메인"을 지켜, 숨은 실행 · 덤프에서 키보드가 아무 창에도 가지 않는 일이 없습니다.
    bool prevLeftDown  = false;
    bool prevRightDown = false;
    hwininput::PointerSample sample;        // 이번 프레임 값(readFloatPointers 가 채움)
    hwininput::PointerSample simSample;     // 시험 대본(--ui-float-input)의 흉내 값
    bool                     simFocused = false;   // 시험 대본의 focus · unfocus
};

// 떠 있는 창 목록의 수명(M3a). 도크 배치(원하는 창 목록)와 OS 창 · 렌더러 대상을 프레임 끝에 맞춥니다. 렌더러 · 플랫폼 호출은
// Ops 로 받아 단위 시험이 가짜로 순서를 기록합니다(slate_input). 이벤트 펌프 안에서는 부르지 않습니다(설계 문서 5절).
class EditorFloatWindows {
public:
    struct Ops {
        std::function<std::unique_ptr<IWindow>(int w, int h, const std::string& title, bool hidden)> createWindow;
        std::function<uint32_t(IWindow*)>             addTarget;      // 0 = 실패
        std::function<void(uint32_t, HSlateContext*)> setUi;
        std::function<void(uint32_t)>                 removeTarget;   // 장치 대기 → 스왑체인 → 표면
        std::function<std::vector<hwinplace::Rect>()> workAreas;      // 비면 끼우지 않음
        std::function<void(int floatId, HSlateContext&)> setupContext; // 새 창 문맥을 만든 직후(팝업 층 훅 등, H2a)
    };
    // 도크가 원하는 창 하나.
    struct Desired {
        int                      floatId = 0;
        hwinplace::Rect          rect;
        std::string              title;
        std::shared_ptr<HWidget> root;
    };

    void init(Ops ops, bool hidden) { ops_ = std::move(ops); hidden_ = hidden; }
    void setTooltipStyle(const HSlateContext::TooltipStyle& s) { tooltip_ = s; hasTooltip_ = true; }

    // 원하는 목록과 맞춥니다: 목록에 없는 창은 없애고(렌더러 대상 → OS 창), 새 번호는 만듭니다(작업 영역 안으로 끼운 자리 · 크기).
    // 남은 창은 제목 · 루트가 바뀌었으면 고칩니다. 만들지 못한 번호는 이번 실행 동안 다시 시도하지 않습니다(경고 한 번).
    void sync(const std::vector<Desired>& want);
    // OS 닫기 단추가 눌린 창 번호(창마다 한 번). 처리(D4: 안의 탭 닫기)는 부른 쪽이 하고, 그 결과 원하는 목록에서 빠지면 다음 sync 가
    // 없앱니다. 처리가 거부되면 keepOpen 으로 닫기 요청을 되돌립니다.
    std::vector<int> takeCloseRequests();
    void keepOpen(int floatId);
    void destroyAll();

    EditorWindow*       find(int floatId);
    const EditorWindow* find(int floatId) const;
    const std::vector<std::unique_ptr<EditorWindow>>& windows() const { return wins_; }
    // OS 창의 지금 자리 · 크기(내용 영역, 화면 좌표). 창이 없거나 자리를 못 읽으면 false.
    bool readRect(int floatId, hwinplace::Rect& out) const;
    void raise(int floatId);

private:
    void destroy(EditorWindow& w);

    Ops  ops_;
    bool hidden_ = false;
    HSlateContext::TooltipStyle tooltip_{};
    bool hasTooltip_ = false;
    std::vector<std::unique_ptr<EditorWindow>> wins_;
    std::vector<int> failedIds_;
};
