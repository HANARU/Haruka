#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

// ── 파일 읽기 · 쓰기 도우미 ─────────────────────────────────────────────────────
namespace hfile {

// 파일 전체를 읽습니다. 없거나 열 수 없으면 false.
inline bool readAll(const std::filesystem::path& path, std::string& out) {
    std::ifstream f(path);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// 손으로 만드는 설정 · 옵션 파일의 바이트를 UTF-8 글로 바꿉니다(2026-11-01, DEF-152). Windows PowerShell 5.1 의 `>` · Out-File 은
// BOM 이 있는 UTF-16LE 로, -Encoding utf8 은 BOM 이 있는 UTF-8 로 씁니다. 예전에는 그런 파일을 그대로 공백으로 나눠, 첫 옵션에
// BOM 이 붙거나 모든 글자 사이에 NUL 이 끼어 옵션이 하나도 맞지 않았습니다(경고에는 멀쩡한 옵션이 거부된 것으로 보임).
// 앞의 UTF-8 BOM 은 지우고, BOM 이 있는 UTF-16(LE · BE)은 UTF-8 로 바꿉니다(짝이 없는 서로게이트는 U+FFFD). UTF-32(BOM FF FE 00 00,
// `-Encoding utf32`)와, BOM 이 없거나 변환한 글에 NUL 이 있는 파일은 무슨 인코딩인지 알 수 없으므로 false 와 이유를 돌려줍니다.
// 줄 끝은 그대로 둡니다.
inline bool decodeTextBytes(std::string& text, std::string* why = nullptr) {
    const auto b = [&](size_t i) { return (uint32_t)(unsigned char)text[i]; };
    if (text.size() >= 4 && b(0) == 0xFF && b(1) == 0xFE && b(2) == 0 && b(3) == 0) {
        // UTF-16LE BOM 과 앞 2바이트가 같습니다. UTF-16 으로 읽으면 글자마다 NUL 이 끼어 옵션이 하나도 맞지 않습니다.
        if (why) *why = "the file is UTF-32; save it as UTF-8";
        return false;
    }
    if (text.size() >= 3 && b(0) == 0xEF && b(1) == 0xBB && b(2) == 0xBF) {
        text.erase(0, 3);
    } else if (text.size() >= 2 && ((b(0) == 0xFF && b(1) == 0xFE) || (b(0) == 0xFE && b(1) == 0xFF))) {
        const bool le = b(0) == 0xFF;
        if (text.size() % 2 != 0) {
            if (why) *why = "the UTF-16 text has an odd number of bytes";
            return false;
        }
        const auto unit = [&](size_t i) { return le ? (b(i) | (b(i + 1) << 8)) : ((b(i) << 8) | b(i + 1)); };
        std::string out;
        out.reserve(text.size() / 2);
        for (size_t i = 2; i < text.size(); i += 2) {
            uint32_t cp = unit(i);
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 2 < text.size() && unit(i + 2) >= 0xDC00 && unit(i + 2) <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (unit(i + 2) - 0xDC00);
                i += 2;
            } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                cp = 0xFFFD;
            }
            if (cp < 0x80) {
                out += (char)cp;
            } else if (cp < 0x800) {
                out += (char)(0xC0 | (cp >> 6));
                out += (char)(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += (char)(0xE0 | (cp >> 12));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            } else {
                out += (char)(0xF0 | (cp >> 18));
                out += (char)(0x80 | ((cp >> 12) & 0x3F));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
        }
        text.swap(out);
    }
    if (text.find('\0') != std::string::npos) {
        if (why) *why = "the file contains NUL bytes (UTF-16 without a byte order mark?); save it as UTF-8";
        return false;
    }
    return true;
}

// 손으로 만드는 설정 파일을 읽습니다(바이트로 읽은 뒤 decodeTextBytes). 없거나 열 수 없으면 false 이고 why 는 건드리지 않습니다.
// 인코딩을 알 수 없으면 false 와 why.
inline bool readConfigText(const std::filesystem::path& path, std::string& out, std::string* why = nullptr) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (decodeTextBytes(out, why)) return true;
    out.clear();
    return false;
}

// 같은 폴더의 임시 파일에 다 쓴 뒤 원본과 바꿉니다(2026-10-15). 쓰는 도중 실패하거나 프로그램이 죽어도
// 원본은 그대로 남습니다. 예전에는 원본을 바로 열어 덮어써, 쓰다 실패하면 반쯤 쓴 씬이 남았습니다.
// 텍스트 모드로 써서 예전 저장과 줄 끝이 같습니다. 실패하면 error 에 이유를 적고 false.
inline bool writeAtomic(const std::filesystem::path& path, const std::string& text, std::string* error = nullptr) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::out | std::ios::trunc);
        if (!f) {
            if (error) *error = "cannot open " + tmp.string() + " for writing";
            return false;
        }
        f << text;
        f.flush();
        if (!f) {
            f.close();
            fs::remove(tmp, ec);
            if (error) *error = "writing " + tmp.string() + " failed";
            return false;
        }
    }
    // MSVC 의 rename 은 대상이 있으면 바꿉니다(MoveFileEx + REPLACE_EXISTING, 테스트로 확인).
    fs::rename(tmp, path, ec);
    if (ec) {
        if (error) *error = "replacing " + path.string() + " failed: " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

// writeAtomic 의 바이너리판(2026-11-01, DEF-153). 바이트를 그대로 씁니다(텍스트 모드는 Windows 에서 0x0A 를 CRLF 로 바꿈).
inline bool writeAtomicBinary(const std::filesystem::path& path, const void* data, size_t size, std::string* error = nullptr) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!f) {
            if (error) *error = "cannot open " + tmp.string() + " for writing";
            return false;
        }
        f.write(static_cast<const char*>(data), (std::streamsize)size);
        f.flush();
        if (!f) {
            f.close();
            fs::remove(tmp, ec);
            if (error) *error = "writing " + tmp.string() + " failed";
            return false;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        if (error) *error = "replacing " + path.string() + " failed: " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace hfile
