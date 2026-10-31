#include "Vfs.hpp"
#include "PakFormat.hpp"
#include "PakReader.hpp"
#include "PathUtf8.hpp"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

namespace hvfs {

namespace {

struct Mount {
    int                               priority = 0;
    uint64_t                          order    = 0;   // 같은 우선순위에서 나중에 마운트한 쪽이 이김
    std::string                       name;
    // 낱개 폴더
    fs::path                          root;
    std::string                       prefixKey;      // 찾기 키 기준 접두어("assets/"), 비면 모두
    // 묶음
    std::unique_ptr<hpak::PakReader>  pak;
};

struct Table {
    std::shared_mutex                   mutex;
    std::vector<std::unique_ptr<Mount>> mounts;   // 우선순위 높은 순으로 정렬
    uint64_t                            nextOrder = 0;
};

Table& table() {
    static Table t;
    return t;
}

void addMount(std::unique_ptr<Mount> m) {
    Table& t = table();
    std::unique_lock lock(t.mutex);
    m->order = t.nextOrder++;
    t.mounts.push_back(std::move(m));
    std::stable_sort(t.mounts.begin(), t.mounts.end(), [](const auto& a, const auto& b) {
        return a->priority != b->priority ? a->priority > b->priority : a->order > b->order;
    });
}

// 찾은 곳: 낱개 파일이면 diskFile, 묶음이면 pak + index.
struct Hit {
    fs::path                diskFile;
    const hpak::PakReader*  pak   = nullptr;
    int                     index = -1;
    bool found() const { return pak || !diskFile.empty(); }
};

// 공유 잠금을 쥔 상태에서 부릅니다. 경로는 UTF-8 로 바꾸고(DEF-123), 올바르지 않은 UTF-8 이면 찾지 못한 것으로 둡니다.
// 예전에는 std::string 으로 fs::path 를 만들어, 올바르지 않은 경로가 std::system_error 를 던졌습니다.
Hit lookupLocked(const Table& t, std::string_view path) {
    Hit hit;
    fs::path direct;
    if (!hpak::pathFromUtf8(path, direct)) return hit;
    if (direct.is_absolute()) {
        std::error_code ec;
        if (fs::is_regular_file(direct, ec)) hit.diskFile = std::move(direct);
        return hit;
    }
    std::string norm;
    if (!hpak::normalizePath(path, norm)) return hit;
    const std::string key = hpak::foldKey(norm);
    fs::path rel;
    if (!hpak::pathFromUtf8(norm, rel)) return hit;
    for (const auto& m : t.mounts) {
        if (m->pak) {
            const int i = m->pak->find(norm);
            if (i >= 0) { hit.pak = m->pak.get(); hit.index = i; return hit; }
            continue;
        }
        if (!m->prefixKey.empty() && key.compare(0, m->prefixKey.size(), m->prefixKey) != 0) continue;
        std::error_code ec;
        const fs::path p = m->root / rel;
        if (fs::is_regular_file(p, ec)) { hit.diskFile = p; return hit; }
    }
    return hit;
}

void setError(std::string* err, std::string_view path, const char* why) noexcept {
    if (!err) return;
    try { *err = std::string(path) + ": " + why; } catch (...) {}
}

// 공개 읽기 함수의 예외 경계(DEF-123). 경로 변환은 던지지 않지만 버퍼 · 문자열 할당과 잠금은 던질 수 있으므로, 모두 받아
// 실패로 돌려줍니다. 소리 콜백처럼 C 코드를 거쳐 불리는 곳에서도 예외가 경계를 넘지 않습니다.
template <class F>
bool guarded(std::string_view path, std::string* err, F&& body) noexcept {
    try {
        return body();
    } catch (const std::bad_alloc&) {
        setError(err, path, "not enough memory to read the file");
    } catch (const std::length_error&) {
        setError(err, path, "not enough memory to read the file");
    } catch (const std::exception& e) {
        setError(err, path, e.what());
    }
    return false;
}

bool readDisk(const fs::path& p, std::vector<uint8_t>& out, std::string* err) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) { if (err) *err = "cannot open " + hpak::utf8Of(p); return false; }
    const std::streamoff n = f.tellg();
    if (n < 0) { if (err) *err = "cannot read " + hpak::utf8Of(p); return false; }
    out.resize((size_t)n);
    f.seekg(0);
    if (n > 0 && !f.read(reinterpret_cast<char*>(out.data()), n)) { if (err) *err = "cannot read " + hpak::utf8Of(p); return false; }
    return true;
}

} // namespace

