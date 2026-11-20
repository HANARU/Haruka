#include "SaveGame.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <system_error>

#include "TextFloat.hpp"

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kHeader = "# Haruka Save v";
constexpr std::string_view kCrc    = "@crc=";

std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}

bool unescape(std::string_view s, std::string& out) {
    out.clear();
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') { out += s[i]; continue; }
        if (++i >= s.size()) return false;
        if (s[i] == '\\') out += '\\';
        else if (s[i] == 'n') out += '\n';
        else if (s[i] == 'r') out += '\r';
        else return false;
    }
    return true;
}

bool toInt64(std::string_view s, int64_t& out) {
    if (s.empty() || s.size() > 24) return false;
    const std::string t(s);
    char* end = nullptr;
    errno = 0;
    const long long v = std::strtoll(t.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0') return false;
    out = (int64_t)v;
    return true;
}

bool toFloat(std::string_view s, float& out) {
    if (s.empty() || s.size() > 48) return false;
    const std::string t(s);
    char* end = nullptr;
    const float v = std::strtof(t.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}

// 파일을 디스크까지 내보내며 씁니다(D2). rename 전에 내용이 디스크에 있어야 전원이 끊겨도 빈 본 파일이 남지 않습니다.
bool writeDurable(const fs::path& path, const std::string& bytes, std::string* why) {
#if defined(_WIN32)
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0) f = nullptr;
#else
    FILE* f = std::fopen(path.c_str(), "wb");
#endif
    if (!f) {
        if (why) *why = "cannot open " + path.generic_string() + " for writing";
        return false;
    }
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size() && std::fflush(f) == 0;
#if defined(_WIN32)
    ok = ok && _commit(_fileno(f)) == 0;
#else
    ok = ok && fsync(fileno(f)) == 0;
#endif
    ok = std::fclose(f) == 0 && ok;
    if (!ok) {
        std::error_code ec;
        fs::remove(path, ec);
        if (why) *why = "writing " + path.generic_string() + " failed";
    }
    return ok;
}

bool readBytes(const fs::path& path, std::string& out) {
#if defined(_WIN32)
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0) f = nullptr;
#else
    FILE* f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return false;
    out.clear();
    char buf[16384];
    for (size_t n; (n = std::fread(buf, 1, sizeof(buf), f)) > 0;) out.append(buf, n);
    const bool ok = !std::ferror(f);
    std::fclose(f);
    return ok;
}

fs::path withSuffix(const fs::path& p, const char* suffix) {
    fs::path q = p;
    q += suffix;
    return q;
}

bool readSlotFile(const fs::path& path, SaveData& out, std::string& why) {
    std::string text;
    if (!readBytes(path, text)) { why = "missing"; return false; }
    return hsave::parse(text, out, &why);
}

} // namespace

// ── SaveData ────────────────────────────────────────────────────────────────────

bool SaveData::validKey(std::string_view key) {
    if (key.empty() || key.size() > 256) return false;
    for (char c : key) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.'
                        || c == ':' || c == '/' || c == '-';
        if (!ok) return false;
    }
    return true;
}

bool SaveData::setString(std::string_view key, std::string_view v) {
    if (!validKey(key)) return false;
    auto it = values_.find(key);
    if (it == values_.end()) values_.emplace(std::string(key), std::string(v));
    else it->second.assign(v);
    return true;
}
bool SaveData::setInt(std::string_view key, int64_t v)  { return setString(key, std::to_string(v)); }
bool SaveData::setFloat(std::string_view key, float v)  { return std::isfinite(v) && setString(key, floatToText(v)); }
bool SaveData::setBool(std::string_view key, bool v)    { return setString(key, v ? "true" : "false"); }
bool SaveData::setVec3(std::string_view key, const glm::vec3& v) {
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false;
    return setString(key, floatToText(v.x) + "," + floatToText(v.y) + "," + floatToText(v.z));
}
bool SaveData::setGuid(std::string_view key, HGuid g) {
    return setString(key, g.valid() ? "actor:" + g.toString() : std::string());
}

