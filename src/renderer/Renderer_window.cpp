// 보조 OS 창 출력(다중 OS 창 M2, 2026-11-21, Docs/Editor_MultiWindow_Design.md D2 · D3).
//
// 3D 화면 없이 HSlate UI 만 그리는 창의 표면 · 스왑체인 · 세마포어 · UI 프레임버퍼를 창마다 둡니다. 메인 창 경로(swapchain_)는
// 건드리지 않고, drawFrame 은 목록이 비어 있으면 이 파일의 함수를 부르지 않습니다(게임 실행 파일 · Android · 창 없는 실행 · XR).
//
// 한 프레임의 순서(drawFrame):
//   슬롯 펜스 대기 → 메인 acquire(무한) → prepareWindowTargets(창마다 목록 → 해시 → acquire 타임아웃 0 → 이미지 펜스) →
//   펜스 리셋 → … 3D … → 메인 UI 기록(아틀라스 업로드 1회) → recordWindowTargets → 제출 1회(받은 창의 세마포어만) →
//   메인 present → presentWindowTargets(창마다 따로)
#include "Renderer.hpp"
#include "WindowTarget.hpp"
#include "GlfwSwapchain.hpp"
#include "RendererLog.hpp"
#include "../core/FrameTimings.hpp"
#include "../ui/slate/HSlateRenderer.hpp"
#include <algorithm>
#include <stdexcept>


Renderer::Renderer()  = default;
Renderer::~Renderer() = default;   // cleanup() 이 이미 모든 창을 없앴습니다(남은 것은 Vulkan 객체 없는 빈 껍데기)

WindowTarget* Renderer::findWindowTarget(uint32_t id) const {
    for (const auto& t : windowTargets_)
        if (t->id == id) return t.get();
    return nullptr;
}

uint32_t Renderer::windowTargetCount() const { return (uint32_t)windowTargets_.size(); }

bool Renderer::anyWindowAcquired() const {
    for (const auto& t : windowTargets_)
        if (t->acquired) return true;
    return false;
}

bool Renderer::anyWindowDrawable() const {
    for (const auto& t : windowTargets_)
        if (!t->failed && t->ui && t->swap && t->swap->hasArea()) return true;
    return false;
}

// 메인 창이 최소화된 비 XR 프레임(M3a, hwin::chooseDesktopFrame 의 AuxOnly). 메인 프레임과 같은 슬롯 펜스 · 명령 버퍼 · 슬롯 정점
// 버퍼를 쓰고, 3D · 메인 UI · 월드 패널 · 타임스탬프는 기록하지 않습니다. 슬롯 번호(frameIdx · frameNum_)는 제출한 경우에만
// 넘기므로 메인 프레임의 "슬롯 펜스가 그 슬롯의 마지막 제출을 지킴" 규칙이 그대로입니다. 프레임 계측 값은 건드리지 않습니다.
void Renderer::drawAuxOnlyFrame() {
    if (!uiRenderer_) return;
    const uint32_t fi = frameIdx;
    VkFence frameFence = frames_.fence(fi);
    vkWaitForFences(device, 1, &frameFence, VK_TRUE, UINT64_MAX);
    prepareWindowTargets(fi, frameFence);
    if (!anyWindowAcquired()) return;   // 해시가 같거나 준비 안 됨 — 펜스는 신호된 채 둠

    // 아래부터는 받은 이미지를 반드시 제출 · 표시합니다(prepareWindowTargets 의 규칙과 같음).
    vkResetFences(device, 1, &frameFence);
    VkCommandBuffer cmd = frames_.cmd(fi);
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo cbbi{};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &cbbi) != VK_SUCCESS)
        throw std::runtime_error("Failed to begin command buffer!");
    uiRenderer_->beginFrame(fi);   // 이 슬롯 정점 버퍼를 비움(메인 프레임의 UI 기록 앞과 같은 자리)
    recordWindowTargets(cmd, fi);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("Failed to record command buffer!");

    submitSems_.clear();
    for (const auto& t : windowTargets_)
        submitSems_.add(t->acquired, t->imageAvail[fi], t->renderDone[t->imgIdx]);
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = (uint32_t)submitSems_.wait.size();
    si.pWaitSemaphores      = submitSems_.wait.data();
    si.pWaitDstStageMask    = submitSems_.stages.data();
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cmd;
    si.signalSemaphoreCount = (uint32_t)submitSems_.signal.size();
    si.pSignalSemaphores    = submitSems_.signal.data();
    if (vkQueueSubmit(graphicsQueue, 1, &si, frameFence) != VK_SUCCESS)
        throw std::runtime_error("Failed to submit draw command!");
    presentWindowTargets();

    frameIdx = (frameIdx + 1) % MAX_FRAMES;
    frameNum_++;
}