void reset() {
    Table& t = table();
    std::unique_lock lock(t.mutex);
    t.mounts.clear();
    t.nextOrder = 0;
}

void mountDirectory(const fs::path& root, const std::string& prefix, int priority) {
    auto m = std::make_unique<Mount>();
    m->priority  = priority;
    m->root      = root;
    m->prefixKey = hpak::foldKey(prefix);
    m->name      = hpak::utf8Of(root) + (prefix.empty() ? "" : " (" + prefix + ")");
    addMount(std::move(m));
}

// 마운트 이름은 전체 경로입니다(DEF-149). 파일 이름만 적으면 묶음을 찾는 폴더가 둘 이상인 Android 에서 어느 폴더의 묶음을
// 썼는지 로그로 알 수 없었습니다.
bool mountPack(const fs::path& file, int priority, std::string& err) {
    auto src = hpak::openFileSource(file, err);
    if (!src) return false;
    auto reader = std::make_unique<hpak::PakReader>();
    if (!reader->open(std::move(src), err)) return false;
    return mountPack(std::move(reader), hpak::utf8Of(file), priority);
}

bool mountPack(std::unique_ptr<hpak::PakReader> reader, const std::string& name, int priority) {
    if (!reader) return false;
    auto m = std::make_unique<Mount>();
    m->priority = priority;
    m->name     = name + " (" + std::to_string(reader->count()) + " files)";
    m->pak      = std::move(reader);
    addMount(std::move(m));
    return true;
}

MountReport mountGamePacks(const fs::path& dir, int basePriority) {
    MountReport rep;
    std::error_code ec;
    std::vector<std::string> dlc;
    std::vector<std::pair<long long, std::string>> patches;
    bool base = false;
    std::string baseName;   // 실제 표기(대소문자를 가리는 파일 시스템에서도 열리게)
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string name = hpak::utf8Of(e.path().filename());
        if (name.empty()) continue;   // UTF-8 로 나타낼 수 없는 이름은 규칙에 맞는 묶음일 수 없음
        const std::string key  = hpak::foldKey(name);
        if (key.size() < 5 || key.compare(key.size() - 5, 5, ".hpak") != 0) continue;
        const std::string stem = key.substr(0, key.size() - 5);
        if (stem == "game") { base = true; baseName = name; continue; }
        if (stem.rfind("dlc_", 0) == 0) { dlc.push_back(name); continue; }
        if (stem.rfind("game_p", 0) == 0 && stem.size() > 6 &&
            std::all_of(stem.begin() + 6, stem.end(), [](char c) { return c >= '0' && c <= '9'; }) && stem.size() <= 6 + 9) {
            patches.emplace_back(std::stoll(stem.substr(6)), name);
            continue;
        }
        rep.failed.push_back(name + ": name does not follow game.hpak / dlc_*.hpak / game_p<N>.hpak; not mounted");
    }
    auto mountOne = [&](const std::string& name, int prio) {
        std::string err;
        fs::path rel;
        if (hpak::pathFromUtf8(name, rel) && mountPack(dir / rel, prio, err)) rep.mounted.push_back(name);
        else rep.failed.push_back(name + ": " + err);
    };
    if (base) mountOne(baseName, basePriority);
    std::sort(dlc.begin(), dlc.end());
    for (const auto& n : dlc) mountOne(n, basePriority + 1);
    std::sort(patches.begin(), patches.end());
    for (const auto& [num, n] : patches) mountOne(n, basePriority + 2 + (int)std::min<long long>(num, kMaxPatchNumber));
    return rep;
}

