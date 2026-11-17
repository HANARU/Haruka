#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "Vertex.hpp"
#include "MeshHandle.hpp"
#include "MaterialHandle.hpp"
#include "RenderContext.hpp"

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
    MeshHandle* uploadMesh(const std::vector<Vertex>& verts,
                           const std::vector<uint32_t>& indices,
                           const MeshBounds* precomputed = nullptr);

    struct MeshBudget {
        uint32_t verts, indices;
        float    vertPct, idxPct;
    };
    MeshBudget budget() const;
    void        freeMesh(MeshHandle* h);

    MaterialHandle* uploadMaterial(
        const TexSourceData& albedo, const TexSourceData& normalMap,
        const TexSourceData& metalRough, const TexSourceData& ao,
        const TexSourceData& emissive);
    void freeMaterial(MaterialHandle* h);

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
    struct BlitSupport { VkFormat fmt; bool ok; };
    std::vector<BlitSupport> blitSupport_;

    VkBuffer         megaVertBuf_   = VK_NULL_HANDLE;
    HarukaAllocation megaVertAlloc_;
    uint32_t         megaVertCount_ = 0;   // vertices used so far

    VkBuffer         megaIdxBuf_    = VK_NULL_HANDLE;
    HarukaAllocation megaIdxAlloc_;
    uint32_t         megaIdxCount_  = 0;   // 32비트 구역에 쓴 인덱스 수
    uint32_t         megaIdx16Count_ = 0;  // 16비트 구역에 쓴 인덱스 수(O3)
};
