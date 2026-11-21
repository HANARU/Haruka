#pragma once
// 콘텐츠 브라우저의 자료(2026-11-21, UE 의 Content Browser 방식). 프로젝트 Content 폴더를 디스크 그대로 폴더 트리로 두고,
// 파일은 확장자로 종류를 정합니다. 화면 · 파일 시스템과 무관해 화면 없이 검산합니다(scene_components 테스트).
//  - 지금 폴더를 볼 때: 하위 폴더 먼저(이름 순), 그다음 파일(이름 순).
//  - 검색어나 종류 거르기가 있으면: 지금 폴더 아래 모든 파일에서 찾고(하위 폴더 포함), 폴더는 보이지 않습니다(UE 와 같음).
// 경로는 정식 논리 경로(/Game/...)이고 이름은 UTF-8 그대로입니다.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace hcb {

inline constexpr const char* kRoot = "/Game";

enum class AssetKind : uint8_t { Folder, Scene, Model, Audio, Cue, Prefab, Nav, Texture, Other };

// 거르기 단추 순서(Folder · Other 는 거르기 대상이 아님).
inline const std::vector<AssetKind>& filterKinds() {
    static const std::vector<AssetKind> k = { AssetKind::Scene, AssetKind::Model, AssetKind::Prefab, AssetKind::Audio,
                                              AssetKind::Cue, AssetKind::Nav, AssetKind::Texture };
    return k;
}
inline uint32_t kindBit(AssetKind k) { return 1u << (uint32_t)k; }

inline const char* kindName(AssetKind k) {
    switch (k) {
        case AssetKind::Folder:  return "Folder";
        case AssetKind::Scene:   return "Scene";
        case AssetKind::Model:   return "Model";
        case AssetKind::Audio:   return "Sound";
        case AssetKind::Cue:     return "Sound Cue";
        case AssetKind::Prefab:  return "Prefab";
        case AssetKind::Nav:     return "Nav Data";
        case AssetKind::Texture: return "Texture";
        default:                 return "File";
    }
}

inline std::string lowerAscii(std::string_view s) {
    std::string o(s);
    for (char& c : o) c = (char)std::tolower((unsigned char)c);
    return o;
}

// 이름의 마지막 '.' 뒤(소문자, 점 포함). 없으면 "".
inline std::string extOf(std::string_view name) {
    const size_t slash = name.find_last_of('/');
    const size_t dot   = name.find_last_of('.');
    const size_t nameStart = slash == std::string_view::npos ? 0 : slash + 1;
    if (dot == std::string_view::npos || dot <= nameStart) return {};   // 점이 없거나 이름 첫 글자(.hidden)
    return lowerAscii(name.substr(dot));
}

inline AssetKind kindOfExt(std::string_view ext) {
    if (ext == ".hscene") return AssetKind::Scene;
    if (ext == ".glb" || ext == ".gltf") return AssetKind::Model;
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") return AssetKind::Audio;
    if (ext == ".hcue") return AssetKind::Cue;
    if (ext == ".hprefab") return AssetKind::Prefab;
    if (ext == ".hnav") return AssetKind::Nav;
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".ktx2" || ext == ".tga" || ext == ".bmp") return AssetKind::Texture;
    return AssetKind::Other;
}

// 목록에 넣지 않는 파일: 숨김(. 으로 시작) · 쓰는 도중의 임시 파일(.tmp).
inline bool hiddenName(std::string_view name) {
    return name.empty() || name[0] == '.' || extOf(name) == ".tmp";
}

inline std::string nameOf(std::string_view path) {
    const size_t slash = path.find_last_of('/');
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}
inline std::string parentOf(std::string_view path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string_view::npos || slash == 0 ? std::string(kRoot) : std::string(path.substr(0, slash));
}
// 확장자를 뺀 이름(보이는 이름).
inline std::string stemOf(std::string_view path) {
    std::string n = nameOf(path);
    const std::string e = extOf(n);
    if (!e.empty()) n.resize(n.size() - e.size());
    return n;
}

struct Item {
    std::string path;    // /Game/models/model.glb · 폴더는 /Game/models
    std::string name;    // 보이는 이름(파일은 확장자 없이)
    AssetKind   kind = AssetKind::Other;
};