// ── 만들기 · 없애기 ───────────────────────────────────────────────────────────────

uint32_t Renderer::addWindowTarget(IWindow* win) {
    if (!win || headless_ || presentQueue == VK_NULL_HANDLE || !queueFamilies_.present.has_value()) {
        HLOG(LogRenderer, Warning) << "Aux window target not added: no presentable desktop surface in this run";
        return 0;
    }
    if (!uiRenderer_ || uiRenderer_->windowRenderPass() == VK_NULL_HANDLE) {
        HLOG(LogRenderer, Warning) << "Aux window target not added: no UI renderer (setUIRenderer first)";
        return 0;
    }
    if (!win->providesSurface()) {
        HLOG(LogRenderer, Warning) << "Aux window target not added: the window gives no Vulkan surface";
        return 0;
    }

    auto t = std::make_unique<WindowTarget>();
    t->id  = nextWindowTargetId_;
    t->win = win;
    try {
        t->surface = win->createSurface(instance);
    } catch (const std::exception& e) {
        HLOG(LogRenderer, Warning) << "Aux window target not added: " << e.what();
        return 0;
    }
    if (t->surface == VK_NULL_HANDLE) {
        HLOG(LogRenderer, Warning) << "Aux window target not added: surface creation failed";
        return 0;
    }
    // 표시는 메인 창의 표시 큐로 합니다. 그 큐 패밀리가 이 표면에 표시할 수 없으면(다른 어댑터에 붙은 모니터 등) 만들지 않습니다.
    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physDevice, queueFamilies_.present.value(), t->surface, &supported);
    if (!supported) {
        HLOG(LogRenderer, Warning) << "Aux window target not added: present queue family " << queueFamilies_.present.value()
                                   << " cannot present to this surface";
        vkDestroySurfaceKHR(instance, t->surface, nullptr);
        return 0;
    }

    t->swap = std::make_unique<GlfwSwapchain>(win, t->surface, physDevice, device,
                                              queueFamilies_.graphics.value(), queueFamilies_.present.value());
    t->swap->setPreferNonBlocking(true);   // MAILBOX → IMMEDIATE → FIFO(D3)
    t->swap->setAcquireTimeout(0);         // 준비되지 않았으면 그 프레임은 건너뜀(D3)
    t->swap->setAuxiliary(true);           // 메인 창의 표시 계측 값을 덮어쓰지 않음

    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (auto& s : t->imageAvail)
        if (vkCreateSemaphore(device, &sci, nullptr, &s) != VK_SUCCESS) {
            HLOG(LogRenderer, Warning) << "Aux window target not added: semaphore creation failed";
            destroyWindowTarget(*t);
            return 0;
        }

    // 넓이가 있으면 지금 만들어 형식을 확인합니다. 최소화된 채 만들어진 창은 처음 넓이가 생긴 프레임에 만듭니다.
    if (t->swap->hasArea() && !rebuildWindowTarget(*t, true)) {
        destroyWindowTarget(*t);
        return 0;
    }
    ++nextWindowTargetId_;
    const uint32_t id = t->id;
    windowTargets_.push_back(std::move(t));
    return id;
}

void Renderer::removeWindowTarget(uint32_t id) {
    auto it = std::find_if(windowTargets_.begin(), windowTargets_.end(),
                           [id](const std::unique_ptr<WindowTarget>& t) { return t->id == id; });
    if (it == windowTargets_.end()) return;
    WindowTarget& t = **it;
    // 이 창을 기다리거나 표시 중인 제출이 남아 있을 수 있어 장치 전체가 쉬기를 기다립니다(창을 닫을 때만이라 드묾).
    vkDeviceWaitIdle(device);
    const WindowTargetStats& st = t.stats;
    HLOG(LogRenderer, Log) << "Aux window target " << id << " removed: presented " << st.presented << ", acquired "
                           << st.acquired << ", unchanged " << st.skippedUnchanged << ", not ready " << st.skippedNotReady
                           << ", no area " << st.skippedNoArea << ", recreated " << st.recreated;
    destroyWindowTarget(t);
    windowTargets_.erase(it);
}

void Renderer::setWindowUi(uint32_t id, HSlateContext* ctx) {
    if (WindowTarget* t = findWindowTarget(id)) {
        t->ui     = ctx;
        t->forced = true;
    }
}

void Renderer::requestWindowRedraw(uint32_t id) {
    if (WindowTarget* t = findWindowTarget(id)) t->forced = true;
}

Renderer::WindowTargetStats Renderer::windowTargetStats(uint32_t id) const {
    const WindowTarget* t = findWindowTarget(id);
    return t ? t->stats : WindowTargetStats{};
}

