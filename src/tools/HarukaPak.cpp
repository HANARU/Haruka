// HarukaPak — Haruka 묶음 파일(.hpak) 도구 (2026-10-24, Docs/Packaging_Design.md 5절)
//
//   HarukaPak build <목록 파일> -o <out.hpak> [-D 이름=값]...   목록대로 파일을 모아 묶음을 만듭니다
//   HarukaPak list <pak>                                       항목 · 크기 · 코덱
//   HarukaPak verify <pak>                                     모든 항목을 풀어 내용 해시를 확인합니다
//   HarukaPak extract <pak> <폴더>                             모든 항목을 폴더에 풉니다
//   HarukaPak shader-manifest -o <out.inc> <.spv | 폴더>...    엔진에 넣을 셰이더 목록(경로 · 내용 해시)을 씁니다(DEF-149)
//
// 목록 파일(Packaging/game.pack.txt)은 한 줄에 명령 하나입니다. '#' 은 주석이고 ${이름} 은 -D 값으로 바꿉니다.
//   map <폴더> <논리 접두어>   폴더 아래 모든 파일을 "<접두어>/<상대 경로>" 로 넣습니다
//   exclude <패턴>...          넣지 않을 논리 경로
//   stored <패턴>...           압축하지 않고 그대로 저장(이미 압축된 형식). 나머지는 LZ4
//   text <패턴>...             줄 끝을 LF 로 바꿔 넣음(체크아웃마다 CRLF/LF 가 달라도 같은 묶음이 나옴)
// 패턴: '*' 는 '/' 를 뺀 아무 글자, '**' 는 '/' 를 포함한 아무 글자, '?' 는 한 글자. '/' 가 없는 패턴은 파일 이름에만
// 맞춥니다. 비교는 ASCII 대소문자를 무시합니다(PakGlob.hpp).
//
// 논리 경로는 UTF-8 입니다. 파일 이름은 코드 페이지와 상관없이 UTF-8 로 바꿔 목차에 넣습니다(DEF-121). 예전에는
// generic_string() 이 시스템 코드 페이지(CP949)로 바꿔, 한글 이름의 에셋이 목차에 CP949 바이트로 들어가 게임이 찾지 못했고
// CP949 로 나타낼 수 없는 글자는 잡히지 않은 예외로 도구가 끝났습니다. 실행 파일에는 엔진과 같은 UTF-8 매니페스트도 넣습니다.
#include "../vfs/PakFormat.hpp"
#include "../vfs/PakReader.hpp"
#include "../vfs/PakWriter.hpp"
#include "../vfs/PathUtf8.hpp"
#include "PakGlob.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using hpak::anyMatch;
using hpak::pathFromUtf8;
using hpak::utf8Of;

int usage() {
    std::fprintf(stderr,
        "usage:\n"
        "  HarukaPak build <manifest> -o <out.hpak> [-D NAME=VALUE]...\n"
        "  HarukaPak list <pack>\n"
        "  HarukaPak verify <pack>\n"
        "  HarukaPak extract <pack> <folder>\n"
        "  HarukaPak shader-manifest -o <out.inc> <file.spv | folder>...\n");
    return 2;
}

// 따옴표로 감싼 토큰을 지원하는 공백 분리.
std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size()) break;
        std::string tok;
        if (line[i] == '"') {
            ++i;
            while (i < line.size() && line[i] != '"') tok += line[i++];
            ++i;
        } else {
            while (i < line.size() && line[i] != ' ' && line[i] != '\t') tok += line[i++];
        }
        out.push_back(tok);
    }
    return out;
}

std::string expand(const std::string& s, const std::map<std::string, std::string>& vars, std::string& err) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '{') {
            const size_t end = s.find('}', i);
            if (end == std::string::npos) { err = "unterminated ${ in '" + s + "'"; return {}; }
            const std::string name = s.substr(i + 2, end - i - 2);
            const auto it = vars.find(name);
            if (it == vars.end()) { err = "${" + name + "} is not defined (pass -D " + name + "=...)"; return {}; }
            out += it->second;
            i = end;
        } else {
            out += s[i];
        }
    }
    return out;
}

bool readFileBytes(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff n = f.tellg();
    out.resize((size_t)n);
    f.seekg(0);
    return n == 0 || (bool)f.read(reinterpret_cast<char*>(out.data()), n);
}

