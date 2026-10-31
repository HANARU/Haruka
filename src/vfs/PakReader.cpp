#include "PakReader.hpp"
#include "PathUtf8.hpp"

#include "lz4.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace hpak {

// ── 읽는 바탕 ──────────────────────────────────────────────────────────────────────
namespace {

#ifdef _WIN32
// 동기 핸들에 OVERLAPPED 로 위치를 주면 ReadFile 이 공유 파일 위치를 쓰지 않아 여러 스레드가 함께 읽을 수 있습니다.
class FileSource final : public ReadSource {
public:
    FileSource(HANDLE h, uint64_t size) : h_(h), size_(size) {}
    ~FileSource() override { CloseHandle(h_); }
    uint64_t size() const override { return size_; }
    bool readAt(uint64_t offset, void* dst, size_t bytes) const override {
        auto* p = static_cast<uint8_t*>(dst);
        while (bytes > 0) {
            const DWORD chunk = (DWORD)std::min<size_t>(bytes, 1u << 30);
            OVERLAPPED ov{};
            ov.Offset     = (DWORD)(offset & 0xFFFFFFFFull);
            ov.OffsetHigh = (DWORD)(offset >> 32);
            DWORD got = 0;
            if (!ReadFile(h_, p, chunk, &got, &ov) || got != chunk) return false;
            p += got; offset += got; bytes -= got;
        }
        return true;
    }
private:
    HANDLE   h_;
    uint64_t size_;
};
#else
class FileSource final : public ReadSource {
public:
    FileSource(int fd, uint64_t size) : fd_(fd), size_(size) {}
    ~FileSource() override { ::close(fd_); }
    uint64_t size() const override { return size_; }
    bool readAt(uint64_t offset, void* dst, size_t bytes) const override {
        auto* p = static_cast<uint8_t*>(dst);
        while (bytes > 0) {
            const ssize_t got = ::pread(fd_, p, bytes, (off_t)offset);
            if (got <= 0) return false;
            p += got; offset += (uint64_t)got; bytes -= (size_t)got;
        }
        return true;
    }
private:
    int      fd_;
    uint64_t size_;
};
#endif

class MemorySource final : public ReadSource {
public:
    explicit MemorySource(std::vector<uint8_t> b) : b_(std::move(b)) {}
    uint64_t size() const override { return b_.size(); }
    bool readAt(uint64_t offset, void* dst, size_t bytes) const override {
        if (offset > b_.size() || bytes > b_.size() - offset) return false;
        if (bytes) std::memcpy(dst, b_.data() + offset, bytes);
        return true;
    }
private:
    std::vector<uint8_t> b_;
};

} // namespace

std::unique_ptr<ReadSource> openFileSource(const std::filesystem::path& path, std::string& err) {
#ifdef _WIN32
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = "cannot open " + utf8Of(path); return nullptr; }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); err = "cannot read the size of " + utf8Of(path); return nullptr; }
    return std::make_unique<FileSource>(h, (uint64_t)sz.QuadPart);
#else
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) { err = "cannot open " + utf8Of(path); return nullptr; }
    struct stat st{};
    if (::fstat(fd, &st) != 0) { ::close(fd); err = "cannot read the size of " + utf8Of(path); return nullptr; }
    return std::make_unique<FileSource>(fd, (uint64_t)st.st_size);
#endif
}

std::unique_ptr<ReadSource> memorySource(std::vector<uint8_t> bytes) {
    return std::make_unique<MemorySource>(std::move(bytes));
}

