#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "HPass.hpp"
#include "../RenderContext.hpp"
#include "../RenderTypes.hpp"

// ── ShadowPass ────────────────────────────────────────────────────────────────
// depth-only 섀도맵. 해상도는 create 때 정해지며 기본은 kDefaultResolution². 화면 크기와 무관한 영구 자원.
// PSO는 공유 PBR 파이프라인 레이아웃(set 0~3)을 사용한다.
class ShadowPass : public HPass {
public:
    // PC 4096²(그림자 거리 20 m 에서 텍셀 약 0.98 cm, D32 64 MiB). VR 에서 가까이 본 그림자 경계의 계단(조사 M12,
    // 2048² 에서 약 2 cm)이 거슬려 2026-11-02 사용자 결정으로 올렸습니다. Quest 는 대역폭 · 메모리 실측 전까지 2048².
#ifdef __ANDROID__
    static constexpr uint32_t kDefaultResolution = 2048;
#else
    static constexpr uint32_t kDefaultResolution = 4096;
#endif

    // 리시버 상수 깊이 바이어스(텍셀 수). updateUBO 가 월드 길이(× 텍셀)로 바꾸고 맞춤의 깊이 범위로 나눠 섀도 NDC 로
    // FrameUBO.shadowParams.z 에 넘깁니다. 2048² 의 예전 값은 월드 19.96 mm(약 1텍셀)였고, 해상도를 올리면 함께 줄어듭니다.
    // 그보다 전의 0.003 NDC(149.7 mm, 텍셀의 7.7 배)는 그 위에 텍셀 8 개짜리 상수를 더 얹고 있었습니다.
    //
    // acne 방지는 세 겹입니다(2026-11-02, 조사 shadow_bias 권장안 A2): 래스터 경사 바이어스(상한 있음), 이 상수,
    // 리시버 노멀 오프셋(pbr.vert). 섀도 패스는 메인 패스와 같은 뒷면 컬링입니다. 예전의 앞면 컬링(뒷면 깊이)은
    // 물체 아랫면을 섀도맵에 남겨, 바닥과 맞닿는 곳에서 가림 면과 바닥의 간격이 바이어스보다 좁은 자리가 밝게
    // 새었습니다. 구는 바닥과 한 점에서 접해 그 간격이 천천히 벌어지므로 접촉점에서 8 ~ 30 cm 띠가, 정육면체 밑동은
    // 약 4 cm 가 새었고, 뒷면 컬링으로 바꾸면 둘 다 0 이 됩니다(창 없는 실측 n2, 바이어스 · PCF 변형보다 큰 차이).
    // 대신 빛을 받는 면이 자기 깊이와 비교되므로, 경사 바이어스를 끄면 바닥 전체에 acne 가 생기고(n2 V6), 노멀
    // 오프셋 없이는 구의 명암 경계 앞에 면 모양 얼룩이 생깁니다(n2 V5 · V8).
    static constexpr float kReceiverBiasTexels = 1.0f;

    // 래스터 경사 바이어스 배율과 그 상한(텍셀 수). 상한은 맞춤의 깊이 범위(hshadow::depthRangeFor)로 나눠 NDC 로 씁니다.
    // 경사 항은 tanθ 에 비례해 빛과 거의 나란한 면에서 폭주하므로(80° 에서 약 19 cm) 상한이 필요합니다.
    // 장치에 depthBiasClamp 기능이 없으면 상한 없이 씁니다(경고 로그). 2.0 은 Filament 2.0 · URP 2.5 와 비슷한 값입니다.
    static constexpr float kSlopeBiasFactor      = 2.0f;
    static constexpr float kSlopeBiasClampTexels = 4.0f;
    // 리시버 노멀 오프셋(텍셀 수). pbr.vert 가 섀도 조회 위치를 빛 쪽 노멀 방향으로 이 길이 × sinθ 만큼 옮깁니다
    // (Filament 식 각도 항, 빛을 정면으로 받는 면은 0). updateUBO 가 월드 길이로 바꿔 FrameUBO.shadowParams.w 로 넘깁니다.
    static constexpr float kNormalOffsetTexels   = 1.5f;

    // resolution 은 2 의 거듭제곱, 256 ~ 4096, 장치 한도 이하여야 하며 아니면 경고 후 기본값을 씁니다.
    void create(RenderContext& rc, VkPipelineLayout pbrLayout, uint32_t resolution = kDefaultResolution);
    void destroy() override;
    void rebuildPipelines() override;

    // 메인 패스와 공유하는 디스크립터/버퍼 바인딩 입력
    struct DrawInputs {
        VkDescriptorSet frameSet, sceneSet, transformSet;
        VkBuffer        vertexBuffer, indexBuffer;
    };
    // CPU 드로우 리스트로 섀도맵 기록. casterActive 는 이 프레임에 섀도 캐스터 라이트가 있는지입니다.
    //  - 캐스터 라이트가 있으면 draws 가 비어 있어도 맵을 비워(깊이 1) 기록합니다. 예전에는 건너뛰어
    //    라이트 프러스텀에 캐스터가 없을 때 앞 프레임의 그림자가 남거나, 한 번도 안 그린 맵을 읽었습니다.
    //  - 캐스터 라이트가 없으면 셰이더는 맵을 읽지 않지만(miscParams.w < 0) 디스크립터는 정적으로
    //    쓰이므로, 레이아웃이 SHADER_READ_ONLY 가 되도록 처음 한 번만 비운 맵을 기록합니다.
    void record(VkCommandBuffer cmd, const DrawInputs& in,
                const std::vector<GpuDrawCmd>& draws, bool casterActive);

    bool        valid() const       { return image_ != VK_NULL_HANDLE; }
    VkImageView shadowView() const  { return view_; }
    VkSampler   sampler() const     { return sampler_; }
    VkFormat    format() const      { return format_; }
    uint32_t    resolution() const  { return resolution_; }

private:
    RenderContext*   rc_        = nullptr;
    VkPipelineLayout pbrLayout_ = VK_NULL_HANDLE;   // FrameResources 소유 (참조만)

    VkImage          image_       = VK_NULL_HANDLE;
    VkImageView      view_        = VK_NULL_HANDLE;
    HarukaAllocation alloc_;
    VkSampler        sampler_     = VK_NULL_HANDLE;
    VkRenderPass     pass_        = VK_NULL_HANDLE;
    VkFramebuffer    framebuffer_ = VK_NULL_HANDLE;
    VkPipeline       pipeline_    = VK_NULL_HANDLE;
    VkFormat         format_        = VK_FORMAT_UNDEFINED;   // create 에서 조회해 고름
    uint32_t         resolution_    = kDefaultResolution;
    bool             linearCompare_ = false;
    bool             layoutReady_ = false;   // 렌더 패스를 한 번 이상 기록해 finalLayout 이 된 뒤인지
};
