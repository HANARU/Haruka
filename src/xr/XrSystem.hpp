#pragma once
#include "../renderer/VulkanDeviceHooks.hpp"
#include "../renderer/RenderTypes.hpp"
#include "../input/HXrInput.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

// ── XrSystem ──────────────────────────────────────────────────────────────────
// OpenXR 연결(VR 첫걸음: PC 에서 Quest Link 등 PC VR 런타임으로 헤드셋에 그림). `--xr` 로 켭니다.
//
// 순서(UE · Unity · Godot 과 같음):
//   1. init()          인스턴스 + 헤드셋(시스템) 확인. 런타임이나 헤드셋이 없으면 false, 데스크톱으로 계속.
//   2. 렌더러 init      이 객체를 IVulkanDeviceHooks 로 받아 OpenXR 이 고른 GPU 로 인스턴스 · 장치를 만듦.
//   3. createSession()  세션 · 원점 공간 · 두 층 배열 스왑체인 · 입력 액션. 엔진이 Play 때 부르고 Stop 때
//                      requestExit() 로 끝냅니다(UE VR Preview · Unity 처럼 Play 동안만 헤드셋에 그림).
//   4. 매 프레임        pollEvents → waitFrame(속도를 헤드셋이 정함) → syncInput → locateViews →
//                      acquireImage → (렌더) → releaseImage → endFrame.
//
// 한 프레임에 눈 자세를 한 번만 받아 컬링 · 렌더 · 제출에 함께 씁니다. 그래서 재투영 여유분만큼
// 절두체를 부풀릴 필요가 없습니다(DEF-32).
//
// 원점 공간은 LOCAL(세션을 시작할 때 머리 위치)입니다. 렌더러가 그 원점을 활성 카메라 액터의 위치와
// 요(yaw)에 맞춰, 헤드셋을 쓰면 에디터 카메라 자리에서 보게 됩니다. 바닥 기준(STAGE)은 게임
// 폰이 생길 때 고릅니다.
class XrSystem final : public IVulkanDeviceHooks {
public:
    XrSystem();
    ~XrSystem() override;
    XrSystem(const XrSystem&)            = delete;
    XrSystem& operator=(const XrSystem&) = delete;

    // 1. 인스턴스와 헤드셋을 준비합니다. 실패해도 예외 없이 false 를 돌려주고 이유를 로그에 남깁니다.
    bool init();
    bool ready() const;   // init 이 성공했고 세션을 만들 수 있음

    // 2. IVulkanDeviceHooks (렌더러가 부름)
    VkResult         createVulkanInstance(const VkInstanceCreateInfo& ci, VkInstance& out) override;
    VkPhysicalDevice vulkanPhysicalDevice(VkInstance instance) override;
    VkResult         createVulkanDevice(VkPhysicalDevice physicalDevice,
                                        const VkDeviceCreateInfo& ci, VkDevice& out) override;

    // 3. 세션. preferredFormat 은 데스크톱 스왑체인 포맷(같으면 톤매핑 파이프라인을 함께 씀).
    bool createSession(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                       uint32_t queueFamily, VkFormat preferredFormat);
    // 세션과 스왑체인을 없앱니다. 렌더러가 스왑체인 이미지를 다 놓은 뒤(장치 대기 후)에 부릅니다.
    void destroySession();
    void shutdown();      // 세션 + 인스턴스

    // ── 매 프레임 ─────────────────────────────────────────────────────────────
    // 런타임 이벤트를 처리합니다. READY 면 세션을 시작하고, STOPPING 이면 끝냅니다.
    void pollEvents();
    bool hasSession() const;       // 세션이 있음(시작 전 · 실행 중 · 멈춤 모두)
    bool sessionRunning() const;   // xrBeginSession 과 xrEndSession 사이
    // 세션이 끝났거나 잃었습니다(EXITING · LOSS_PENDING). 엔진이 렌더러에서 스왑체인을 떼고
    // destroySession() 을 부른 뒤 데스크톱으로 계속합니다.
    bool sessionLost() const;
    // 세션을 정상 순서로 끝내 달라고 런타임에 요청합니다(xrRequestExitSession). 런타임이 STOPPING 을
    // 보내면 pollEvents 가 xrEndSession 을 부르고, EXITING 이 오면 sessionLost() 가 참이 됩니다.
    // 세션이 돌고 있지 않으면(헤드셋을 벗어 IDLE 등) 요청할 것이 없어 false 이고, 그때는 바로 지웁니다.
    bool requestExit();
    bool exitRequested() const;    // 이 세션의 끝을 requestExit 로 요청했음(런타임이 끝낸 것과 구별)

    struct FrameInfo {
        bool    shouldRender = false;   // 헤드셋이 화면을 보여 주는 중(VISIBLE · FOCUSED)
        int64_t displayTime  = 0;       // 이 프레임이 보일 예상 시각(XrTime)
    };
    // xrWaitFrame + xrBeginFrame. 헤드셋 주사율에 맞춰 기다리므로 루프 속도를 헤드셋이 정합니다.
    // false 면 이번 프레임은 XR 없이 갑니다(endFrame 도 부르지 않음).
    bool beginFrame(FrameInfo& out);
    // 입력 액션을 동기화하고 손 · 머리 자세를 displayTime 기준으로 읽습니다(FOCUSED 일 때만).
    void syncInput(int64_t displayTime, HXrInputState& out);
    // 이번 프레임의 눈 자세 · 시야를 한 번 받습니다. 렌더와 endFrame 이 같은 값을 씁니다(DEF-32).
    bool locateViews(int64_t displayTime, std::array<XrEyePose, kMaxViews>& eyes);
    bool acquireImage(uint32_t& index);   // xrAcquire + xrWait
    void releaseImage();
    // rendered 면 두 눈 투영 레이어를 제출하고, 아니면 빈 프레임을 제출합니다.
    void endFrame(int64_t displayTime, bool rendered);

    // ── 렌더러가 프레임버퍼를 만들 스왑체인 정보 ──────────────────────────────
    const std::vector<VkImage>& swapchainImages() const;
    VkFormat   swapchainFormat() const;
    VkExtent2D eyeExtent() const;
    // 헤드셋 주사율(Hz). 물리 스텝을 맞출 때 씁니다. 모르면 0.
    float displayRefreshRate() const;

private:
    struct Impl;   // OpenXR 헤더는 cpp 에만 둡니다(엔진 다른 곳이 OpenXR 를 모르게).
    std::unique_ptr<Impl> impl_;
};
