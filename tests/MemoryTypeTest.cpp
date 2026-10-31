// 메모리 타입 선택(DEF-42 · DEF-06)의 검산입니다. 창도 장치도 링크도 필요 없습니다.
//
// 이 테스트가 존재하는 이유: 핵심 힙이 고르는 메모리 타입은 GPU 의 메모리 표에 따라 달라지는데, 개발 PC 하나로는
// Quest · ReBAR 없는 PC · 작은 UMA 기기의 표를 볼 수 없습니다. 그런 표를 손으로 채워 선택 규칙과 오류 문구를 고정합니다.
//
// 실행: build/Release/HarukaMemoryTest.exe

#include "../src/memory/HarukaAllocator.hpp"
#include "../src/memory/MemoryTypeSelect.hpp"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

constexpr VkDeviceSize MiB = 1024ull * 1024ull;
constexpr VkDeviceSize GiB = 1024ull * MiB;

constexpr VkMemoryPropertyFlags DL = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
constexpr VkMemoryPropertyFlags HV = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
constexpr VkMemoryPropertyFlags HC = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
constexpr VkMemoryPropertyFlags CA = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
constexpr VkMemoryPropertyFlags LZ = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
constexpr VkMemoryPropertyFlags PR = VK_MEMORY_PROPERTY_PROTECTED_BIT;

// 힙 크기 목록과 (플래그, 힙 번호) 목록으로 메모리 표를 만듭니다.
VkPhysicalDeviceMemoryProperties table(const std::vector<VkDeviceSize>& heaps,
                                       const std::vector<std::pair<VkMemoryPropertyFlags, uint32_t>>& types) {
    VkPhysicalDeviceMemoryProperties p{};
    p.memoryHeapCount = (uint32_t)heaps.size();
    for (uint32_t i = 0; i < p.memoryHeapCount; ++i) {
        p.memoryHeaps[i].size  = heaps[i];
        p.memoryHeaps[i].flags = 0;
    }
    p.memoryTypeCount = (uint32_t)types.size();
    for (uint32_t i = 0; i < p.memoryTypeCount; ++i) {
        p.memoryTypes[i].propertyFlags = types[i].first;
        p.memoryTypes[i].heapIndex     = types[i].second;
        if (types[i].first & DL) p.memoryHeaps[types[i].second].flags |= VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
    }
    return p;
}

// Quest Pro(gpuinfo.org 보고서): 힙 0 하나(DL, 약 12.4GiB), 타입 6개.
VkPhysicalDeviceMemoryProperties questPro() {
    return table({ 12 * GiB + 400 * MiB },
                 { { DL, 0 }, { DL, 0 }, { DL | HV | CA, 0 }, { DL | HV | HC, 0 }, { DL | HV | HC | CA, 0 }, { DL | LZ, 0 } });
}

// ReBAR 를 켠 데스크톱: VRAM 8GB(힙 0), 시스템 32GB(힙 1), 마지막 타입이 VRAM 전체의 DL|HV|HC.
VkPhysicalDeviceMemoryProperties rebarDesktop() {
    return table({ 8 * GiB, 32 * GiB }, { { DL, 0 }, { HV | HC, 1 }, { HV | HC | CA, 1 }, { DL | HV | HC, 0 } });
}

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

} // namespace

