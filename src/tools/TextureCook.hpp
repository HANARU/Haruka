#pragma once
#include "../assets/ImportSettings.hpp"
#include "../assets/TextureCompression.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// ── 묶을 때 텍스처 쿠킹 (2026-11-21, Docs/Texture_Compression.md D5 · D6) ─────────────────────────────────
// 모델을 런타임과 같은 glTF 로더로 읽어 최종 RGBA8 화소를 얻고, 압축할 크기의 텍스처마다 KTX2 를 만듭니다. 결과는
// <캐시>/textures/<대상>/<열쇠>.ktx2 에 두고, 파일이 있고 읽히면 다시 압축하지 않습니다. 모델은 가상 파일 시스템으로 읽으므로
// 부르는 쪽이 먼저 /Game/ · /Config/ 를 마운트합니다.
namespace htexcook {

struct Stats {
    int      models = 0, failedModels = 0;
    int      textures = 0;        // 압축한(또는 캐시에서 쓴) 서로 다른 텍스처
    int      encoded = 0, cached = 0;
    int      small = 0;           // 한 변이 4보다 작아 RGBA8 그대로 둔 텍스처(팩터만 있는 1x1 등)
    uint64_t rgbaBytes = 0;       // 같은 텍스처를 RGBA8 + 밉으로 올렸을 때
    uint64_t ktxBytes  = 0;       // 단계 데이터 합(머리 제외)
    double   seconds = 0.0;
};

struct Output {
    std::string          logicalPath;   // "textures/<대상>/<열쇠>.ktx2"
    std::vector<uint8_t> bytes;
};

// 텍스처 하나. 캐시에 맞는 파일이 있으면 그것을 읽고(fromCache), 아니면 압축해 캐시에 씁니다(임시 파일 → 이름 바꾸기).
bool cookTexture(const uint8_t* rgba, int w, int h, htex::Use u, htex::Target t, const std::filesystem::path& cacheDir,
                 std::vector<uint8_t>& ktx, bool& fromCache, std::string& err);

// 모델 하나. 같은 열쇠는 한 번만 out 에 넣습니다(seen 이 이미 넣은 논리 경로).
bool cookModel(const std::string& modelPath, const ImportSettings& settings, htex::Target t,
               const std::filesystem::path& cacheDir, std::vector<Output>& out, std::vector<std::string>& seen,
               Stats& st, std::string& err);

} // namespace htexcook
