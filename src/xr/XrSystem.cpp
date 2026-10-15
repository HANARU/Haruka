#include "XrSystem.hpp"
#include "../core/HLog.hpp"

// Vulkan 연결 확장만 씁니다. Win32 전용 확장(XR_USE_PLATFORM_WIN32)은 쓰지 않으므로 windows.h 가
// 필요 없습니다.
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

static constinit HLogCategory LogXR{ "XR", ELogVerbosity::Log };

namespace {

const char* resultName(XrInstance inst, XrResult r) {
    static thread_local char buf[XR_MAX_RESULT_STRING_SIZE];
    if (inst != XR_NULL_HANDLE && XR_SUCCEEDED(xrResultToString(inst, r, buf))) return buf;
    std::snprintf(buf, sizeof(buf), "XrResult %d", (int)r);
    return buf;
}

const char* stateName(XrSessionState s) {
    switch (s) {
        case XR_SESSION_STATE_IDLE:         return "IDLE";
        case XR_SESSION_STATE_READY:        return "READY";
        case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
        case XR_SESSION_STATE_VISIBLE:      return "VISIBLE";
        case XR_SESSION_STATE_FOCUSED:      return "FOCUSED";
        case XR_SESSION_STATE_STOPPING:     return "STOPPING";
        case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
        case XR_SESSION_STATE_EXITING:      return "EXITING";
        default:                            return "UNKNOWN";
    }
}

// OpenXR 구조체의 고정 길이 이름 칸에 복사합니다(넘치면 자름, 항상 널 종료).
template <size_t N>
void copyName(char (&dst)[N], const char* src) {
    std::snprintf(dst, N, "%s", src);
}

HPose toPose(const XrPosef& p, bool valid) {
    HPose h;
    h.position    = { p.position.x, p.position.y, p.position.z };
    h.orientation = glm::quat(p.orientation.w, p.orientation.x, p.orientation.y, p.orientation.z);
    h.valid       = valid;
    return h;
}

constexpr XrSpaceLocationFlags kPoseValid =
    XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;

} // namespace

// ── Impl ──────────────────────────────────────────────────────────────────────
struct XrSystem::Impl {
    XrInstance     instance = XR_NULL_HANDLE;
    XrSystemId     systemId = XR_NULL_SYSTEM_ID;
    XrSession      session  = XR_NULL_HANDLE;
    XrSpace        appSpace = XR_NULL_HANDLE;    // LOCAL
    XrSpace        headSpace = XR_NULL_HANDLE;   // VIEW
    XrSwapchain    swapchain = XR_NULL_HANDLE;
    XrSessionState state   = XR_SESSION_STATE_UNKNOWN;
    bool           running = false;
    bool           lost    = false;
    bool           exitRequested = false;   // requestExit 로 끝내는 중
    bool           hasRefreshRateExt = false;
    XrEnvironmentBlendMode blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

    std::vector<VkImage> images;
    VkFormat             format = VK_FORMAT_UNDEFINED;
    VkExtent2D           eyeExtent{};
    std::array<XrView, kMaxViews> views{};   // 이번 프레임 locateViews 결과(endFrame 이 그대로 씀)
    bool                 viewsValid = false;
    float                periodHz   = 0.f;   // predictedDisplayPeriod 에서 구한 주사율