void toLf(std::vector<uint8_t>& d) {
    std::vector<uint8_t> o;
    o.reserve(d.size());
    for (size_t i = 0; i < d.size(); ++i)
        if (!(d[i] == '\r' && i + 1 < d.size() && d[i + 1] == '\n')) o.push_back(d[i]);
    d.swap(o);
}

int cmdBuild(int argc, char** argv) {
    std::string manifest, outPath;
    std::map<std::string, std::string> vars;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) outPath = argv[++i];
        else if (a == "-D" && i + 1 < argc) {
            const std::string kv = argv[++i];
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) return usage();
            vars[kv.substr(0, eq)] = kv.substr(eq + 1);
        }
        else if (manifest.empty()) manifest = a;
        else return usage();
    }
    if (manifest.empty() || outPath.empty()) return usage();

    std::ifstream mf(manifest);
    if (!mf) { std::fprintf(stderr, "error: cannot open %s\n", manifest.c_str()); return 1; }
    std::vector<std::pair<fs::path, std::string>> maps;
    std::vector<std::string> excludes, storedPats, textPats;
    std::string line;
    int lineNo = 0;
    while (std::getline(mf, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;
        std::string err;
        std::vector<std::string> t = tokenize(line);
        for (auto& tok : t) {
            tok = expand(tok, vars, err);
            if (!err.empty()) { std::fprintf(stderr, "%s:%d: %s\n", manifest.c_str(), lineNo, err.c_str()); return 1; }
        }
        const std::string& cmd = t[0];
        fs::path mapDir;
        if (cmd == "map" && t.size() == 3) {
            if (!pathFromUtf8(t[1], mapDir)) { std::fprintf(stderr, "%s:%d: folder is not valid UTF-8\n", manifest.c_str(), lineNo); return 1; }
            maps.emplace_back(mapDir, t[2]);
        }
        else if (cmd == "exclude" && t.size() >= 2) excludes.insert(excludes.end(), t.begin() + 1, t.end());
        else if (cmd == "stored"  && t.size() >= 2) storedPats.insert(storedPats.end(), t.begin() + 1, t.end());
        else if (cmd == "text"    && t.size() >= 2) textPats.insert(textPats.end(), t.begin() + 1, t.end());
        else { std::fprintf(stderr, "%s:%d: unknown or malformed line: %s\n", manifest.c_str(), lineNo, line.c_str()); return 1; }
    }

    std::vector<hpak::PakInput> inputs;
    for (const auto& [dir, prefixRaw] : maps) {
        std::string prefix = prefixRaw;
        while (!prefix.empty() && prefix.back() == '/') prefix.pop_back();
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) { std::fprintf(stderr, "error: map folder not found: %s\n", utf8Of(dir).c_str()); return 1; }
        for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::error_code relEc;
            const fs::path relPath = fs::relative(it->path(), dir, relEc);
            if (relEc) { std::fprintf(stderr, "error: %s: %s\n", utf8Of(it->path()).c_str(), relEc.message().c_str()); return 1; }
            const std::string rel = utf8Of(relPath);
            if (rel.empty()) {
                std::fprintf(stderr, "error: a file name under %s cannot be converted to UTF-8\n", utf8Of(dir).c_str());
                return 1;
            }
            const std::string logical = prefix.empty() ? rel : prefix + "/" + rel;
            if (anyMatch(excludes, logical)) continue;
            hpak::PakInput in;
            in.path = logical;
            if (!readFileBytes(it->path(), in.data)) { std::fprintf(stderr, "error: cannot read %s\n", utf8Of(it->path()).c_str()); return 1; }
            if (anyMatch(textPats, logical)) toLf(in.data);
            in.codec = anyMatch(storedPats, logical) ? hpak::Codec::Stored : hpak::Codec::Lz4;
            inputs.push_back(std::move(in));
        }
        if (ec) { std::fprintf(stderr, "error: cannot list %s: %s\n", utf8Of(dir).c_str(), ec.message().c_str()); return 1; }
    }

    // 임시 파일에 다 쓴 뒤 바꿉니다(쓰다 실패해도 옛 묶음이 남음).
    const fs::path out(outPath), tmp = fs::path(outPath + ".tmp");
    std::error_code ec;
    if (out.has_parent_path()) fs::create_directories(out.parent_path(), ec);
    hpak::WriteStats st;
    std::string err;
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { std::fprintf(stderr, "error: cannot write %s\n", utf8Of(tmp).c_str()); return 1; }
        if (!hpak::writePak(std::move(inputs), f, err, &st)) {
            f.close(); fs::remove(tmp, ec);
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
    }
    fs::rename(tmp, out, ec);
    if (ec) { std::fprintf(stderr, "error: cannot replace %s: %s\n", utf8Of(out).c_str(), ec.message().c_str()); return 1; }
    std::printf("HarukaPak: %s  %llu files (%llu compressed), %llu -> %llu bytes of data, pack %llu bytes\n",
                utf8Of(out).c_str(), (unsigned long long)st.files, (unsigned long long)st.compressedFiles,
                (unsigned long long)st.rawBytes, (unsigned long long)st.storedBytes, (unsigned long long)st.packBytes);
    return 0;
}