class ContentIndex {
public:
    // Content 아래 상대 경로('/' 구분)로 짓습니다. 폴더 목록에는 빈 폴더도 넣을 수 있습니다(파일의 상위 폴더는 저절로 생김).
    void build(const std::vector<std::string>& relFiles, const std::vector<std::string>& relDirs) {
        folders_.clear();
        files_.clear();
        folders_[kRoot];
        auto addFolder = [&](const std::string& rel) {
            std::string cur = kRoot;
            size_t pos = 0;
            while (pos < rel.size()) {
                const size_t slash = rel.find('/', pos);
                const std::string part = rel.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
                if (part.empty() || hiddenName(part)) return false;
                const std::string next = cur + "/" + part;
                folders_[cur].insert(next);
                folders_[next];
                cur = next;
                if (slash == std::string::npos) break;
                pos = slash + 1;
            }
            return true;
        };
        for (const auto& d : relDirs)
            if (!d.empty()) addFolder(d);
        for (const auto& f : relFiles) {
            const std::string name = nameOf(f);
            if (hiddenName(name)) continue;
            const size_t slash = f.find_last_of('/');
            if (slash != std::string::npos && !addFolder(f.substr(0, slash))) continue;   // 숨은 폴더 안
            Item it;
            it.path = std::string(kRoot) + "/" + f;
            it.kind = kindOfExt(extOf(name));
            it.name = stemOf(name);
            files_.push_back(std::move(it));
        }
        std::sort(files_.begin(), files_.end(), [](const Item& a, const Item& b) { return a.path < b.path; });
    }

    bool hasFolder(const std::string& path) const { return folders_.count(path) != 0; }
    // 바로 아래 폴더(이름 순, 대소문자 무시).
    std::vector<std::string> childFolders(const std::string& path) const {
        std::vector<std::string> out;
        auto it = folders_.find(path);
        if (it == folders_.end()) return out;
        out.assign(it->second.begin(), it->second.end());
        std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return lowerAscii(a) < lowerAscii(b); });
        return out;
    }
    const std::vector<Item>& files() const { return files_; }
    const Item* find(const std::string& path) const {
        for (const auto& f : files_) if (f.path == path) return &f;
        return nullptr;
    }
    // 이 종류의 파일 경로(이름 순).
    std::vector<std::string> pathsOfKind(AssetKind k) const {
        std::vector<std::string> out;
        for (const auto& f : files_) if (f.kind == k) out.push_back(f.path);
        return out;
    }

    // 지금 폴더에서 보이는 항목. filterMask 0 = 거르지 않음.
    std::vector<Item> view(const std::string& folder, std::string_view search, uint32_t filterMask) const {
        std::vector<Item> out;
        const std::string q = lowerAscii(search);
        const bool recursive = !q.empty() || filterMask != 0;
        const std::string prefix = folder + "/";
        if (!recursive) {
            for (const auto& c : childFolders(folder)) {
                Item it;
                it.path = c;
                it.name = nameOf(c);
                it.kind = AssetKind::Folder;
                out.push_back(std::move(it));
            }
        }
        std::vector<Item> files;
        for (const auto& f : files_) {
            if (f.path.compare(0, prefix.size(), prefix) != 0) continue;
            if (!recursive && f.path.find('/', prefix.size()) != std::string::npos) continue;   // 하위 폴더의 것
            if (filterMask && !(filterMask & kindBit(f.kind))) continue;
            if (!q.empty() && lowerAscii(nameOf(f.path)).find(q) == std::string::npos) continue;
            files.push_back(f);
        }
        std::sort(files.begin(), files.end(), [](const Item& a, const Item& b) {
            const std::string la = lowerAscii(a.name), lb = lowerAscii(b.name);
            return la != lb ? la < lb : a.path < b.path;
        });
        for (auto& f : files) out.push_back(std::move(f));
        return out;
    }

private:
    std::map<std::string, std::set<std::string>> folders_;   // 폴더 → 바로 아래 폴더
    std::vector<Item> files_;
};

} // namespace hcb