std::string SaveData::getString(std::string_view key, std::string_view def) const {
    auto it = values_.find(key);
    return it != values_.end() ? it->second : std::string(def);
}
int64_t SaveData::getInt(std::string_view key, int64_t def) const {
    auto it = values_.find(key);
    int64_t v = 0;
    return it != values_.end() && toInt64(it->second, v) ? v : def;
}
float SaveData::getFloat(std::string_view key, float def) const {
    auto it = values_.find(key);
    float v = 0.f;
    return it != values_.end() && toFloat(it->second, v) ? v : def;
}
bool SaveData::getBool(std::string_view key, bool def) const {
    auto it = values_.find(key);
    if (it == values_.end()) return def;
    if (it->second == "true" || it->second == "1") return true;
    if (it->second == "false" || it->second == "0") return false;
    return def;
}
glm::vec3 SaveData::getVec3(std::string_view key, const glm::vec3& def) const {
    auto it = values_.find(key);
    if (it == values_.end()) return def;
    const std::string_view s = it->second;
    const size_t a = s.find(','), b = a == std::string_view::npos ? a : s.find(',', a + 1);
    if (b == std::string_view::npos || s.find(',', b + 1) != std::string_view::npos) return def;
    glm::vec3 v;
    if (!toFloat(s.substr(0, a), v.x) || !toFloat(s.substr(a + 1, b - a - 1), v.y) || !toFloat(s.substr(b + 1), v.z)) return def;
    return v;
}
HGuid SaveData::getGuid(std::string_view key) const {
    auto it = values_.find(key);
    HGuid g;
    if (it == values_.end() || it->second.rfind("actor:", 0) != 0) return HGuid{};
    return HGuid::parse(std::string_view(it->second).substr(6), g) ? g : HGuid{};
}

bool SaveData::has(std::string_view key) const { return values_.find(key) != values_.end(); }
bool SaveData::erase(std::string_view key) {
    auto it = values_.find(key);
    if (it == values_.end()) return false;
    values_.erase(it);
    return true;
}
void SaveData::clear() {
    values_.clear();
    dataVersion = 0;
    title.clear();
    savedAt = 0;
}

// ── 형식 ────────────────────────────────────────────────────────────────────────

namespace hsave {

uint32_t crc32(const void* data, size_t size) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::string serialize(const SaveData& data) {
    std::string s;
    s += kHeader;
    s += std::to_string(SaveData::kFormatVersion) + "\n";
    s += "@dataVersion=" + std::to_string(data.dataVersion) + "\n";
    s += "@title=" + escape(data.title) + "\n";
    s += "@savedAt=" + std::to_string(data.savedAt) + "\n";
    for (const auto& [k, v] : data.values()) s += k + "=" + escape(v) + "\n";
    char crc[16];
    std::snprintf(crc, sizeof(crc), "%08x", crc32(s.data(), s.size()));
    s += kCrc;
    s += crc;
    s += "\n";
    return s;
}

bool parse(std::string_view text, SaveData& out, std::string* why) {
    out.clear();
    const auto fail = [&](std::string msg) {
        out.clear();
        if (why) *why = std::move(msg);
        return false;
    };
    if (text.substr(0, kHeader.size()) != kHeader) return fail("not a Haruka save file (no header)");
    // 마지막 줄의 CRC. 끝의 줄바꿈 하나는 있어도 없어도 됩니다.
    std::string_view body = text;
    if (!body.empty() && body.back() == '\n') body.remove_suffix(1);
    const size_t lastNl = body.rfind('\n');
    if (lastNl == std::string_view::npos || body.substr(lastNl + 1, kCrc.size()) != kCrc)
        return fail("no checksum line (the file was cut short)");
    const std::string_view crcText = body.substr(lastNl + 1 + kCrc.size());
    const std::string_view covered = text.substr(0, lastNl + 1);
    char expect[16];
    std::snprintf(expect, sizeof(expect), "%08x", crc32(covered.data(), covered.size()));
    if (crcText != expect) return fail("checksum mismatch (the file is damaged)");

    size_t pos = 0;
    bool   first = true;
    while (pos < covered.size()) {
        size_t end = covered.find('\n', pos);
        if (end == std::string_view::npos) end = covered.size();
        const std::string_view line = covered.substr(pos, end - pos);
        pos = end + 1;
        if (first) {
            first = false;
            int64_t v = 0;
            if (!toInt64(line.substr(kHeader.size()), v) || v < 1) return fail("bad header '" + std::string(line) + "'");
            if (v > SaveData::kFormatVersion)
                return fail("made by a newer engine (format v" + std::to_string(v) + ", this build reads up to v"
                            + std::to_string(SaveData::kFormatVersion) + ")");
            continue;
        }
        if (line.empty()) continue;
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) return fail("line without '=': '" + std::string(line.substr(0, 60)) + "'");
        const std::string_view key = line.substr(0, eq);
        std::string value;
        if (!unescape(line.substr(eq + 1), value)) return fail("bad escape in '" + std::string(key) + "'");
        if (key == "@dataVersion") {
            int64_t v = 0;
            if (!toInt64(value, v)) return fail("bad @dataVersion '" + value + "'");
            out.dataVersion = (int)v;
        } else if (key == "@title") {
            out.title = std::move(value);
        } else if (key == "@savedAt") {
            if (!toInt64(value, out.savedAt)) return fail("bad @savedAt '" + value + "'");
        } else if (!key.empty() && key[0] == '@') {
            // 같은 형식 버전 안에서 늘어난 메타 줄은 건너뜁니다.
        } else if (!out.setString(key, value)) {
            return fail("bad name '" + std::string(key.substr(0, 60)) + "'");
        }
    }
    return true;
}

} // namespace hsave

