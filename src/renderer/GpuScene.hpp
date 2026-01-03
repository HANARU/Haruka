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

// ── GpuScene ──────────────────────────────────────────────────────────────────
// 자산이 사는 곳 (RendererRefactor.md §3-③): 메가버퍼(정점/인덱스),
// 메시/머티리얼 업로드, 텍스처 슬롯. AssetManager가 만지는 모든 GPU 자원.
class GpuScene {
public:
    void create(RenderContext& rc);   // 메가버퍼 할당 (Zone B)
    void destroy();

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

    TexSlot uploadTexture(const uint8_t* pixels, int w, int h,
                          VkFormat fmt = VK_FORMAT_R8G8B8A8_SRGB);
    void    freeTexSlot(TexSlot slot);

    VkBuffer vertexBuffer() const { return megaVertBuf_; }
    VkBuffer indexBuffer()  const { return megaIdxBuf_;  }

private:
    RenderContext* rc_ = nullptr;

    VkBuffer         megaVertBuf_   = VK_NULL_HANDLE;
    HarukaAllocation megaVertAlloc_;
    uint32_t         megaVertCount_ = 0;   // vertices used so far

    VkBuffer         megaIdxBuf_    = VK_NULL_HANDLE;
    HarukaAllocation megaIdxAlloc_;
    uint32_t         megaIdxCount_  = 0;   // indices used so far
};
