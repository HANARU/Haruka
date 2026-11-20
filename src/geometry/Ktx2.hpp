#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ── KTX2 컨테이너 읽기 · 쓰기 (2026-11-21, Docs/Texture_Compression.md D1) ────────────────────────────
// 미리 압축한 텍스처(BC · ASTC)를 담는 Khronos KTX 2.0 파일입니다. 외부 라이브러리 없이 이 파일이 머리(80바이트) · 단계 색인 ·
// 데이터 형식 설명(DFD) 기본 블록 하나를 다룹니다. supercompression 은 쓰지 않습니다(0 만 받음). 면 1개 · 배열 아님 · 2D 만.
//
// 단계 0 이 가장 큰 밉입니다(색인 순서). 파일 안의 데이터는 규격대로 작은 밉부터 놓고, 단계마다 16바이트로 정렬합니다(블록 크기와 4
// 의 최소공배수). 읽기는 각 단계가 파일 안에 있고, 길이가 형식 · 크기로 계산한 값과 같은지 확인합니다. DFD 는 범위만 확인하고
// 내용은 믿지 않습니다(형식은 vkFormat 하나로 정함).
//
// Vulkan 함수를 부르지 않습니다(형식 번호만 씀). 엔진 · 묶음 도구 · 시험이 함께 씁니다.
namespace hktx2 {

// 블록 압축 형식의 블록 크기 · 바이트. 모르는 형식이면 bytes == 0.
struct BlockInfo {
    uint32_t w = 0, h = 0, bytes = 0;
    bool     srgb = false;
};
BlockInfo blockInfo(uint32_t vkFormat);
// 한 단계(가로 w · 세로 h)의 바이트 수. 모르는 형식이면 0.
uint64_t levelBytes(uint32_t vkFormat, uint32_t w, uint32_t h);
// 형식 이름(로그용, 예: "BC7_SRGB"). 모르면 "format <번호>".
std::string formatName(uint32_t vkFormat);

struct Level {
    uint64_t offset = 0;   // 파일 처음부터
    uint64_t length = 0;
};

struct Image {
    uint32_t           vkFormat = 0;
    uint32_t           width = 0, height = 0;
    std::vector<Level> levels;   // [0] = 가장 큰 밉
    std::vector<uint8_t> bytes;  // 파일 전체(레벨 오프셋이 가리킴)
    const uint8_t* levelData(size_t i) const { return bytes.data() + levels[i].offset; }
};

// 파일 바이트를 확인하고 out 에 담습니다(bytes 는 옮겨 받음). 실패하면 false 와 이유.
bool parse(std::vector<uint8_t>&& bytes, Image& out, std::string& err);

// 단계 데이터(가장 큰 밉부터)로 KTX2 파일을 만듭니다. 각 단계 길이는 levelBytes 와 같아야 합니다(아니면 빈 결과).
std::vector<uint8_t> write(uint32_t vkFormat, uint32_t width, uint32_t height,
                           const std::vector<std::vector<uint8_t>>& levels);

} // namespace hktx2
