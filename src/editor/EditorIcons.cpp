#include "EditorIcons.hpp"
#include "../core/HLog.hpp"
#include "../vfs/Vfs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// nanosvg 는 헤더 하나에 구현이 들어 있어 이 파일에서만 켭니다. 제3자 코드라 경고는 끕니다.
#if defined(_MSC_VER)
// C4702(닿지 않는 코드)는 최적화 단계에서 함수 단위로 나와 push/pop 범위를 벗어나므로, 이 파일 전체에서 끕니다.
#pragma warning(disable: 4702)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static constinit HLogCategory LogEditor{ "Editor", ELogVerbosity::Log };

void EditorIcons::ImageDeleter::operator()(NSVGimage* p) const { if (p) nsvgDelete(p); }
void EditorIcons::RastDeleter::operator()(NSVGrasterizer* p) const { if (p) nsvgDeleteRasterizer(p); }

const std::vector<const char*>& EditorIcons::names() {
    static const std::vector<const char*> k = {
        "play", "pause", "square", "save", "folder-open", "refresh-cw",
        "mouse-pointer-2", "move", "rotate-cw", "scaling", "globe", "box", "magnet", "camera",
        "eye", "eye-off", "x", "chevron-down", "chevron-right", "search", "plus", "trash-2",
        "settings", "file", "sun", "volume-2", "layers", "check",
    };
    return k;
}

EditorIcons::EditorIcons() : rast_(nsvgCreateRasterizer()) {}
EditorIcons::~EditorIcons() = default;

bool EditorIcons::addSvg(std::string_view name, std::string_view svgText) {
    std::vector<char> buf(svgText.begin(), svgText.end());
    buf.push_back('\0');
    NSVGimage* img = nsvgParse(buf.data(), "px", 96.f);
    if (!img) return false;
    if (img->width <= 0.f || img->height <= 0.f || !img->shapes) { nsvgDelete(img); return false; }
    images_[std::string(name)].reset(img);
    return true;
}

bool EditorIcons::load(const std::string& folder) {
    std::string missing;
    size_t loaded = 0;
    for (const char* n : names()) {
        std::string text;
        if (hvfs::readText(folder + n + ".svg", text) && addSvg(n, text)) { ++loaded; continue; }
        if (!missing.empty()) missing += ", ";
        missing += n;
    }
    if (loaded == 0) {
        HLOG(LogEditor, Warning) << "Editor icons not found in " << folder << "; using text glyphs instead";
        return false;
    }
    if (!missing.empty())
        HLOG(LogEditor, Warning) << "Editor icons missing (text glyphs used): " << missing;
    HLOG(LogEditor, Log) << "Editor icons: " << loaded << " loaded from " << folder;
    return true;
}

bool EditorIcons::has(std::string_view name) const {
    return images_.find(std::string(name)) != images_.end();
}

bool EditorIcons::rasterize(std::string_view name, int px, std::vector<uint8_t>& alpha) const {
    auto it = images_.find(std::string(name));
    if (it == images_.end() || !rast_ || px <= 0 || px > 256) return false;
    NSVGimage* img = it->second.get();
    // 4배로 그린 뒤 4×4 평균으로 줄입니다(가는 선이 1px 아래로 내려가도 고르게 보임).
    constexpr int kSS = 4;
    const int big = px * kSS;
    std::vector<uint8_t> rgba((size_t)big * big * 4, 0);
    const float scale = (float)big / std::max(img->width, img->height);
    const float ox    = ((float)big - img->width * scale) * 0.5f;
    const float oy    = ((float)big - img->height * scale) * 0.5f;
    nsvgRasterize(rast_.get(), img, ox, oy, scale, rgba.data(), big, big, big * 4);
    alpha.assign((size_t)px * px, 0);
    for (int y = 0; y < px; ++y) {
        for (int x = 0; x < px; ++x) {
            int sum = 0;
            for (int sy = 0; sy < kSS; ++sy)
                for (int sx = 0; sx < kSS; ++sx)
                    sum += rgba[(((size_t)(y * kSS + sy) * big) + (x * kSS + sx)) * 4 + 3];
            alpha[(size_t)y * px + x] = (uint8_t)((sum + kSS * kSS / 2) / (kSS * kSS));
        }
    }
    return true;
}