// ── 마운트 ─────────────────────────────────────────────────────────────────────────
bool PakReader::open(std::unique_ptr<ReadSource> source, std::string& err) {
    entries_.clear(); blocks_.clear(); strings_.clear(); index_.clear(); src_.reset();
    if (!source) { err = "no source"; return false; }
    const uint64_t srcSize = source->size();
    if (srcSize < kHeaderSize) { err = "file is smaller than the header (truncated or not a .hpak)"; return false; }

    Header h{};
    if (!source->readAt(0, &h, sizeof(h))) { err = "cannot read the header"; return false; }
    if (std::memcmp(h.magic, kMagic, 4) != 0) { err = "not a .hpak file (bad magic)"; return false; }
    if (h.major != kVersionMajor) {
        err = "unsupported .hpak version " + std::to_string(h.major) + "." + std::to_string(h.minor);
        return false;
    }
    if ((uint32_t)hash64(&h, offsetof(Header, headerHash)) != h.headerHash) { err = "header is damaged (hash mismatch)"; return false; }
    if (h.headerSize < kHeaderSize) { err = "header size is too small"; return false; }
    if (h.incompatFlags & ~kKnownIncompatFlags) { err = "the pack uses features this engine does not know (incompat flags)"; return false; }
    if (h.blockSizeLog2 != kBlockSizeLog2) { err = "unsupported block size"; return false; }
    if (h.packSize > srcSize) { err = "file is truncated (" + std::to_string(srcSize) + " of " + std::to_string(h.packSize) + " bytes)"; return false; }
    if (h.tocOffset < h.headerSize || h.tocSize < kTocHeaderSize || h.tocOffset > h.packSize ||
        h.tocSize > h.packSize - h.tocOffset) {
        err = "table of contents is out of range"; return false;
    }
    if (h.tocSize > (uint64_t)std::numeric_limits<uint32_t>::max()) { err = "table of contents is too large"; return false; }

    std::vector<uint8_t> toc((size_t)h.tocSize);
    if (!source->readAt(h.tocOffset, toc.data(), toc.size())) { err = "cannot read the table of contents"; return false; }
    if (hash64(toc.data(), toc.size()) != h.tocHash) { err = "table of contents is damaged (hash mismatch)"; return false; }

    TocHeader th{};
    std::memcpy(&th, toc.data(), sizeof(th));
    if (std::memcmp(th.magic, kTocMagic, 4) != 0) { err = "table of contents is damaged (bad magic)"; return false; }
    if (th.entryRecordSize < kEntrySize) { err = "entry records are too small"; return false; }
    // 개수 × 크기를 모두 더하면 64비트가 넘칠 수 있으므로(DEF-122), 남은 목차 크기를 줄여 가며 나눗셈으로 확인합니다.
    uint64_t rest = toc.size() - kTocHeaderSize;
    if (h.entryCount > rest / th.entryRecordSize) { err = "table of contents is shorter than its counts"; return false; }
    rest -= (uint64_t)h.entryCount * th.entryRecordSize;
    if (th.blockCount > rest / 4) { err = "table of contents is shorter than its counts"; return false; }
    rest -= (uint64_t)th.blockCount * 4;
    if (th.stringBytes > rest) { err = "table of contents is shorter than its counts"; return false; }

    const uint8_t* p = toc.data() + kTocHeaderSize;
    entries_.resize(h.entryCount);
    for (uint32_t i = 0; i < h.entryCount; ++i, p += th.entryRecordSize)
        std::memcpy(&entries_[i], p, kEntrySize);   // 더 긴 레코드(뒤 버전)는 앞 48B 만 읽습니다
    blocks_.resize(th.blockCount);
    if (th.blockCount) std::memcpy(blocks_.data(), p, (size_t)th.blockCount * 4);
    p += (size_t)th.blockCount * 4;
    strings_.assign(reinterpret_cast<const char*>(p), th.stringBytes);

    index_.reserve(entries_.size());
    for (size_t i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];
        // 문자열은 NUL 로 끝나므로 경로 뒤에 한 바이트가 더 있어야 합니다.
        if (e.pathLength == 0 || (uint64_t)e.pathOffset + e.pathLength >= strings_.size()) {
            err = "entry " + std::to_string(i) + " has a path outside the string table"; entries_.clear(); return false;
        }
        if (e.dataOffset < h.headerSize || e.dataOffset > h.tocOffset || e.storedSize > h.tocOffset - e.dataOffset) {
            err = "entry " + std::to_string(i) + " has data outside the pack"; entries_.clear(); return false;
        }
        if (e.codec == (uint8_t)Codec::Lz4) {
            const uint64_t nb = blockCountFor(e.size);
            uint64_t sum = 0;
            if (nb > blocks_.size() || e.firstBlock > blocks_.size() - nb) {
                err = "entry " + std::to_string(i) + " has blocks outside the block table"; entries_.clear(); return false;
            }
            for (uint64_t b = 0; b < nb; ++b) {
                const uint32_t word   = blocks_[(size_t)(e.firstBlock + b)];
                const uint64_t stored = word & ~kBlockRawBit;
                const uint64_t rawLen = std::min<uint64_t>(kBlockSize, e.size - b * kBlockSize);
                // 원문 블록은 크기가 같아야 하고, 압축 블록은 비어 있지 않으며 LZ4 가 풀 수 있는 배율 안이어야 합니다.
                // 그래야 조작한 목차가 실제 바이트 없이 거대한 압축 전 크기를 선언하지 못합니다(DEF-122).
                const bool ok = (word & kBlockRawBit) ? stored == rawLen : (stored != 0 && rawLen <= stored * kMaxLz4Ratio);
                if (!ok) {
                    err = "entry " + std::to_string(i) + " has a damaged block table"; entries_.clear(); return false;
                }
                sum += stored;
            }
            if (sum != e.storedSize) {
                err = "entry " + std::to_string(i) + " block sizes do not add up"; entries_.clear(); return false;
            }
        } else if (e.codec == (uint8_t)Codec::Stored && e.storedSize != e.size) {
            err = "entry " + std::to_string(i) + " stored size does not match its size"; entries_.clear(); return false;
        }
        const std::string_view raw(strings_.data() + e.pathOffset, e.pathLength);
        std::string norm;
        if (!normalizePath(raw, norm) || norm != raw) {
            err = "entry " + std::to_string(i) + " has an invalid path"; entries_.clear(); return false;
        }
        if (!index_.emplace(foldKey(norm), (int)i).second) {
            err = "path '" + norm + "' appears twice"; entries_.clear(); return false;
        }
    }
    src_ = std::move(source);
    return true;
}

