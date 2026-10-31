#pragma once

// ── Vulkan 기준선 (패키징 2단계, DEF-97 · DEF-04 · 창 없는 렌더 경로) ───────────────────────────
// 엔진이 기대는 Vulkan 범위를 한곳에 모읍니다. 확장 이름 · 함수 이름 · 기능 결정이 여러 곳에 흩어져 있으면 한 곳만
// 고쳐도 다른 곳이 옛 이름을 부릅니다(예전에는 push descriptor 를 부르는 6곳이 모두 1.4 코어 이름을 썼습니다).
//
// 규칙:
//   · 인스턴스 · 장치 모두 Vulkan 1.1 입니다. Quest 2 · Pro 는 1.1.295 이고(Quest 3 · 3S 는 1.3), Android 의 libvulkan 은
//     vkCmdPipelineBarrier2 를 API 33, vkCmdPushDescriptorSet 을 API 36 부터 내보냅니다. 장치 버전이 높아도 코어 경로를
//     고르지 않습니다(같은 계획이 나옵니다).
//   · VK_KHR_push_descriptor 는 필수, VK_KHR_synchronization2 는 선택입니다(없으면 1.0 배리어, BarrierCompat.hpp).
//     둘 다 1.1 에 기댑니다(vk.xml depends). push descriptor 확장에는 기능 구조체가 없습니다.
//   · 기능 체인에는 sync2 구조체 하나만 잇습니다. Vulkan11~14Features 는 잇지 않습니다(1.1 장치에는 없는 구조체이고,
//     Vulkan13Features 와 개별 구조체를 함께 달면 VUID-VkDeviceCreateInfo-pNext-06532 위반).
//   · 확장 함수는 vkGetDeviceProcAddr 로 KHR 이름만 묻습니다. 1.2 이상 코어 이름은 링크하지도 묻지도 않습니다.
//   · 이방성 필터링이 없는 장치는 끄고 계속합니다(사용자 결정 2026-10-31). 다중 샘플 저장 이미지는 XR 장치이고
//     지원할 때만 켭니다(DEF-110).
//
// 순수 함수만 둡니다. Vulkan 형과 매크로만 쓰고 Vulkan 함수는 부르지 않으므로(장치 함수는 인자로 받음) 링크 없이
// HarukaVulkanBaselineTest(vulkan_baseline)가 창 · 장치 없이 검산합니다.

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hvk {

inline constexpr uint32_t kVulkanApiVersion    = VK_API_VERSION_1_1;
inline constexpr uint32_t kMinDeviceApiVersion = VK_API_VERSION_1_1;

// major · minor 만 비교합니다(patch 무시).
constexpr uint32_t majorMinor(uint32_t v) { return (VK_API_VERSION_MAJOR(v) << 10) | VK_API_VERSION_MINOR(v); }

// 로그용 "1.1.295".
inline std::string versionString(uint32_t v) {
    return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) + "." +
           std::to_string(VK_API_VERSION_PATCH(v));
}

// ── 장치 고르기 ───────────────────────────────────────────────────────────────────────────────
// 물리 장치에서 한 번 읽어 담는 값입니다. 조회는 렌더러가 하고 판단은 planDevice 가 합니다.
struct DeviceCaps {
    uint32_t     apiVersion                  = 0;
    bool         extSwapchain                = false;
    bool         extPushDescriptor           = false;
    bool         extSync2                    = false;
    bool         featSync2                   = false;   // VkPhysicalDeviceSynchronization2FeaturesKHR
    bool         featSamplerAnisotropy       = false;
    bool         featStorageImageMultisample = false;
    uint32_t     maxPushDescriptors          = 0;       // 로그용(규격 최솟값 32)
    VkDeviceSize maxMemoryAllocationSize     = 0;       // 0 = 모름(Maintenance3, 할당자에 넘김)
};

struct DevicePlan {
    bool                     suitable = false;
    std::string              whyNot;                  // 맞지 않는 이유(여럿이면 "; " 로 이음)
    std::vector<const char*> extensions;              // 켤 장치 확장. 이 목록만 장치 생성에 씁니다
    bool                     sync2                   = false;
    bool                     samplerAnisotropy       = false;
    bool                     storageImageMultisample = false;
};

