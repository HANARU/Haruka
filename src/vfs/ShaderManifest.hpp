#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ── 셰이더 목록: 엔진 빌드와 묶음의 짝 확인 (2026-11-01, DEF-149) ─────────────────────────────────
// 엔진은 SPIR-V 를 리플렉션 없이 읽고, 파이프라인 레이아웃(바인딩 · 푸시 상수)은 C++ 쪽에 고정되어 있습니다. 그래서 묶음
// (game.hpak)의 셰이더가 실행 파일과 다른 빌드의 것이면 잡는 곳이 없었습니다(기기에는 검증 레이어도 없음). 빌드가 컴파일한
// 셰이더의 논리 경로와 내용 해시(XXH3-64, 묶음 목차의 contentHash 와 같은 값)를 HarukaPak shader-manifest 가 목록 파일로
// 적고(엔진에 컴파일되어 들어감), 엔진은 마운트한 뒤 실제로 읽힐 셰이더와 이 목록을 비교합니다. 묶음 형식(v1)은 그대로입니다.
namespace hpak {

struct ShaderManifestEntry {
    const char* path;          // 논리 경로("shaders/pbr.frag.spv")
    uint64_t    contentHash;   // hash64(파일 내용)
};

struct ShaderMismatch {
    std::string path;
    bool        missing = false;   // 찾지 못함. 아니면 내용이 다름
};

// lookup(path, outHash) 는 그 논리 경로의 내용 해시를 구해 true, 찾지 못하면 false 를 돌려줍니다(엔진은 hvfs::contentHash).
// 목록 순서대로 어긋난 항목을 모읍니다.
template <class Lookup>
std::vector<ShaderMismatch> compareShaderManifest(const ShaderManifestEntry* entries, size_t count, Lookup&& lookup) {
    std::vector<ShaderMismatch> out;
    for (size_t i = 0; i < count; ++i) {
        uint64_t h = 0;
        if (!lookup(entries[i].path, h))      out.push_back({ entries[i].path, true });
        else if (h != entries[i].contentHash) out.push_back({ entries[i].path, false });
    }
    return out;
}

} // namespace hpak
