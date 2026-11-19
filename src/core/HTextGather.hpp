#pragma once
#include "HText.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// ── HTEXT 모으기 (2026-11-19, 글자 체계 T3-9, Docs/Text_System.md) ─────────────────────────────────
// 소스(.cpp · .hpp · .h)에서 HTEXT("ns", "key", "원문") 을 찾아 번역 틀(.pot)을 만듭니다. UE 의 GatherText 에 해당합니다.
// 세 인자는 문자열 리터럴이어야 하고(이어 붙인 리터럴 "a" "b" 는 받음), // 주석 뒤의 HTEXT 는 건너뜁니다.
// 같은 ns · key 에 다른 원문이 있으면 충돌로 알립니다(번역 키가 하나의 원문만 가리키도록).
namespace htext_gather {

struct Entry {
    std::string ns, key, source;
    std::string where;   // "file:line"(처음 나온 곳)
};

struct Result {
    std::vector<Entry>       entries;    // ns · key 차례
    std::vector<std::string> conflicts;  // 같은 키 · 다른 원문
};

// s[i] 의 공백 · 줄바꿈을 건너뜁니다.
inline void skipSpace(std::string_view s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
}

// s[i] 에서 이어 붙인 문자열 리터럴을 읽습니다("a" "b" → ab). C 이스케이프(\" \\ \n \t \xHH)를 풉니다. 리터럴이 아니면 거짓.
inline bool readLiteral(std::string_view s, size_t& i, std::string& out) {
    out.clear();
    bool any = false;
    for (;;) {
        skipSpace(s, i);
        if (i >= s.size() || s[i] != '"') return any;
        ++i;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c == '\\' && i < s.size()) {
                const char e = s[i++];
                switch (e) {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case '0': c = '\0'; break;
                    case 'x': {
                        int v = 0, n = 0;
                        while (i < s.size() && n < 2 && std::isxdigit((unsigned char)s[i])) {
                            const char h = s[i++];
                            v = v * 16 + (h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10);
                            ++n;
                        }
                        c = (char)v;
                        break;
                    }
                    default: c = e; break;
                }
            }
            out.push_back(c);
        }
        if (i >= s.size()) return false;
        ++i;   // 닫는 따옴표
        any = true;
    }
}

// 한 파일의 글에서 모읍니다.
inline void gatherText(std::string_view text, const std::string& fileName, Result& r, std::map<std::string, size_t>& index) {
    const std::string_view tag = "HTEXT(";
    for (size_t at = text.find(tag); at != std::string_view::npos; at = text.find(tag, at + tag.size())) {
        // 이름의 일부(MY_HTEXT(...))이거나 // 주석 뒤면 건너뜀
        if (at > 0 && (std::isalnum((unsigned char)text[at - 1]) || text[at - 1] == '_')) continue;
        const size_t lineStart = text.rfind('\n', at) == std::string_view::npos ? 0 : text.rfind('\n', at) + 1;
        if (text.substr(lineStart, at - lineStart).find("//") != std::string_view::npos) continue;
        size_t i = at + tag.size();
        std::string ns, key, src;
        if (!readLiteral(text, i, ns)) continue;
        skipSpace(text, i);
        if (i >= text.size() || text[i] != ',') continue;
        ++i;
        if (!readLiteral(text, i, key)) continue;
        skipSpace(text, i);
        if (i >= text.size() || text[i] != ',') continue;
        ++i;
        if (!readLiteral(text, i, src)) continue;
        skipSpace(text, i);
        if (i >= text.size() || text[i] != ')') continue;
        const size_t line = (size_t)std::count(text.begin(), text.begin() + (std::ptrdiff_t)at, '\n') + 1;
        const std::string where = fileName + ":" + std::to_string(line);
        const std::string k = ns + "," + key;
        auto it = index.find(k);
        if (it == index.end()) {
            index.emplace(k, r.entries.size());
            r.entries.push_back({ ns, key, src, where });
        } else if (r.entries[it->second].source != src) {
            r.conflicts.push_back(k + ": '" + r.entries[it->second].source + "' (" + r.entries[it->second].where + ") vs '" +
                                  src + "' (" + where + ")");
        }
    }
}

// 폴더들을 돌며 모읍니다. 파일 이름은 root 기준 '/' 경로로 남깁니다(로컬 절대 경로를 .pot 에 넣지 않음).
inline Result gatherDirs(const std::vector<std::filesystem::path>& dirs) {
    namespace fs = std::filesystem;
    Result r;
    std::map<std::string, size_t> index;
    for (const fs::path& dir : dirs) {
        std::error_code ec;
        std::vector<fs::path> files;
        for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) continue;
            const std::string ext = it->path().extension().string();
            if (ext == ".cpp" || ext == ".hpp" || ext == ".h") files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& f : files) {
            std::ifstream in(f, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const std::u8string rel = f.lexically_relative(dir.parent_path()).generic_u8string();
            gatherText(text, std::string(rel.begin(), rel.end()), r, index);
        }
    }
    std::sort(r.entries.begin(), r.entries.end(), [](const Entry& a, const Entry& b) {
        return a.ns != b.ns ? a.ns < b.ns : a.key < b.key;
    });
    return r;
}

// .pot 글(번역 틀). 번역 칸은 비어 있습니다.
inline std::string toPot(const Result& r) {
    std::string out =
        "# Haruka Engine text template. Generated by HarukaPak gather-text; do not edit by hand.\n"
        "msgid \"\"\n"
        "msgstr \"\"\n"
        "\"Content-Type: text/plain; charset=UTF-8\\n\"\n\n";
    for (const Entry& e : r.entries) {
        out += "#: " + e.where + "\n";
        out += "msgctxt \"" + htext::poEscape(e.ns + "," + e.key) + "\"\n";
        out += "msgid \"" + htext::poEscape(e.source) + "\"\n";
        out += "msgstr \"\"\n\n";
    }
    return out;
}

} // namespace htext_gather
