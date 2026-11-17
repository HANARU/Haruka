#pragma once
#include "../RenderContext.hpp"
#include "../RenderTypes.hpp"
#include "../RetireQueue.hpp"
#include "../../ui/slate/HSlateContext.hpp"
#include <vector>
#include <cstdint>

class HSlateRenderer;

// ── UIPanelPass (3D UI 패널 P1a, 2026-11-16) ──────────────────────────────────────
// 월드 패널의 오프스크린 출력입니다. 패널마다 sRGB 텍스처 하나(밉 사슬 포함)를 두고, 다시 그릴 패널만 그 프레임에
// HSlate 로 0단계에 그린 뒤 GPU 블릿으로 밉을 만듭니다(DEF-165 와 같은 방식). 위젯 트리 · 배치 · 그리기 목록은
// HSlate 쪽(HSlateContext · HSlateRenderer::buildPanel · recordPanel)이 맡고, 여기는 이미지 · 프레임버퍼 · 밉 · 배치 전환만 맡습니다.
//
// 텍스처 하나를 두 프레임 슬롯이 같이 씁니다. 같은 큐에 차례로 제출되므로, 다시 그릴 때 패널 렌더 패스의 외부
// 의존성(앞 프레임의 조각 셰이더 읽기 · 블릿 뒤)과 프레임 첫 배리어가 앞 프레임의 읽기가 끝난 뒤에 쓰게 합니다.
// 텍스처 색은 알파를 곱한 값입니다(HSlateRenderer::recordPanel).
class UIPanelPass {
public:
    // 0 = 없음. 아래 16비트는 칸 번호 + 1, 위 16비트는 그 칸의 세대입니다. 지운 칸을 다른 패널이 다시 쓰면 세대가 바뀌어,
    // 옛 핸들을 쥔 패널은 무효 핸들을 보고 새로 만듭니다(두 패널이 한 텍스처에 그리지 않음).
    using Handle = uint32_t;
    static constexpr uint32_t kMaxSide = 4096;   // 한 변 상한(텍셀). 1,250 px/m 기준 약 3.3 m

    void init(RenderContext* rc) { rc_ = rc; }
    void destroy();                              // 모든 패널 텍스처를 지웁니다(장치가 놀고 있을 때)
    // 프레임 슬롯의 펜스를 기다린 직후 렌더러의 제출 번호로 부릅니다. 끝난 프레임만 쓰던 텍스처를 실제로 지웁니다.
    void beginFrame(uint64_t submitNum);

    // 크기는 [1, kMaxSide] 로 자릅니다. 패널 렌더 패스는 ui 가 가진 것을 씁니다. 실패하면 0.
    Handle create(HSlateRenderer& ui, uint32_t width, uint32_t height);
    // 핸들을 바로 무효로 하고(칸은 곧 다시 씀), 텍스처는 지연 삭제 대기열에 넣습니다. 진행 중인 프레임이 다 쓴 뒤
    // beginFrame 이 지웁니다(GPU 전체를 기다리지 않음: 패널 삭제 · 크기 변경 때 헤드셋 프레임이 끊기지 않게).
    void   release(Handle h);
    size_t retiredCount() const { return retired_.size(); }

    // 패널의 그리기 목록을 만들어(렌더러의 추출 단계, 월드를 읽는 곳에서) 직전에 그린 목록과 해시가 다르거나 강제 표시가
    // 있으면 이번 프레임의 record 에 넘깁니다. 넘겼으면 true. record 는 문맥(컴포넌트)을 보지 않고 넘겨받은 목록만 씁니다.
    bool   update(Handle h, HSlateRenderer& ui, HSlateContext& ctx);
    // 그리기 목록이 같아도 다음 update 에서 다시 그리게 표시합니다(강제).
    void   requestRedraw(Handle h);
    bool   hasQueued() const;
    // 넘겨받은 패널을 그리고 밉을 만듭니다. 메인 패스보다 먼저(렌더 패스 밖에서) 부릅니다.
    void   record(VkCommandBuffer cmd, HSlateRenderer& ui, uint32_t frameIdx);

    struct Stats { uint64_t redrawn = 0, skipped = 0; };   // 누적: GPU 로 그린 횟수 · 목록이 같아 건너뛴 횟수
    const Stats& stats() const { return stats_; }

    // 셰이더가 읽는 뷰(밉 전체, SHADER_READ_ONLY). 한 번도 그리지 않았으면 VK_NULL_HANDLE 입니다.
    // 이번 프레임에 넘겨받은 패널도 그려진 것으로 봅니다(같은 프레임에 메인 패스보다 먼저 그려지므로).
    VkImageView view(Handle h) const;
    bool        valid(Handle h) const { return get(h) != nullptr; }
    VkExtent2D  extent(Handle h) const;
    uint32_t    mipLevels(Handle h) const;
    uint32_t    liveCount() const;
    std::vector<Handle> liveHandles() const;

private:
    struct Target {
        VkImage          image      = VK_NULL_HANDLE;
        HarukaAllocation alloc;
        VkImageView      sampleView = VK_NULL_HANDLE;   // 밉 전체
        VkImageView      rtView     = VK_NULL_HANDLE;   // 0단계(프레임버퍼)
        VkFramebuffer    fb         = VK_NULL_HANDLE;
        VkExtent2D       extent{};
        uint32_t         mips  = 1;
        bool             live      = false;
        bool             drawn     = false;   // 한 번이라도 그림(셰이더가 읽어도 됨)
        bool             forced    = false;   // 목록이 같아도 다시 그림(requestRedraw · 예산 초과 뒤)
        bool             queued    = false;   // 이번 프레임 record 에 넘김
        uint64_t         hash      = 0;       // 마지막으로 넘긴 그리기 목록의 해시
        HSlateDrawList   list;                // 이번 프레임에 그릴 목록(다음 update 가 다시 씀)
        uint16_t         gen       = 0;
    };
    static Handle makeHandle(size_t index, uint16_t gen) { return ((Handle)gen << 16) | (Handle)(index + 1); }

    RenderContext*      rc_ = nullptr;
    std::vector<Target> targets_;   // 핸들 = 세대 · 칸 번호 + 1, 지운 칸은 다시 씁니다
    int                canBlit_ = -1;   // 형식 확인 결과(-1 = 아직)
    Stats              stats_;
    uint64_t           frame_   = 0;    // 마지막 beginFrame 의 제출 번호(그 프레임까지 지운 텍스처를 썼을 수 있음)
    RetireQueue<Target> retired_{ (uint64_t)MAX_FRAMES };

    const Target* get(Handle h) const;
    Target*       get(Handle h);
    bool canBlitMips();
    void recordMips(VkCommandBuffer cmd, const Target& t) const;
    void destroyTarget(Target& t);      // 칸을 비웁니다(Vulkan 객체를 지우고 세대는 남김)
    void freeVulkan(Target& t);         // Vulkan 객체만 지웁니다
};