bool hasPackFiles(const fs::path& dir) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string key = hpak::foldKey(hpak::utf8Of(e.path().filename()));
        if (key.size() >= 5 && key.compare(key.size() - 5, 5, ".hpak") == 0) return true;
    }
    return false;
}

bool exists(std::string_view path) {
    return guarded(path, nullptr, [&] {
        Table& t = table();
        std::shared_lock lock(t.mutex);
        return lookupLocked(t, path).found();
    });
}

bool stat(std::string_view path, Stat& out) {
    out = Stat{};
    return guarded(path, nullptr, [&] {
        Table& t = table();
        std::shared_lock lock(t.mutex);
        const Hit hit = lookupLocked(t, path);
        if (hit.pak) {
            const hpak::Entry& e = hit.pak->entry(hit.index);
            out.size = e.size; out.inPack = true; out.contentHash = e.contentHash;
            return true;
        }
        if (hit.diskFile.empty()) return false;
        std::error_code ec;
        out.size = (uint64_t)fs::file_size(hit.diskFile, ec);
        if (ec) return false;
        const auto ft = fs::last_write_time(hit.diskFile, ec);
        out.mtime = ec ? 0 : (uint64_t)ft.time_since_epoch().count();
        return true;
    });
}

// 읽는 동안 공유 잠금을 쥡니다. 여러 스레드가 함께 읽을 수 있고, 마운트 표를 바꾸는 쪽(reset · mount)만 기다립니다.
bool readAll(std::string_view path, std::vector<uint8_t>& out, std::string* err) {
    return guarded(path, err, [&] {
        Table& t = table();
        std::shared_lock lock(t.mutex);
        const Hit hit = lookupLocked(t, path);
        if (hit.pak) {
            std::string e;
            if (hit.pak->readAll(hit.index, out, e)) return true;
            if (err) *err = std::string(path) + ": " + e;
            return false;
        }
        if (hit.diskFile.empty()) { if (err) *err = std::string(path) + ": not found"; return false; }
        return readDisk(hit.diskFile, out, err);
    });
}

// 글로 바꿀 때 한 번 더 복사하므로, 그 할당도 경계 안에 둡니다(2026-10-31 검토).
bool readText(std::string_view path, std::string& out, std::string* err) {
    return guarded(path, err, [&] {
        std::vector<uint8_t> bytes;
        if (!readAll(path, bytes, err)) return false;
        out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return true;
    });
}

bool contentHash(std::string_view path, uint64_t& out) {
    return guarded(path, nullptr, [&] {
        Table& t = table();
        std::shared_lock lock(t.mutex);
        const Hit hit = lookupLocked(t, path);
        if (hit.pak) { out = hit.pak->entry(hit.index).contentHash; return true; }
        if (hit.diskFile.empty()) return false;
        std::ifstream f(hit.diskFile, std::ios::binary);
        if (!f) return false;
        hpak::Hasher h;
        std::vector<char> buf(1 << 16);
        while (f) {
            f.read(buf.data(), (std::streamsize)buf.size());
            const std::streamsize n = f.gcount();
            if (n > 0) h.update(buf.data(), (size_t)n);
        }
        out = h.digest();
        return true;
    });
}

fs::path diskPath(std::string_view path) {
    try {
        Table& t = table();
        std::shared_lock lock(t.mutex);
        return lookupLocked(t, path).diskFile;
    } catch (const std::exception&) {
        return {};
    }
}

std::vector<std::string> describe() {
    Table& t = table();
    std::shared_lock lock(t.mutex);
    std::vector<std::string> out;
    for (const auto& m : t.mounts)
        out.push_back((m->pak ? "pack " : "folder ") + m->name + " [priority " + std::to_string(m->priority) + "]");
    return out;
}

} // namespace hvfs
