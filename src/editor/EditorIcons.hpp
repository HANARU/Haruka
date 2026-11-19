#pragma once
#include "../ui/slate/HIconSource.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct NSVGimage;
struct NSVGrasterizer;

// ── EditorIcons ───────────────────────────────────────────────────────────────
// 에디터 아이콘(D3, 2026-11-19). /Engine/Editor/Icons/<이름>.svg(Lucide, ISC)를 읽어 두고, 글꼴 아틀라스가 크기마다 요청하면
// nanosvg 로 래스터화합니다(4배로 그려 줄임). 이름은 Lucide 파일 이름 그대로입니다(예: "play", "eye-off").
class EditorIcons : public HIconSource {
public:
    // 에디터가 쓰는 아이콘 이름 목록. load() 가 이것을 모두 읽습니다.
    static const std::vector<const char*>& names();

    EditorIcons();
    ~EditorIcons() override;
    EditorIcons(const EditorIcons&)            = delete;
    EditorIcons& operator=(const EditorIcons&) = delete;

    // 가상 파일 시스템에서 읽습니다. 하나도 못 읽으면 거짓(경고 한 줄). 일부만 없으면 그 이름만 글자로 돌아갑니다.
    bool load(const std::string& folder = "/Engine/Editor/Icons/");
    // SVG 원문 하나를 등록합니다(시험 · load 공용). 읽지 못하면 거짓입니다.
    bool addSvg(std::string_view name, std::string_view svgText);
    size_t count() const { return images_.size(); }

    bool has(std::string_view name) const override;
    bool rasterize(std::string_view name, int px, std::vector<uint8_t>& alpha) const override;

private:
    struct ImageDeleter { void operator()(NSVGimage* p) const; };
    struct RastDeleter  { void operator()(NSVGrasterizer* p) const; };
    std::unordered_map<std::string, std::unique_ptr<NSVGimage, ImageDeleter>> images_;
    std::unique_ptr<NSVGrasterizer, RastDeleter> rast_;
};