void Renderer::destroyWindowFramebuffers(WindowTarget& t) {
    for (VkFramebuffer fb : t.framebuffers)
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device, fb, nullptr);
    t.framebuffers.clear();
}

bool Renderer::buildWindowFramebuffers(WindowTarget& t) {
    destroyWindowFramebuffers(t);
    if (!uiRenderer_ || uiRenderer_->windowRenderPass() == VK_NULL_HANDLE) return false;
    const auto&      views  = t.swap->imageViews();
    const VkExtent2D extent = t.swap->extent();
    t.framebuffers.assign(views.size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < views.size(); ++i) {
        VkFramebufferCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = uiRenderer_->windowRenderPass();
        ci.attachmentCount = 1;
        ci.pAttachments    = &views[i];
        ci.width           = extent.width;
        ci.height          = extent.height;
        ci.layers          = 1;
        if (vkCreateFramebuffer(device, &ci, nullptr, &t.framebuffers[i]) != VK_SUCCESS) return false;
    }
    return true;
}

// 그 창의 스왑체인과 UI 프레임버퍼만 다시 만듭니다. 씬 타깃 · 메인 스왑체인은 건드리지 않습니다(recreateSwapChain 과 다름).
// 장치 대기는 이 창의 이미지를 읽는 제출이 남지 않게 하려는 것입니다(크기 변경 · 표시 실패 때만이라 드묾).
bool Renderer::rebuildWindowTarget(WindowTarget& t, bool first) {
    if (!t.swap->hasArea()) return false;   // 부른 쪽이 hasArea 를 보지만, 최소화 중에는 만들 수 없으므로 한 번 더
    vkDeviceWaitIdle(device);
    destroyWindowFramebuffers(t);
    try {
        if (t.created) t.swap->recreate();
        else { t.swap->create(); t.win->resetResizedFlag(); }
    } catch (const std::exception& e) {
        HLOG(LogRenderer, Warning) << "Aux window target " << t.id << " stopped: " << e.what();
        t.failed = true;
        return false;
    }
    t.created = true;
    // 파이프라인은 메인 UI 형식으로 만들었습니다. 형식이 다르면 포맷별 패스 · 파이프라인이 필요하므로 이 단계에서는 그 창을
    // 그리지 않습니다(chooseFormat 이 B8G8R8A8_SRGB 를 먼저 고르므로 보통은 같음).
    if (t.swap->format() != uiRenderer_->colorFormat()) {
        HLOG(LogRenderer, Warning) << "Aux window target " << t.id << " stopped: surface format " << (int)t.swap->format()
                                   << " differs from the UI format " << (int)uiRenderer_->colorFormat();
        t.failed = true;
        return false;
    }
    const uint32_t count = t.swap->imageCount();
    t.imagesInFlight.assign(count, VK_NULL_HANDLE);
    if (t.renderDone.size() != count) {
        for (VkSemaphore s : t.renderDone) vkDestroySemaphore(device, s, nullptr);
        t.renderDone.assign(count, VK_NULL_HANDLE);
        VkSemaphoreCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (auto& s : t.renderDone) vkCreateSemaphore(device, &sci, nullptr, &s);
    }
    if (!buildWindowFramebuffers(t)) {
        HLOG(LogRenderer, Warning) << "Aux window target " << t.id << " stopped: framebuffer creation failed";
        t.failed = true;
        return false;
    }
    t.drawn  = false;   // 새 이미지는 내용이 없습니다
    t.forced = true;
    if (!first) ++t.stats.recreated;
    if (first) {
        const VkExtent2D e = t.swap->extent();
        HLOG(LogRenderer, Log) << "Aux window target " << t.id << " added: " << e.width << "x" << e.height << ", "
                               << count << " images";
    }
    return true;
}

// 순서: (부른 쪽의 장치 대기) → 프레임버퍼 · 세마포어 → 스왑체인 → 표면. OS 창은 부른 쪽이 그 뒤에 없앱니다.
void Renderer::destroyWindowTarget(WindowTarget& t) {
    destroyWindowFramebuffers(t);
    for (VkSemaphore& s : t.imageAvail)
        if (s != VK_NULL_HANDLE) { vkDestroySemaphore(device, s, nullptr); s = VK_NULL_HANDLE; }
    for (VkSemaphore s : t.renderDone)
        if (s != VK_NULL_HANDLE) vkDestroySemaphore(device, s, nullptr);
    t.renderDone.clear();
    t.imagesInFlight.clear();
    t.swap.reset();   // vkDestroySwapchainKHR(표면보다 먼저)
    if (t.surface != VK_NULL_HANDLE) { vkDestroySurfaceKHR(instance, t.surface, nullptr); t.surface = VK_NULL_HANDLE; }
    t.win = nullptr;
}

