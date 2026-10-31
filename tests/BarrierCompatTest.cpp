// 배리어 기록 분기(DEF-04)의 검산입니다. 창도 장치도 링크도 필요 없습니다.
//
// 이 테스트가 존재하는 이유: sync2 가 없는 장치에서 1.0 배리어로 바꾼 값이 틀리면 화면이 깨지거나 검증 오류가 나는데,
// 개발 PC 는 sync2 를 지원해 그 경로를 타지 않습니다. 변환표와 엔진이 실제로 쓰는 값 전수를 숫자로 고정합니다.
//
// 실행: build/Release/HarukaBarrierTest.exe

#include "../src/renderer/BarrierCompat.hpp"

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

template <class H>
H fakeHandle(uint64_t v) {
    static_assert(sizeof(H) <= sizeof(v), "handle size");
    H h{};
    std::memcpy(&h, &v, sizeof(H));
    return h;
}

// ── 규격 "Supported access types" 표의 1.0 부분 ─────────────────────────────────────
// 접근 비트 하나마다 그 접근을 지원하는 단계들. MEMORY_READ · MEMORY_WRITE 는 모든 단계가 지원합니다.
constexpr VkPipelineStageFlags kShaderStages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
    VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
    VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

VkPipelineStageFlags supportingStages(VkAccessFlags bit) {
    switch (bit) {
        case VK_ACCESS_INDIRECT_COMMAND_READ_BIT:          return VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
        case VK_ACCESS_INDEX_READ_BIT:                     return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        case VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT:          return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        case VK_ACCESS_UNIFORM_READ_BIT:                   return kShaderStages;
        case VK_ACCESS_INPUT_ATTACHMENT_READ_BIT:          return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        case VK_ACCESS_SHADER_READ_BIT:                    return kShaderStages;
        case VK_ACCESS_SHADER_WRITE_BIT:                   return kShaderStages;
        case VK_ACCESS_COLOR_ATTACHMENT_READ_BIT:          return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:         return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT:
        case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:
            return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        case VK_ACCESS_TRANSFER_READ_BIT:
        case VK_ACCESS_TRANSFER_WRITE_BIT:                 return VK_PIPELINE_STAGE_TRANSFER_BIT;
        case VK_ACCESS_HOST_READ_BIT:
        case VK_ACCESS_HOST_WRITE_BIT:                     return VK_PIPELINE_STAGE_HOST_BIT;
        default:                                           return 0;
    }
}

