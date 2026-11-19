#pragma once
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

// 프로젝트 콘텐츠 논리 경로 규칙 (P1, 2026-11-19, Docs/Engine_Structure_Design.md 14절).
//
// 정식 논리 경로는 "/Game/<나머지>" 입니다. P1 단계에서는 읽는 쪽만 바꾸므로
// 옛 "assets/<나머지>"(대소문자 무시)를 받으면 "/Game/<나머지>" 로 통일합니다.
// 단, "assets/cache" 는 파생 캐시 경로라 그대로 두고(P3 에서 Saved/ 로 이동),
// "shaders/...", 절대 디스크 경로, 임시 파일 등 그 밖의 경로도 건드리지 않습니다.
namespace hvfs {

inline constexpr std::string_view kGamePrefix   = "/Game/";
inline constexpr std::string_view kConfigPrefix = "/Config/";
inline constexpr std::string_view kEnginePrefix = "/Engine/";   // 엔진 콘텐츠(D3, 2026-11-19: <엔진>/Engine/Content, 에디터 아이콘 등)

namespace detail {

inline bool startsWithAsciiFold(std::string_view s, std::string_view prefix) noexcept {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(s[i]);
        const unsigned char b = static_cast<unsigned char>(prefix[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

inline bool equalsAsciiFold(std::string_view a, std::string_view b) noexcept {
    return a.size() == b.size() && startsWithAsciiFold(a, b);
}

} // namespace detail

inline bool isGamePath(std::string_view path) noexcept {
    return detail::startsWithAsciiFold(path, kGamePrefix);
}

inline std::string_view gameRelative(std::string_view path) noexcept {
    return isGamePath(path) ? path.substr(kGamePrefix.size()) : std::string_view{};
}

inline bool isConfigPath(std::string_view path) noexcept {
    return detail::startsWithAsciiFold(path, kConfigPrefix);
}

inline std::string_view configRelative(std::string_view path) noexcept {
    return isConfigPath(path) ? path.substr(kConfigPrefix.size()) : std::string_view{};
}

inline bool isEnginePath(std::string_view path) noexcept {
    return detail::startsWithAsciiFold(path, kEnginePrefix);
}

// 가상 마운트가 가로챌 논리 경로인지. POSIX(Android)에서는 "/Game/..." · "/Config/..." · "/Engine/..." 도
// std::filesystem::path::is_absolute() 로 보이므로, 절대 디스크 경로 판정보다 먼저 봅니다.
inline bool isVirtualLogicalPath(std::string_view path) noexcept {
    return isGamePath(path) || isConfigPath(path) || isEnginePath(path);
}

inline bool isDiskAbsolutePath(std::string_view path, const std::filesystem::path& fsPath) {
    return !isVirtualLogicalPath(path) && fsPath.is_absolute();
}

inline std::string toCanonical(std::string_view path) {
    if (detail::startsWithAsciiFold(path, "assets/cache/") ||
        detail::equalsAsciiFold(path, "assets/cache")) {
        return std::string(path);
    }
    if (detail::equalsAsciiFold(path, "assets/project.ini")) {
        return "/Config/project.ini";
    }
    if (isConfigPath(path)) {
        std::string out;
        out.reserve(path.size());
        out.append(kConfigPrefix);
        out.append(path.substr(kConfigPrefix.size()));
        return out;
    }
    if (detail::startsWithAsciiFold(path, "assets/")) {
        std::string out;
        out.reserve(kGamePrefix.size() + path.size() - 7);
        out.append(kGamePrefix);
        out.append(path.substr(7));
        return out;
    }
    if (isGamePath(path)) {
        std::string out;
        out.reserve(path.size());
        out.append(kGamePrefix);
        out.append(path.substr(kGamePrefix.size()));
        return out;
    }
    if (isEnginePath(path)) {
        std::string out;
        out.reserve(path.size());
        out.append(kEnginePrefix);
        out.append(path.substr(kEnginePrefix.size()));
        return out;
    }
    return std::string(path);
}

} // namespace hvfs
