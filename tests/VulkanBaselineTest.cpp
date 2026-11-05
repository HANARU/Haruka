// Vulkan 기준선(DEF-97 · DEF-04 · 창 없는 렌더 경로)의 검산입니다. 창도 장치도 링크도 필요 없습니다.
//
// 이 테스트가 존재하는 이유: 장치 고르기 · 켤 확장 · 기능 체인 · 함수 이름은 PC 의 1.4 드라이버에서는 틀려도 우연히
// 돌아갑니다. Quest 2 · Pro(Vulkan 1.1)와 Android API 34 에서 처음 드러나므로, 실기 없이 숫자와 이름으로 고정합니다.
//
// 실행: build/Release/HarukaVulkanBaselineTest.exe

#include "../src/renderer/VulkanBaseline.hpp"
#include "../src/xr/XrVersion.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

bool has(const std::vector<const char*>& v, const char* name) {
    for (const char* s : v) if (std::strcmp(s, name) == 0) return true;
    return false;
}

bool sameList(const std::vector<const char*>& a, const std::vector<const char*>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (std::strcmp(a[i], b[i]) != 0) return false;
    return true;
}

bool noDuplicates(const std::vector<const char*>& v) {
    for (size_t i = 0; i < v.size(); ++i)
        for (size_t j = i + 1; j < v.size(); ++j)
            if (std::strcmp(v[i], v[j]) == 0) return false;
    return true;
}

// Quest Pro 형 장치(gpuinfo.org 의 Quest Pro 보고서 기준 값).
hvk::DeviceCaps questPro() {
    hvk::DeviceCaps c;
    c.apiVersion            = VK_MAKE_API_VERSION(0, 1, 1, 295);
    c.extSwapchain          = true;
    c.extPushDescriptor     = true;
    c.extSync2              = true;
    c.featSync2             = true;
    c.featSamplerAnisotropy = true;
    c.maxPushDescriptors    = 32;
    return c;
}

// ── 가짜 vkGetDeviceProcAddr ──────────────────────────────────────────────────
std::vector<std::string> g_asked;
bool g_havePush = true, g_haveBarrier2 = true;

VKAPI_ATTR void VKAPI_CALL dummyFn() {}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGetDeviceProcAddr(VkDevice, const char* name) {
    g_asked.emplace_back(name);
    const std::string n = name;
    if (n == "vkCmdPushDescriptorSetKHR" && g_havePush)   return reinterpret_cast<PFN_vkVoidFunction>(&dummyFn);
    if (n == "vkCmdPipelineBarrier2KHR" && g_haveBarrier2) return reinterpret_cast<PFN_vkVoidFunction>(&dummyFn);
    // 코어 이름에도 답하는 드라이버처럼 굴어, 로더가 코어 이름을 물으면 "찾아서 써 버리는" 실수를 숨기지 않게 합니다.
    if (n == "vkCmdPushDescriptorSet" || n == "vkCmdPipelineBarrier2") return reinterpret_cast<PFN_vkVoidFunction>(&dummyFn);
    return nullptr;
}

VKAPI_ATTR void VKAPI_CALL fakeBarrier1(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags,
                                        uint32_t, const VkMemoryBarrier*, uint32_t, const VkBufferMemoryBarrier*,
                                        uint32_t, const VkImageMemoryBarrier*) {}

bool asked(const char* name) {
    for (const auto& s : g_asked) if (s == name) return true;
    return false;
}

// ── 큐 계열: 예전 Renderer::findQueueFamilies 와 같은 루프(present 는 표에서 읽음) ──────────────
hvk::QueueFamilies oldRule(const std::vector<VkQueueFamilyProperties>& fams, const std::vector<VkBool32>& present) {
    hvk::QueueFamilies idx;
    for (uint32_t i = 0; i < (uint32_t)fams.size(); i++) {
        if (fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) idx.graphics = i;
        if (present[i]) idx.present = i;
        if (idx.graphics.has_value() && idx.present.has_value()) break;
    }
    return idx;
}

VkQueueFamilyProperties fam(VkQueueFlags f) {
    VkQueueFamilyProperties p{};
    p.queueFlags = f;
    p.queueCount = 1;
    return p;
}

bool sameQ(const hvk::QueueFamilies& a, const hvk::QueueFamilies& b) {
    return a.graphics == b.graphics && a.present == b.present;
}

} // namespace