bool openPak(const char* path, hpak::PakReader& r) {
    std::string err;
    auto src = hpak::openFileSource(path, err);
    if (!src || !r.open(std::move(src), err)) { std::fprintf(stderr, "error: %s: %s\n", path, err.c_str()); return false; }
    return true;
}

int cmdList(const char* pak) {
    hpak::PakReader r;
    if (!openPak(pak, r)) return 1;
    for (int i = 0; i < (int)r.count(); ++i) {
        const hpak::Entry& e = r.entry(i);
        std::printf("%10llu %10llu  %-6s  %.*s\n", (unsigned long long)e.size, (unsigned long long)e.storedSize,
                    e.codec == (uint8_t)hpak::Codec::Lz4 ? "lz4" : e.codec == 0 ? "stored" : "?",
                    (int)r.path(i).size(), r.path(i).data());
    }
    std::printf("%zu files\n", r.count());
    return 0;
}

int cmdVerify(const char* pak) {
    hpak::PakReader r;
    if (!openPak(pak, r)) return 1;
    int bad = 0;
    std::vector<uint8_t> buf;
    for (int i = 0; i < (int)r.count(); ++i) {
        std::string err;
        if (!r.readAll(i, buf, err)) {
            ++bad;
            std::printf("BAD  %.*s: %s\n", (int)r.path(i).size(), r.path(i).data(), err.c_str());
        }
    }
    std::printf("%s: %zu files, %d damaged\n", pak, r.count(), bad);
    return bad ? 1 : 0;
}

int cmdExtract(const char* pak, const char* dir) {
    hpak::PakReader r;
    if (!openPak(pak, r)) return 1;
    std::vector<uint8_t> buf;
    for (int i = 0; i < (int)r.count(); ++i) {
        std::string err;
        if (!r.readAll(i, buf, err)) { std::fprintf(stderr, "error: %.*s: %s\n", (int)r.path(i).size(), r.path(i).data(), err.c_str()); return 1; }
        fs::path rel;
        if (!pathFromUtf8(r.path(i), rel)) { std::fprintf(stderr, "error: entry %d has a path that is not valid UTF-8\n", i); return 1; }
        const fs::path out = fs::path(dir) / rel;
        std::error_code ec;
        fs::create_directories(out.parent_path(), ec);
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        if (!f || !f.write(reinterpret_cast<const char*>(buf.data()), (std::streamsize)buf.size())) {
            std::fprintf(stderr, "error: cannot write %s\n", utf8Of(out).c_str());
            return 1;
        }
    }
    std::printf("extracted %zu files to %s\n", r.count(), dir);
    return 0;
}