int main() {
    std::printf("Haruka 메모리 타입 선택 검산\n");

    std::printf("\n== A. 핵심 힙 ==\n");
    {
        const hmem::CoreHeapPlan a1 = hmem::planCoreHeap(questPro(), kHeapTarget);
        check(a1.fail == hmem::CoreHeapFail::None && a1.memType == 3 && a1.heapIndex == 0 && a1.deviceLocal && !a1.tight,
              "A1 Quest Pro: 타입 3(DL|HV|HC), 힙 0, 장치 로컬, 여유 있음");

        const hmem::CoreHeapPlan a2 = hmem::planCoreHeap(rebarDesktop(), kHeapTarget);
        check(a2.fail == hmem::CoreHeapFail::None && a2.memType == 3 && a2.deviceLocal,
              "A2 ReBAR 데스크톱: 타입 3(VRAM 의 DL|HV|HC)");

        // ReBAR 없는 데스크톱: 256MB BAR 의 DL|HV|HC 가 시스템 메모리 타입보다 앞에 있습니다.
        const auto noRebar = table({ 8 * GiB, 32 * GiB, 256 * MiB }, { { DL, 0 }, { DL | HV | HC, 2 }, { HV | HC, 1 } });
        const hmem::CoreHeapPlan a3 = hmem::planCoreHeap(noRebar, kHeapTarget);
        check(a3.fail == hmem::CoreHeapFail::None && a3.memType == 2 && a3.heapIndex == 1 && !a3.deviceLocal,
              "A3 ReBAR 없는 데스크톱: 256MB BAR 타입이 아니라 시스템 메모리 타입 2(대체 경로도 힙 크기를 봄)");

        const auto smallUma = table({ 768 * MiB }, { { DL | HV | HC, 0 } });
        const hmem::CoreHeapPlan a4 = hmem::planCoreHeap(smallUma, kHeapTarget);
        check(a4.fail == hmem::CoreHeapFail::HeapTooSmall && a4.heapBytes == 768 * MiB && a4.memType == 0,
              "A4 작은 UMA(768MB): HeapTooSmall, 힙 크기 768MB");

        const auto exact = table({ 1 * GiB }, { { DL | HV | HC, 0 } });
        const hmem::CoreHeapPlan a5 = hmem::planCoreHeap(exact, kHeapTarget);
        check(a5.fail == hmem::CoreHeapFail::None && a5.memType == 0 && a5.tight,
              "A5 힙이 정확히 1GiB: 고르되 tight(경고만, 멈추지 않음)");

        const auto noHv = table({ 8 * GiB }, { { DL, 0 }, { DL | LZ, 0 } });
        check(hmem::planCoreHeap(noHv, kHeapTarget).fail == hmem::CoreHeapFail::NoHostVisibleType,
              "A6 HOST_VISIBLE 타입이 없으면 NoHostVisibleType");
        // HOST_VISIBLE 이지만 COHERENT 가 아닌 타입만 있어도 같습니다.
        const auto hvOnly = table({ 8 * GiB }, { { DL, 0 }, { HV | CA, 0 } });
        check(hmem::planCoreHeap(hvOnly, kHeapTarget).fail == hmem::CoreHeapFail::NoHostVisibleType,
              "A6 HOST_COHERENT 가 없는 HV 타입만 있어도 NoHostVisibleType");

        const VkPhysicalDeviceMemoryProperties empty{};
        check(hmem::planCoreHeap(empty, kHeapTarget).fail == hmem::CoreHeapFail::NoHostVisibleType,
              "A7 메모리 타입이 0개면 NoHostVisibleType");

        // 작은 힙 여럿이면 가장 큰 HV 힙을 이유로 적습니다.
        const auto twoSmall = table({ 256 * MiB, 512 * MiB }, { { HV | HC, 0 }, { HV | HC, 1 } });
        const hmem::CoreHeapPlan a8 = hmem::planCoreHeap(twoSmall, kHeapTarget);
        check(a8.fail == hmem::CoreHeapFail::HeapTooSmall && a8.heapIndex == 1 && a8.heapBytes == 512 * MiB,
              "A8 HV 힙이 여럿이고 모두 작으면 가장 큰 힙(512MB)을 이유로 적음");

        check(hmem::selectMemType(questPro(), DL | HV | HC, kHeapTarget, ~0u & ~(1u << 3)) == 4,
              "A9 typeBits 에서 뺀 타입은 건너뜀(3 을 빼면 4)");
    }

    std::printf("\n== B. 지연 할당 타입 ==\n");
    {
        check(hmem::selectLazyMemType(questPro(), 0x3F) == 5, "B1 Quest Pro, typeBits 0x3F → 5");
        check(hmem::selectLazyMemType(questPro(), 0x1F) == UINT32_MAX, "B2 typeBits 에 5 가 없으면 없음(Zone B 로 감)");
        check(hmem::selectLazyMemType(rebarDesktop(), ~0u) == UINT32_MAX, "B3 LAZILY 가 없는 표 → 없음");
        const auto prot = table({ 1 * GiB }, { { DL | LZ | PR, 0 }, { DL | LZ, 0 } });
        check(hmem::selectLazyMemType(prot, 0x3) == 1, "B4 PROTECTED 지연 할당 타입은 건너뜀 → 1");
        check(hmem::selectLazyMemType(questPro(), 0) == UINT32_MAX, "B5 typeBits 0 → 없음");
    }

    std::printf("\n== C. 오류 문구 ==\n");
    {
        const hmem::CoreHeapPlan a4 = hmem::planCoreHeap(table({ 768 * MiB }, { { DL | HV | HC, 0 } }), kHeapTarget);
        const std::string c1 = hmem::describeCoreHeapFailure(a4, VK_SUCCESS, 0, kHeapTarget);
        std::printf("    %s\n", c1.c_str());
        check(contains(c1, "1024 MB") && contains(c1, "768 MB"), "C1 작은 힙: 필요한 1024 MB 와 실제 768 MB 가 들어 있음");
        check(!contains(c1, "VkResult") && !contains(c1, "max single"), "C1 할당 전 실패라 VkResult · 최대 할당 칸은 없음");

        const hmem::CoreHeapPlan q = hmem::planCoreHeap(questPro(), kHeapTarget);
        const std::string c2 = hmem::describeCoreHeapFailure(q, VK_ERROR_OUT_OF_DEVICE_MEMORY, 1 * GiB, kHeapTarget);
        std::printf("    %s\n", c2.c_str());
        check(contains(c2, "VK_ERROR_OUT_OF_DEVICE_MEMORY") && contains(c2, "(-2)"), "C2 할당 실패: VkResult 이름과 숫자");
        check(contains(c2, "memory type 3") && contains(c2, "heap 0") && contains(c2, "12688 MB"),
              "C2 타입 번호 · 힙 번호 · 힙 크기가 들어 있음");
        check(contains(c2, "max single allocation 1024 MB"), "C2 최대 단일 할당 크기가 들어 있음");

        const std::string c3 = hmem::describeCoreHeapFailure(q, (VkResult)-1000069000, 0, kHeapTarget);
        std::printf("    %s\n", c3.c_str());
        check(contains(c3, "-1000069000") && !contains(c3, "VK_ERROR_"), "C3 이름표에 없는 VkResult 는 숫자만");

        const std::string c4 = hmem::describeCoreHeapFailure(
            hmem::planCoreHeap(table({ 8 * GiB }, { { DL, 0 } }), kHeapTarget), VK_SUCCESS, 0, kHeapTarget);
        check(contains(c4, "no host-visible"), "C4 HV 타입이 없으면 그 이유를 적음");
    }

    std::printf("\n== D. Zone 상수 ==\n");
    {
        static_assert(kZoneCOffset + kZoneCSize == kHeapTarget, "Zone C ends at the heap end");
        static_assert(kZoneBOffset % (64 * 1024) == 0 && kZoneCOffset % (64 * 1024) == 0, "Zone offsets are 64KiB aligned");
        check(kZoneCOffset + kZoneCSize == kHeapTarget, "D1 Zone C 가 힙 끝에서 끝남");
        check(kZoneBOffset % (64 * 1024) == 0 && kZoneCOffset % (64 * 1024) == 0, "D2 Zone B · C 시작이 64KiB 배수");
        check(kHeapTarget == 1 * GiB, "D3 핵심 힙 목표는 1GiB(Quest 의 maxMemoryAllocationSize 와 같음)");
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
