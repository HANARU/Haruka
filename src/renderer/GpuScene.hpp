#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "Vertex.hpp"
#include "MeshHandle.hpp"
#include "MaterialHandle.hpp"
#include "RenderContext.hpp"
#include "RangeAllocator.hpp"

#include <cstdint>

struct TexSourceData;

// Mega-buffer sizes (Zone B)
constexpr VkDeviceSize kMegaVertBytes = 128ULL * 1024 * 1024;   // 128 MB
constexpr VkDeviceSize kMegaIdxBytes  =  64ULL * 1024 * 1024;   //  64 MB
// 인덱스 버퍼의 앞 절반은 32비트 인덱스(정점 65,536 개 초과 메시), 뒤 절반은 16비트 인덱스입니다(O3). 16비트 구역의 바인딩
// 오프셋입니다(인덱스 크기의 배수라 Vulkan 정렬 규칙을 만족).
constexpr VkDeviceSize kIdx16Base     = kMegaIdxBytes / 2;

// ── GpuScene ──────────────────────────────────────────────────────────────────
// 자산이 사는 곳 (RendererRefactor.md §3-③): 메가버퍼(정점/인덱스),
// 메시/머티리얼 업로드, 텍스처 슬롯. AssetManager가 만지는 모든 GPU 자원.
class GpuScene {
public:
    void create(RenderContext& rc);   // 메가버퍼 할당 (Zone B)
    void destroy();

    // 인덱스는 올리기 전에 정점 캐시 순서로 바꾸고(O3, geometry/MeshOptimize.hpp), 정점이 65,536 개 이하면 16비트로 넣습니다.
    // precomputed 가 주어지면 정점 순회를 건너뜁니다. 절차 도형은 바운드를
    // 해석적으로 알고 있고, 순회로 만든 값은 곡면에서 과소 추정이기 때문입니다.
    // 디버그 빌드에서는 순회도 수행해 해석값과 대조하고 불일치를 경고합니다.
    // preOrdered 면 인덱스가 이미 정점 캐시 순서라 정렬을 건너뜁니다(가져올 때 정렬한 모델, 비동기 로딩).
    MeshHandle* uploadMesh(const std::vector<Vertex>& verts,
                           const std::vector<uint32_t>& indices,
                           const MeshBounds* precomputed = nullptr, bool preOrdered = false);
    // LOD 용 인덱스만 올립니다(O13): base 와 같은 정점 구간(firstVertex) · 16비트 여부 · 바운드를 씁니다.
    MeshHandle* uploadLodIndices(const MeshHandle& base, const std::vector<uint32_t>& indices, bool preOrdered = false);

    struct MeshBudget {
        uint32_t verts, indices;
        float    vertPct, idxPct;
    };
    MeshBudget budget() const;
    // 메시 구간을 메가 버퍼에 돌려주고 핸들을 지웁니다(바로). GPU 가 아직 쓸 수 있으면 retireMesh 를 쓰십시오.
    void        freeMesh(MeshHandle* h);

    // ── 지연 해제 (비동기 로딩 D4) ──
    // frame = 지금 렌더러의 프레임 번호. collectRetired(현재 번호) 가 maxFrames 뒤에 실제로 지웁니다(그 사이 제출이 끝남).
    void retireMesh(MeshHandle* h, uint64_t frame);
    void retireMaterial(MaterialHandle* h, uint64_t frame);
    void collectRetired(uint64_t frame, uint32_t maxFrames);
    size_t retiredCount() const { return retired_.size(); }
    // 펜스가 끝난 업로드의 스테이징 · 명령 버퍼를 놓습니다. wait 면 모두 끝날 때까지 기다림(종료).
    void collectUploads(bool wait);
    size_t pendingUploads() const { return uploads_.size(); }

    MaterialHandle* uploadMaterial(
        const TexSourceData& albedo, const TexSourceData& normalMap,
        const TexSourceData& metalRough, const TexSourceData& ao,
        const TexSourceData& emissive);
    void freeMaterial(MaterialHandle* h);   // 바로 지움(장치가 쓰지 않을 때만, 예: 종료 · 시험)

    // 1x1 보다 크면 GPU 블릿으로 밉 사슬을 만듭니다(DEF-165, MipChain.hpp). 형식이 블릿을 받지 않으면 1단계로 올립니다.
    TexSlot uploadTexture(const uint8_t* pixels, int w, int h,
                          VkFormat fmt = VK_FORMAT_R8G8B8A8_SRGB);
    void    freeTexSlot(TexSlot slot);

