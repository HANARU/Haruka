// 빌드된 모든 SPIR-V(.spv) 파일의 머리말 판이 1.0(0x00010000)인지 검산합니다(E1a, E0b 후속).
// 실행: HarukaSpirvTest <shader_bin_dir>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

} // namespace

int main(int argc, char** argv) {
    std::printf("Haruka SPIR-V 판 검산 (E1a)\n");
    if (argc < 2) {
        std::printf("usage: HarukaSpirvTest <shader_bin_dir>\n");
        return 2;
    }

    const std::filesystem::path dir = argv[1];
    std::error_code ec;
    check(std::filesystem::is_directory(dir, ec), "셰이더 출력 폴더가 있음: " + dir.generic_string());

    std::vector<std::filesystem::path> spvFiles;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".spv") {
            spvFiles.push_back(entry.path());
        }
    }
    std::sort(spvFiles.begin(), spvFiles.end());
    check(spvFiles.size() >= 20, ".spv 파일이 20개 이상 있음 (" + std::to_string(spvFiles.size()) + "개)");

    constexpr uint32_t kSpirvMagic      = 0x07230203u;
    constexpr uint32_t kSpirvVersion1_0 = 0x00010000u;

    for (const auto& p : spvFiles) {
        const std::string name = p.filename().string();
        std::ifstream f(p, std::ios::binary);
        uint32_t hdr[2] = { 0, 0 };
        const bool readOk = f && f.read(reinterpret_cast<char*>(hdr), sizeof(hdr)).good();
        if (!readOk) {
            check(false, name + ": 머리말 8바이트를 읽음");
            continue;
        }
        if (hdr[0] != kSpirvMagic || hdr[1] != kSpirvVersion1_0) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s: magic=0x%08X version=0x%08X (기대: 0x%08X, 0x%08X)",
                          name.c_str(), hdr[0], hdr[1], kSpirvMagic, kSpirvVersion1_0);
            check(false, buf);
        } else {
            check(true, name + ": SPIR-V 1.0 (0x00010000)");
        }
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