int PakReader::find(std::string_view logical) const {
    std::string norm;
    if (!normalizePath(logical, norm)) return -1;
    const auto it = index_.find(foldKey(norm));
    return it == index_.end() ? -1 : it->second;
}

std::string_view PakReader::path(int index) const {
    const Entry& e = entries_[(size_t)index];
    return std::string_view(strings_.data() + e.pathOffset, e.pathLength);
}

bool PakReader::entryReadable(const Entry& e, std::string& err) const {
    if (e.flags & ~kKnownEntryFlags) { err = "the file uses features this engine does not know (entry flags)"; return false; }
    if (e.codec != (uint8_t)Codec::Stored && e.codec != (uint8_t)Codec::Lz4) {
        err = "the file uses an unknown compression (codec " + std::to_string(e.codec) + ")";
        return false;
    }
    if (e.size > (uint64_t)std::numeric_limits<size_t>::max()) { err = "the file is too large for this build"; return false; }
    return true;
}

// 블록 하나를 풉니다. storedOffset 은 그 블록이 항목 데이터 안에서 시작하는 위치입니다.
bool PakReader::readBlock(const Entry& e, uint64_t b, uint64_t storedOffset, std::vector<uint8_t>& out,
                          std::string& err) const {
    const uint32_t word   = blocks_[(size_t)(e.firstBlock + b)];
    const uint32_t stored = word & ~kBlockRawBit;
    const uint64_t rawLen = std::min<uint64_t>(kBlockSize, e.size - b * kBlockSize);
    out.resize((size_t)rawLen);
    if (word & kBlockRawBit) {
        if (stored != rawLen) { err = "raw block has the wrong size"; return false; }
        if (!src_->readAt(e.dataOffset + storedOffset, out.data(), (size_t)rawLen)) { err = "read failed"; return false; }
        return true;
    }
    std::vector<char> comp(stored);
    if (!src_->readAt(e.dataOffset + storedOffset, comp.data(), stored)) { err = "read failed"; return false; }
    const int n = LZ4_decompress_safe(comp.data(), reinterpret_cast<char*>(out.data()), (int)stored, (int)rawLen);
    if (n != (int)rawLen) { err = "compressed block is damaged"; return false; }
    return true;
}

