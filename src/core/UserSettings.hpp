#pragma once
// 사용자 설정 층(2026-11-20, Docs/Save_And_Settings.md D6 ~ D8).
//
// 플레이어가 고른 값(소리 크기 · 그래픽 · 키 · 편안함)을 쓰기 루트의 Config/user_settings.ini 에 둡니다. 프로젝트 설정(project.ini)
// 위에 **바꾼 값만** 덮는 층이라(UE GameUserSettings · Godot override.cfg), 프로젝트 기본값이 바뀌면 바꾸지 않은 값은 따라갑니다.
// 알려진 이름은 형 · 범위 · 적용 시점이 정해져 있고 범위 밖은 잘라 알립니다. input.action.* · input.axis.* 는 project.ini 와 같은 글,
// game.* 는 게임이 자유롭게 씁니다. 모르는 이름은 알리되 버리지 않습니다(새 버전의 키를 옛 실행 파일이 지우지 않게).
//
// 엔진(runtime/Engine)이 시작할 때 모두 적용하고, 그 뒤에는 revision() 이 바뀐 프레임에 바로 적용 항목만 다시 적용합니다.
// 파일은 처음 값을 바꿀 때 생깁니다. 메인 스레드 전용입니다.

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class UserSettings {
public:
    static constexpr const char* kPath = "Config/user_settings.ini";   // 쓰기 루트 기준

    enum class Kind  { Bool, Float, Int, Choice, Text };
    enum class Apply { Live, Restart };
    struct KeyInfo {
        std::string_view name;      // prefix 면 앞부분("input.action.")
        Kind             kind;
        float            lo = 0.f, hi = 0.f;   // Float · Int
        std::string_view choices;   // Choice: "scene|on|off"
        Apply            apply = Apply::Live;
        bool             prefix = false;
        std::string_view help;      // 파일 머리 설명
    };
    // 알려진 이름(정확히 맞는 이름, 없으면 앞부분이 맞는 prefix 항목). 모르면 nullptr.
    static const KeyInfo* find(std::string_view name);
    static const std::vector<KeyInfo>& keys();

    // 파일 글을 읽어 지금 값을 모두 바꿉니다. 알릴 것은 messages 에.
    void parse(const std::string& text, std::vector<std::string>& messages);
    std::string serialize() const;
    // 파일을 읽습니다. 없으면 false(값은 비움). BOM · UTF-16 은 UTF-8 로 바꿔 읽습니다.
    bool load(const std::filesystem::path& path, std::vector<std::string>& messages);
    // 원자적으로 씁니다(hfile::writeAtomic).
    bool save(const std::filesystem::path& path, std::string* why = nullptr);

    // 값을 바꿉니다. 형을 확인하고 정규 글로 저장합니다(bool 은 true/false, 실수는 floatToText). 범위 밖이면 잘라 true 이고 why 에
    // 알림. 형이 틀리거나 모르는 이름(game.* 이 아닌)이면 false 와 why. 값이 실제로 바뀌면 revision 이 오릅니다.
    bool set(std::string_view name, std::string_view value, std::string* why = nullptr);
    bool setFloat(std::string_view name, float v, std::string* why = nullptr);
    bool setBool(std::string_view name, bool v, std::string* why = nullptr);
    bool setInt(std::string_view name, int v, std::string* why = nullptr);
    // 덮어쓴 값을 지워 프로젝트 기본값으로 돌아갑니다. 있었으면 true.
    bool reset(std::string_view name);
    void resetAll();

    bool has(std::string_view name) const;
    std::optional<std::string> raw(std::string_view name) const;
    float       getFloat (std::string_view name, float def) const;
    bool        getBool  (std::string_view name, bool def) const;
    int         getInt   (std::string_view name, int def) const;
    std::string getString(std::string_view name, std::string_view def = {}) const;

    uint64_t revision() const { return revision_; }
    const std::map<std::string, std::string, std::less<>>& values() const { return values_; }

private:
    bool store(std::string_view name, std::string value);
    std::map<std::string, std::string, std::less<>> values_;
    uint64_t revision_ = 0;
};

// 편안함 설정(D7). 엔진이 월드 서비스(world.setService<ComfortSettings>)로 두고, 이동 · 리그 컴포넌트가 씬 값 대신 씁니다.
// 값마다 "씬 값을 씀" 이 있습니다(0 · -1).
struct ComfortSettings {
    float snapTurnDeg = 0.f;   // 0 = 씬 값, 그 밖에는 5 ~ 180
    int   teleport    = -1;    // -1 = 씬 값, 0 = 끔, 1 = 켬
    int   height      = -1;    // -1 = 씬 값, 0 = 추적(서서), 1 = 고정 눈높이(앉아서)
    float eyeHeight   = 0.f;   // 0 = 씬 값, 그 밖에는 0.5 ~ 2.5 m

    float snapTurn(float sceneDeg) const { return snapTurnDeg > 0.f ? snapTurnDeg : sceneDeg; }
    bool  teleportOn(bool scene) const   { return teleport < 0 ? scene : teleport == 1; }
    bool  trackedHeight(bool scene) const { return height < 0 ? scene : height == 0; }
    float eye(float sceneM) const        { return eyeHeight > 0.f ? eyeHeight : sceneM; }

    static ComfortSettings from(const UserSettings& s);
    bool operator==(const ComfortSettings& o) const {
        return snapTurnDeg == o.snapTurnDeg && teleport == o.teleport && height == o.height && eyeHeight == o.eyeHeight;
    }
};