    VkBuffer vertexBuffer() const { return megaVertBuf_; }
    VkBuffer indexBuffer()  const { return megaIdxBuf_;  }

    // 스킨 메시의 프레임별 정점 구간(2026-11-07, Hand_Grasp_Design 4-1). 처음 부를 때 메가 버퍼에서 슬롯 MAX_FRAMES 개
    // 분량을 한 번 잡고 계속 다시 씁니다(메가 버퍼는 반납이 없는 범프 할당). 렌더러가 그 슬롯의 펜스를 기다린 뒤 CPU 로
    // 스키닝한 정점을 직접 쓰고(Zone B 는 매핑 · HOST_COHERENT), 그리기는 firstVertex 를 정점 오프셋으로 씁니다.
    // 메가 버퍼가 모자라면 null 을 돌려주고 한 번 경고합니다.
    static constexpr uint32_t kSkinArenaVerts = 32768;   // 슬롯마다(손 두 개는 수천 정점)
    // 정점은 GPU 형식(PackedVertex, QTangent)입니다. 스키닝은 Vertex 로 한 뒤 hqtan::packRange 로 옮겨 씁니다.
    PackedVertex* skinArena(uint32_t frameSlot, uint32_t& firstVertex);

private:
    RenderContext* rc_ = nullptr;

    bool     skinArenaReserved_ = false;
    bool     skinArenaFailed_   = false;
    uint32_t skinArenaFirst_    = 0;

    // 형식마다 밉 블릿 가능 여부를 한 번만 묻습니다(DEF-165). 텍스처는 SRGB · UNORM 두 형식뿐입니다.
    bool canBlitMips(VkFormat fmt);

    // 텍스처 하나를 올리는 명령을 cmd 에 기록합니다. 스테이징 버퍼는 staging 에 담아, 제출이 끝난 뒤 부르는 쪽이 놓습니다
    // (2026-11-18, 성능 안정화 1-6). 머티리얼의 텍스처 여러 장을 한 번에 제출하려고 uploadTexture 에서 나눴습니다.
    struct Staging { VkBuffer buf = VK_NULL_HANDLE; HarukaAllocation alloc; };
    TexSlot recordTextureUpload(VkCommandBuffer cmd, const uint8_t* pixels, int w, int h, VkFormat fmt,
                                std::vector<Staging>& staging);
    void    freeStaging(std::vector<Staging>& staging);
    // 기록한 업로드를 기다리지 않고 제출하고, 펜스가 끝나면 staging 을 놓도록 남깁니다(D3).
    void    submitUpload(VkCommandBuffer cmd, std::vector<Staging>&& staging);
    struct PendingUpload { VkFence fence; VkCommandBuffer cmd; std::vector<Staging> staging; };
    std::vector<PendingUpload> uploads_;
    struct Retired { uint64_t frame; MeshHandle* mesh; MaterialHandle* material; };
    std::vector<Retired> retired_;

    // 머티리얼에 텍스처가 없을 때 쓰는 1x1 기본값(albedo 흰색 · 평평한 법선 · metalRough 기본 · AO 흰색 · 이미시브 검정).
    // 예전에는 머티리얼마다 다섯 장을 따로 올려(장마다 제출 · 대기), 색만 다른 도형 2,000 개를 여는 데 4 초 넘게 걸렸습니다.
    // 처음 머티리얼을 올릴 때 한 번 만들고 destroy 에서 지웁니다.
    enum FallbackTex { kFbAlbedo, kFbNormal, kFbMetalRough, kFbAo, kFbEmissive, kFbCount };
    TexSlot fallback_[kFbCount];
    bool    fallbackReady_ = false;
    struct BlitSupport { VkFormat fmt; bool ok; };
    std::vector<BlitSupport> blitSupport_;

    VkBuffer         megaVertBuf_   = VK_NULL_HANDLE;
    HarukaAllocation megaVertAlloc_;
    VkBuffer         megaIdxBuf_    = VK_NULL_HANDLE;
    HarukaAllocation megaIdxAlloc_;
    // 메가 버퍼 구역마다 구간 할당자(비동기 로딩 D5, 2026-11-20). 예전에는 범프 할당이라 지운 메시의 공간이 돌아오지 않았습니다.
    RangeAllocator   vertRanges_;    // 정점(PackedVertex 단위)
    RangeAllocator   idx32Ranges_;   // 앞 절반: 32비트 인덱스
    RangeAllocator   idx16Ranges_;   // 뒤 절반: 16비트 인덱스(O3)
};
