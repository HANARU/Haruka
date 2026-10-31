#pragma once
#include "PakFormat.hpp"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

// ── .hpak 쓰기 (2026-10-24) ───────────────────────────────────────────────────────
// 묶음 도구(HarukaPak)와 시험이 씁니다. 같은 입력이면 입력 순서와 상관없이 같은 바이트를 씁니다(결정적 출력): 항목을
// 대소문자를 접은 경로 순으로 정렬해 그 순서로 데이터를 쓰고, 시각 · 절대 경로를 기록하지 않으며, 패딩은 0, LZ4 HC
// 수준은 고정입니다.
namespace hpak {

struct PakInput {
    std::string          path;                 // 논리 경로(쓰기 전에 정규화)
    std::vector<uint8_t> data;                 // 압축 전 내용
    Codec                codec = Codec::Lz4;   // 원하는 코덱. 압축 이득이 작으면(원본의 90% 이상) 그대로 저장합니다
};

struct WriteStats {
    uint64_t files           = 0;
    uint64_t compressedFiles = 0;
    uint64_t rawBytes        = 0;   // 압축 전 합계
    uint64_t storedBytes     = 0;   // 묶음 안 데이터 합계
    uint64_t packBytes       = 0;   // 묶음 파일 크기
};

// out 은 위치를 옮길 수 있는 스트림이어야 합니다(머리를 마지막에 다시 씀). 실패하면 err 에 이유를 적고 false.
// 대소문자만 다른 두 경로, 같은 경로 두 번, 정규화할 수 없는 경로는 오류입니다.
bool writePak(std::vector<PakInput> inputs, std::ostream& out, std::string& err, WriteStats* stats = nullptr);

} // namespace hpak
