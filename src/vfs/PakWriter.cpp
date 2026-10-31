#include "PakWriter.hpp"

#include "lz4.h"
#include "lz4hc.h"

#include <algorithm>
#include <cstring>

namespace hpak {

namespace {

constexpr int    kLz4HcLevel = 9;      // LZ4HC_CLEVEL_DEFAULT. 바꾸면 같은 입력의 출력이 달라집니다
constexpr double kKeepRatio  = 0.9;    // 압축 결과가 원본의 이 비율 이상이면 그대로 저장

struct Item {
    std::string          path;   // 정규화한 경로
    std::string          key;    // 찾기 키
    std::vector<uint8_t> data;
    Codec                codec;
};

class Out {
public:
    explicit Out(std::ostream& s) : s_(s) {}
    uint64_t pos() const { return pos_; }
    void write(const void* p, size_t n) {
        s_.write(static_cast<const char*>(p), (std::streamsize)n);
        pos_ += n;
    }
    void padTo(uint64_t target) {
        static const char zeros[4096] = {};
        while (pos_ < target) {
            const size_t n = (size_t)std::min<uint64_t>(sizeof(zeros), target - pos_);
            write(zeros, n);
        }
    }
    bool good() const { return (bool)s_; }
private:
    std::ostream& s_;
    uint64_t      pos_ = 0;
};

} // namespace

bool writePak(std::vector<PakInput> inputs, std::ostream& stream, std::string& err, WriteStats* stats) {
    std::vector<Item> items;
    items.reserve(inputs.size());
    for (auto& in : inputs) {
        Item it;
        std::string why;
        if (!normalizePath(in.path, it.path, &why)) {
            err = "'" + in.path + "': " + why;
            return false;
        }
        if (in.codec != Codec::Stored && in.codec != Codec::Lz4) {
            err = "'" + in.path + "': this writer only writes Stored and LZ4";
            return false;
        }
        it.key   = foldKey(it.path);
        it.data  = std::move(in.data);
        it.codec = in.codec;
        items.push_back(std::move(it));
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.key != b.key ? a.key < b.key : a.path < b.path;
    });
    for (size_t i = 1; i < items.size(); ++i) {
        if (items[i].key != items[i - 1].key) continue;
        err = items[i].path == items[i - 1].path
            ? "'" + items[i].path + "' is listed twice"
            : "'" + items[i - 1].path + "' and '" + items[i].path + "' differ only in letter case";
        return false;
    }
    if (items.size() > 0xFFFFFFFFull) { err = "too many files"; return false; }

    Out out(stream);
    Header hdr{};
    out.write(&hdr, sizeof(hdr));   // 자리만 잡고 마지막에 다시 씁니다

    std::vector<Entry>    entries;
    std::vector<uint32_t> blocks;
    std::string           strings;
    WriteStats            st;
    std::vector<uint8_t>  packed;
    std::vector<char>     tmp((size_t)LZ4_compressBound((int)kBlockSize));

    for (const Item& it : items) {
        Entry e{};
        e.size        = it.data.size();
        e.contentHash = hash64(it.data.data(), it.data.size());
        e.pathOffset  = (uint32_t)strings.size();
        e.pathLength  = (uint16_t)it.path.size();
        strings += it.path;
        strings.push_back('\0');

        // LZ4: 64KiB 블록마다 따로 압축하고, 블록이 줄지 않으면 그 블록은 원문으로 둡니다.
        bool useLz4 = false;
        std::vector<uint32_t> itemBlocks;
        packed.clear();
        if (it.codec == Codec::Lz4 && e.size > 0) {
            for (uint64_t off = 0; off < e.size; off += kBlockSize) {
                const int len = (int)std::min<uint64_t>(kBlockSize, e.size - off);
                const char* src = reinterpret_cast<const char*>(it.data.data() + off);
                const int c = LZ4_compress_HC(src, tmp.data(), len, (int)tmp.size(), kLz4HcLevel);
                if (c > 0 && c < len) {
                    itemBlocks.push_back((uint32_t)c);
                    packed.insert(packed.end(), tmp.data(), tmp.data() + c);
                } else {
                    itemBlocks.push_back((uint32_t)len | kBlockRawBit);
                    packed.insert(packed.end(), src, src + len);
                }
            }
            useLz4 = (double)packed.size() < kKeepRatio * (double)e.size;
        }

        out.padTo(alignUp(out.pos(), alignFor(e.size)));
        e.dataOffset = out.pos();
        if (useLz4) {
            e.codec      = (uint8_t)Codec::Lz4;
            e.firstBlock = (uint32_t)blocks.size();
            e.storedSize = packed.size();
            blocks.insert(blocks.end(), itemBlocks.begin(), itemBlocks.end());
            out.write(packed.data(), packed.size());
            ++st.compressedFiles;
        } else {
            e.codec      = (uint8_t)Codec::Stored;
            e.storedSize = e.size;
            if (e.size) out.write(it.data.data(), it.data.size());
        }
        st.rawBytes    += e.size;
        st.storedBytes += e.storedSize;
        entries.push_back(e);
    }

    out.padTo(alignUp(out.pos(), 1u << kMinAlignLog2));
    const uint64_t tocOffset = out.pos();

    TocHeader th{};
    std::memcpy(th.magic, kTocMagic, 4);
    th.entryRecordSize = kEntrySize;
    th.blockCount      = (uint32_t)blocks.size();
    th.stringBytes     = (uint32_t)strings.size();
    std::vector<uint8_t> toc(sizeof(th) + entries.size() * kEntrySize + blocks.size() * 4 + strings.size());
    uint8_t* p = toc.data();
    std::memcpy(p, &th, sizeof(th));                             p += sizeof(th);
    if (!entries.empty()) { std::memcpy(p, entries.data(), entries.size() * kEntrySize); p += entries.size() * kEntrySize; }
    if (!blocks.empty())  { std::memcpy(p, blocks.data(), blocks.size() * 4);            p += blocks.size() * 4; }
    if (!strings.empty()) { std::memcpy(p, strings.data(), strings.size()); }
    out.write(toc.data(), toc.size());

    std::memcpy(hdr.magic, kMagic, 4);
    hdr.major         = kVersionMajor;
    hdr.minor         = kVersionMinor;
    hdr.headerSize    = kHeaderSize;
    hdr.entryCount    = (uint32_t)entries.size();
    hdr.tocOffset     = tocOffset;
    hdr.tocSize       = toc.size();
    hdr.tocHash       = hash64(toc.data(), toc.size());
    hdr.packSize      = tocOffset + toc.size();
    hdr.minAlignLog2  = (uint8_t)kMinAlignLog2;
    hdr.blockSizeLog2 = (uint8_t)kBlockSizeLog2;
    hdr.headerHash    = (uint32_t)hash64(&hdr, offsetof(Header, headerHash));
    stream.seekp(0);
    stream.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    stream.seekp((std::streamoff)hdr.packSize);
    if (!stream) { err = "write failed"; return false; }

    st.files     = entries.size();
    st.packBytes = hdr.packSize;
    if (stats) *stats = st;
    return true;
}

} // namespace hpak
