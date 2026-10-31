#pragma once
#include "PakFormat.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// ── .hpak 읽기 (2026-10-24) ───────────────────────────────────────────────────────
// 마운트할 때 머리 해시 · 목차 해시 · 전체 크기와 모든 항목의 범위를 확인하고, 그 뒤로는 바뀌지 않습니다. 읽기는 위치를
// 지정한 읽기만 쓰므로 여러 스레드가 한 묶음을 함께 읽을 수 있습니다(miniaudio 의 비동기 디코딩 등).
namespace hpak {

// 묶음을 읽는 바탕. 파일 · 메모리, Android 단계에서는 APK 안 에셋의 fd + 시작 위치가 됩니다.
class ReadSource {
public:
    virtual ~ReadSource() = default;
    virtual uint64_t size() const = 0;
    // offset 에서 bytes 만큼 읽습니다. 여러 스레드가 동시에 부를 수 있어야 합니다.
    virtual bool readAt(uint64_t offset, void* dst, size_t bytes) const = 0;
};
std::unique_ptr<ReadSource> openFileSource(const std::filesystem::path& path, std::string& err);
std::unique_ptr<ReadSource> memorySource(std::vector<uint8_t> bytes);

class PakReader {
public:
    // 확인에 실패하면 false 와 이유(손상 · 잘림 · 모르는 major · 모르는 incompat 비트 등).
    bool open(std::unique_ptr<ReadSource> source, std::string& err);

    size_t           count() const { return entries_.size(); }
    // 논리 경로로 찾습니다(정규화 후 ASCII 대소문자 무시). 없으면 -1.
    int              find(std::string_view logical) const;
    std::string_view path(int index) const;
    const Entry&     entry(int index) const { return entries_[(size_t)index]; }

    // 파일 전체를 풀어 읽고 내용 해시를 확인합니다. 모르는 코덱 · 플래그면 그 파일만 실패합니다.
    bool readAll(int index, std::vector<uint8_t>& out, std::string& err) const;
    // 압축 전 기준 [offset, offset + bytes) 를 읽습니다(스트림용, 필요한 블록만 풂). 내용 해시는 확인하지 않습니다.
    bool readRange(int index, uint64_t offset, void* dst, size_t bytes, std::string& err) const;

private:
    bool entryReadable(const Entry& e, std::string& err) const;
    bool readBlock(const Entry& e, uint64_t blockIndex, uint64_t storedOffset, std::vector<uint8_t>& out,
                   std::string& err) const;

    std::unique_ptr<ReadSource>          src_;
    std::vector<Entry>                   entries_;
    std::vector<uint32_t>                blocks_;
    std::string                          strings_;
    std::unordered_map<std::string, int> index_;   // 찾기 키 → 항목
};

} // namespace hpak
