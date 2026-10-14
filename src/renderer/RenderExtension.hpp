#pragma once

// ── 렌더 확장 지점 (DEF-17 · DEF-18) ───────────────────────────────────────────
// Layer 2(도메인)가 렌더러를 고치지 않고 정해진 지점에 그리기를 끼워 넣는 통로입니다. Godot 의
// CompositorEffect, Unity URP 의 Renderer Feature(RenderPassEvent), UE 의 SceneViewExtension 처럼
// 고정된 지점 방식입니다. 렌더 그래프(Bevy)는 렌더러를 다시 짜는 규모라 쓰지 않았습니다.
//
// 메인 패스는 한 렌더 패스 안의 서브패스 3개(깊이 → 불투명 → 반투명)입니다(Quest 타일 GPU 를 위한
// 구조). 그래서 불투명 끝 · 반투명 끝 지점은 별도 패스가 아니라 그 서브패스 안에서 그립니다. 파이프라인은
// HRenderDeviceInfo::mainPass 와 그 서브패스 번호로 만들어야 합니다.
//
// 지점마다 눈별로 부릅니다(단안 1번, 스테레오 2번). 같은 지점에 확장이 여럿이면 등록한 순서대로 부릅니다.
// 렌더러는 단일 스레드라(Threading.md) 등록 · 해제 · 기록이 모두 메인 스레드에서 일어납니다.

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

#include "passes/HPass.hpp"

class RenderContext;

enum class HRenderStage : uint32_t {
    OpaqueEnd = 0,    // 메인 패스 불투명 서브패스 끝(불투명 뒤, 반투명 앞). 하늘 · 우주 배경 같은 것
    TransparentEnd,   // 메인 패스 반투명 서브패스(반투명 뒤, 에디터 기즈모 앞)
    AfterScene,       // 메인 패스 뒤, AO · Bloom · 톤매핑 앞. 렌더 패스 밖이라 자기 패스 · 컴퓨트를 기록
    AfterTonemap,     // 톤매핑 뒤, UI 앞. 출력 이미지의 그 눈 사각형 안(LDR, 비네팅 같은 화면 효과)
    Count
};

constexpr uint32_t stageBit(HRenderStage s) { return 1u << (uint32_t)s; }

// create 때 받는 것. 렌더러가 사는 동안 그대로입니다.
struct HRenderDeviceInfo {
    const RenderContext*  rc = nullptr;        // 장치 · 할당기 · 파이프라인 캐시 · MSAA 샘플 수 · 깊이 포맷 · 생성 헬퍼
    VkDescriptorSetLayout frameSetLayout = VK_NULL_HANDLE;   // set 0 (FrameUBO, shaders/include/haruka_shared.h)
    VkRenderPass          mainPass = VK_NULL_HANDLE;         // OpaqueEnd · TransparentEnd 가 열려 있는 렌더 패스
    uint32_t              opaqueSubpass      = 1;
    uint32_t              transparentSubpass = 2;
    VkFormat              hdrFormat = VK_FORMAT_UNDEFINED;   // 메인 패스 색 · AfterScene 의 HDR 이미지 포맷
};

// record 때 받는 것.
struct HRenderStageContext {
    HRenderStage    stage      = HRenderStage::OpaqueEnd;
    VkCommandBuffer cmd        = VK_NULL_HANDLE;
    uint32_t        frameIndex = 0;   // 프레임 슬롯(0 .. MAX_FRAMES-1). 프레임별 버퍼를 고를 때 씁니다
    uint32_t        viewIndex  = 0;   // 눈 번호(단안 0, 스테레오 0=왼쪽 · 1=오른쪽)
    bool            mirror     = false;   // XR 중 왼쪽 눈을 에디터 뷰포트에 다시 비추는 호출(AfterTonemap 만)

    glm::mat4 view{ 1.f };
    glm::mat4 proj{ 1.f };
    glm::vec3 eyePos{ 0.f };
    VkDescriptorSet frameSet = VK_NULL_HANDLE;   // set 0. 셰이더는 FrameUBO 의 viewProj[viewIndex] 를 씁니다

    // 렌더 패스 안 지점(OpaqueEnd · TransparentEnd · AfterTonemap). 뷰포트와 시저는 rect 로 설정되어 있고,
    // 확장이 바꿨다면 렌더러가 뒤에서 다시 설정합니다. AfterTonemap 의 렌더 패스는 출력 포맷마다 다르므로
    // 파이프라인을 renderPass(또는 colorFormat)마다 만들어 두십시오.
    VkRenderPass          renderPass  = VK_NULL_HANDLE;
    uint32_t              subpass     = 0;
    VkFormat              colorFormat = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples     = VK_SAMPLE_COUNT_1_BIT;
    VkRect2D              rect{};

    // AfterScene. HDR 은 1샘플(MSAA 를 푼 결과)이고 SHADER_READ_ONLY_OPTIMAL 로 받습니다. 쓰려면 바꿔 쓰고
    // 같은 레이아웃으로 돌려놓아야 합니다. 깊이는 MSAA 이고 DEPTH_STENCIL_READ_ONLY_OPTIMAL(읽기 전용)입니다.
    // 렌더러가 이 지점 앞뒤에 그래픽 · 컴퓨트 전체 배리어를 둡니다.
    VkImage     hdrImage  = VK_NULL_HANDLE;
    VkImageView hdrView   = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkExtent2D  sceneExtent{};
};

// Layer 2 가 구현합니다. 수명주기는 HPass 와 같은 뜻입니다:
//   create               등록할 때 한 번. 크기와 무관한 자원
//   acquireSizeDependent 등록할 때와 창 · 뷰포트 크기가 바뀐 뒤. 크기 의존 자원
//   rebuildPipelines     등록할 때와 셰이더 핫 리로드 때. 새 파이프라인을 다 만든 뒤에 옛 것을 지우십시오
//   releaseSizeDependent 크기가 바뀌기 전
//   destroy              해제할 때와 렌더러가 끝날 때. 크기 의존 자원까지 모두(여러 번 불려도 안전하게)
class IRenderExtension : public HPass {
public:
    virtual uint32_t stages() const = 0;   // stageBit(...) 의 합
    virtual void create(const HRenderDeviceInfo& info) = 0;
    virtual void record(const HRenderStageContext& ctx) = 0;
};

// EngineContext 로 게임 모듈에 넘기는 좁은 인터페이스입니다(DEF-18). 렌더러 전체를 넘기지 않습니다.
class IRenderExtensionHost {
public:
    virtual ~IRenderExtensionHost() = default;
    // 등록하면 바로 create · acquireSizeDependent · rebuildPipelines 를 부르고 다음 프레임부터 그립니다.
    // 실패하면(예외) 오류를 남기고 등록하지 않습니다. 같은 확장을 두 번 넣으면 무시합니다.
    virtual void addRenderExtension(std::shared_ptr<IRenderExtension> ext) = 0;
    // 장치가 쉬기를 기다린 뒤 destroy 를 부르고 뺍니다. 렌더러가 끝날 때 남은 확장은 렌더러가 정리합니다.
    virtual void removeRenderExtension(IRenderExtension* ext) = 0;
};