// ── SaveStore ───────────────────────────────────────────────────────────────────

bool SaveStore::validSlotName(std::string_view slot) {
    if (slot.empty() || slot.size() > 64) return false;
    for (char c : slot)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}

fs::path SaveStore::pathOf(std::string_view slot) const { return folder_ / (std::string(slot) + ".hsave"); }

bool SaveStore::save(std::string_view slot, SaveData& data, std::string* why) {
    if (!validSlotName(slot)) {
        if (why) *why = "bad slot name '" + std::string(slot) + "' (use a-z, 0-9, _ and -, up to 64)";
        return false;
    }
    std::error_code ec;
    fs::create_directories(folder_, ec);
    data.savedAt = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const fs::path main = pathOf(slot), bak = withSuffix(main, ".bak"), tmp = withSuffix(main, ".tmp");
    if (!writeDurable(tmp, hsave::serialize(data), why)) return false;
    // 온전한 이전 저장본만 .bak 으로 옮깁니다. 손상된 본 파일로 좋은 .bak 을 덮지 않습니다.
    SaveData prev;
    std::string prevWhy;
    if (readSlotFile(main, prev, prevWhy)) {
        fs::rename(main, bak, ec);
        if (ec) {
            if (why) *why = "keeping the previous save as " + bak.generic_string() + " failed: " + ec.message();
            fs::remove(tmp, ec);
            return false;
        }
    }
    fs::rename(tmp, main, ec);
    if (ec) {
        if (why) *why = "replacing " + main.generic_string() + " failed: " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

SaveStore::LoadResult SaveStore::load(std::string_view slot, SaveData& out) const {
    LoadResult r;
    out.clear();
    if (!validSlotName(slot)) { r.why = "bad slot name '" + std::string(slot) + "'"; return r; }
    const fs::path main = pathOf(slot), bak = withSuffix(main, ".bak");
    std::string mainWhy, bakWhy;
    if (readSlotFile(main, out, mainWhy)) { r.ok = true; return r; }
    if (readSlotFile(bak, out, bakWhy)) {
        r.ok = r.fromBackup = true;
        r.why = mainWhy == "missing" ? "the save file is missing; read the backup" : "the save file is unreadable (" + mainWhy + "); read the backup";
        return r;
    }
    if (mainWhy == "missing" && bakWhy == "missing") r.why = "no save in slot '" + std::string(slot) + "'";
    else r.why = "save: " + mainWhy + "; backup: " + bakWhy;
    return r;
}

bool SaveStore::exists(std::string_view slot) const {
    if (!validSlotName(slot)) return false;
    std::error_code ec;
    const fs::path main = pathOf(slot);
    return fs::exists(main, ec) || fs::exists(withSuffix(main, ".bak"), ec);
}

bool SaveStore::remove(std::string_view slot, std::string* why) {
    if (!validSlotName(slot)) {
        if (why) *why = "bad slot name '" + std::string(slot) + "'";
        return false;
    }
    const fs::path main = pathOf(slot);
    bool ok = true;
    for (const fs::path& p : { main, withSuffix(main, ".bak"), withSuffix(main, ".tmp") }) {
        std::error_code ec;
        fs::remove(p, ec);
        if (ec) {
            ok = false;
            if (why) *why = "removing " + p.generic_string() + " failed: " + ec.message();
        }
    }
    return ok;
}

std::vector<SaveStore::SlotInfo> SaveStore::list() const {
    std::vector<std::string> names;
    std::error_code ec;
    for (fs::directory_iterator it(folder_, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string n = it->path().filename().string();
        for (const char* suf : { ".hsave.bak", ".hsave" }) {
            const size_t len = std::char_traits<char>::length(suf);
            if (n.size() > len && n.compare(n.size() - len, len, suf) == 0) {
                n.resize(n.size() - len);
                if (validSlotName(n)) names.push_back(n);
                break;
            }
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    std::vector<SlotInfo> out;
    for (const std::string& n : names) {
        SaveData d;
        const LoadResult r = load(n, d);
        if (!r.ok) continue;
        out.push_back({ n, d.title, d.dataVersion, d.savedAt, r.fromBackup });
    }
    return out;
}
