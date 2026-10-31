#pragma once
#include "../vfs/PakFormat.hpp"   // hpak::hash64(XXH3-64)

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// ── 파이프라인 캐시 파일 (2026-11-01, DEF-153) ─────────────────────────────────────────────────
//   [머리 32B: "HPLC" · 형식 버전 · Vulkan 데이터 크기 · XXH3-64 · 예약][Vulkan 캐시 데이터(vkGetPipelineCacheData)]
// 읽을 때 잘림 · 손상(크기 · 해시)과 Vulkan 캐시 머리(VkPipelineCacheHeaderVersionOne 의 머리 크기 · 버전 · vendorID ·
// deviceID · pipelineCacheUUID)를 확인하고, 하나라도 맞지 않으면 버려 빈 캐시로 시작합니다. 예전에는 읽은 바이트를 그대로
// 드라이버에 넘겨, 저장 도중 프로세스가 죽어 잘린 파일이 VUID-VkPipelineCacheCreateInfo-initialDataSize-00768 위반이 되었습니다
// (기기에는 검증 레이어가 없어 드라이버가 거부할지 죽을지 알 수 없음). Godot · UE 도 같은 이유로 자체 머리로 감쌉니다.
namespace hpcache {

constexpr char     kMagic[4]   = { 'H', 'P', 'L', 'C' };
constexpr uint32_t kVersion    = 1;
constexpr size_t   kHeaderSize = 32;
// VkPipelineCacheHeaderVersionOne 의 크기(머리 크기 · 버전 · vendorID · deviceID 각 4B + UUID).
constexpr size_t   kVkHeaderOneSize = 16 + VK_UUID_SIZE;

#pragma pack(push, 1)
struct FileHeader {
    char     magic[4];
    uint32_t version;
    uint64_t dataSize;   // 뒤따르는 Vulkan 데이터의 크기
    uint64_t dataHash;   // 그 데이터의 XXH3-64
    uint64_t reserved;   // 0
};
#pragma pack(pop)
static_assert(sizeof(FileHeader) == kHeaderSize, "FileHeader layout");

// Vulkan 캐시 데이터를 파일 내용으로 감쌉니다.
inline std::vector<uint8_t> wrap(const void* data, size_t size) {
    FileHeader h{};
    std::memcpy(h.magic, kMagic, sizeof(kMagic));
    h.version  = kVersion;
    h.dataSize = size;
    h.dataHash = hpak::hash64(data, size);
    std::vector<uint8_t> out(kHeaderSize + size);
    std::memcpy(out.data(), &h, kHeaderSize);
    if (size) std::memcpy(out.data() + kHeaderSize, data, size);
    return out;
}

// 파일 내용에서 이 장치에 넘길 Vulkan 캐시 데이터의 위치를 찾습니다(offset · size). 맞지 않으면 false 와 이유(로그용).
inline bool unwrap(const std::vector<uint8_t>& file, const VkPhysicalDeviceProperties& props,
                   size_t& offset, size_t& size, std::string& why) {
    if (file.size() < kHeaderSize) { why = "the file is too short"; return false; }
    FileHeader h;
    std::memcpy(&h, file.data(), kHeaderSize);
    if (std::memcmp(h.magic, kMagic, sizeof(kMagic)) != 0 || h.version != kVersion) {
        why = "not a Haruka pipeline cache file (written by an older build?)";
        return false;
    }
    if (h.dataSize != file.size() - kHeaderSize) { why = "the file is cut short or has extra bytes"; return false; }
    const uint8_t* d = file.data() + kHeaderSize;
    if (hpak::hash64(d, (size_t)h.dataSize) != h.dataHash) { why = "the data is damaged (hash mismatch)"; return false; }
    if (h.dataSize < kVkHeaderOneSize) { why = "the Vulkan cache header is too short"; return false; }
    uint32_t vkHeaderSize = 0, vkHeaderVersion = 0, vendorID = 0, deviceID = 0;
    std::memcpy(&vkHeaderSize,    d + 0,  4);
    std::memcpy(&vkHeaderVersion, d + 4,  4);
    std::memcpy(&vendorID,        d + 8,  4);
    std::memcpy(&deviceID,        d + 12, 4);
    if (vkHeaderSize < kVkHeaderOneSize || vkHeaderSize > h.dataSize ||
        vkHeaderVersion != (uint32_t)VK_PIPELINE_CACHE_HEADER_VERSION_ONE) {
        why = "the Vulkan cache header is not version one";
        return false;
    }
    if (vendorID != props.vendorID || deviceID != props.deviceID ||
        std::memcmp(d + 16, props.pipelineCacheUUID, VK_UUID_SIZE) != 0) {
        why = "it was made by another GPU or driver version";
        return false;
    }
    offset = kHeaderSize;
    size   = (size_t)h.dataSize;
    return true;
}

} // namespace hpcache