// 셰이더 목록(2026-11-01, DEF-149). 받은 .spv 파일(폴더를 주면 그 바로 아래의 .spv 전부)을 이름순으로
// "{ "shaders/<이름>", 0x<해시>ull }," 줄로 씁니다. 엔진(src/core/ShaderManifest.cpp)이 이 파일을 배열 안에 넣고, 마운트 뒤 실제로
// 읽힐 셰이더와 비교합니다. 해시는 묶음 목차와 같은 hash64(파일 내용)입니다(.spv 는 text 변환을 하지 않으므로 묶음 안의 값과 같음).
// 빌드는 셰이더 파일 목록을 넘깁니다. 폴더를 훑으면 목록에서 셰이더를 빼도 명령이 바뀌지 않아 이 파일이 다시 만들어지지 않았습니다
// (2026-11-01 독립 검토). 이름은 C 문자열에 그대로 넣을 수 있는 글자(영문 · 숫자 · '.' · '_' · '-')만 받습니다.
int cmdShaderManifest(int argc, char** argv) {
    std::string outPath;
    std::vector<std::string> inputs;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) outPath = argv[++i];
        else inputs.push_back(a);
    }
    if (inputs.empty() || outPath.empty()) return usage();

    std::map<std::string, uint64_t> shaders;   // 이름순
    std::error_code ec;
    auto addFile = [&](const fs::path& p) -> bool {
        const std::string name = utf8Of(p.filename());
        if (name.size() < 5 || hpak::foldKey(name.substr(name.size() - 4)) != ".spv") {
            std::fprintf(stderr, "error: not a .spv file: %s\n", utf8Of(p).c_str());
            return false;
        }
        for (const char c : name) {
            const bool okChar = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                                c == '.' || c == '_' || c == '-';
            if (!okChar) { std::fprintf(stderr, "error: shader file name not supported: %s\n", name.c_str()); return false; }
        }
        if (shaders.count(name)) { std::fprintf(stderr, "error: %s is given twice\n", name.c_str()); return false; }
        std::vector<uint8_t> bytes;
        if (!readFileBytes(p, bytes)) { std::fprintf(stderr, "error: cannot read %s\n", utf8Of(p).c_str()); return false; }
        shaders[name] = hpak::hash64(bytes.data(), bytes.size());
        return true;
    };
    for (const auto& in : inputs) {
        fs::path p;
        if (!pathFromUtf8(in, p)) { std::fprintf(stderr, "error: path is not valid UTF-8\n"); return 1; }
        if (fs::is_directory(p, ec)) {
            for (const auto& e : fs::directory_iterator(p, ec)) {
                if (!e.is_regular_file(ec)) continue;
                const std::string name = utf8Of(e.path().filename());
                if (name.size() >= 5 && hpak::foldKey(name.substr(name.size() - 4)) == ".spv" && !addFile(e.path())) return 1;
            }
            if (ec) { std::fprintf(stderr, "error: cannot list %s: %s\n", utf8Of(p).c_str(), ec.message().c_str()); return 1; }
        } else if (!addFile(p)) {
            return 1;
        }
    }
    if (shaders.empty()) { std::fprintf(stderr, "error: no .spv files given\n"); return 1; }

    std::string text = "// Generated by HarukaPak shader-manifest; do not edit.\n";
    for (const auto& [name, hash] : shaders) {
        char line[160];
        std::snprintf(line, sizeof(line), "{ \"shaders/%s\", 0x%016llxull },\n", name.c_str(), (unsigned long long)hash);
        text += line;
    }
    const fs::path out(outPath), tmp = fs::path(outPath + ".tmp");
    if (out.has_parent_path()) fs::create_directories(out.parent_path(), ec);
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f || !f.write(text.data(), (std::streamsize)text.size())) {
            std::fprintf(stderr, "error: cannot write %s\n", utf8Of(tmp).c_str());
            return 1;
        }
    }
    fs::rename(tmp, out, ec);
    if (ec) { std::fprintf(stderr, "error: cannot replace %s: %s\n", utf8Of(out).c_str(), ec.message().c_str()); return 1; }
    std::printf("HarukaPak: shader manifest %s (%zu shaders)\n", utf8Of(out).c_str(), shaders.size());
    return 0;
}

} // namespace

// 예상하지 못한 예외(메모리 부족, 파일 시스템 오류)는 abort 로 끝나지 않고 이유와 종료 코드 1 로 끝납니다(DEF-121). 빌드의
// GamePack 단계가 이 도구를 부르므로, 실패하면 무엇 때문인지 빌드 출력에 남아야 합니다.
int main(int argc, char** argv) {
    try {
        if (argc < 2) return usage();
        const std::string cmd = argv[1];
        if (cmd == "build") return cmdBuild(argc, argv);
        if (cmd == "list" && argc == 3) return cmdList(argv[2]);
        if (cmd == "verify" && argc == 3) return cmdVerify(argv[2]);
        if (cmd == "extract" && argc == 4) return cmdExtract(argv[2], argv[3]);
        if (cmd == "shader-manifest") return cmdShaderManifest(argc, argv);
        return usage();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
