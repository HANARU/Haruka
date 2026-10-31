// 실행 파일이 Vulkan 로더에서 가져오는 함수 이름의 검산입니다(DEF-97). 창도 GPU 도 필요 없습니다.
//
// 이 테스트가 존재하는 이유: 엔진과 게임 코드는 한 실행 파일(Android 는 한 .so)에 링크됩니다. 어디선가 1.2 이상 코어 이름이나
// 확장 이름(vkCmdPushDescriptorSet · vkCmdPipelineBarrier2 등)을 이름으로 부르면, PC 의 1.4 로더에서는 잘 돌지만 Quest 의
// libvulkan(API 34)에서는 링크 · 로드가 막히고 1.1 장치에서는 빈 함수로 뛰어 멈춥니다. PC 에서 미리 잡으려고 실행 파일의
// 가져오기 표를 읽어, vulkan-1.dll 에서 가져오는 이름이 1.0 · 1.1 코어와 VK_KHR_surface · VK_KHR_swapchain 뿐인지 봅니다.
// 확장 함수는 vkGetDeviceProcAddr 로 받아야 합니다(VulkanBaseline.hpp).
//
// 실행: HarukaVkImportTest <실행 파일>... <vulkan_core.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// vulkan_core.h 에서 허용할 함수 이름을 뽑습니다. 블록은 "#define VK_VERSION_1_0 1" · "#define VK_KHR_surface 1" 같은 줄로
// 바뀌고, 그 안의 "VKAPI_CALL vkXxx(" 원형을 모읍니다.
std::set<std::string> allowedFromHeader(const std::string& text) {
    static const std::set<std::string> kBlocks = { "VK_VERSION_1_0", "VK_VERSION_1_1", "VK_KHR_surface", "VK_KHR_swapchain" };
    const std::regex blockRe(R"(^#define (VK_VERSION_\d+_\d+|VK_[A-Z]+_[a-z0-9_]+) 1\s*$)");
    const std::regex protoRe(R"(VKAPI_CALL (vk\w+)\()");
    std::set<std::string> allowed;
    std::string block;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = end + 1;
        std::smatch m;
        if (std::regex_match(line, m, blockRe)) { block = m[1]; continue; }
        if (kBlocks.count(block) && std::regex_search(line, m, protoRe)) allowed.insert(m[1]);
    }
    return allowed;
}

