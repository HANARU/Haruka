#pragma once
// 자막 표시(2026-11-21, Docs/Accessibility.md D4).
//
// 자막 큐(core/Subtitles)의 보이는 줄을 3D UI 패널 경로(UIPanelPass)로 그립니다. 액터가 아니라 엔진이 가진 월드 서비스라 씬 파일 ·
// 아웃라이너에 나오지 않고, 추출(RenderExtract)이 visible() 일 때 패널 목록 끝에 더합니다. 자리는 엔진이 프레임마다 place 로 정합니다.
//   XR(머리 고정): 머리 앞 kXrDistance, 시선 아래 kXrDownDeg, 폭 kXrWidth. 판 앞면이 머리를 향함.
//   데스크톱(화면 아래): 카메라 앞 kDeskDistance, 화면 폭의 kDeskWidthFrac, 아래 여백 kDeskBottomFrac. 글자 높이는 화면 높이 기준.
// 판은 배경이 투명하고 줄마다 반투명 배경이 있습니다. 판 높이는 최대 줄 수 × 두 줄(긴 글 줄바꿈) 기준이라 보이는 줄 수가 바뀌어도
// 텍스처 크기는 그대로입니다(글자 크기 · 배율 · 화면 비율이 바뀔 때만 바뀜).

#include "../core/Accessibility.hpp"
#include "../renderer/RenderSceneInput.hpp"
#include "../ui/slate/HSlateContext.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

class SubtitleQueue;

class SubtitlePanel : public IRenderPanelSource {
public:
    static constexpr float kBaseFontPx     = 40.f;    // 배치 픽셀(배율 1)
    static constexpr float kMaxFontScale   = 4.f;     // 크기 × 글자 배율 상한
    static constexpr int   kRowsPerLine    = 2;       // 줄 하나가 줄바꿈으로 차지할 수 있는 행 수
    static constexpr float kXrDistance     = 1.2f;    // m
    static constexpr float kXrDownDeg      = 15.f;
    static constexpr float kXrWidth        = 0.9f;    // m
    static constexpr float kXrLayoutWidth  = 1200.f;  // 배치 픽셀(글자 40 px = 3 cm, 1.2 m 에서 약 1.4°, 한 행 약 60 자)
    static constexpr float kDeskDistance   = 0.5f;    // m(카메라 근평면보다 멀게)
    static constexpr float kDeskWidthFrac  = 0.8f;
    static constexpr float kDeskBottomFrac = 0.06f;
    static constexpr float kDeskFontFrac   = 0.045f;  // 배율 1 의 글자 높이 ÷ 화면 높이
    static constexpr uint32_t kMaxSide     = 4096;

    // 배치 픽셀 기준 판 크기. layoutWidth 는 폭, fontPx 는 글자 크기, maxLines 는 최대 줄 수입니다.
    static glm::vec2 layoutSize(float layoutWidth, float fontPx, int maxLines);
    float fontPx() const { return fontPx_; }

    struct Placement {
        glm::vec3 position{ 0.f };
        glm::quat rotation{ 1.f, 0.f, 0.f, 0.f };
        float     metersPerPixel = 0.001f;   // 배치 픽셀 하나의 월드 크기
        float     layoutWidth    = kXrLayoutWidth;
    };
    // 머리 고정(XR). 판 가운데 자리와 회전(앞면 +Z 가 머리를 향함).
    static Placement placeXr(const glm::vec3& headPos, const glm::quat& headRot);
    // 화면 아래(데스크톱). fovYDeg · aspect 는 화면 카메라 값. 판 높이(배치 픽셀)를 알아야 아래 여백에 맞출 수 있습니다.
    static Placement placeDesktop(const glm::vec3& camPos, const glm::quat& camRot, float fovYDeg, float aspect,
                                  float fontScale, int maxLines);

    // 엔진이 프레임마다: 설정 · 큐를 보고 트리를 다시 만들지 정하고(줄 · 설정이 바뀌었을 때만), 자리를 정합니다.
    void update(const SubtitleQueue& queue, const AccessSettings& access);
    void place(const glm::vec3& headPos, const glm::quat& headRot, bool xr, float fovYDeg, float aspect);

    // 추출이 읽는 값.
    bool       visible() const { return visible_; }
    // 판 월드 행렬(자리 × 회전 × 크기). 헤더에 두어 추출만 링크하는 시험이 이 파일을 더하지 않아도 됩니다.
    glm::mat4  model() const {
        const float mpp = placement_.metersPerPixel;
        return glm::translate(glm::mat4(1.f), placement_.position) * glm::mat4_cast(placement_.rotation)
             * glm::scale(glm::mat4(1.f), glm::vec3(layout_.x * mpp, layout_.y * mpp, 1.f));
    }
    glm::uvec2 textureSize() const { return texSize_; }
    // 지금 트리에 들어간 줄(시험용, 줄바꿈 뒤 행).
    const std::vector<std::string>& rows() const { return rows_; }

    HSlateContext& slate() override;
    bool takeDirty() override { const bool d = dirty_; dirty_ = false; return d; }
    void markDirty() override { dirty_ = true; }

private:
    void rebuild(const SubtitleQueue& queue);

    HSlateContext slate_;
    bool          visible_  = false;
    bool          dirty_    = true;
    bool          needBuild_ = true;
    uint64_t      builtRevision_ = ~0ull;
    AccessSettings builtAccess_;
    float         builtLayoutWidth_ = 0.f;
    int           maxLines_   = 2;
    float         fontPx_     = kBaseFontPx;
    Placement     placement_;
    glm::vec2     layout_{ kXrLayoutWidth, 100.f };
    glm::uvec2    texSize_{ 1600, 100 };
    std::vector<std::string> rows_;
    const SubtitleQueue* queue_ = nullptr;
    AccessSettings access_;
};
