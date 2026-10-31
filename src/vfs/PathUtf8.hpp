#pragma once
#include <filesystem>
#include <string>
#include <string_view>

// ── 경로와 UTF-8 (2026-10-31, DEF-121 · DEF-123) ───────────────────────────────────────────
// 논리 경로와 묶음 목차의 경로는 UTF-8 입니다. std::string 으로 std::filesystem::path 를 만들면 Windows 에서는 프로세스
// 코드 페이지로 해석되어(매니페스트가 없는 도구는 CP949), 올바르지 않거나 그 코드 페이지로 나타낼 수 없는 글자는
// std::system_error 를 던집니다. 아래 두 함수는 코드 페이지와 상관없이 UTF-8 로 바꾸고 예외를 던지지 않습니다.
namespace hpak {

// UTF-8 문자열을 경로로 바꿉니다. 올바르지 않은 UTF-8 이면 false 입니다.
inline bool pathFromUtf8(std::string_view utf8, std::filesystem::path& out) noexcept {
    try {
        out = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
        return true;
    } catch (...) {
        return false;
    }
}

// 경로를 '/' 구분자의 UTF-8 로 바꿉니다. 바꿀 수 없는 경로(짝이 맞지 않는 서로게이트)면 빈 문자열입니다.
inline std::string utf8Of(const std::filesystem::path& p) noexcept {
    try {
        const std::u8string s = p.generic_u8string();
        return std::string(reinterpret_cast<const char*>(s.data()), s.size());
    } catch (...) {
        return {};
    }
}

} // namespace hpak