// ALL_GRAPHICS · ALL_COMMANDS 를 개별 단계로 펼칩니다.
VkPipelineStageFlags expand(VkPipelineStageFlags s) {
    if (s & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) s |= 0xFFFFu & ~VK_PIPELINE_STAGE_HOST_BIT;
    if (s & VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
        s |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | kShaderStages |
             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    return s;
}

// 1.0 규칙 두 가지: 단계 마스크가 0 이 아니고(VUID-...-03937), 모든 접근 비트를 단계 하나 이상이 지원(02815 · 02816).
bool valid10(VkPipelineStageFlags stage, VkAccessFlags access) {
    if (stage == 0) return false;
    const VkPipelineStageFlags e = expand(stage);
    for (uint32_t b = 0; b < 32; ++b) {
        const VkAccessFlags bit = 1u << b;
        if (!(access & bit)) continue;
        if (bit == VK_ACCESS_MEMORY_READ_BIT || bit == VK_ACCESS_MEMORY_WRITE_BIT) continue;
        if (!(e & supportingStages(bit))) return false;
    }
    return true;
}

// ── 가짜 함수 ────────────────────────────────────────────────────────────────
struct Call1 {
    int                               count = 0;
    VkCommandBuffer                   cmd   = VK_NULL_HANDLE;
    VkPipelineStageFlags              src = 0, dst = 0;
    VkDependencyFlags                 flags = 0;
    uint32_t                          mem = 0, buf = 0, img = 0;
    std::vector<VkMemoryBarrier>       mems;
    std::vector<VkBufferMemoryBarrier> bufs;
    std::vector<VkImageMemoryBarrier>  imgs;
};
struct Call2 {
    int                     count = 0;
    VkCommandBuffer         cmd   = VK_NULL_HANDLE;
    const VkDependencyInfo* di    = nullptr;
};
Call1 g_c1;
Call2 g_c2;

VKAPI_ATTR void VKAPI_CALL fakeBarrier1(VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst,
                                        VkDependencyFlags flags, uint32_t memCount, const VkMemoryBarrier* mem,
                                        uint32_t bufCount, const VkBufferMemoryBarrier* buf, uint32_t imgCount,
                                        const VkImageMemoryBarrier* img) {
    ++g_c1.count;
    g_c1.cmd = cmd; g_c1.src = src; g_c1.dst = dst; g_c1.flags = flags;
    g_c1.mem = memCount; g_c1.buf = bufCount; g_c1.img = imgCount;
    g_c1.mems.assign(mem, mem + memCount);
    g_c1.bufs.assign(buf, buf + bufCount);
    g_c1.imgs.assign(img, img + imgCount);
}

VKAPI_ATTR void VKAPI_CALL fakeBarrier2(VkCommandBuffer cmd, const VkDependencyInfo* di) {
    ++g_c2.count;
    g_c2.cmd = cmd;
    g_c2.di  = di;
}

void resetCalls() { g_c1 = Call1{}; g_c2 = Call2{}; }

// 구조체 사이 빈 바이트는 복사 때 보장되지 않으므로 칸마다 비교합니다.
bool sameImageBarrier(const VkImageMemoryBarrier& a, const VkImageMemoryBarrier& b) {
    const VkImageSubresourceRange& r = a.subresourceRange;
    const VkImageSubresourceRange& s = b.subresourceRange;
    return a.sType == b.sType && a.pNext == b.pNext && a.srcAccessMask == b.srcAccessMask &&
           a.dstAccessMask == b.dstAccessMask && a.oldLayout == b.oldLayout && a.newLayout == b.newLayout &&
           a.srcQueueFamilyIndex == b.srcQueueFamilyIndex && a.dstQueueFamilyIndex == b.dstQueueFamilyIndex &&
           a.image == b.image && r.aspectMask == s.aspectMask && r.baseMipLevel == s.baseMipLevel &&
           r.levelCount == s.levelCount && r.baseArrayLayer == s.baseArrayLayer && r.layerCount == s.layerCount;
}

// ── 엔진이 쓰는 배리어 모양 ──────────────────────────────────────────────────────
VkImageMemoryBarrier2 imageBarrier(VkPipelineStageFlags2 srcS, VkAccessFlags2 srcA, VkPipelineStageFlags2 dstS,
                                   VkAccessFlags2 dstA, VkImageLayout oldL, VkImageLayout newL,
                                   VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT, uint32_t mips = 1,
                                   uint32_t layers = 1) {
    VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    b.srcStageMask        = srcS;
    b.srcAccessMask       = srcA;
    b.dstStageMask        = dstS;
    b.dstAccessMask       = dstA;
    b.oldLayout           = oldL;
    b.newLayout           = newL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = fakeHandle<VkImage>(0x1000);
    b.subresourceRange    = { aspect, 0, mips, 0, layers };
    return b;
}

VkMemoryBarrier2 memoryBarrier(VkPipelineStageFlags2 srcS, VkAccessFlags2 srcA, VkPipelineStageFlags2 dstS,
                               VkAccessFlags2 dstA) {
    VkMemoryBarrier2 b{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    b.srcStageMask  = srcS;
    b.srcAccessMask = srcA;
    b.dstStageMask  = dstS;
    b.dstAccessMask = dstA;
    return b;
}

VkDependencyInfo depImages(const VkImageMemoryBarrier2* b, uint32_t n) {
    VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    di.imageMemoryBarrierCount = n;
    di.pImageMemoryBarriers    = b;
    return di;
}

VkDependencyInfo depMemory(const VkMemoryBarrier2* b, uint32_t n) {
    VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    di.memoryBarrierCount = n;
    di.pMemoryBarriers    = b;
    return di;
}

} // namespace

int main() {
    std::printf("Haruka 배리어 기록 분기 검산\n");

    std::printf("\n== 1. 단계 표 ==\n");
    {
        bool same = true;
        for (uint32_t b = 0; b <= 16; ++b) {
            const VkPipelineStageFlags2 bit = 1ull << b;
            bool e1 = false, e2 = false;
            same = same && hsync::toStage1(bit, hsync::Scope::Src, &e1) == (VkPipelineStageFlags)bit && e1;
            same = same && hsync::toStage1(bit, hsync::Scope::Dst, &e2) == (VkPipelineStageFlags)bit && e2;
        }
        check(same, "0~16번 비트 17개는 src · dst 모두 같은 값, 정확함");

        bool transfer = true;
        for (VkPipelineStageFlags2 s : { VK_PIPELINE_STAGE_2_COPY_BIT, VK_PIPELINE_STAGE_2_RESOLVE_BIT,
                                         VK_PIPELINE_STAGE_2_BLIT_BIT, VK_PIPELINE_STAGE_2_CLEAR_BIT }) {
            bool e = false;
            transfer = transfer && hsync::toStage1(s, hsync::Scope::Dst, &e) == VK_PIPELINE_STAGE_TRANSFER_BIT && e;
        }
        check(transfer, "COPY · RESOLVE · BLIT · CLEAR → TRANSFER");
        check(hsync::toStage1(VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, hsync::Scope::Dst) == VK_PIPELINE_STAGE_VERTEX_INPUT_BIT &&
              hsync::toStage1(VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, hsync::Scope::Dst) ==
                  VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
              "INDEX_INPUT · VERTEX_ATTRIBUTE_INPUT → VERTEX_INPUT");
        check(hsync::toStage1(VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT, hsync::Scope::Dst) ==
                  VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
              "PRE_RASTERIZATION_SHADERS → VERTEX_SHADER(tessellation · geometry 비트가 섞이지 않음)");
        check(hsync::toStage1(VK_PIPELINE_STAGE_2_NONE, hsync::Scope::Src) == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT &&
              hsync::toStage1(VK_PIPELINE_STAGE_2_NONE, hsync::Scope::Dst) == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
              "NONE 은 src 에서 TOP, dst 에서 BOTTOM");
        check(hsync::toStage1(VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, hsync::Scope::Src) ==
                  (VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
              "섞인 값 COPY | FRAGMENT_SHADER → TRANSFER | FRAGMENT_SHADER");
        bool e1 = true, e2 = true;
        const VkPipelineStageFlags v1 = hsync::toStage1(VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR, hsync::Scope::Src, &e1);
        const VkPipelineStageFlags v2 = hsync::toStage1(1ull << 39, hsync::Scope::Dst, &e2);
        check(v1 == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT && !e1 && v2 == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT && !e2,
              "표에 없는 비트(VIDEO_DECODE · 1<<39)는 ALL_COMMANDS, 정확하지 않음");
        bool nonzero = true;
        for (VkPipelineStageFlags2 s : { VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_COPY_BIT, 1ull << 39, 1ull << 63 })
            nonzero = nonzero && hsync::toStage1(s, hsync::Scope::Src) != 0 && hsync::toStage1(s, hsync::Scope::Dst) != 0;
        check(nonzero, "어떤 입력에도 결과가 0 이 아님");
    }

    std::printf("\n== 2. 접근 표 ==\n");
    {
        bool same = true;
        for (uint32_t b = 0; b <= 16; ++b) {
            const VkAccessFlags2 bit = 1ull << b;
            bool e = false;
            same = same && hsync::toAccess1(bit, &e) == (VkAccessFlags)bit && e;
        }
        check(same, "0~16번 비트 17개는 같은 값, 정확함");
        check(hsync::toAccess1(VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) == VK_ACCESS_SHADER_READ_BIT &&
              hsync::toAccess1(VK_ACCESS_2_SHADER_STORAGE_READ_BIT) == VK_ACCESS_SHADER_READ_BIT,
              "SHADER_SAMPLED_READ · SHADER_STORAGE_READ → SHADER_READ");
        check(hsync::toAccess1(VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) == VK_ACCESS_SHADER_WRITE_BIT,
              "SHADER_STORAGE_WRITE → SHADER_WRITE");
        check(hsync::toAccess1(VK_ACCESS_2_NONE) == 0, "NONE → 0");
        bool e = true;
        check(hsync::toAccess1(VK_ACCESS_2_SHADER_BINDING_TABLE_READ_BIT_KHR, &e) ==
                  (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT) && !e,
              "표에 없는 비트(SHADER_BINDING_TABLE_READ)는 MEMORY_READ | MEMORY_WRITE, 정확하지 않음");
    }

    std::printf("\n== 3. 엔진이 쓰는 값 전수 ==\n");
    {
        // 이름은 패키징 2단계 명세(DEF-04 현재 상태)의 번호입니다. T3 · T4 는 T1 · T2 와, T6 · T7 은 T1 과(층 6 · 밉 여럿),
        // T8 · T9 는 T2 와, M3 은 M1 과 같은 조합이라 따로 두지 않습니다. T5(깊이 전환)는 하는 일이 없어 DEF-04 에서
        // 지우지만(사용자 결정 2026-10-31), 깊이 첨부 단계 · 접근의 변환 규칙을 보려고 남깁니다.
        struct Row {
            const char*           name;
            VkPipelineStageFlags2 s2src; VkAccessFlags2 a2src;
            VkPipelineStageFlags2 s2dst; VkAccessFlags2 a2dst;
            VkPipelineStageFlags  s1src; VkAccessFlags  a1src;
            VkPipelineStageFlags  s1dst; VkAccessFlags  a1dst;
        };
        const VkAccessFlags2 m1DstA2 = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                       VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                       VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        const VkAccessFlags  m1DstA1 = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        const Row rows[] = {
            { "T1 텍스처 업로드 전(UNDEFINED → TRANSFER_DST)",
              VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT },
            { "T2 텍스처 업로드 후(TRANSFER_DST → SHADER_READ_ONLY)",
              VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT },
            { "T5 깊이 첨부 전환(규칙 확인용)",
              VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
              VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT },
            { "T10 UI 없을 때 표시 전환(COLOR_ATTACHMENT → PRESENT_SRC)",
              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
              VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
              VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0 },
            { "M1 프레임 시작(DEF-104) · M3 눈 사이",
              VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, m1DstA2,
              VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, m1DstA1 },
            { "M2 렌더 확장 앞뒤(fullBarrier)",
              VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
              VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
              VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
              VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT },
            { "M4 XR 거울",
              VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
              VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
              VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
              VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
              VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT },
        };
        for (const Row& r : rows) {
            bool e1 = false, e2 = false, e3 = false, e4 = false;
            const VkPipelineStageFlags s = hsync::toStage1(r.s2src, hsync::Scope::Src, &e1);
            const VkPipelineStageFlags d = hsync::toStage1(r.s2dst, hsync::Scope::Dst, &e2);
            const VkAccessFlags        a = hsync::toAccess1(r.a2src, &e3);
            const VkAccessFlags        b = hsync::toAccess1(r.a2dst, &e4);
            check(s == r.s1src && a == r.a1src && d == r.s1dst && b == r.a1dst && e1 && e2 && e3 && e4,
                  std::string(r.name) + ": 기대값과 같고 정확함");
            check(valid10(s, a) && valid10(d, b), std::string(r.name) + ": 1.0 규칙(단계 0 금지 · 접근 지원) 만족");
        }

        // 교차 검사: 이미 1.0 으로 돌고 있는 HSlate 흰 텍스처 업로드(HSlateRenderer.cpp 의 두 vkCmdPipelineBarrier)와
        // 비트까지 같아야 합니다.
        check(rows[0].s1src == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT && rows[0].s1dst == VK_PIPELINE_STAGE_TRANSFER_BIT &&
              rows[0].a1src == 0 && rows[0].a1dst == VK_ACCESS_TRANSFER_WRITE_BIT,
              "T1 결과가 HSlate 업로드 전 배리어와 같음");
        check(rows[1].s1src == VK_PIPELINE_STAGE_TRANSFER_BIT && rows[1].s1dst == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT &&
              rows[1].a1src == VK_ACCESS_TRANSFER_WRITE_BIT && rows[1].a1dst == VK_ACCESS_SHADER_READ_BIT,
              "T2 결과가 HSlate 업로드 후 배리어와 같음");
    }

    std::printf("\n== 4. toLegacy ==\n");
    {
        VkImageMemoryBarrier2 b = imageBarrier(VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COPY_BIT,
                                               VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 7, 6);
        b.srcQueueFamilyIndex = 2;
        b.dstQueueFamilyIndex = 3;
        b.subresourceRange.baseMipLevel   = 1;
        b.subresourceRange.baseArrayLayer = 2;
        VkDependencyInfo di = depImages(&b, 1);
        di.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        hsync::Legacy l;
        const hsync::ConvertError e = hsync::toLegacy(di, l);
        const VkImageMemoryBarrier& o = l.img[0];
        check(e == hsync::ConvertError::None && l.imgCount == 1 && l.memCount == 0 && l.bufCount == 0, "이미지 배리어 하나");
        check(o.sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER && o.pNext == nullptr, "1.0 sType, pNext 는 nullptr");
        check(o.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && o.newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
              o.srcQueueFamilyIndex == 2 && o.dstQueueFamilyIndex == 3 && o.image == b.image,
              "레이아웃 · 큐 계열 · 이미지 핸들이 그대로");
        check(o.subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && o.subresourceRange.baseMipLevel == 1 &&
              o.subresourceRange.levelCount == 7 && o.subresourceRange.baseArrayLayer == 2 &&
              o.subresourceRange.layerCount == 6,
              "subresourceRange 가 그대로");
        check(l.flags == VK_DEPENDENCY_BY_REGION_BIT, "dependencyFlags(BY_REGION)가 그대로");
        check(l.exact, "정확한 변환");

        // 배리어 둘: NONE→COPY 와 COPY→FRAGMENT. src 합에 TOP 이 섞이지 않습니다.
        const VkImageMemoryBarrier2 two[2] = {
            imageBarrier(VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COPY_BIT,
                         VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
            imageBarrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
        };
        hsync::Legacy l2;
        hsync::toLegacy(depImages(two, 2), l2);
        check(l2.srcStage == VK_PIPELINE_STAGE_TRANSFER_BIT, "두 배리어의 src 합은 TRANSFER 만(TOP 이 섞이지 않음)");
        check(l2.dstStage == (VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
              "dst 합은 TRANSFER | FRAGMENT_SHADER");
        check(l2.img[0].srcAccessMask == 0 && l2.img[1].dstAccessMask == VK_ACCESS_SHADER_READ_BIT,
              "접근은 배리어마다 따로 바뀜");

        // 종류가 섞인 경우
        const VkMemoryBarrier2 mb = memoryBarrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                                                  VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT);
        VkBufferMemoryBarrier2 bb{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
        bb.srcStageMask        = VK_PIPELINE_STAGE_2_COPY_BIT;
        bb.srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        bb.dstStageMask        = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
        bb.dstAccessMask       = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        bb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bb.buffer              = fakeHandle<VkBuffer>(0x2000);
        bb.offset              = 256;
        bb.size                = 4096;
        VkDependencyInfo mix{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        mix.memoryBarrierCount       = 1; mix.pMemoryBarriers       = &mb;
        mix.bufferMemoryBarrierCount = 1; mix.pBufferMemoryBarriers = &bb;
        mix.imageMemoryBarrierCount  = 1; mix.pImageMemoryBarriers  = &two[1];
        hsync::Legacy l3;
        check(hsync::toLegacy(mix, l3) == hsync::ConvertError::None && l3.memCount == 1 && l3.bufCount == 1 &&
              l3.imgCount == 1, "메모리 · 버퍼 · 이미지가 섞여도 각 개수가 맞음");
        const VkBufferMemoryBarrier& ob = l3.buf[0];
        check(ob.sType == VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER && ob.buffer == bb.buffer && ob.offset == 256 &&
              ob.size == 4096 && ob.srcAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT &&
              ob.dstAccessMask == VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
              "버퍼 배리어 내용이 그대로(오프셋 · 크기 · 접근)");
        check(l3.mem[0].sType == VK_STRUCTURE_TYPE_MEMORY_BARRIER && l3.mem[0].srcAccessMask == VK_ACCESS_MEMORY_WRITE_BIT &&
              l3.mem[0].dstAccessMask == VK_ACCESS_MEMORY_READ_BIT, "메모리 배리어 내용이 그대로");
        check(l3.srcStage == (VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT) &&
              l3.dstStage == (VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
              "단계는 세 종류를 모두 합침");

        hsync::Legacy l4;
        const VkDependencyInfo empty{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        check(hsync::toLegacy(empty, l4) == hsync::ConvertError::None &&
              l4.srcStage == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT && l4.dstStage == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
              "배리어가 0개면 TOP / BOTTOM");

        const VkMemoryBarrier2 odd = memoryBarrier(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_NONE,
                                                   VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                                                   VK_ACCESS_2_SHADER_BINDING_TABLE_READ_BIT_KHR);
        hsync::Legacy l5;
        hsync::toLegacy(depMemory(&odd, 1), l5);
        check(!l5.exact, "표에 없는 비트가 있으면 exact = false");
    }

    std::printf("\n== 5. 거부 ==\n");
    {
        resetCalls();
        hvk::DeviceFns legacy;
        legacy.barrier1 = &fakeBarrier1;
        const VkCommandBuffer cmd = fakeHandle<VkCommandBuffer>(0x77);

        for (VkImageLayout bad : { VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL }) {
            const VkImageMemoryBarrier2 b1 = imageBarrier(VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                                                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED, bad);
            const VkImageMemoryBarrier2 b2 = imageBarrier(VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                                                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, bad,
                                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            hsync::Legacy l;
            check(hsync::toLegacy(depImages(&b1, 1), l) == hsync::ConvertError::LayoutNot1_0 &&
                  hsync::toLegacy(depImages(&b2, 1), l) == hsync::ConvertError::LayoutNot1_0,
                  std::string("sync2 전용 레이아웃(") + (bad == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL ? "ATTACHMENT" : "READ_ONLY") +
                      "_OPTIMAL)은 old · new 모두 거부");
            check(hsync::record(legacy, cmd, depImages(&b1, 1)) == hsync::ConvertError::LayoutNot1_0,
                  "record 도 같은 오류를 돌려줌");
        }

        std::vector<VkMemoryBarrier2> many(hsync::kMaxPerCall + 1,
                                           memoryBarrier(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_NONE,
                                                         VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_NONE));
        hsync::Legacy l;
        check(hsync::toLegacy(depMemory(many.data(), hsync::kMaxPerCall + 1), l) == hsync::ConvertError::TooMany,
              "배리어가 kMaxPerCall + 1 개면 TooMany");
        check(hsync::toLegacy(depMemory(many.data(), hsync::kMaxPerCall), l) == hsync::ConvertError::None,
              "kMaxPerCall 개까지는 받음");
        check(hsync::record(legacy, cmd, depMemory(many.data(), hsync::kMaxPerCall + 1)) == hsync::ConvertError::TooMany,
              "record 도 TooMany");

        // 버퍼 · 이미지 배리어도 종류마다 같은 한도입니다.
        std::vector<VkImageMemoryBarrier2> imgs(hsync::kMaxPerCall + 1,
                                                imageBarrier(VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                                                             VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                             VK_IMAGE_LAYOUT_UNDEFINED,
                                                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));
        check(hsync::toLegacy(depImages(imgs.data(), hsync::kMaxPerCall + 1), l) == hsync::ConvertError::TooMany &&
              hsync::toLegacy(depImages(imgs.data(), hsync::kMaxPerCall), l) == hsync::ConvertError::None,
              "이미지 배리어도 kMaxPerCall + 1 개면 TooMany, kMaxPerCall 개는 받음");
        std::vector<VkBufferMemoryBarrier2> bufs(hsync::kMaxPerCall + 1, VkBufferMemoryBarrier2{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 });
        VkDependencyInfo bufDi{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        bufDi.bufferMemoryBarrierCount = hsync::kMaxPerCall + 1;
        bufDi.pBufferMemoryBarriers    = bufs.data();
        check(hsync::toLegacy(bufDi, l) == hsync::ConvertError::TooMany, "버퍼 배리어도 kMaxPerCall + 1 개면 TooMany");
        check(g_c1.count == 0 && g_c2.count == 0, "거부한 경우 barrier1 · barrier2 를 한 번도 부르지 않음");

        hvk::DeviceFns none;
        const VkMemoryBarrier2 m = memoryBarrier(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_NONE,
                                                 VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_NONE);
        check(hsync::record(none, cmd, depMemory(&m, 1)) == hsync::ConvertError::NoFunction && g_c1.count == 0,
              "함수가 하나도 없으면 NoFunction(채우지 않은 DeviceFns)");
    }

    std::printf("\n== 6. 발행 분기 ==\n");
    {
        const VkCommandBuffer cmd = fakeHandle<VkCommandBuffer>(0x1234);
        const VkImageMemoryBarrier2 b = imageBarrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                     VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        const VkDependencyInfo di = depImages(&b, 1);

        resetCalls();
        hvk::DeviceFns sync2;
        sync2.barrier2 = &fakeBarrier2;
        sync2.barrier1 = &fakeBarrier1;
        sync2.sync2    = true;
        check(hsync::record(sync2, cmd, di) == hsync::ConvertError::None, "sync2: 기록 성공");
        check(g_c2.count == 1 && g_c2.di == &di && g_c2.cmd == cmd && g_c1.count == 0,
              "sync2: barrier2 를 1번, 같은 di 포인터로 부르고 barrier1 은 0번");

        resetCalls();
        hvk::DeviceFns legacy;
        legacy.barrier1 = &fakeBarrier1;
        legacy.barrier2 = &fakeBarrier2;   // 포인터가 있어도 sync2 가 꺼져 있으면 쓰지 않음
        legacy.sync2    = false;
        bool exact = false;
        check(hsync::record(legacy, cmd, di, &exact) == hsync::ConvertError::None && exact, "1.0: 기록 성공, 정확함");
        hsync::Legacy want;
        hsync::toLegacy(di, want);
        check(g_c1.count == 1 && g_c2.count == 0 && g_c1.cmd == cmd, "1.0: barrier1 을 1번, barrier2 는 0번");
        check(g_c1.src == want.srcStage && g_c1.dst == want.dstStage && g_c1.flags == want.flags &&
              g_c1.mem == 0 && g_c1.buf == 0 && g_c1.img == 1,
              "1.0: 단계 · flags · 세 개수가 toLegacy 결과와 같음");
        check(g_c1.imgs.size() == 1 && sameImageBarrier(g_c1.imgs[0], want.img[0]),
              "1.0: 이미지 배리어 내용이 toLegacy 결과와 같음(모든 칸)");

        // 표에 없는 비트를 넓혀 바꾸면 호출하는 쪽이 경고할 수 있게 exact = false 를 알립니다.
        resetCalls();
        const VkMemoryBarrier2 odd = memoryBarrier(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_NONE,
                                                   VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                                                   VK_ACCESS_2_SHADER_BINDING_TABLE_READ_BIT_KHR);
        bool exactOdd = true;
        check(hsync::record(legacy, cmd, depMemory(&odd, 1), &exactOdd) == hsync::ConvertError::None && !exactOdd &&
              g_c1.count == 1,
              "1.0: 넓혀 바꾼 배리어는 기록하되 exact = false 로 알림");
        bool exactSync2 = false;
        check(hsync::record(sync2, cmd, depMemory(&odd, 1), &exactSync2) == hsync::ConvertError::None && exactSync2,
              "sync2: 바꾸지 않으므로 exact = true");
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
