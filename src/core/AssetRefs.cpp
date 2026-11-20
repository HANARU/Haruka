#include "AssetRefs.hpp"

#include "FileUtil.hpp"
#include "Utf8.hpp"
#include "../vfs/LogicalPath.hpp"
#include "../vfs/PathUtf8.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>

namespace fs = std::filesystem;

namespace hrefs {
namespace {

bool readBytes(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool eqFold(std::string_view a, std::string_view b) { return hvfs::detail::equalsAsciiFold(a, b); }
bool startsFold(std::string_view s, std::string_view p) { return hvfs::detail::startsWithAsciiFold(s, p); }

std::string lowerExt(const std::string& logical) {
    const size_t slash = logical.find_last_of('/');
    const size_t dot   = logical.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    std::string e = logical.substr(dot);
    for (char& c : e) c = utf8::asciiLower(c);
    return e;
}

std::string dirOf(const std::string& logical) {
    const size_t slash = logical.find_last_of('/');
    return slash == std::string::npos ? std::string() : logical.substr(0, slash);
}

// "a/b/../c/./d" → "a/c/d". 맨 앞의 "/" 는 지킵니다. 루트 위로 올라가면 빈 문자열.
std::string normalizeSegments(const std::string& path) {
    std::vector<std::string> parts;
    size_t pos = 0;
    const bool abs = !path.empty() && path[0] == '/';
    while (pos <= path.size()) {
        size_t next = path.find('/', pos);
        if (next == std::string::npos) next = path.size();
        const std::string seg = path.substr(pos, next - pos);
        if (seg == "..") {
            if (parts.empty()) return {};
            parts.pop_back();
        } else if (!seg.empty() && seg != ".") {
            parts.push_back(seg);
        }
        pos = next + 1;
    }
    std::string out = abs ? "/" : "";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += '/';
        out += parts[i];
    }
    return out;
}

std::string percentDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
            out += (char)(hex(s[i + 1]) * 16 + hex(s[i + 2]));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

bool isRefPath(const std::string& canon) {
    if (hvfs::isGamePath(canon)) return canon.size() > hvfs::kGamePrefix.size();
    if (hvfs::isEnginePath(canon)) return canon.size() > hvfs::kEnginePrefix.size();
    return false;
}

void refsInLines(const std::string& file, const std::string& bytes, std::vector<Ref>& out) {
    size_t pos = 0;
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF)
        pos = 3;
    int lineNo = 0;
    while (pos < bytes.size()) {
        ++lineNo;
        size_t eol = bytes.find('\n', pos);
        if (eol == std::string::npos) eol = bytes.size();
        size_t b = pos, e = eol;
        if (e > b && bytes[e - 1] == '\r') --e;
        pos = eol + 1;
        while (b < e && (bytes[b] == ' ' || bytes[b] == '\t')) ++b;
        if (b >= e || bytes[b] == '#' || bytes[b] == ';') continue;
        size_t sep = b;
        while (sep < e && bytes[sep] != '=' && bytes[sep] != ':') ++sep;
        if (sep >= e) continue;
        size_t ke = sep;
        while (ke > b && (bytes[ke - 1] == ' ' || bytes[ke - 1] == '\t')) --ke;
        const std::string key = bytes.substr(b, ke - b);
        if (key == "ACTOR" || key == "NAME" || key == "GUID") continue;
        // 프리팹 덮어쓰기 "OVERRIDE:<짝> <경로>=<값>" 은 값이 '=' 뒤입니다(Docs/Linked_Prefabs.md D6).
        if (key == "OVERRIDE") {
            size_t eq = sep + 1;
            while (eq < e && bytes[eq] != '=') ++eq;
            if (eq >= e) continue;
            sep = eq;
        }
        size_t vb = sep + 1, ve = e;
        while (vb < ve && (bytes[vb] == ' ' || bytes[vb] == '\t')) ++vb;
        while (ve > vb && (bytes[ve - 1] == ' ' || bytes[ve - 1] == '\t')) --ve;
        if (vb >= ve) continue;
        const std::string canon = hvfs::toCanonical(std::string_view(bytes).substr(vb, ve - vb));
        if (!isRefPath(canon)) continue;
        Ref r;
        r.file       = file;
        r.line       = lineNo;
        r.key        = key;
        r.target     = canon;
        r.valueBegin = vb;
        r.valueEnd   = ve;
        out.push_back(std::move(r));
    }
}

// glTF 의 "uri": "..." 를 모읍니다. data: URI 와 절대 URL 은 뺍니다.
void refsInGltf(const std::string& file, const std::string& bytes, std::vector<Ref>& out) {
    const std::string dir = dirOf(file);
    size_t at = 0;
    while ((at = bytes.find("\"uri\"", at)) != std::string::npos) {
        size_t p = at + 5;
        at = p;
        while (p < bytes.size() && (bytes[p] == ' ' || bytes[p] == '\t' || bytes[p] == '\r' || bytes[p] == '\n')) ++p;
        if (p >= bytes.size() || bytes[p] != ':') continue;
        ++p;
        while (p < bytes.size() && (bytes[p] == ' ' || bytes[p] == '\t' || bytes[p] == '\r' || bytes[p] == '\n')) ++p;
        if (p >= bytes.size() || bytes[p] != '"') continue;
        const size_t vb = ++p;
        std::string raw;
        while (p < bytes.size() && bytes[p] != '"') {
            if (bytes[p] == '\\' && p + 1 < bytes.size()) { raw += bytes[p + 1]; p += 2; continue; }
            raw += bytes[p++];
        }
        const size_t ve = p;
        if (raw.empty() || raw.rfind("data:", 0) == 0 || raw.find("://") != std::string::npos) continue;
        const std::string target = normalizeSegments(dir + "/" + percentDecode(raw));
        if (target.empty()) continue;
        Ref r;
        r.file       = file;
        r.line       = 1 + (int)std::count(bytes.begin(), bytes.begin() + (std::ptrdiff_t)vb, '\n');
        r.key        = "uri";
        r.target     = target;
        r.readOnly   = true;
        r.uri        = raw;
        r.valueBegin = vb;
        r.valueEnd   = ve;
        out.push_back(std::move(r));
    }
}

bool validSegment(const std::string& seg) {
    if (seg.empty() || seg == "." || seg == "..") return false;
    if (seg.back() == ' ' || seg.back() == '.' || seg.front() == ' ') return false;
    for (char ch : seg) {
        const unsigned char c = (unsigned char)ch;
        if (c < 32 || std::string("\\:*?\"<>|").find(ch) != std::string::npos) return false;
    }
    return true;
}

bool validGamePath(const std::string& canon, std::string& why) {
    if (!hvfs::isGamePath(canon) || canon.size() <= hvfs::kGamePrefix.size()) { why = "only paths under /Game can be moved"; return false; }
    if (!utf8::isValid(canon)) { why = "the path is not valid UTF-8"; return false; }
    const std::string rel(hvfs::gameRelative(canon));
    size_t pos = 0;
    while (pos <= rel.size()) {
        size_t next = rel.find('/', pos);
        if (next == std::string::npos) next = rel.size();
        if (!validSegment(rel.substr(pos, next - pos))) {
            why = "each folder and file name must be non-empty, must not be '.' or '..', must not start with a space or end with a space or dot, "
                  "and must not contain \\ : * ? \" < > |";
            return false;
        }
        pos = next + 1;
    }
    return true;
}

std::string trimSlash(std::string s) {
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

} // namespace

std::vector<Ref> refsInText(const std::string& file, const std::string& bytes) {
    std::vector<Ref> out;
    const std::string ext = lowerExt(file);
    if (ext == ".hscene" || ext == ".hprefab" || ext == ".ini") refsInLines(file, bytes, out);
    else if (ext == ".gltf") refsInGltf(file, bytes, out);
    return out;
}

bool toDisk(const std::vector<Root>& roots, const std::string& logical, fs::path& out) {
    for (const Root& r : roots) {
        if (eqFold(logical, r.logical)) { out = r.disk; return true; }
        if (startsFold(logical, r.logical + "/")) {
            fs::path rel;
            if (!hpak::pathFromUtf8(std::string_view(logical).substr(r.logical.size() + 1), rel)) return false;
            out = r.disk / rel;
            return true;
        }
    }
    return false;
}

Index scan(const std::vector<Root>& roots) {
    Index idx;
    for (const Root& r : roots) {
        std::error_code ec;
        if (!fs::is_directory(r.disk, ec)) continue;
        for (fs::recursive_directory_iterator it(r.disk, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const std::string rel = hpak::utf8Of(fs::relative(it->path(), r.disk, ec));
            if (rel.empty() || ec) { ec.clear(); continue; }
            idx.assets.push_back(r.logical + "/" + rel);
        }
    }
    std::sort(idx.assets.begin(), idx.assets.end());
    for (const std::string& a : idx.assets) {
        const std::string ext = lowerExt(a);
        if (ext != ".hscene" && ext != ".hprefab" && ext != ".ini" && ext != ".gltf") continue;
        fs::path disk;
        std::string bytes;
        if (!toDisk(roots, a, disk) || !readBytes(disk, bytes)) continue;
        std::vector<Ref> refs = refsInText(a, bytes);
        idx.refs.insert(idx.refs.end(), std::make_move_iterator(refs.begin()), std::make_move_iterator(refs.end()));
    }
    return idx;
}

std::vector<Ref> referencesTo(const Index& index, const std::string& target, bool folder) {
    const std::string t = trimSlash(hvfs::toCanonical(target));
    std::vector<Ref> out;
    for (const Ref& r : index.refs)
        if (folder ? startsFold(r.target, t + "/") : eqFold(r.target, t)) out.push_back(r);
    return out;
}

std::vector<Ref> missing(const Index& index, const std::vector<Root>& roots) {
    std::vector<Ref> out;
    for (const Ref& r : index.refs) {
        fs::path disk;
        if (!toDisk(roots, r.target, disk)) continue;
        std::error_code ec;
        if (!fs::is_regular_file(disk, ec)) out.push_back(r);
    }
    return out;
}

std::string mapPath(const MovePlan& plan, const std::string& path) {
    if (plan.from.empty()) return path;
    if (eqFold(path, plan.from)) return plan.to;
    if (plan.folder && startsFold(path, plan.from + "/")) return plan.to + path.substr(plan.from.size());
    return path;
}

std::string rewriteText(const std::string& file, const std::string& bytes, const MovePlan& plan, int* count) {
    std::vector<Ref> refs = refsInText(file, bytes);
    std::sort(refs.begin(), refs.end(), [](const Ref& a, const Ref& b) { return a.valueBegin > b.valueBegin; });
    std::string out = bytes;
    int n = 0;
    for (const Ref& r : refs) {
        if (r.readOnly) continue;
        const std::string mapped = mapPath(plan, r.target);
        if (mapped == r.target) continue;
        out.replace(r.valueBegin, r.valueEnd - r.valueBegin, mapped);
        ++n;
    }
    if (count) *count = n;
    return out;
}

MovePlan planMove(const Index& index, const std::vector<Root>& roots, const std::string& fromIn, const std::string& toIn,
                  const std::vector<fs::path>& sourceDirs) {
    MovePlan plan;
    plan.from = trimSlash(hvfs::toCanonical(fromIn));
    plan.to   = trimSlash(hvfs::toCanonical(toIn));
    std::string why;
    if (!validGamePath(plan.from, why)) { plan.error = "source: " + why; return plan; }
    if (!validGamePath(plan.to, why))   { plan.error = "destination: " + why; return plan; }
    if (plan.from == plan.to) { plan.error = "the source and the destination are the same"; return plan; }
    const bool caseOnly = eqFold(plan.from, plan.to);

    fs::path fromDisk, toDiskPath;
    std::error_code ec;
    if (!toDisk(roots, plan.from, fromDisk) || !fs::exists(fromDisk, ec)) { plan.error = plan.from + " does not exist"; return plan; }
    if (!toDisk(roots, plan.to, toDiskPath)) { plan.error = "no folder for " + plan.to; return plan; }
    if (!caseOnly && fs::exists(toDiskPath, ec)) { plan.error = plan.to + " already exists"; return plan; }
    plan.folder = fs::is_directory(fromDisk, ec);
    if (plan.folder && startsFold(plan.to, plan.from + "/")) { plan.error = "a folder cannot be moved into itself"; return plan; }
    if (!plan.folder && lowerExt(plan.from) != lowerExt(plan.to)) {
        plan.error = "the file extension cannot change (" + lowerExt(plan.from) + " -> " + lowerExt(plan.to) + ")";
        return plan;
    }

    // glTF 의 상대 uri 는 고쳐 쓰지 않으므로, 옮긴 뒤에도 같은 상대 위치여야 합니다.
    for (const Ref& r : index.refs) {
        if (!r.readOnly) continue;
        const std::string newSrc = mapPath(plan, r.file);
        const std::string newTgt = mapPath(plan, r.target);
        if (newSrc == r.file && newTgt == r.target) continue;
        const std::string still = normalizeSegments(dirOf(newSrc) + "/" + percentDecode(r.uri));
        if (!eqFold(still, newTgt)) {
            plan.error = r.file + " uses " + r.target + " by the relative path '" + r.uri +
                         "'; move the folder that holds both instead";
            return plan;
        }
    }

    // 고쳐 쓸 값을 파일별로 모읍니다.
    std::map<std::string, size_t> editOf;
    for (const Ref& r : index.refs) {
        if (r.readOnly) continue;
        const std::string mapped = mapPath(plan, r.target);
        if (mapped == r.target) continue;
        auto it = editOf.find(r.file);
        if (it == editOf.end()) {
            it = editOf.emplace(r.file, plan.edits.size()).first;
            plan.edits.push_back({ r.file, {} });
        }
        plan.edits[it->second].values.emplace_back(r, mapped);
    }

    // C++ 소스 안의 같은 문자열은 고칠 수 없으므로 알리기만 합니다.
    for (const fs::path& dir : sourceDirs) {
        if (!fs::is_directory(dir, ec)) continue;
        for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string ext = hpak::utf8Of(it->path().extension());
            for (char& c : ext) c = utf8::asciiLower(c);
            if (ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".inl") continue;
            std::string bytes;
            if (!readBytes(it->path(), bytes)) continue;
            for (size_t at = bytes.find(plan.from); at != std::string::npos; at = bytes.find(plan.from, at + 1)) {
                const int line = 1 + (int)std::count(bytes.begin(), bytes.begin() + (std::ptrdiff_t)at, '\n');
                plan.codeWarnings.push_back(hpak::utf8Of(it->path()) + ":" + std::to_string(line));
            }
        }
    }
    return plan;
}

bool applyMove(const MovePlan& plan, const std::vector<Root>& roots, std::string* error) {
    auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
    if (!plan.ok()) return fail(plan.error);

    struct Saved { fs::path disk; std::string bytes; };
    std::vector<Saved> originals;
    auto restore = [&] {
        for (const Saved& s : originals) hfile::writeAtomicBinary(s.disk, s.bytes.data(), s.bytes.size());
    };

    // 1) 참조하는 파일을 고쳐 씁니다. 훑은 뒤 파일이 바뀌었으면(값 자리가 다르면) 멈춥니다.
    for (const Edit& e : plan.edits) {
        fs::path disk;
        std::string bytes;
        if (!toDisk(roots, e.file, disk) || !readBytes(disk, bytes)) { restore(); return fail("cannot read " + e.file); }
        const std::vector<Ref> now = refsInText(e.file, bytes);
        std::vector<std::pair<Ref, std::string>> values = e.values;
        for (const auto& [r, v] : values) {
            const bool same = std::any_of(now.begin(), now.end(), [&r](const Ref& n) {
                return n.valueBegin == r.valueBegin && n.valueEnd == r.valueEnd && n.target == r.target;
            });
            if (!same) { restore(); return fail(e.file + " changed after it was scanned; scan again"); }
        }
        std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) { return a.first.valueBegin > b.first.valueBegin; });
        std::string edited = bytes;
        for (const auto& [r, v] : values) edited.replace(r.valueBegin, r.valueEnd - r.valueBegin, v);
        std::string why;
        if (!hfile::writeAtomicBinary(disk, edited.data(), edited.size(), &why)) { restore(); return fail(why); }
        originals.push_back({ disk, std::move(bytes) });
    }

    // 2) 파일(폴더)을 옮깁니다.
    fs::path fromDisk, toDiskPath;
    if (!toDisk(roots, plan.from, fromDisk) || !toDisk(roots, plan.to, toDiskPath)) { restore(); return fail("no folder for the paths"); }
    std::error_code ec;
    if (toDiskPath.has_parent_path()) fs::create_directories(toDiskPath.parent_path(), ec);
    ec.clear();
    fs::rename(fromDisk, toDiskPath, ec);
    if (ec) { restore(); return fail("moving " + plan.from + " failed: " + ec.message()); }
    return true;
}

} // namespace hrefs