// PE32+ 의 가져오기 디렉터리에서 dllName(대소문자 무시)이 주는 이름을 모읍니다. 서수 가져오기는 "#서수" 로 담습니다.
bool importsFrom(const std::vector<uint8_t>& pe, const std::string& dllName, std::vector<std::string>& names,
                 std::string& err) {
    auto u16 = [&](size_t o) -> uint32_t { return o + 2 <= pe.size() ? (uint32_t)(pe[o] | pe[o + 1] << 8) : 0; };
    auto u32 = [&](size_t o) -> uint32_t {
        return o + 4 <= pe.size() ? (uint32_t)pe[o] | (uint32_t)pe[o + 1] << 8 | (uint32_t)pe[o + 2] << 16 | (uint32_t)pe[o + 3] << 24 : 0;
    };
    auto u64 = [&](size_t o) -> uint64_t { return (uint64_t)u32(o) | (uint64_t)u32(o + 4) << 32; };
    if (pe.size() < 0x40 || pe[0] != 'M' || pe[1] != 'Z') { err = "not an MZ file"; return false; }
    const size_t peOff = u32(0x3C);
    if (u32(peOff) != 0x00004550) { err = "no PE signature"; return false; }
    const size_t coff        = peOff + 4;
    const uint32_t nSections = u16(coff + 2);
    const uint32_t optSize   = u16(coff + 16);
    const size_t opt         = coff + 20;
    if (u16(opt) != 0x20B) { err = "not PE32+"; return false; }
    const uint32_t importRva = u32(opt + 112 + 8);   // 데이터 디렉터리 1번(가져오기)
    const size_t   secTable  = opt + optSize;
    auto rvaToOff = [&](uint32_t rva, size_t& off) {
        for (uint32_t i = 0; i < nSections; ++i) {
            const size_t s = secTable + (size_t)i * 40;
            const uint32_t va = u32(s + 12), vsize = std::max(u32(s + 8), u32(s + 16)), raw = u32(s + 20);
            if (rva >= va && rva < va + vsize) { off = (size_t)raw + (rva - va); return off < pe.size(); }
        }
        return false;
    };
    size_t desc = 0;
    if (!importRva || !rvaToOff(importRva, desc)) { err = "no import directory"; return false; }
    for (;; desc += 20) {
        const uint32_t oft = u32(desc), nameRva = u32(desc + 12), ft = u32(desc + 16);
        if (!oft && !nameRva && !ft) break;
        size_t nameOff = 0;
        if (!rvaToOff(nameRva, nameOff)) { err = "bad DLL name"; return false; }
        std::string dll(reinterpret_cast<const char*>(&pe[nameOff]));
        std::transform(dll.begin(), dll.end(), dll.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (dll != dllName) continue;
        size_t thunk = 0;
        if (!rvaToOff(oft ? oft : ft, thunk)) { err = "bad thunk table"; return false; }
        for (;; thunk += 8) {
            const uint64_t t = u64(thunk);
            if (!t) break;
            if (t & (1ull << 63)) { names.push_back("#" + std::to_string(t & 0xFFFF)); continue; }
            size_t hint = 0;
            if (!rvaToOff((uint32_t)t, hint)) { err = "bad import name"; return false; }
            names.emplace_back(reinterpret_cast<const char*>(&pe[hint + 2]));
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::printf("Haruka Vulkan 가져오기 표 검산\n");
    if (argc < 3) {
        std::printf("usage: HarukaVkImportTest <exe>... <vulkan_core.h>\n");
        return 2;
    }
    std::vector<uint8_t> headerBytes;
    if (!readFile(argv[argc - 1], headerBytes)) {
        std::printf("cannot read %s\n", argv[argc - 1]);
        return 2;
    }
    const std::set<std::string> allowed = allowedFromHeader(std::string(headerBytes.begin(), headerBytes.end()));
    std::printf("\n== 허용 목록(1.0 · 1.1 코어 + VK_KHR_surface · VK_KHR_swapchain) ==\n");
    check(allowed.size() >= 150, "vulkan_core.h 에서 원형 " + std::to_string(allowed.size()) + "개를 읽음(150개 이상)");
    check(allowed.count("vkCreateInstance") && allowed.count("vkGetPhysicalDeviceFeatures2") &&
          allowed.count("vkCreateSwapchainKHR") && allowed.count("vkDestroySurfaceKHR"),
          "1.0 · 1.1 코어와 표면 · 스왑체인 함수가 들어 있음");
    check(!allowed.count("vkCmdPipelineBarrier2") && !allowed.count("vkCmdPushDescriptorSet") &&
          !allowed.count("vkCmdPushDescriptorSetKHR") && !allowed.count("vkCmdPipelineBarrier2KHR"),
          "1.2 이상 코어 · 확장 함수는 들어 있지 않음");

    for (int i = 1; i < argc - 1; ++i) {
        const std::string exe = argv[i];
        const std::string name = exe.substr(exe.find_last_of("/\\") + 1);
        std::printf("\n== %s ==\n", name.c_str());
        std::vector<uint8_t> pe;
        std::vector<std::string> imports;
        std::string err;
        if (!readFile(exe, pe) || !importsFrom(pe, "vulkan-1.dll", imports, err)) {
            check(false, name + ": 가져오기 표를 읽음 (" + (err.empty() ? "cannot read" : err) + ")");
            continue;
        }
        check(imports.size() >= 50, name + ": vulkan-1.dll 에서 " + std::to_string(imports.size()) + "개를 가져옴");
        std::vector<std::string> bad;
        for (const auto& n : imports) if (!allowed.count(n)) bad.push_back(n);
        for (const auto& n : bad) std::printf("    not allowed: %s\n", n.c_str());
        check(bad.empty(), name + ": 1.0 · 1.1 코어와 표면 · 스왑체인 밖의 이름을 가져오지 않음");
        const bool core14 = std::find(imports.begin(), imports.end(), "vkCmdPushDescriptorSet") != imports.end();
        const bool core13 = std::find(imports.begin(), imports.end(), "vkCmdPipelineBarrier2") != imports.end();
        check(!core14 && !core13, name + ": vkCmdPushDescriptorSet(1.4) · vkCmdPipelineBarrier2(1.3) 를 가져오지 않음");
        const bool gdpa = std::find(imports.begin(), imports.end(), "vkGetDeviceProcAddr") != imports.end();
        check(gdpa, name + ": 확장 함수는 vkGetDeviceProcAddr 로 받음");
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
