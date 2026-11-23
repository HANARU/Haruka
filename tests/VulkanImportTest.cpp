// 실행 파일이 Vulkan 로더에 정적 링크되지 않고, 엔진 코드가 1.0 · 1.1 코어와 VK_KHR_surface · VK_KHR_swapchain 밖의
// Vulkan 함수를 이름으로 부르지 않는지 검산합니다(DEF-97 · E0a). 창도 GPU 도 필요 없습니다.
//
// 이 테스트가 존재하는 이유:
//   (가) volk 전환 이후 HarukaEngine.exe · HarukaGame.exe 의 PE 가져오기 표에 vulkan-1.dll 이 없어야 합니다.
//        (드라이버가 없어도 프로세스가 뜨고, 엔진이 volkInitialize() 실패를 잡아 안내한 뒤 종료할 수 있습니다.)
//   (나) 엔진 정적 라이브러리(HarukaRuntime.lib)의 COFF 오브젝트(.obj)가 참조하는 미해결 외부 기호 중 vk[A-Z]... 이름을
//        모아 vulkan_core.h 의 허용 목록(1.0 · 1.1 코어 + VK_KHR_surface · VK_KHR_swapchain)과 대조합니다.
//        volk.h 는 모든 함수를 전역 포인터 변수로 선언하므로, 엔진 코드가 vkCmdPipelineBarrier2 등 1.2+ 함수를
//        이름으로 부르면 해당 .obj 의 미해결 외부 기호에 나타나 이 시험이 잡습니다.
//        확장 함수는 vkGetDeviceProcAddr 로 받아야 합니다(VulkanBaseline.hpp).
//
// 실행: HarukaVkImportTest <exe>... <lib>... <vulkan_core.h>

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

// PE32+ 의 가져오기 디렉터리에서 dllName(대소문자 무시)이 주는 이름을 모읍니다.
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
    std::printf("Haruka Vulkan 로더 · 기호 검산 (E0a / DEF-97)\n");
    if (argc < 4) {
        std::printf("usage: HarukaVkImportTest <exe>... <lib>... <vulkan_core.h>\n");
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
        std::printf("\n== (가) %s PE 가져오기 표 ==\n", name.c_str());
        std::vector<uint8_t> pe;
        std::vector<std::string> imports;
        std::string err;
        if (!readFile(exe, pe) || !importsFrom(pe, "vulkan-1.dll", imports, err)) {
            check(false, name + ": PE 가져오기 표를 읽음 (" + (err.empty() ? "cannot read" : err) + ")");
            continue;
        }
        for (const auto& n : imports) std::printf("    unexpected import: %s\n", n.c_str());
        check(imports.empty(), name + ": vulkan-1.dll 을 가져오지 않음 (volk 동적 로드)");
    }

    for (const std::string& lib : libs) {
        const std::string name = lib.substr(lib.find_last_of("/\\") + 1);
        std::printf("\n== (나) %s COFF 미해결 Vulkan 기호 ==\n", name.c_str());
        std::vector<uint8_t> ar;
        std::set<std::string> syms;
        size_t objCount = 0;
        std::string err;
        if (!readFile(lib, ar) || !undefinedVkSymbolsFromLib(ar, syms, objCount, err)) {
            check(false, name + ": COFF 아카이브를 읽음 (" + (err.empty() ? "cannot read" : err) + ")");
            continue;
        }
        const bool isRuntime = name.find("HarukaRuntime") != std::string::npos;
        check(objCount >= (isRuntime ? 10u : 1u), name + ": COFF 오브젝트 " + std::to_string(objCount) + "개를 검사함");
        if (isRuntime) {
            check(syms.size() >= 30, name + ": 미해결 vk 기호 " + std::to_string(syms.size()) + "개를 찾음(30개 이상)");
        }
        std::vector<std::string> bad;
        for (const auto& s : syms) if (!allowed.count(s)) bad.push_back(s);
        for (const auto& s : bad) std::printf("    not allowed: %s\n", s.c_str());
        check(bad.empty(), name + ": 1.0 · 1.1 코어와 표면 · 스왑체인 밖의 Vulkan 함수를 부르지 않음");
        check(!syms.count("vkCmdPushDescriptorSet") && !syms.count("vkCmdPipelineBarrier2"),
              name + ": vkCmdPushDescriptorSet(1.4) · vkCmdPipelineBarrier2(1.3) 를 부르지 않음");
        if (isRuntime) {
            check(syms.count("vkGetDeviceProcAddr") > 0, name + ": 확장 함수는 vkGetDeviceProcAddr 로 받음");
        }
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