int main() {
    std::printf("Haruka Vulkan 기준선 검산\n");

    std::printf("\n== A. 장치 계획 ==\n");
    {
        const hvk::DevicePlan p = hvk::planDevice(questPro(), true, false);
        check(p.suitable && p.whyNot.empty(), "A1 Quest Pro 형(1.1.295) 장치가 맞음");
        check(sameList(p.extensions, { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
                                       VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME }),
              "A1 켤 확장 = swapchain · push_descriptor · synchronization2");
        check(p.sync2 && p.samplerAnisotropy && !p.storageImageMultisample, "A1 sync2 켬, 이방성 켬, MS 저장 끔");
        check(noDuplicates(p.extensions), "A1 확장 목록에 중복 없음");

        hvk::DeviceCaps q3 = questPro();
        q3.apiVersion = VK_MAKE_API_VERSION(0, 1, 3, 295);
        hvk::DeviceCaps pc = questPro();
        pc.apiVersion = VK_MAKE_API_VERSION(0, 1, 4, 325);
        const hvk::DevicePlan p3 = hvk::planDevice(q3, true, false), p4 = hvk::planDevice(pc, true, false);
        check(sameList(p3.extensions, p.extensions) && p3.sync2 == p.sync2 && p3.samplerAnisotropy == p.samplerAnisotropy,
              "A2 Quest 3 형(1.3)도 같은 계획(코어 경로를 고르지 않음)");
        check(sameList(p4.extensions, p.extensions) && p4.sync2 == p.sync2, "A2 PC 형(1.4)도 같은 계획");

        hvk::DeviceCaps noExt = questPro();
        noExt.extSync2 = false; noExt.featSync2 = false;
        const hvk::DevicePlan pa3 = hvk::planDevice(noExt, true, false);
        check(pa3.suitable && !pa3.sync2 && !has(pa3.extensions, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME),
              "A3 sync2 확장이 없으면 맞되 sync2 끔, 목록에도 없음");

        hvk::DeviceCaps noFeat = questPro();
        noFeat.featSync2 = false;
        const hvk::DevicePlan pa4 = hvk::planDevice(noFeat, true, false);
        check(pa4.suitable && !pa4.sync2 && !has(pa4.extensions, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME),
              "A4 확장은 있고 기능이 거짓이면 sync2 끔, 확장도 켜지 않음");

        hvk::DeviceCaps noPush = questPro();
        noPush.extPushDescriptor = false;
        const hvk::DevicePlan pa5 = hvk::planDevice(noPush, true, false);
        check(!pa5.suitable && pa5.whyNot.find(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) != std::string::npos,
              "A5 push_descriptor 가 없으면 거부, 이유에 확장 이름");

        hvk::DeviceCaps v10 = questPro();
        v10.apiVersion = VK_MAKE_API_VERSION(0, 1, 0, 300);
        const hvk::DevicePlan pa6 = hvk::planDevice(v10, true, false);
        check(!pa6.suitable && pa6.whyNot.find("1.1") != std::string::npos && pa6.whyNot.find("1.0.300") != std::string::npos,
              "A6 1.0 장치는 거부, 이유에 요구 버전 1.1 과 장치 버전");

        hvk::DeviceCaps noSwap = questPro();
        noSwap.extSwapchain = false;
        const hvk::DevicePlan pa7a = hvk::planDevice(noSwap, true, false), pa7b = hvk::planDevice(noSwap, false, false);
        check(!pa7a.suitable && pa7a.whyNot.find(VK_KHR_SWAPCHAIN_EXTENSION_NAME) != std::string::npos,
              "A7 swapchain 이 없고 화면이 필요하면 거부");
        check(pa7b.suitable && !has(pa7b.extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME),
              "A7 화면이 필요 없으면 swapchain 없이 맞음");
        const hvk::DevicePlan pa7c = hvk::planDevice(questPro(), false, false);
        check(!has(pa7c.extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME) &&
              sameList(pa7c.extensions, { VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME }),
              "A7 창이 없으면 장치가 지원해도 swapchain 을 켜지 않음(창 없는 경로 · Quest)");

        hvk::DeviceCaps noAniso = questPro();
        noAniso.featSamplerAnisotropy = false;
        const hvk::DevicePlan pa8 = hvk::planDevice(noAniso, true, false);
        check(pa8.suitable && !pa8.samplerAnisotropy, "A8 이방성이 없으면 거부하지 않고 끔");

        hvk::DeviceCaps ms = questPro();
        ms.featStorageImageMultisample = true;
        check(hvk::planDevice(ms, true, true).storageImageMultisample, "A9 XR 장치이고 지원하면 MS 저장 이미지 켬");
        check(!hvk::planDevice(ms, true, false).storageImageMultisample, "A9 XR 장치가 아니면 끔(DEF-110 동작 유지)");

        const hvk::DevicePlan force = hvk::planDevice(questPro(), true, false, true);
        check(force.suitable && !force.sync2 && !has(force.extensions, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME),
              "A10 --vk-no-sync2 면 지원해도 sync2 끔, 확장도 켜지 않음");

        hvk::DeviceCaps clampOn = questPro();
        clampOn.featDepthBiasClamp = true;
        const hvk::DevicePlan pa12 = hvk::planDevice(clampOn, true, false);
        check(pa12.suitable && pa12.depthBiasClamp, "A12 depthBiasClamp 를 지원하면 켬(섀도 경사 바이어스 상한)");
        const hvk::DevicePlan pa12b = hvk::planDevice(questPro(), true, false);
        check(pa12b.suitable && !pa12b.depthBiasClamp, "A12 지원하지 않으면 거부하지 않고 끔");

        hvk::DeviceCaps dcOn = questPro();
        dcOn.featDepthClamp = true;
        check(hvk::planDevice(dcOn, true, false).depthClamp, "A13 depthClamp 를 지원하면 켬(섀도 캐스터 pancaking)");
        check(hvk::planDevice(questPro(), true, false).suitable && !hvk::planDevice(questPro(), true, false).depthClamp,
              "A13 지원하지 않으면 거부하지 않고 끔(투영 near 를 늘려 대신)");

        hvk::DeviceCaps both = questPro();
        both.extPushDescriptor = false;
        both.apiVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        const hvk::DevicePlan pa11 = hvk::planDevice(both, true, false);
        check(!pa11.suitable && pa11.whyNot.find("; ") != std::string::npos, "A11 이유가 여럿이면 모두 적음");
    }

    std::printf("\n== B. 기능 체인 ==\n");
    {
        auto walk = [](const void* head, int& count, bool& coreStruct, bool& sync2On) {
            count = 0; coreStruct = false; sync2On = false;
            for (const VkBaseInStructure* s = static_cast<const VkBaseInStructure*>(head); s; s = s->pNext) {
                ++count;
                if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES ||
                    s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES ||
                    s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES ||
                    s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES)
                    coreStruct = true;
                if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR)
                    sync2On = reinterpret_cast<const VkPhysicalDeviceSynchronization2FeaturesKHR*>(s)->synchronization2 == VK_TRUE;
            }
        };
        hvk::FeatureChain fc;
        hvk::DeviceCaps ms = questPro();
        ms.featStorageImageMultisample = true;
        const hvk::DevicePlan on = hvk::planDevice(ms, true, true);
        int n = 0; bool core = false, s2 = false;
        walk(hvk::buildFeatureChain(on, fc), n, core, s2);
        check(n == 1 && s2 && !core, "B1 sync2 를 켜면 sync2 구조체 정확히 하나(synchronization2 = VK_TRUE)");
        check(fc.core.samplerAnisotropy == VK_TRUE && fc.core.shaderStorageImageMultisample == VK_TRUE,
              "B1 기본 기능이 계획과 같음(이방성 · MS 저장)");
        check(fc.core.depthBiasClamp == VK_FALSE, "B1 depthBiasClamp 를 지원하지 않는 계획이면 VK_FALSE");
        hvk::DeviceCaps clampCaps = questPro();
        clampCaps.featDepthBiasClamp = true;
        hvk::FeatureChain fcClamp;
        hvk::buildFeatureChain(hvk::planDevice(clampCaps, true, false), fcClamp);
        check(fcClamp.core.depthBiasClamp == VK_TRUE, "B1 depthBiasClamp 계획이면 기본 기능에 VK_TRUE");
        check(fc.core.depthClamp == VK_FALSE && fcClamp.core.depthClamp == VK_FALSE,
              "B1 depthClamp 를 지원하지 않는 계획이면 VK_FALSE");
        hvk::DeviceCaps dcCaps = questPro();
        dcCaps.featDepthClamp = true;
        hvk::FeatureChain fcDc;
        hvk::buildFeatureChain(hvk::planDevice(dcCaps, true, false), fcDc);
        check(fcDc.core.depthClamp == VK_TRUE && fcDc.core.depthBiasClamp == VK_FALSE,
              "B1 depthClamp 계획이면 기본 기능에 VK_TRUE(depthBiasClamp 와 따로)");

        hvk::DeviceCaps noExt = questPro();
        noExt.extSync2 = false;
        noExt.featSamplerAnisotropy = false;
        const hvk::DevicePlan off = hvk::planDevice(noExt, false, false);
        hvk::FeatureChain fc2;
        const void* head = hvk::buildFeatureChain(off, fc2);
        check(head == nullptr, "B2 sync2 를 끄면 체인 없음(nullptr)");
        check(fc2.core.samplerAnisotropy == VK_FALSE && fc2.core.shaderStorageImageMultisample == VK_FALSE,
              "B2 이방성 · MS 저장 끔이 기본 기능에 그대로");

        // 이전 값이 남지 않음(같은 FeatureChain 을 다시 씀)
        const void* again = hvk::buildFeatureChain(off, fc);
        check(again == nullptr && fc.core.samplerAnisotropy == VK_FALSE, "B3 다시 만들면 이전 값이 남지 않음");
    }

    std::printf("\n== C. 장치 함수 ==\n");
    {
        const VkDevice dev = VK_NULL_HANDLE;
        hvk::DeviceFns fns;
        std::string err;

        g_asked.clear(); g_havePush = true; g_haveBarrier2 = true;
        const hvk::DevicePlan sync = hvk::planDevice(questPro(), true, false);
        const bool ok1 = hvk::loadDeviceFns(dev, &fakeGetDeviceProcAddr, &fakeBarrier1, sync, fns, &err);
        check(ok1 && fns.push && fns.barrier2 && fns.sync2 && fns.barrier1 == &fakeBarrier1 && fns.anisotropy,
              "C1 sync2 계획: push · barrier2 를 받고 sync2 켬, barrier1 은 넘긴 값");
        check(asked("vkCmdPushDescriptorSetKHR") && asked("vkCmdPipelineBarrier2KHR") && g_asked.size() == 2,
              "C1 KHR 이름 두 개만 물음");
        check(!asked("vkCmdPushDescriptorSet") && !asked("vkCmdPipelineBarrier2") && !asked("vkCmdPipelineBarrier"),
              "C1 코어 이름(1.4 push · 1.3 barrier2 · 1.0 barrier)은 묻지 않음");
        check(!fns.depthBiasClamp, "C1 depthBiasClamp 는 계획을 따름(끔)");
        hvk::DeviceCaps clampFn = questPro();
        clampFn.featDepthBiasClamp = true;
        hvk::DeviceFns fnsClamp;
        hvk::loadDeviceFns(dev, &fakeGetDeviceProcAddr, &fakeBarrier1, hvk::planDevice(clampFn, true, false), fnsClamp, &err);
        check(fnsClamp.depthBiasClamp, "C1 depthBiasClamp 는 계획을 따름(켬, 섀도 패스가 rc.vk 로 읽음)");
        hvk::DeviceCaps dcFn = questPro();
        dcFn.featDepthClamp = true;
        hvk::DeviceFns fnsDc;
        hvk::loadDeviceFns(dev, &fakeGetDeviceProcAddr, &fakeBarrier1, hvk::planDevice(dcFn, true, false), fnsDc, &err);
        check(fnsDc.depthClamp && !fnsClamp.depthClamp, "C1 depthClamp 는 계획을 따름(섀도 패스 · 맞춤이 rc.vk 로 읽음)");

        g_asked.clear();
        hvk::DeviceCaps noExt = questPro();
        noExt.extSync2 = false;
        const hvk::DevicePlan legacy = hvk::planDevice(noExt, true, false);
        const bool ok2 = hvk::loadDeviceFns(dev, &fakeGetDeviceProcAddr, &fakeBarrier1, legacy, fns, &err);
        check(ok2 && fns.push && !fns.barrier2 && !fns.sync2, "C2 sync2 가 아니면 barrier2 는 NULL, sync2 끔");
        check(g_asked.size() == 1 && asked("vkCmdPushDescriptorSetKHR"), "C2 barrier2 이름은 묻지 않음");

        g_asked.clear(); g_havePush = false; err.clear();
        const bool ok3 = hvk::loadDeviceFns(dev, &fakeGetDeviceProcAddr, &fakeBarrier1, sync, fns, &err);
        check(!ok3 && err.find("vkCmdPushDescriptorSetKHR") != std::string::npos, "C3 push 가 없으면 실패, 오류에 함수 이름");

        g_asked.clear(); g_havePush = true; g_haveBarrier2 = false;
        const bool ok4 = hvk::loadDeviceFns(dev, &fakeGetDeviceProcAddr, &fakeBarrier1, sync, fns, &err);
        check(ok4 && !fns.sync2 && !fns.barrier2 && fns.barrier1 == &fakeBarrier1,
              "C4 barrier2 가 NULL 이면 성공하되 sync2 를 끔(1.0 배리어로 내려감)");

        g_haveBarrier2 = true; err.clear();
        const bool ok5 = hvk::loadDeviceFns(dev, &fakeGetDeviceProcAddr, nullptr, legacy, fns, &err);
        check(!ok5 && !err.empty(), "C5 sync2 도 아니고 1.0 배리어 함수도 없으면 실패");
        const bool ok6 = hvk::loadDeviceFns(dev, nullptr, &fakeBarrier1, sync, fns, &err);
        check(!ok6, "C6 vkGetDeviceProcAddr 가 없으면 실패");
    }

    std::printf("\n== D. XR 런타임 범위 ==\n");
    {
        const uint32_t v10 = VK_API_VERSION_1_0, v11 = VK_API_VERSION_1_1, v12 = VK_API_VERSION_1_2,
                       v13 = VK_API_VERSION_1_3;
        check(hvk::checkApiRange(v11, v10, v12) == hvk::ApiRange::InRange, "D1 1.1 은 1.0 ~ 1.2 안(Quest Link)");
        check(hvk::checkApiRange(v11, v12, v13) == hvk::ApiRange::BelowMin, "D2 1.1 은 1.2 ~ 1.3 의 최솟값 미만");
        check(hvk::checkApiRange(v11, v10, v10) == hvk::ApiRange::AboveMax, "D3 1.1 은 1.0 ~ 1.0 의 최댓값 초과");
        check(hvk::checkApiRange(v11, VK_MAKE_API_VERSION(0, 1, 1, 0), VK_MAKE_API_VERSION(0, 1, 1, 295)) ==
                  hvk::ApiRange::InRange,
              "D4 patch 는 무시(1.1.0 ~ 1.1.295 에 1.1 이 들어감)");
        check(hvk::checkApiRange(VK_MAKE_API_VERSION(0, 1, 1, 999), v10, VK_MAKE_API_VERSION(0, 1, 1, 0)) ==
                  hvk::ApiRange::InRange,
              "D4 엔진 쪽 patch 도 무시");
        check(hvk::checkApiRange(v11, 0, 0) == hvk::ApiRange::Unknown, "D5 범위를 모르면 Unknown");

        constexpr uint64_t xr12 = (1ull << 48) | (2ull << 32);
        static_assert(hxr::vkApiFromXrVersion(xr12) == VK_MAKE_API_VERSION(0, 1, 2, 0), "XrVersion 1.2 → Vulkan 1.2");
        check(hxr::vkApiFromXrVersion(xr12) == VK_MAKE_API_VERSION(0, 1, 2, 0), "D6 XrVersion(1.2) → Vulkan 1.2");
        check(hxr::vkApiFromXrVersion((1ull << 48) | (1ull << 32) | 295ull) == VK_MAKE_API_VERSION(0, 1, 1, 0),
              "D6 XrVersion 의 patch 는 버림");
        check(xr12 != (uint64_t)VK_MAKE_API_VERSION(0, 1, 2, 0), "D6 두 부호화가 실제로 다름(그대로 비교하면 틀림)");
        const uint32_t huge = hxr::vkApiFromXrVersion(0xFFFFull << 48 | 0xFFFFull << 32);
        check(VK_API_VERSION_MAJOR(huge) == 0x7F && VK_API_VERSION_MINOR(huge) == 0x3FF && VK_API_VERSION_VARIANT(huge) == 0,
              "D7 칸을 넘는 값은 Vulkan 칸의 최댓값으로 자름(variant 비트를 건드리지 않음)");
        check(hvk::checkApiRange(v11, v10, huge) == hvk::ApiRange::InRange, "D7 아주 큰 최댓값은 제한 없음으로 남음");
        // minor 만 칸을 넘는 경우: 자르지 않으면 넘친 비트가 major 로 올라가 1.x 가 2.x 로 읽힙니다.
        const uint32_t bigMinor = hxr::vkApiFromXrVersion((1ull << 48) | (0x500ull << 32));
        check(VK_API_VERSION_MAJOR(bigMinor) == 1 && VK_API_VERSION_MINOR(bigMinor) == 0x3FF,
              "D8 minor 만 큰 값(1.1280)은 major 1 을 지키고 minor 를 1023 으로 자름");

        // XR 경로의 인스턴스 버전(DEF-164): 런타임 최댓값까지 올리되 로더를 넘지 않고 기준선 아래로 내리지 않음.
        const uint32_t base = hvk::kVulkanApiVersion, v14 = VK_API_VERSION_1_4;
        check(hvk::xrInstanceApiVersion(base, v12, v14) == v12, "D9 Quest Link(1.0 ~ 1.2, 로더 1.4) 는 1.2");
        check(hvk::checkApiRange(hvk::xrInstanceApiVersion(base, v12, v14), v10, v12) == hvk::ApiRange::InRange,
              "D9 올린 값도 런타임 범위 안");
        check(hvk::xrInstanceApiVersion(base, v11, v14) == v11, "D10 최댓값이 1.1 이면 1.1");
        check(hvk::xrInstanceApiVersion(base, v10, v14) == base, "D10 최댓값이 기준선보다 낮아도 기준선 아래로 내리지 않음");
        check(hvk::xrInstanceApiVersion(base, v13, v12) == v12, "D11 로더가 1.2 면 런타임 최댓값 1.3 대신 1.2");
        check(hvk::xrInstanceApiVersion(base, v12, v10) == base, "D11 로더가 기준선보다 낮아도 기준선(생성 실패는 로더가 알림)");
        check(hvk::xrInstanceApiVersion(base, 0, v14) == base, "D12 범위를 모르면 기준선 그대로");
        check(hvk::xrInstanceApiVersion(base, VK_MAKE_API_VERSION(0, 1, 2, 295), VK_MAKE_API_VERSION(0, 1, 4, 341)) == v12,
              "D13 patch 는 0 으로(1.2.295 → 1.2.0)");
        check(hvk::xrInstanceApiVersion(base, huge, VK_MAKE_API_VERSION(0, 1, 3, 250)) == v13,
              "D13 제한 없는 최댓값은 로더 버전(1.3.250 → 1.3.0)");
        check(hvk::checkApiRange(hvk::xrInstanceApiVersion(base, v13, v14), v12, v13) == hvk::ApiRange::InRange,
              "D14 런타임 범위가 1.2 ~ 1.3 이어도 XR 을 끄지 않음(예전 1.1 고정은 최솟값 미만)");
        static_assert(hvk::xrInstanceApiVersion(hvk::kVulkanApiVersion, 0, 0) == hvk::kVulkanApiVersion,
                      "XR range unknown keeps the baseline");
    }

    std::printf("\n== E. 기준선 잠금 ==\n");
    {
        static_assert(hvk::kVulkanApiVersion == VK_API_VERSION_1_1, "Vulkan baseline is 1.1");
        check(hvk::kVulkanApiVersion == VK_API_VERSION_1_1 && hvk::kMinDeviceApiVersion == VK_API_VERSION_1_1,
              "E1 인스턴스 · 장치 기준선이 1.1");
        check(hvk::versionString(VK_MAKE_API_VERSION(0, 1, 1, 295)) == "1.1.295", "E2 버전 문자열 1.1.295");
    }

    std::printf("\n== F. 큐 계열 ==\n");
    {
        const VkQueueFlags G = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
        const VkQueueFlags C = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
        const VkQueueFlags T = VK_QUEUE_TRANSFER_BIT;

        const std::vector<VkQueueFamilyProperties> a = { fam(G), fam(C) };
        const hvk::QueueFamilies qa = hvk::selectQueueFamilies(a, { VK_TRUE, VK_FALSE }, true);
        check(qa.graphics == 0u && qa.present == 0u, "F1 0번이 그래픽 + 표시 → (0, 0)");

        const hvk::QueueFamilies qb = hvk::selectQueueFamilies(a, {}, false);
        check(qb.complete(false) && qb.graphics == 0u && !qb.present.has_value(),
              "F2 표시가 필요 없고 표시 정보도 없으면 graphics = 0 으로 완전, present 비어 있음");
        check(!qb.complete(true), "F2 같은 결과는 표시가 필요하면 불완전");

        const std::vector<VkQueueFamilyProperties> c = { fam(G), fam(C) };
        const hvk::QueueFamilies qc = hvk::selectQueueFamilies(c, { VK_FALSE, VK_TRUE }, true);
        check(qc.graphics == 0u && qc.present == 1u, "F3 그래픽은 0번, 표시는 1번에만 → (0, 1)");

        const std::vector<VkQueueFamilyProperties> d = { fam(C), fam(T) };
        check(!hvk::selectQueueFamilies(d, { VK_TRUE, VK_TRUE }, true).complete(true) &&
              !hvk::selectQueueFamilies(d, {}, false).complete(false),
              "F4 그래픽 계열이 없으면 불완전");

        check(!hvk::selectQueueFamilies(a, { VK_FALSE, VK_FALSE }, true).complete(true), "F5 표시가 필요한데 없으면 불완전");

        // 예전 규칙과 같은 결과(다섯 가지 입력). 마지막 둘은 그래픽 계열이 여럿인 경우입니다.
        const std::vector<std::pair<std::vector<VkQueueFamilyProperties>, std::vector<VkBool32>>> cases = {
            { a, { VK_TRUE, VK_FALSE } },
            { c, { VK_FALSE, VK_TRUE } },
            { d, { VK_TRUE, VK_TRUE } },
            { { fam(G), fam(G) }, { VK_FALSE, VK_TRUE } },
            { { fam(C), fam(G), fam(G) }, { VK_TRUE, VK_FALSE, VK_FALSE } },
        };
        bool same = true;
        for (const auto& [f, pres] : cases) same = same && sameQ(hvk::selectQueueFamilies(f, pres, true), oldRule(f, pres));
        check(same, "F6 표시가 필요할 때 예전 findQueueFamilies 와 같은 결과(다섯 가지)");

        const hvk::QueueFamilies two = hvk::selectQueueFamilies({ fam(C), fam(G), fam(G) }, {}, false);
        check(two.graphics == 1u, "F7 표시가 필요 없으면 첫 그래픽 계열에서 멈춤");
    }

    std::printf("\n== G. 섀도맵 깊이 형식 ==\n");
    {
        const VkFormatFeatureFlags A = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        const VkFormatFeatureFlags S = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        const VkFormatFeatureFlags L = VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        auto table = [](VkFormatFeatureFlags d32, VkFormatFeatureFlags d16, VkFormatFeatureFlags x8d24) {
            return [=](VkFormat f) -> VkFormatFeatureFlags {
                if (f == VK_FORMAT_D32_SFLOAT)          return d32;
                if (f == VK_FORMAT_D16_UNORM)           return d16;
                if (f == VK_FORMAT_X8_D24_UNORM_PACK32) return x8d24;
                return 0;
            };
        };
        auto is = [](hvk::ShadowFormatChoice c, VkFormat f, bool lin) { return c.format == f && c.linearCompare == lin; };

        check(is(hvk::chooseShadowFormat(table(A | S | L, A | S | L, A | S | L)), VK_FORMAT_D32_SFLOAT, true),
              "G1 PC 형(세 형식 모두 어태치먼트 + 샘플 + LINEAR) → D32, LINEAR");
        check(is(hvk::chooseShadowFormat(table(S, A | S, 0)), VK_FORMAT_D16_UNORM, false),
              "G2 Quest 형 기준선(D32 샘플만, D16 LINEAR 없음, X8_D24 없음) → D16, LINEAR 아님");
        check(is(hvk::chooseShadowFormat(table(A | S, A | S | L, 0)), VK_FORMAT_D16_UNORM, true),
              "G3 D32 는 LINEAR 없음, D16 은 LINEAR 있음 → LINEAR 가 되는 D16");
        check(is(hvk::chooseShadowFormat(table(A | S, A | S, 0)), VK_FORMAT_D32_SFLOAT, false),
              "G4 Quest 형 형식 프로필(D32 · D16 모두 LINEAR 없음) → D32, LINEAR 아님");
        check(hvk::chooseShadowFormat(table(0, 0, 0)).format == VK_FORMAT_UNDEFINED, "G5 맞는 형식이 없으면 UNDEFINED");
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