// needSwapchain 은 창이 표면을 줄 때만 참입니다(창 없는 경로 · Quest 는 거짓). forceNoSync2 는 1.0 배리어 경로를
// PC 에서 시험하는 명령줄 스위치(--vk-no-sync2)입니다.
inline DevicePlan planDevice(const DeviceCaps& caps, bool needSwapchain, bool xrDevice, bool forceNoSync2 = false) {
    DevicePlan p;
    auto reject = [&](const std::string& why) {
        if (!p.whyNot.empty()) p.whyNot += "; ";
        p.whyNot += why;
    };
    if (majorMinor(caps.apiVersion) < majorMinor(kMinDeviceApiVersion))
        reject("Vulkan 1.1 or newer is required (device reports " + versionString(caps.apiVersion) + ")");
    if (!caps.extPushDescriptor) reject("missing " VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    if (needSwapchain && !caps.extSwapchain) reject("missing " VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    p.suitable = p.whyNot.empty();

    if (needSwapchain) p.extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    p.extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    p.sync2 = caps.extSync2 && caps.featSync2 && !forceNoSync2;
    if (p.sync2) p.extensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    p.samplerAnisotropy       = caps.featSamplerAnisotropy;
    p.storageImageMultisample = xrDevice && caps.featStorageImageMultisample;
    return p;
}

// 장치 생성의 기능 체인입니다. 반환값을 VkDeviceCreateInfo::pNext 에, &out.core 를 pEnabledFeatures 에 넣습니다.
struct FeatureChain {
    VkPhysicalDeviceSynchronization2FeaturesKHR sync2{};
    VkPhysicalDeviceFeatures                    core{};
};

inline const void* buildFeatureChain(const DevicePlan& plan, FeatureChain& out) {
    out = FeatureChain{};
    out.core.samplerAnisotropy             = plan.samplerAnisotropy ? VK_TRUE : VK_FALSE;
    out.core.shaderStorageImageMultisample = plan.storageImageMultisample ? VK_TRUE : VK_FALSE;
    if (!plan.sync2) return nullptr;
    out.sync2.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
    out.sync2.pNext            = nullptr;
    out.sync2.synchronization2 = VK_TRUE;
    return &out.sync2;
}

// ── 큐 계열 ───────────────────────────────────────────────────────────────────────────────────
// present 정보는 표면이 있을 때만 렌더러가 채웁니다(vkGetPhysicalDeviceSurfaceSupportKHR).
struct QueueFamilies {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    bool complete(bool needPresent) const { return graphics.has_value() && (!needPresent || present.has_value()); }
};

// 예전 findQueueFamilies 와 같은 규칙입니다: 앞에서부터 걸으며 그래픽 계열을 덮어쓰고, 다 찾으면 멈춥니다.
inline QueueFamilies selectQueueFamilies(const std::vector<VkQueueFamilyProperties>& fams,
                                         const std::vector<VkBool32>& presentSupport, bool needPresent) {
    QueueFamilies q;
    for (uint32_t i = 0; i < (uint32_t)fams.size(); ++i) {
        if (fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) q.graphics = i;
        if (needPresent && i < presentSupport.size() && presentSupport[i]) q.present = i;
        if (q.complete(needPresent)) break;
    }
    return q;
}

// ── 장치 함수 ─────────────────────────────────────────────────────────────────────────────────
// 렌더러가 장치를 만든 뒤 한 번 채워 RenderContext(rc.vk)로 나눠 줍니다. 패스 · 확장 · UI 는 이 포인터로만 부릅니다.
struct DeviceFns {
    PFN_vkCmdPushDescriptorSetKHR push       = nullptr;
    PFN_vkCmdPipelineBarrier2KHR  barrier2   = nullptr;   // sync2 를 켰을 때만
    PFN_vkCmdPipelineBarrier      barrier1   = nullptr;   // 1.0 코어(&vkCmdPipelineBarrier)
    bool                          sync2      = false;     // barrier2 로 기록하는지
    bool                          anisotropy = false;
};

// 묻는 이름은 "vkCmdPushDescriptorSetKHR" 와(sync2 계획일 때만) "vkCmdPipelineBarrier2KHR" 둘뿐입니다. barrier1 은
// 호출하는 쪽이 정적 &vkCmdPipelineBarrier 를 넘깁니다(이 헤더가 로더에 링크하지 않도록). push 가 없으면 false 이고
// err 에 함수 이름을 적습니다. barrier2 가 없으면 sync2 를 끄고(1.0 배리어로 내려감) true 입니다.
inline bool loadDeviceFns(VkDevice dev, PFN_vkGetDeviceProcAddr getDeviceProcAddr, PFN_vkCmdPipelineBarrier barrier1,
                          const DevicePlan& plan, DeviceFns& out, std::string* err) {
    out = DeviceFns{};
    out.barrier1   = barrier1;
    out.anisotropy = plan.samplerAnisotropy;
    auto fail = [&](const char* what) {
        if (err) *err = what;
        return false;
    };
    if (!getDeviceProcAddr) return fail("vkGetDeviceProcAddr is null");
    out.push = reinterpret_cast<PFN_vkCmdPushDescriptorSetKHR>(getDeviceProcAddr(dev, "vkCmdPushDescriptorSetKHR"));
    if (!out.push) return fail("vkCmdPushDescriptorSetKHR is not available (" VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME ")");
    if (plan.sync2) {
        out.barrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(getDeviceProcAddr(dev, "vkCmdPipelineBarrier2KHR"));
        out.sync2    = out.barrier2 != nullptr;
    }
    if (!out.sync2 && !out.barrier1) return fail("no pipeline barrier function (vkCmdPipelineBarrier is null)");
    return true;
}

// ── XR 런타임의 Vulkan 범위 ───────────────────────────────────────────────────────────────────
// 엔진 버전(api)이 런타임이 알려 준 범위 안인지 봅니다. major · minor 만 비교합니다. 0 은 범위를 모른다는 뜻입니다.
// 최솟값보다 낮으면 XR 을 끄고, 최댓값보다 높으면 경고만 합니다(사용자 결정 2026-10-31).
enum class ApiRange { InRange, BelowMin, AboveMax, Unknown };

constexpr ApiRange checkApiRange(uint32_t api, uint32_t minVk, uint32_t maxVk) {
    if (minVk == 0 || maxVk == 0) return ApiRange::Unknown;
    if (majorMinor(api) < majorMinor(minVk)) return ApiRange::BelowMin;
    if (majorMinor(api) > majorMinor(maxVk)) return ApiRange::AboveMax;
    return ApiRange::InRange;
}

} // namespace hvk