// ── 프레임 ────────────────────────────────────────────────────────────────────────

void Renderer::prepareWindowTargets(uint32_t fi, VkFence frameFence) {
    for (auto& up : windowTargets_) {
        WindowTarget& t = *up;
        t.acquired = false;
        if (t.failed || !t.ui || !uiRenderer_) continue;
        if (!t.swap->hasArea()) { ++t.stats.skippedNoArea; continue; }   // 최소화(DEF-106)
        if (!t.created || t.swap->needsRecreation()) {
            if (!rebuildWindowTarget(t, !t.created)) continue;
        }

        // 목록은 넓이가 있을 때만 만듭니다. 아틀라스는 여기서 올리지 않습니다(모든 창의 목록을 만든 뒤 한 번).
        const VkExtent2D extent = t.swap->extent();
        uiRenderer_->buildWindow(*t.ui, extent, t.list);
        t.hash = drawListHash(t.list);
        const hwin::FramePlan plan = hwin::planFrame(true, t.drawn, t.forced, t.hash, t.lastHash);
        if (plan == hwin::FramePlan::SkipUnchanged) { ++t.stats.skippedUnchanged; continue; }

        uint32_t img = 0;
        const hwin::AcquireOutcome a = hwin::classifyAcquire(t.swap->acquireNextImage(t.imageAvail[fi], img));
        if (a.notReady) { ++t.stats.skippedNotReady; continue; }   // 세마포어는 신호되지 않음 → 대기 목록에 넣지 않음
        if (a.failed) {
            HLOG(LogRenderer, Warning) << "Aux window target " << t.id << " stopped: image acquire failed";
            t.failed = true;
            continue;
        }
        if (a.recreate) t.forced = true;   // OUT_OF_DATE 면 스왑체인이 dirty 를 켰고, 다음 프레임에 다시 만듦
        if (!a.acquired) continue;
        // 받았으면 이번 제출이 반드시 그 세마포어를 기다리고 표시해야 합니다(아래부터는 건너뛰는 길이 없음).
        // 이 이미지를 마지막으로 그린 제출이 끝나기를 기다립니다(메인 창 경로와 같은 이미지별 펜스). 프레임 펜스를 리셋하기 전이라
        // 같은 슬롯 펜스여도 이미 신호된 상태입니다.
        if (t.imagesInFlight[img] != VK_NULL_HANDLE)
            vkWaitForFences(device, 1, &t.imagesInFlight[img], VK_TRUE, UINT64_MAX);
        t.imagesInFlight[img] = frameFence;
        t.imgIdx   = img;
        t.acquired = true;
        ++t.stats.acquired;
    }
}

void Renderer::recordWindowTargets(VkCommandBuffer cmd, uint32_t fi) {
    if (!uiRenderer_) return;
    // 모든 창의 목록을 만든 뒤 한 번 올립니다. 메인 오버레이 record 가 이미 올렸으면 아무것도 하지 않습니다(메인 창이 최소화된
    // XR 프레임처럼 메인 UI 를 기록하지 않은 프레임에만 여기서 올림). 업로드는 따로 제출하고 큐가 쉬기를 기다리므로 이 프레임
    // 명령보다 먼저 끝납니다.
    uiRenderer_->flushFontUploads();
    for (auto& up : windowTargets_) {
        WindowTarget& t = *up;
        if (!t.acquired) continue;
        const bool complete = uiRenderer_->recordWindow(cmd, t.list, t.framebuffers[t.imgIdx], t.swap->extent(), fi);
        if (complete) {
            t.lastHash = t.hash;
            t.forced   = false;
        } else {
            t.forced = true;   // 예산 때문에 지우기만 함 — 다음 프레임에 다시
            ++t.stats.budgetCleared;
        }
        t.drawn = true;
    }
}

void Renderer::presentWindowTargets() {
    for (auto& up : windowTargets_) {
        WindowTarget& t = *up;
        if (!t.acquired) continue;
        t.acquired = false;
        const hwin::PresentOutcome p = hwin::classifyPresent(t.swap->present(presentQueue, t.imgIdx, t.renderDone[t.imgIdx]));
        if (p.shown) ++t.stats.presented;
        if (p.recreate || t.swap->needsRecreation()) t.forced = true;   // 다음 프레임 prepare 가 그 창만 다시 만듦
        if (p.failed) {
            HLOG(LogRenderer, Warning) << "Aux window target " << t.id << " stopped: present failed";
            t.failed = true;
        }
    }
}
