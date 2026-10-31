#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// ── Haruka 묶음 파일(.hpak) 형식 v1 (2026-10-24, Docs/Packaging_Design.md 5절) ─────────────────
// 게임 실행 파일이 읽는 에셋 · 셰이더를 파일 하나로 묶습니다. 배치는 다음과 같습니다(모든 값은 little-endian, 오프셋은
// 묶음 시작 기준 절대값).
//
//   [머리 128B] [파일 데이터 ...] [목차]
//
// 목차를 끝에 두어 한 번에 순차로 쓸 수 있습니다(Godot v3/v4 pck · O3DE Archive 와 같은 배치). 머리에 목차의 위치 ·
// 크기 · 해시를 적습니다. 목차는 목차 머리 32B, 항목 레코드 배열, 블록 표(u32), 문자열 표(UTF-8, NUL 종료) 순입니다.
//
// 결정(2026-10-24 사용자 결정, 권장안 전부): 압축은 파일마다 고르고 v1 코덱은 그대로 저장 · LZ4(64KiB 독립 블록),
// 손상 검사는 XXH3-64, 경로는 원래 표기를 저장하고 찾을 때 ASCII 대소문자를 무시, 정렬은 16B(64KiB 이상 항목은 4KiB).
// 암호화 · 서명 · 삭제 표시는 비트 자리만 남깁니다.
namespace hpak {

static_assert(std::endian::native == std::endian::little, "the .hpak reader/writer assume a little-endian host");

constexpr char     kMagic[4]        = { 'H', 'P', 'A', 'K' };
constexpr char     kTocMagic[4]     = { 'H', 'T', 'O', 'C' };
constexpr uint16_t kVersionMajor    = 1;   // 다르면 거부
constexpr uint16_t kVersionMinor    = 0;   // 하위 호환 추가만(새 코덱 번호, compat 비트, 레코드 뒤에 필드)
constexpr uint32_t kHeaderSize      = 128;
constexpr uint32_t kTocHeaderSize   = 32;
constexpr uint32_t kEntrySize       = 48;
constexpr uint32_t kMinAlignLog2    = 4;           // 항목 16B 정렬
constexpr uint32_t kBigAlign        = 4096;        // 큰 항목 4KiB 정렬(나중의 직접 I/O · 패치 청크)
constexpr uint64_t kBigEntryBytes   = 64 * 1024;   // 이 크기 이상이면 큰 항목
constexpr uint32_t kBlockSizeLog2   = 16;          // LZ4 블록 64KiB
constexpr uint32_t kBlockSize       = 1u << kBlockSizeLog2;
constexpr uint32_t kBlockRawBit     = 0x80000000u; // 블록 표: 이 비트가 켜지면 그 블록은 원문 그대로
constexpr size_t   kMaxPathBytes    = 1024;

enum class Codec : uint8_t {
    Stored = 0,   // 그대로 저장(ogg · png · wav 처럼 이미 압축되었거나 압축 이득이 작은 파일)
    Lz4    = 1,   // 64KiB 독립 블록. 중간부터 읽을 때 그 블록만 풂
    Zstd   = 2,   // 예약(v1 리더는 이 파일만 열지 못함)
};

// 머리 플래그. incompat 의 모르는 비트가 있으면 마운트를 거부하고, compat 의 모르는 비트는 무시합니다.
constexpr uint32_t kIncompatTocEncrypted = 1u << 0;   // 예약
constexpr uint32_t kCompatSignatureBlock = 1u << 0;   // 예약
constexpr uint32_t kKnownIncompatFlags   = 0;
// 항목 플래그(예약). 모르는 비트가 켜진 항목은 그 파일만 열지 못합니다.
constexpr uint8_t  kEntryEncrypted = 1u << 0;
constexpr uint8_t  kEntryRemoved   = 1u << 1;   // 패치 묶음이 아래 묶음의 파일을 지움(Godot 의 PACK_FILE_REMOVAL 자리)
constexpr uint8_t  kKnownEntryFlags = 0;

#pragma pack(push, 1)
struct Header {                      // 128B, 오프셋 0
    char     magic[4];               //   0 "HPAK"
    uint16_t major;                  //   4
    uint16_t minor;                  //   6
    uint32_t headerSize;             //   8 128
    uint32_t incompatFlags;          //  12
    uint32_t compatFlags;            //  16
    uint32_t entryCount;             //  20
    uint64_t tocOffset;              //  24 16B 정렬
    uint64_t tocSize;                //  32
    uint64_t tocHash;                //  40 목차 전체의 XXH3-64
    uint64_t packSize;               //  48 잘린 파일 감지
    uint8_t  minAlignLog2;           //  56 4
    uint8_t  blockSizeLog2;          //  57 16
    uint8_t  reserved[66];           //  58 0(서명 오프셋 · 빌드 ID 자리)
    uint32_t headerHash;             // 124 바이트 0~123 의 XXH3-64 하위 32비트
};
struct TocHeader {                   // 32B
    char     magic[4];               //   0 "HTOC"
    uint32_t entryRecordSize;        //   4 48(뒤에 필드를 붙이면 커짐, 리더는 이 크기만큼 건너뜀)
    uint32_t blockCount;             //   8
    uint32_t stringBytes;            //  12
    uint8_t  reserved[16];           //  16
};
struct Entry {                       // 48B
    uint64_t dataOffset;             //   0
    uint64_t storedSize;             //   8 묶음 안의 크기
    uint64_t size;                   //  16 압축 전 크기
    uint64_t contentHash;            //  24 압축 전 내용의 XXH3-64
    uint32_t pathOffset;             //  32 문자열 표 안의 위치
    uint16_t pathLength;             //  36
    uint8_t  codec;                  //  38 Codec
    uint8_t  flags;                  //  39
    uint32_t firstBlock;             //  40 블록 표 안의 첫 칸(LZ4 일 때)
    uint32_t reserved;               //  44
};
#pragma pack(pop)
static_assert(sizeof(Header) == kHeaderSize && offsetof(Header, headerHash) == 124, "Header layout");
static_assert(sizeof(TocHeader) == kTocHeaderSize, "TocHeader layout");
static_assert(sizeof(Entry) == kEntrySize && offsetof(Entry, firstBlock) == 40, "Entry layout");

// 압축 전 크기에서 LZ4 블록 수. 크기가 2^64 에 가까워도 넘치지 않게 나눗셈으로 셉니다(DEF-122).
constexpr uint64_t blockCountFor(uint64_t size) { return size / kBlockSize + (size % kBlockSize != 0 ? 1 : 0); }
// LZ4 블록 하나가 풀릴 수 있는 최대 배율. LZ4 형식에서 압축된 1바이트는 많아야 255바이트로 풀리므로, 이보다 큰 배율을
// 선언한 블록은 조작된 묶음입니다(마운트 때 거부해 압축 전 크기를 묶음 크기에 묶음).
constexpr uint64_t kMaxLz4Ratio = 256;
// 항목 데이터의 정렬.
constexpr uint64_t alignFor(uint64_t size) { return size >= kBigEntryBytes ? kBigAlign : (1u << kMinAlignLog2); }
constexpr uint64_t alignUp(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

// 논리 경로를 정규화합니다(도구와 엔진이 같은 함수를 씀). '\' 를 '/' 로, 앞의 "./" 를 지우고, 다음을 거부합니다:
// 빈 경로, 맨 앞 '/', ':', 빈 구성요소, "." · "..", 맨 끝 '/', 제어 문자, <>"|?*, 1024B 초과. 대소문자는 그대로 둡니다.
bool normalizePath(std::string_view in, std::string& out, std::string* why = nullptr);
// 찾기 키: 정규화한 경로의 ASCII 대문자를 소문자로. 묶음 안 · 마운트 표의 찾기는 이 키로 합니다.
std::string foldKey(std::string_view normalized);

// XXH3-64(xxHash v0.8.3). 묶음의 목차 · 머리 · 내용 해시와 glTF 캐시의 원본 해시가 같은 함수를 씁니다.
uint64_t hash64(const void* data, size_t size);
// 여러 조각을 이어 해시합니다(큰 파일을 나눠 읽을 때).
class Hasher {
public:
    Hasher();
    ~Hasher();
    Hasher(const Hasher&) = delete;
    Hasher& operator=(const Hasher&) = delete;
    void     update(const void* data, size_t size);
    uint64_t digest() const;
private:
    void* state_;
};

} // namespace hpak
