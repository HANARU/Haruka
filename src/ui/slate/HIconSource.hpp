#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

// ── HIconSource ───────────────────────────────────────────────────────────────
// 이름으로 찾는 단색 아이콘의 공급자입니다(D3, 2026-11-19). 글꼴 아틀라스가 글리프처럼 크기마다 한 번 래스터화해
// 같은 아틀라스에 넣고, 그릴 때는 알파 × 색으로 칠합니다. SVG 를 읽는 구현은 에디터(editor/EditorIcons)에 있어,
// UI 모듈은 SVG 라이브러리를 모릅니다.
class HIconSource {
public:
    virtual ~HIconSource() = default;
    virtual bool has(std::string_view name) const = 0;
    // px × px 알파 비트맵(행 우선, 한 바이트씩)을 alpha 에 씁니다. 없는 이름이면 거짓입니다.
    virtual bool rasterize(std::string_view name, int px, std::vector<uint8_t>& alpha) const = 0;
};