// 읽기 함수는 예외를 던지지 않습니다(DEF-123). 큰 파일을 담을 메모리가 없으면 bad_alloc 대신 false 와 이유를 돌려줍니다.
// 오디오 비동기 로딩처럼 C 코드(miniaudio)를 거쳐 불리는 곳이 있어, 예외가 그 경계를 넘으면 잠금을 쥔 채 프로그램이 끝납니다.
bool PakReader::readAll(int index, std::vector<uint8_t>& out, std::string& err) const {
    if (index < 0 || (size_t)index >= entries_.size()) { err = "no such file"; return false; }
    const Entry& e = entries_[(size_t)index];
    if (!entryReadable(e, err)) return false;
    try {
        out.resize((size_t)e.size);
        if (e.codec == (uint8_t)Codec::Stored) {
            if (e.size && !src_->readAt(e.dataOffset, out.data(), (size_t)e.size)) { err = "read failed"; return false; }
        } else {
            std::vector<uint8_t> block;
            uint64_t storedOffset = 0;
            for (uint64_t b = 0; b < blockCountFor(e.size); ++b) {
                if (!readBlock(e, b, storedOffset, block, err)) return false;
                std::memcpy(out.data() + b * kBlockSize, block.data(), block.size());
                storedOffset += blocks_[(size_t)(e.firstBlock + b)] & ~kBlockRawBit;
            }
        }
    } catch (const std::bad_alloc&) {
        err = "not enough memory to read the file"; return false;
    } catch (const std::length_error&) {
        err = "not enough memory to read the file"; return false;
    }
    if (hash64(out.data(), out.size()) != e.contentHash) { err = "file content is damaged (hash mismatch)"; return false; }
    return true;
}

bool PakReader::readRange(int index, uint64_t offset, void* dst, size_t bytes, std::string& err) const {
    if (index < 0 || (size_t)index >= entries_.size()) { err = "no such file"; return false; }
    const Entry& e = entries_[(size_t)index];
    if (!entryReadable(e, err)) return false;
    if (offset > e.size || bytes > e.size - offset) { err = "range is outside the file"; return false; }
    if (bytes == 0) return true;
    if (e.codec == (uint8_t)Codec::Stored) {
        if (!src_->readAt(e.dataOffset + offset, dst, bytes)) { err = "read failed"; return false; }
        return true;
    }
    auto* d = static_cast<uint8_t*>(dst);
    const uint64_t first = offset / kBlockSize, last = (offset + bytes - 1) / kBlockSize;
    uint64_t storedOffset = 0;
    for (uint64_t b = 0; b < first; ++b) storedOffset += blocks_[(size_t)(e.firstBlock + b)] & ~kBlockRawBit;
    try {
        std::vector<uint8_t> block;
        for (uint64_t b = first; b <= last; ++b) {
            if (!readBlock(e, b, storedOffset, block, err)) return false;
            const uint64_t blockStart = b * kBlockSize;
            const uint64_t from = std::max(offset, blockStart);
            const uint64_t to   = std::min(offset + bytes, blockStart + block.size());
            std::memcpy(d + (from - offset), block.data() + (from - blockStart), (size_t)(to - from));
            storedOffset += blocks_[(size_t)(e.firstBlock + b)] & ~kBlockRawBit;
        }
    } catch (const std::bad_alloc&) {
        err = "not enough memory to read the file"; return false;
    }
    return true;
}

} // namespace hpak
