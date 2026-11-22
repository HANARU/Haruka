#pragma once
// ── 에셋 고르기 목록 (H3b, 2026-11-22) ──────────────────────────────────────────────────────────────────────────
// 디테일 패널 에셋 경로 칸(PropDesc::AssetPath)의 고르기 팝업이 보이는 목록을 계산합니다(UE 의 에셋 고르기 칸 축소판). 화면 없이
// 시험합니다(tests/EditorCommandTest.cpp). 팝업 위젯 · 키 · 마우스는 AssetPicker 가 맡습니다.
//   - 종류: PropDesc::assetType 은 콘텐츠 브라우저 종류 이름(hcb::kindName)을 쉼표로 이은 글입니다("Sound,Sound Cue"). 비면 모든
//     에셋 종류(폴더 · 기타 제외).
//   - 검색: 공백으로 나눈 낱말이 모두 이름(확장자 없는 파일 이름)에 들어가야 합니다(ASCII 대소문자 무시, UE 와 같이 폴더 경로는 보지 않음).
//   - 순서: 이름(대소문자 무시) → 경로.
#include "ContentBrowserModel.hpp"

#include <string>
#include <vector>

namespace hasset {

// 종류 이름 목록 → 종류. 모르는 이름은 버립니다. 결과가 비면 "모든 종류"입니다.
inline std::vector<hcb::AssetKind> kindsOf(const std::string& assetType) {
    std::vector<hcb::AssetKind> out;
    size_t pos = 0;
    while (pos <= assetType.size()) {
        size_t comma = assetType.find(',', pos);
        if (comma == std::string::npos) comma = assetType.size();
        std::string name = assetType.substr(pos, comma - pos);
        while (!name.empty() && name.front() == ' ') name.erase(name.begin());
        while (!name.empty() && name.back() == ' ') name.pop_back();
        for (hcb::AssetKind k : hcb::filterKinds())
            if (!name.empty() && name == hcb::kindName(k)) out.push_back(k);
        pos = comma + 1;
    }
    return out;
}

inline bool accepts(const std::vector<hcb::AssetKind>& kinds, hcb::AssetKind k) {
    if (k == hcb::AssetKind::Folder || k == hcb::AssetKind::Other) return false;
    if (kinds.empty()) return true;
    for (hcb::AssetKind x : kinds) if (x == k) return true;
    return false;
}

// 경로 하나가 그 종류인지(확장자로 봄). 빈 경로는 거짓.
inline bool acceptsPath(const std::vector<hcb::AssetKind>& kinds, const std::string& path) {
    if (path.empty()) return false;
    return accepts(kinds, hcb::kindOfExt(hcb::extOf(hcb::nameOf(path))));
}

inline std::vector<std::string> queryTerms(const std::string& query) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : query) {
        if (c == ' ' || c == '\t') { if (!cur.empty()) out.push_back(hcb::lowerAscii(cur)); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(hcb::lowerAscii(cur));
    return out;
}

inline bool matches(const hcb::Item& it, const std::vector<std::string>& terms) {
    if (terms.empty()) return true;
    const std::string name = hcb::lowerAscii(it.name);
    for (const std::string& t : terms)
        if (name.find(t) == std::string::npos) return false;
    return true;
}

inline std::vector<hcb::Item> buildList(const hcb::ContentIndex& index, const std::vector<hcb::AssetKind>& kinds, const std::string& query) {
    std::vector<hcb::Item> out;
    const std::vector<std::string> terms = queryTerms(query);
    for (const hcb::Item& f : index.files())
        if (accepts(kinds, f.kind) && matches(f, terms)) out.push_back(f);
    std::sort(out.begin(), out.end(), [](const hcb::Item& a, const hcb::Item& b) {
        const std::string la = hcb::lowerAscii(a.name), lb = hcb::lowerAscii(b.name);
        return la != lb ? la < lb : a.path < b.path;
    });
    return out;
}

inline int indexOf(const std::vector<hcb::Item>& list, const std::string& path) {
    for (int i = 0; i < (int)list.size(); ++i) if (list[(size_t)i].path == path) return i;
    return -1;
}

// ↑ ↓: 끝에서 멈춤(목록이 비면 -1, 아직 고른 것이 없으면 처음 · 끝).
inline int step(int count, int cur, int dir) {
    if (count <= 0) return -1;
    if (cur < 0) return dir > 0 ? 0 : count - 1;
    return std::clamp(cur + dir, 0, count - 1);
}

// 칸에 보일 이름: 비면 빈 글(부르는 쪽이 None), 아니면 확장자 없는 파일 이름.
inline std::string displayName(const std::string& path) { return path.empty() ? std::string() : hcb::stemOf(path); }

} // namespace hasset