    // 입력(DEF-20)
    XrActionSet actionSet = XR_NULL_HANDLE;
    XrAction    gripPose = XR_NULL_HANDLE, aimPose = XR_NULL_HANDLE;
    XrAction    trigger = XR_NULL_HANDLE, squeeze = XR_NULL_HANDLE, thumbstick = XR_NULL_HANDLE;
    XrAction    primary = XR_NULL_HANDLE, secondary = XR_NULL_HANDLE;
    XrAction    stickClick = XR_NULL_HANDLE, menu = XR_NULL_HANDLE;
    XrPath      handPath[2] = { XR_NULL_PATH, XR_NULL_PATH };
    XrSpace     gripSpace[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
    XrSpace     aimSpace[2]  = { XR_NULL_HANDLE, XR_NULL_HANDLE };

    PFN_xrGetVulkanGraphicsRequirements2KHR getRequirements = nullptr;
    PFN_xrCreateVulkanInstanceKHR           createVkInstance = nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR       getVkDevice      = nullptr;
    PFN_xrCreateVulkanDeviceKHR             createVkDevice   = nullptr;
    PFN_xrGetDisplayRefreshRateFB           getRefreshRate   = nullptr;

    // 실패를 로그에 남기고 false 를 돌려줍니다.
    bool ok(XrResult r, const char* what) const {
        if (XR_SUCCEEDED(r)) return true;
        HLOG(LogXR, Error) << what << " failed: " << resultName(instance, r);
        return false;
    }
    template <class Fn>
    bool load(const char* name, Fn& out) const {
        return ok(xrGetInstanceProcAddr(instance, name, reinterpret_cast<PFN_xrVoidFunction*>(&out)), name);
    }

    bool createActions();
    bool createSwapchain(VkFormat preferred);
};

XrSystem::XrSystem() : impl_(std::make_unique<Impl>()) {}
XrSystem::~XrSystem() { shutdown(); }

bool XrSystem::ready() const { return impl_->systemId != XR_NULL_SYSTEM_ID; }
bool XrSystem::hasSession() const { return impl_->session != XR_NULL_HANDLE; }
bool XrSystem::sessionRunning() const { return impl_->running; }
bool XrSystem::sessionLost() const { return impl_->lost; }
bool XrSystem::exitRequested() const { return impl_->exitRequested; }
const std::vector<VkImage>& XrSystem::swapchainImages() const { return impl_->images; }
VkFormat   XrSystem::swapchainFormat() const { return impl_->format; }
VkExtent2D XrSystem::eyeExtent() const { return impl_->eyeExtent; }

// ── 1. 인스턴스 · 시스템 ──────────────────────────────────────────────────────
bool XrSystem::init() {
    Impl& m = *impl_;

    // 런타임이 없으면 로더가 여기서 실패합니다(XR_ERROR_RUNTIME_UNAVAILABLE 등).
    uint32_t extCount = 0;
    XrResult r = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr);
    if (XR_FAILED(r)) {
        HLOG(LogXR, Warning) << "No OpenXR runtime is available (" << resultName(XR_NULL_HANDLE, r)
                             << "). Install and start a PC VR runtime such as Meta Quest Link. "
                                "Continuing in desktop mode.";
        return false;
    }
    std::vector<XrExtensionProperties> exts(extCount, { XR_TYPE_EXTENSION_PROPERTIES });
    xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, exts.data());
    auto has = [&](const char* name) {
        return std::any_of(exts.begin(), exts.end(),
                           [&](const XrExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
    };
    if (!has(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME)) {
        HLOG(LogXR, Warning) << "The OpenXR runtime does not support " << XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME
                             << ". Continuing in desktop mode.";
        return false;
    }
    std::vector<const char*> enabled = { XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME };
    m.hasRefreshRateExt = has(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    if (m.hasRefreshRateExt) enabled.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);

    XrInstanceCreateInfo ci{ XR_TYPE_INSTANCE_CREATE_INFO };
    copyName(ci.applicationInfo.applicationName, "HarukaEngine");
    copyName(ci.applicationInfo.engineName, "Haruka");
    ci.applicationInfo.applicationVersion = 1;
    ci.applicationInfo.engineVersion      = 1;
    // 1.0 으로 만듭니다. 1.1 기능을 쓰지 않고, 1.0 만 지원하는 런타임에서도 열리게 하기 위해서입니다.
    ci.applicationInfo.apiVersion         = XR_API_VERSION_1_0;
    ci.enabledExtensionCount = (uint32_t)enabled.size();
    ci.enabledExtensionNames = enabled.data();
    if (!m.ok(xrCreateInstance(&ci, &m.instance), "xrCreateInstance")) return false;

    XrInstanceProperties ip{ XR_TYPE_INSTANCE_PROPERTIES };
    if (XR_SUCCEEDED(xrGetInstanceProperties(m.instance, &ip)))
        HLOG(LogXR, Log) << "OpenXR runtime: " << ip.runtimeName << " "
                         << XR_VERSION_MAJOR(ip.runtimeVersion) << "." << XR_VERSION_MINOR(ip.runtimeVersion)
                         << "." << XR_VERSION_PATCH(ip.runtimeVersion);

    XrSystemGetInfo sgi{ XR_TYPE_SYSTEM_GET_INFO };
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = xrGetSystem(m.instance, &sgi, &m.systemId);
    if (XR_FAILED(r)) {
        HLOG(LogXR, Warning) << "No headset found (" << resultName(m.instance, r)
                             << "). Connect the headset (for Quest, start Quest Link) before launching. "
                                "Continuing in desktop mode.";
        m.systemId = XR_NULL_SYSTEM_ID;
        xrDestroyInstance(m.instance);
        m.instance = XR_NULL_HANDLE;
        return false;
    }
    XrSystemProperties sp{ XR_TYPE_SYSTEM_PROPERTIES };
    if (XR_SUCCEEDED(xrGetSystemProperties(m.instance, m.systemId, &sp)))
        HLOG(LogXR, Log) << "Headset: " << sp.systemName;

    bool loaded = m.load("xrGetVulkanGraphicsRequirements2KHR", m.getRequirements) &&
                  m.load("xrCreateVulkanInstanceKHR",           m.createVkInstance) &&
                  m.load("xrGetVulkanGraphicsDevice2KHR",       m.getVkDevice) &&
                  m.load("xrCreateVulkanDeviceKHR",             m.createVkDevice);
    if (m.hasRefreshRateExt) m.load("xrGetDisplayRefreshRateFB", m.getRefreshRate);
    // 세션을 만들기 전에 반드시 불러야 합니다(안 부르면 xrCreateSession 이 실패).
    XrGraphicsRequirementsVulkan2KHR req{ XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR };
    if (!loaded || !m.ok(m.getRequirements(m.instance, m.systemId, &req), "xrGetVulkanGraphicsRequirements2KHR")) {
        shutdown();
        return false;
    }
    HLOG(LogXR, Log) << "Runtime Vulkan API range: "
                     << XR_VERSION_MAJOR(req.minApiVersionSupported) << "." << XR_VERSION_MINOR(req.minApiVersionSupported)
                     << " to " << XR_VERSION_MAJOR(req.maxApiVersionSupported) << "." << XR_VERSION_MINOR(req.maxApiVersionSupported);
    return true;
}

// ── 2. Vulkan 연결부 ──────────────────────────────────────────────────────────
VkResult XrSystem::createVulkanInstance(const VkInstanceCreateInfo& ci, VkInstance& out) {
    Impl& m = *impl_;
    XrVulkanInstanceCreateInfoKHR xci{ XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR };
    xci.systemId               = m.systemId;
    xci.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
    xci.vulkanCreateInfo       = &ci;
    VkResult vr = VK_ERROR_INITIALIZATION_FAILED;
    if (!m.ok(m.createVkInstance(m.instance, &xci, &out, &vr), "xrCreateVulkanInstanceKHR"))
        return VK_ERROR_INITIALIZATION_FAILED;
    return vr;
}

VkPhysicalDevice XrSystem::vulkanPhysicalDevice(VkInstance instance) {
    Impl& m = *impl_;
    XrVulkanGraphicsDeviceGetInfoKHR gi{ XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR };
    gi.systemId       = m.systemId;
    gi.vulkanInstance = instance;
    VkPhysicalDevice pd = VK_NULL_HANDLE;
    if (!m.ok(m.getVkDevice(m.instance, &gi, &pd), "xrGetVulkanGraphicsDevice2KHR"))
        return VK_NULL_HANDLE;
    return pd;
}

VkResult XrSystem::createVulkanDevice(VkPhysicalDevice physicalDevice,
                                      const VkDeviceCreateInfo& ci, VkDevice& out) {
    Impl& m = *impl_;
    XrVulkanDeviceCreateInfoKHR dci{ XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR };
    dci.systemId               = m.systemId;
    dci.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
    dci.vulkanPhysicalDevice   = physicalDevice;
    dci.vulkanCreateInfo       = &ci;
    VkResult vr = VK_ERROR_INITIALIZATION_FAILED;
    if (!m.ok(m.createVkDevice(m.instance, &dci, &out, &vr), "xrCreateVulkanDeviceKHR"))
        return VK_ERROR_INITIALIZATION_FAILED;
    return vr;
}

// ── 3. 세션 ───────────────────────────────────────────────────────────────────
bool XrSystem::createSession(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                             uint32_t queueFamily, VkFormat preferredFormat) {
    Impl& m = *impl_;
    if (!ready()) return false;

    XrGraphicsBindingVulkan2KHR gb{ XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR };
    gb.instance         = instance;
    gb.physicalDevice   = physicalDevice;
    gb.device           = device;
    gb.queueFamilyIndex = queueFamily;
    gb.queueIndex       = 0;   // 렌더러가 제출하는 바로 그 큐(그래픽 큐 0번)
    XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO };
    sci.next     = &gb;
    sci.systemId = m.systemId;
    if (!m.ok(xrCreateSession(m.instance, &sci, &m.session), "xrCreateSession")) return false;

    XrReferenceSpaceCreateInfo rs{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rs.poseInReferenceSpace.orientation.w = 1.f;
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (!m.ok(xrCreateReferenceSpace(m.session, &rs, &m.appSpace), "xrCreateReferenceSpace(LOCAL)")) {
        destroySession();
        return false;
    }
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    m.ok(xrCreateReferenceSpace(m.session, &rs, &m.headSpace), "xrCreateReferenceSpace(VIEW)");

    // 불투명 합성을 고릅니다(VR). 런타임이 첫 번째로 권하는 것이 불투명이 아니면 그것을 씁니다.
    uint32_t bc = 0;
    if (XR_SUCCEEDED(xrEnumerateEnvironmentBlendModes(m.instance, m.systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &bc, nullptr)) && bc > 0) {
        std::vector<XrEnvironmentBlendMode> modes(bc);
        xrEnumerateEnvironmentBlendModes(m.instance, m.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                         bc, &bc, modes.data());
        m.blendMode = std::find(modes.begin(), modes.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE) != modes.end()
                    ? XR_ENVIRONMENT_BLEND_MODE_OPAQUE : modes[0];
    }

    if (!m.createSwapchain(preferredFormat) || !m.createActions()) {
        destroySession();
        return false;
    }
    m.lost = false;
    return true;
}

bool XrSystem::Impl::createSwapchain(VkFormat preferred) {
    uint32_t vc = 0;
    if (!ok(xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                              0, &vc, nullptr), "xrEnumerateViewConfigurationViews"))
        return false;
    if (vc != kMaxViews) {
        HLOG(LogXR, Error) << "Expected " << kMaxViews << " stereo views, runtime reports " << vc;
        return false;
    }
    std::vector<XrViewConfigurationView> cfg(vc, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
    xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                      vc, &vc, cfg.data());
    // 두 눈이 같은 크기를 권한다고 보고 한 장의 두 층 배열에 담습니다(조사에서 권한 방식).
    eyeExtent = { cfg[0].recommendedImageRectWidth, cfg[0].recommendedImageRectHeight };

    uint32_t fc = 0;
    xrEnumerateSwapchainFormats(session, 0, &fc, nullptr);
    std::vector<int64_t> fmts(fc);
    xrEnumerateSwapchainFormats(session, fc, &fc, fmts.data());
    // 데스크톱과 같은 포맷이면 톤매핑 파이프라인을 함께 씁니다. 없으면 sRGB 포맷을, 그것도 없으면
    // 런타임이 가장 먼저 권하는 것을 씁니다(UNORM 이면 셰이더가 sRGB 곡선을 적용).
    const int64_t wanted[] = { (int64_t)preferred, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB };
    int64_t chosen = fmts.empty() ? 0 : fmts[0];
    for (int64_t w : wanted)
        if (std::find(fmts.begin(), fmts.end(), w) != fmts.end()) { chosen = w; break; }
    format = (VkFormat)chosen;

    XrSwapchainCreateInfo sc{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
    sc.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    sc.format      = chosen;
    sc.sampleCount = 1;
    sc.width       = eyeExtent.width;
    sc.height      = eyeExtent.height;
    sc.faceCount   = 1;
    sc.arraySize   = kMaxViews;   // 0 층 = 왼쪽 눈, 1 층 = 오른쪽 눈
    sc.mipCount    = 1;
    if (!ok(xrCreateSwapchain(session, &sc, &swapchain), "xrCreateSwapchain")) return false;

    uint32_t ic = 0;
    xrEnumerateSwapchainImages(swapchain, 0, &ic, nullptr);
    std::vector<XrSwapchainImageVulkan2KHR> imgs(ic, { XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR });
    if (!ok(xrEnumerateSwapchainImages(swapchain, ic, &ic,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(imgs.data())), "xrEnumerateSwapchainImages"))
        return false;
    images.clear();
    for (const auto& im : imgs) images.push_back(im.image);
    HLOG(LogXR, Log) << "Swapchain: " << eyeExtent.width << "x" << eyeExtent.height
                     << " per eye, 2 layers, " << ic << " images, VkFormat " << chosen;
    return true;
}

// 기본 액션 세트 하나(조사에서 권한 첫 단계). Touch 컨트롤러와 Khronos 단순 컨트롤러에 바인딩을 제안합니다.
bool XrSystem::Impl::createActions() {
    XrActionSetCreateInfo asci{ XR_TYPE_ACTION_SET_CREATE_INFO };
    copyName(asci.actionSetName, "gameplay");
    copyName(asci.localizedActionSetName, "Gameplay");
    if (!ok(xrCreateActionSet(instance, &asci, &actionSet), "xrCreateActionSet")) return false;

    xrStringToPath(instance, "/user/hand/left",  &handPath[0]);
    xrStringToPath(instance, "/user/hand/right", &handPath[1]);

    auto makeAction = [&](XrAction& out, XrActionType type, const char* name, const char* label) {
        XrActionCreateInfo ci{ XR_TYPE_ACTION_CREATE_INFO };
        ci.actionType = type;
        copyName(ci.actionName, name);
        copyName(ci.localizedActionName, label);
        ci.countSubactionPaths = 2;
        ci.subactionPaths      = handPath;
        return ok(xrCreateAction(actionSet, &ci, &out), name);
    };
    if (!makeAction(gripPose,   XR_ACTION_TYPE_POSE_INPUT,     "grip_pose",        "Grip pose") ||
        !makeAction(aimPose,    XR_ACTION_TYPE_POSE_INPUT,     "aim_pose",         "Aim pose") ||
        !makeAction(trigger,    XR_ACTION_TYPE_FLOAT_INPUT,    "trigger",          "Trigger") ||
        !makeAction(squeeze,    XR_ACTION_TYPE_FLOAT_INPUT,    "squeeze",          "Squeeze") ||
        !makeAction(thumbstick, XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick",       "Thumbstick") ||
        !makeAction(primary,    XR_ACTION_TYPE_BOOLEAN_INPUT,  "primary",          "Primary button") ||
        !makeAction(secondary,  XR_ACTION_TYPE_BOOLEAN_INPUT,  "secondary",        "Secondary button") ||
        !makeAction(stickClick, XR_ACTION_TYPE_BOOLEAN_INPUT,  "thumbstick_click", "Thumbstick click") ||
        !makeAction(menu,       XR_ACTION_TYPE_BOOLEAN_INPUT,  "menu",             "Menu"))
        return false;

    auto path = [&](const char* s) { XrPath p = XR_NULL_PATH; xrStringToPath(instance, s, &p); return p; };
    auto suggest = [&](const char* profile, const std::vector<XrActionSuggestedBinding>& b) {
        XrInteractionProfileSuggestedBinding sb{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        sb.interactionProfile     = path(profile);
        sb.suggestedBindings      = b.data();
        sb.countSuggestedBindings = (uint32_t)b.size();
        // 런타임이 모르는 프로파일이면 실패합니다. 다른 프로파일로 계속할 수 있으므로 경고만 남깁니다.
        const XrResult r = xrSuggestInteractionProfileBindings(instance, &sb);
        if (XR_FAILED(r))
            HLOG(LogXR, Warning) << "Binding suggestion for " << profile << " rejected: " << resultName(instance, r);
    };
    suggest("/interaction_profiles/oculus/touch_controller", {
        { gripPose,   path("/user/hand/left/input/grip/pose") },
        { gripPose,   path("/user/hand/right/input/grip/pose") },
        { aimPose,    path("/user/hand/left/input/aim/pose") },
        { aimPose,    path("/user/hand/right/input/aim/pose") },
        { trigger,    path("/user/hand/left/input/trigger/value") },
        { trigger,    path("/user/hand/right/input/trigger/value") },
        { squeeze,    path("/user/hand/left/input/squeeze/value") },
        { squeeze,    path("/user/hand/right/input/squeeze/value") },
        { thumbstick, path("/user/hand/left/input/thumbstick") },
        { thumbstick, path("/user/hand/right/input/thumbstick") },
        { primary,    path("/user/hand/left/input/x/click") },
        { primary,    path("/user/hand/right/input/a/click") },
        { secondary,  path("/user/hand/left/input/y/click") },
        { secondary,  path("/user/hand/right/input/b/click") },
        { stickClick, path("/user/hand/left/input/thumbstick/click") },
        { stickClick, path("/user/hand/right/input/thumbstick/click") },
        { menu,       path("/user/hand/left/input/menu/click") },
    });
    suggest("/interaction_profiles/khr/simple_controller", {
        { gripPose, path("/user/hand/left/input/grip/pose") },
        { gripPose, path("/user/hand/right/input/grip/pose") },
        { aimPose,  path("/user/hand/left/input/aim/pose") },
        { aimPose,  path("/user/hand/right/input/aim/pose") },
        { trigger,  path("/user/hand/left/input/select/click") },
        { trigger,  path("/user/hand/right/input/select/click") },
        { menu,     path("/user/hand/left/input/menu/click") },
        { menu,     path("/user/hand/right/input/menu/click") },
    });

    for (int h = 0; h < 2; ++h) {
        XrActionSpaceCreateInfo sci{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
        sci.subactionPath = handPath[h];
        sci.poseInActionSpace.orientation.w = 1.f;
        sci.action = gripPose;
        ok(xrCreateActionSpace(session, &sci, &gripSpace[h]), "xrCreateActionSpace(grip)");
        sci.action = aimPose;
        ok(xrCreateActionSpace(session, &sci, &aimSpace[h]), "xrCreateActionSpace(aim)");
    }
    XrSessionActionSetsAttachInfo ai{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    ai.countActionSets = 1;
    ai.actionSets      = &actionSet;
    return ok(xrAttachSessionActionSets(session, &ai), "xrAttachSessionActionSets");
}

void XrSystem::destroySession() {
    Impl& m = *impl_;
    for (int h = 0; h < 2; ++h) {
        if (m.gripSpace[h]) { xrDestroySpace(m.gripSpace[h]); m.gripSpace[h] = XR_NULL_HANDLE; }
        if (m.aimSpace[h])  { xrDestroySpace(m.aimSpace[h]);  m.aimSpace[h]  = XR_NULL_HANDLE; }
    }
    // 액션 세트를 지우면 그 액션도 함께 지워집니다.
    if (m.actionSet) { xrDestroyActionSet(m.actionSet); m.actionSet = XR_NULL_HANDLE; }
    m.gripPose = m.aimPose = m.trigger = m.squeeze = m.thumbstick = XR_NULL_HANDLE;
    m.primary = m.secondary = m.stickClick = m.menu = XR_NULL_HANDLE;
    if (m.swapchain) { xrDestroySwapchain(m.swapchain); m.swapchain = XR_NULL_HANDLE; }
    m.images.clear();
    if (m.headSpace) { xrDestroySpace(m.headSpace); m.headSpace = XR_NULL_HANDLE; }
    if (m.appSpace)  { xrDestroySpace(m.appSpace);  m.appSpace  = XR_NULL_HANDLE; }
    if (m.session)   { xrDestroySession(m.session); m.session   = XR_NULL_HANDLE; }
    m.running       = false;
    m.exitRequested = false;
    m.state         = XR_SESSION_STATE_UNKNOWN;
}

bool XrSystem::requestExit() {
    Impl& m = *impl_;
    if (!m.session || !m.running) return false;
    if (!m.ok(xrRequestExitSession(m.session), "xrRequestExitSession")) return false;
    m.exitRequested = true;
    return true;
}

void XrSystem::shutdown() {
    Impl& m = *impl_;
    destroySession();
    if (m.instance) { xrDestroyInstance(m.instance); m.instance = XR_NULL_HANDLE; }
    m.systemId = XR_NULL_SYSTEM_ID;
}

// ── 4. 매 프레임 ──────────────────────────────────────────────────────────────
void XrSystem::pollEvents() {
    Impl& m = *impl_;
    if (!m.instance) return;
    XrEventDataBuffer ev{ XR_TYPE_EVENT_DATA_BUFFER };
    while (xrPollEvent(m.instance, &ev) == XR_SUCCESS) {
        switch (ev.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            const auto& e = *reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
            if (e.session != m.session) break;
            m.state = e.state;
            HLOG(LogXR, Log) << "Session state: " << stateName(e.state);
            if (e.state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi{ XR_TYPE_SESSION_BEGIN_INFO };
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                m.running = m.ok(xrBeginSession(m.session, &bi), "xrBeginSession");
            } else if (e.state == XR_SESSION_STATE_STOPPING) {
                m.ok(xrEndSession(m.session), "xrEndSession");
                m.running = false;
            } else if (e.state == XR_SESSION_STATE_EXITING || e.state == XR_SESSION_STATE_LOSS_PENDING) {
                m.running = false;
                m.lost    = true;   // 엔진이 렌더러에서 스왑체인을 뗀 뒤 destroySession
            }
            break;
        }
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            HLOG(LogXR, Warning) << "OpenXR instance loss pending; ending XR for this run";
            m.running = false;
            m.lost    = true;
            break;
        case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
            for (int h = 0; h < 2; ++h) {
                XrInteractionProfileState ps{ XR_TYPE_INTERACTION_PROFILE_STATE };
                if (m.session && XR_SUCCEEDED(xrGetCurrentInteractionProfile(m.session, m.handPath[h], &ps))
                    && ps.interactionProfile != XR_NULL_PATH) {
                    char buf[XR_MAX_PATH_LENGTH]; uint32_t len = 0;
                    if (XR_SUCCEEDED(xrPathToString(m.instance, ps.interactionProfile, sizeof(buf), &len, buf)))
                        HLOG(LogXR, Log) << (h == 0 ? "Left" : "Right") << " controller profile: " << buf;
                }
            }
            break;
        }
        default:
            break;
        }
        ev = { XR_TYPE_EVENT_DATA_BUFFER };
    }
}

bool XrSystem::beginFrame(FrameInfo& out) {
    Impl& m = *impl_;
    out = {};
    if (!m.running) return false;
    XrFrameWaitInfo wi{ XR_TYPE_FRAME_WAIT_INFO };
    XrFrameState    fs{ XR_TYPE_FRAME_STATE };
    if (!m.ok(xrWaitFrame(m.session, &wi, &fs), "xrWaitFrame")) return false;
    XrFrameBeginInfo bi{ XR_TYPE_FRAME_BEGIN_INFO };
    // XR_FRAME_DISCARDED 도 성공 코드입니다(앞 프레임을 끝내지 못했을 때).
    if (!m.ok(xrBeginFrame(m.session, &bi), "xrBeginFrame")) return false;
    if (fs.predictedDisplayPeriod > 0) m.periodHz = 1e9f / (float)fs.predictedDisplayPeriod;
    out.shouldRender = fs.shouldRender == XR_TRUE;
    out.displayTime  = fs.predictedDisplayTime;
    m.viewsValid     = false;
    return true;
}

void XrSystem::syncInput(int64_t displayTime, HXrInputState& out) {
    Impl& m = *impl_;
    out = {};
    if (!m.running || m.state != XR_SESSION_STATE_FOCUSED || !m.actionSet) return;
    XrActiveActionSet aas{ m.actionSet, XR_NULL_PATH };
    XrActionsSyncInfo si{ XR_TYPE_ACTIONS_SYNC_INFO };
    si.countActiveActionSets = 1;
    si.activeActionSets      = &aas;
    if (XR_FAILED(xrSyncActions(m.session, &si))) return;
    out.focused = true;

    if (m.headSpace) {
        XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
        if (XR_SUCCEEDED(xrLocateSpace(m.headSpace, m.appSpace, displayTime, &loc)))
            out.head = toPose(loc.pose, (loc.locationFlags & kPoseValid) == kPoseValid);
    }
    for (int h = 0; h < 2; ++h) {
        HHandInput& hand = out.hands[h];
        XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO };
        gi.subactionPath = m.handPath[h];

        XrActionStateFloat f{ XR_TYPE_ACTION_STATE_FLOAT };
        gi.action = m.trigger;
        if (XR_SUCCEEDED(xrGetActionStateFloat(m.session, &gi, &f)) && f.isActive) { hand.trigger = f.currentState; hand.active = true; }
        f = { XR_TYPE_ACTION_STATE_FLOAT };
        gi.action = m.squeeze;
        if (XR_SUCCEEDED(xrGetActionStateFloat(m.session, &gi, &f)) && f.isActive) { hand.squeeze = f.currentState; hand.active = true; }

        XrActionStateVector2f v{ XR_TYPE_ACTION_STATE_VECTOR2F };
        gi.action = m.thumbstick;
        if (XR_SUCCEEDED(xrGetActionStateVector2f(m.session, &gi, &v)) && v.isActive) {
            hand.thumbstick = { v.currentState.x, v.currentState.y };
            hand.active = true;
        }

        auto button = [&](XrAction a, bool& dst) {
            XrActionStateBoolean b{ XR_TYPE_ACTION_STATE_BOOLEAN };
            gi.action = a;
            if (XR_SUCCEEDED(xrGetActionStateBoolean(m.session, &gi, &b)) && b.isActive) {
                dst = b.currentState == XR_TRUE;
                hand.active = true;
            }
        };
        button(m.primary, hand.primary);
        button(m.secondary, hand.secondary);
        button(m.stickClick, hand.thumbstickClick);
        button(m.menu, hand.menu);

        // 손 자세와 속도(던지기).
        XrSpaceVelocity vel{ XR_TYPE_SPACE_VELOCITY };
        XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
        loc.next = &vel;
        if (m.gripSpace[h] && XR_SUCCEEDED(xrLocateSpace(m.gripSpace[h], m.appSpace, displayTime, &loc))) {
            hand.grip = toPose(loc.pose, (loc.locationFlags & kPoseValid) == kPoseValid);
            if (vel.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT)
                hand.linearVelocity = { vel.linearVelocity.x, vel.linearVelocity.y, vel.linearVelocity.z };
            if (vel.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT)
                hand.angularVelocity = { vel.angularVelocity.x, vel.angularVelocity.y, vel.angularVelocity.z };
        }
        XrSpaceLocation aim{ XR_TYPE_SPACE_LOCATION };
        if (m.aimSpace[h] && XR_SUCCEEDED(xrLocateSpace(m.aimSpace[h], m.appSpace, displayTime, &aim)))
            hand.aim = toPose(aim.pose, (aim.locationFlags & kPoseValid) == kPoseValid);
    }
}

bool XrSystem::locateViews(int64_t displayTime, std::array<XrEyePose, kMaxViews>& eyes) {
    Impl& m = *impl_;
    XrViewLocateInfo li{ XR_TYPE_VIEW_LOCATE_INFO };
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime           = displayTime;
    li.space                 = m.appSpace;
    XrViewState vs{ XR_TYPE_VIEW_STATE };
    uint32_t n = 0;
    for (auto& v : m.views) v = { XR_TYPE_VIEW };
    if (XR_FAILED(xrLocateViews(m.session, &li, &vs, kMaxViews, &n, m.views.data())) || n != kMaxViews)
        return false;
    // 머리 추적을 잃으면 이 프레임은 그리지 않습니다(빈 프레임 제출). 엉뚱한 자세로 그리면 멀미가 납니다.
    const XrViewStateFlags need = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    if ((vs.viewStateFlags & need) != need) return false;
    for (uint32_t i = 0; i < kMaxViews; ++i) {
        const XrView& v = m.views[i];
        XrEyePose& e = eyes[i];
        e.position   = { v.pose.position.x, v.pose.position.y, v.pose.position.z };
        e.qx = v.pose.orientation.x; e.qy = v.pose.orientation.y;
        e.qz = v.pose.orientation.z; e.qw = v.pose.orientation.w;
        e.angleLeft = v.fov.angleLeft;  e.angleRight = v.fov.angleRight;
        e.angleUp   = v.fov.angleUp;    e.angleDown  = v.fov.angleDown;
    }
    m.viewsValid = true;
    return true;
}

bool XrSystem::acquireImage(uint32_t& index) {
    Impl& m = *impl_;
    XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    if (!m.ok(xrAcquireSwapchainImage(m.swapchain, &ai, &index), "xrAcquireSwapchainImage")) return false;
    XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wi.timeout = XR_INFINITE_DURATION;
    if (!m.ok(xrWaitSwapchainImage(m.swapchain, &wi), "xrWaitSwapchainImage")) {
        releaseImage();
        return false;
    }
    return true;
}

void XrSystem::releaseImage() {
    Impl& m = *impl_;
    XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    m.ok(xrReleaseSwapchainImage(m.swapchain, &ri), "xrReleaseSwapchainImage");
}

void XrSystem::endFrame(int64_t displayTime, bool rendered) {
    Impl& m = *impl_;
    std::array<XrCompositionLayerProjectionView, kMaxViews> pv{};
    XrCompositionLayerProjection layer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    const XrCompositionLayerBaseHeader* layers[] = {
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer) };
    const bool submit = rendered && m.viewsValid;
    if (submit) {
        // 렌더할 때 쓴 바로 그 자세 · 시야를 제출합니다(DEF-32: 한 프레임에 자세 하나).
        for (uint32_t i = 0; i < kMaxViews; ++i) {
            pv[i] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
            pv[i].pose = m.views[i].pose;
            pv[i].fov  = m.views[i].fov;
            pv[i].subImage.swapchain       = m.swapchain;
            pv[i].subImage.imageRect       = { { 0, 0 }, { (int32_t)m.eyeExtent.width, (int32_t)m.eyeExtent.height } };
            pv[i].subImage.imageArrayIndex = i;
        }
        layer.space     = m.appSpace;
        layer.viewCount = kMaxViews;
        layer.views     = pv.data();
    }
    XrFrameEndInfo ei{ XR_TYPE_FRAME_END_INFO };
    ei.displayTime          = displayTime;
    ei.environmentBlendMode = m.blendMode;
    ei.layerCount           = submit ? 1u : 0u;
    ei.layers               = submit ? layers : nullptr;
    m.ok(xrEndFrame(m.session, &ei), "xrEndFrame");
}

float XrSystem::displayRefreshRate() const {
    const Impl& m = *impl_;
    if (m.getRefreshRate && m.session) {
        float hz = 0.f;
        if (XR_SUCCEEDED(m.getRefreshRate(m.session, &hz)) && hz > 0.f) return hz;
    }
    return m.periodHz;
}
