#pragma once
// 저장 게임(2026-11-20, Docs/Save_And_Settings.md D1 ~ D5).
//
// SaveData 는 게임이 정한 값 묶음입니다(UE USaveGame 의 필드 자리). 이름=값 텍스트로 쓰고, 마지막 줄의 CRC-32 로 잘림 · 손상을
// 잡습니다. SaveStore 는 쓰기 루트의 Saves/ 폴더에서 슬롯 이름으로 파일을 맡습니다: 임시 파일에 다 쓰고 디스크로 내보낸 뒤,
// 온전한 이전 파일을 .bak 으로 옮기고 임시 파일을 본 이름으로 옮깁니다. 읽을 때 본 파일이 없거나 손상됐으면 .bak 을 읽습니다.
//
// 형식(v1):
//   # Haruka Save v1
//   @dataVersion=<게임이 정한 정수>
//   @title=<제목>
//   @savedAt=<유닉스 초>
//   <이름>=<값>            이름은 [A-Za-z0-9_.:/-], 값의 \ · 줄바꿈은 \\ · \n · \r
//   @crc=<8자리 소문자 hex>  그 앞 모든 바이트의 CRC-32
//
// 메인 스레드 전용입니다(Threading.md 규칙 1). 파일 하나는 작다고 봅니다(통째로 읽고 씀).

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec3.hpp>

#include "Guid.hpp"

class SaveData {
public:
    static constexpr int kFormatVersion = 1;

    int         dataVersion = 0;   // 게임이 정함. 옛 파일을 읽은 뒤 이 값을 보고 옮깁니다(D4)
    std::string title;             // 슬롯 목록에 보일 제목
    int64_t     savedAt = 0;       // 유닉스 초. SaveStore::save 가 채움

    // 이름이 규칙에 맞지 않으면 false 이고 아무것도 바꾸지 않습니다.
    bool setString(std::string_view key, std::string_view v);
    bool setInt   (std::string_view key, int64_t v);
    bool setFloat (std::string_view key, float v);
    bool setBool  (std::string_view key, bool v);
    bool setVec3  (std::string_view key, const glm::vec3& v);
    // 액터 참조. 값은 HActorRef 와 같은 actor:<16자리 hex> 입니다(없는 GUID 는 빈 값).
    bool setGuid  (std::string_view key, HGuid g);

    // 없는 이름이나 형이 맞지 않는 값은 def 를 돌려줍니다(D3).
    std::string getString(std::string_view key, std::string_view def = {}) const;
    int64_t     getInt   (std::string_view key, int64_t def = 0) const;
    float       getFloat (std::string_view key, float def = 0.f) const;
    bool        getBool  (std::string_view key, bool def = false) const;
    glm::vec3   getVec3  (std::string_view key, const glm::vec3& def = glm::vec3(0.f)) const;
    HGuid       getGuid  (std::string_view key) const;

    bool has(std::string_view key) const;
    bool erase(std::string_view key);
    void clear();
    const std::map<std::string, std::string, std::less<>>& values() const { return values_; }

    static bool validKey(std::string_view key);

private:
    std::map<std::string, std::string, std::less<>> values_;
};

namespace hsave {

uint32_t crc32(const void* data, size_t size);

// 파일 글(마지막 CRC 줄 포함).
std::string serialize(const SaveData& data);
// 파일 글을 읽습니다. 실패하면 false 와 이유(out 은 비움). 더 새 형식 버전 · CRC 틀림 · 잘림 · 깨진 줄을 거절합니다.
bool parse(std::string_view text, SaveData& out, std::string* why = nullptr);

} // namespace hsave

class SaveStore {
public:
    // folder 는 보통 happ::inWriteRoot("Saves").
    explicit SaveStore(std::filesystem::path folder) : folder_(std::move(folder)) {}

    // [a-z0-9_-] 1 ~ 64 자(D5).
    static bool validSlotName(std::string_view slot);

    // data.savedAt 을 지금 시각으로 채우고 씁니다. 실패하면 false 와 이유(이전 저장본은 그대로).
    bool save(std::string_view slot, SaveData& data, std::string* why = nullptr);

    struct LoadResult {
        bool        ok         = false;
        bool        fromBackup = false;   // 본 파일이 없거나 손상돼 .bak 을 읽음
        std::string why;                  // 실패 이유, 또는 백업을 읽은 이유
    };
    LoadResult load(std::string_view slot, SaveData& out) const;

    bool exists(std::string_view slot) const;     // 본 파일이나 .bak 이 있음
    bool remove(std::string_view slot, std::string* why = nullptr);   // 본 파일 · .bak · .tmp

    struct SlotInfo {
        std::string name;
        std::string title;
        int         dataVersion = 0;
        int64_t     savedAt     = 0;
        bool        fromBackup  = false;
    };
    // 읽을 수 있는 슬롯(이름순). 본 파일과 .bak 이 모두 손상된 슬롯은 넣지 않습니다.
    std::vector<SlotInfo> list() const;

    const std::filesystem::path& folder() const { return folder_; }
    std::filesystem::path pathOf(std::string_view slot) const;   // <폴더>/<슬롯>.hsave

private:
    std::filesystem::path folder_;
};
