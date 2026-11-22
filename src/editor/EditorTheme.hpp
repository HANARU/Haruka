#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <cstdint>

// 에디터 색 토큰 정본(D1, Handoff/Plan_Editor_Design.md §3).
// 스왑체인(B8G8R8A8_SRGB)이 렌더 출력을 sRGB 로 인코딩하므로, 모든 색은 sRGB 16진수로 적고
// IEC 61966-2-1 EOTF 로 선형 값으로 바꿔 씁니다.
namespace EditorTheme {

namespace detail {
inline constexpr double kLn2 = 0.69314718055994530942;

constexpr double expSeries(double y) {
    int m = 0;
    while (y >  0.5 * kLn2) { y -= kLn2; ++m; }
    while (y < -0.5 * kLn2) { y += kLn2; --m; }
    double term = 1.0;
    double sum  = 1.0;
    for (int n = 1; n <= 16; ++n) {
        term *= y / (double)n;
        sum  += term;
    }
    while (m > 0) { sum *= 2.0; --m; }
    while (m < 0) { sum *= 0.5; ++m; }
    return sum;
}

constexpr double lnSeries(double x) {
    int k = 0;
    while (x < 0.5) { x *= 2.0; --k; }
    while (x > 1.0) { x *= 0.5; ++k; }
    const double u  = (x - 1.0) / (x + 1.0);
    const double u2 = u * u;
    double p   = u;
    double sum = 0.0;
    for (int n = 1; n <= 17; n += 2) {
        sum += p / (double)n;
        p   *= u2;
    }
    return 2.0 * sum + (double)k * kLn2;
}

constexpr float srgbByteToLinear(uint8_t byte) {
    const double c = (double)byte / 255.0;
    if (c <= 0.04045) return (float)(c / 12.92);
    return (float)expSeries(2.4 * lnSeries((c + 0.055) / 1.055));
}
} // namespace detail

// sRGB 24비트 16진수(0xRRGGBB) → 선형 RGBA.
constexpr glm::vec4 srgb(uint32_t rgbHex, float alpha = 1.f) {
    return {
        detail::srgbByteToLinear((uint8_t)((rgbHex >> 16) & 0xFFu)),
        detail::srgbByteToLinear((uint8_t)((rgbHex >>  8) & 0xFFu)),
        detail::srgbByteToLinear((uint8_t)( rgbHex        & 0xFFu)),
        alpha
    };
}

constexpr glm::vec4 withAlpha(glm::vec4 c, float alpha) {
    return { c.x, c.y, c.z, alpha };
}

// ── 핵심 역할 토큰 (Plan_Editor_Design.md §3) ─────────────────────────────────
inline constexpr glm::vec4 bgWindow      = srgb(0x151516);   // 메뉴 바 · 툴바 · 탭 줄 · 상태바
inline constexpr glm::vec4 bgPanel       = srgb(0x1E1E1F);   // 패널 바탕 · 활성 탭
inline constexpr glm::vec4 bgHeader      = srgb(0x262628);   // 섹션 머리 · 패널 머리 · 컴포넌트 머리
inline constexpr glm::vec4 bgInput       = srgb(0x111112);   // 입력 칸 · 검색 상자 · 모달 목록

inline constexpr glm::vec4 ctlNormal     = srgb(0x2B2B2E);   // 버튼 · 칩 보통
inline constexpr glm::vec4 ctlHover      = srgb(0x38383C);   // 버튼 · 칩 호버
inline constexpr glm::vec4 ctlPressed    = srgb(0x45454A);   // 버튼 · 칩 누름

inline constexpr glm::vec4 lineDivider   = srgb(0x303033);   // 1px 구분선 · 테두리 · 도크 분할선

inline constexpr glm::vec4 textPrimary   = srgb(0xE2E2E4);   // 본문 · 활성 탭
inline constexpr glm::vec4 textSecondary = srgb(0xA3A3A8);   // 보조 글자 · 비활성 탭 · 라벨
inline constexpr glm::vec4 textDisabled  = srgb(0x6A6A70);   // 비활성 · 숨김 · 플레이스홀더 · Verbose

inline constexpr glm::vec4 accent        = srgb(0x2D6BD6);   // 강조 · 포커스 · 활성 밑줄
inline constexpr glm::vec4 accentHover   = srgb(0x4A84E8);   // 강조 호버 · 수정 표지 · 활성 바
inline constexpr glm::vec4 selectRow     = srgb(0x24466E);   // 선택 행 · 활성 칩 · 현재 씬

inline constexpr glm::vec4 ok            = srgb(0x3FA463);   // 성공 · Play · Yes/On
inline constexpr glm::vec4 warn          = srgb(0xE0B34A);   // 경고 · Pause · 수치 리드아웃
inline constexpr glm::vec4 error         = srgb(0xE0525A);   // 오류 · Stop · 삭제

inline constexpr glm::vec4 axisX         = srgb(0xE0525A);   // X 축 라벨
inline constexpr glm::vec4 axisY         = srgb(0x6CBF5A);   // Y 축 라벨
inline constexpr glm::vec4 axisZ         = srgb(0x4D8DF0);   // Z 축 라벨

// ── 확장 역할 토큰 (상태 변형 · 팝업 · 오버레이 · 배지) ───────────────────────
inline constexpr glm::vec4 transparent   { 0.f, 0.f, 0.f, 0.f };
inline constexpr glm::vec4 textOnAccent  = srgb(0xFFFFFF);   // 선택 행 · 편집 중 텍스트
inline constexpr glm::vec4 bgPopup       = srgb(0x19191B);   // 메뉴 드롭다운 · 카테고리 팝업 · 모달
inline constexpr glm::vec4 borderPopup   = srgb(0x46464C);   // 팝업 · 모달 · 호버 입력칸 테두리
inline constexpr glm::vec4 accentPressed = srgb(0x1E4B99);   // 강조 버튼 누름
inline constexpr glm::vec4 selectRowHover= srgb(0x2D5787);   // 선택 행 위 호버

inline constexpr glm::vec4 okBg          = srgb(0x235937);   // Yes/On/Play 어두운 바탕
inline constexpr glm::vec4 okHover       = srgb(0x2E7548);
inline constexpr glm::vec4 warnBg        = srgb(0x7A5C1E);   // Pause 어두운 바탕
inline constexpr glm::vec4 warnHover     = srgb(0x9C7526);
inline constexpr glm::vec4 errorBg       = srgb(0x5C2226);   // No/Off/Stop/삭제 어두운 바탕
inline constexpr glm::vec4 errorHover    = srgb(0x7D2D33);

inline constexpr glm::vec4 viewportBarBg = withAlpha(bgWindow, 0.85f);
inline constexpr glm::vec4 statsBackdrop = srgb(0x000000, 0.55f);
inline constexpr glm::vec4 shadow        = srgb(0x000000, 0.35f);
inline constexpr glm::vec4 modalScrim    = srgb(0x000000, 0.45f);
inline constexpr glm::vec4 dockFill      = withAlpha(accent, 0.30f);
inline constexpr glm::vec4 dockBorder    = withAlpha(accentHover, 0.90f);

inline constexpr glm::vec4 badgeScene    = axisZ;
inline constexpr glm::vec4 badgeModel    = accentHover;
inline constexpr glm::vec4 badgeAudio    = warn;
inline constexpr glm::vec4 badgePrefab   = axisY;
// 링크된 프리팹(Docs/Linked_Prefabs.md D9): 아웃라이너의 인스턴스 이름 · 디테일의 덮어쓴 값 머리.
inline constexpr glm::vec4 prefabInstance = badgePrefab;
inline constexpr glm::vec4 prefabOverride = accentHover;
inline constexpr glm::vec4 logCategory   = srgb(0x8CA6C8);   // 로그 카테고리 열
// 디테일 속성 행(H3a): 포인터가 올라간 행 바탕 · 기본값 되돌리기 화살표(UE 의 노란 화살표).
inline constexpr glm::vec4 propertyRowHover = srgb(0x262628);
inline constexpr glm::vec4 resetArrow       = warn;

// ── D2 모서리 · 테두리 · 그림자 토큰 (1x 기준 px, UI 배율 s() 를 곱해 사용) ────
inline constexpr float     radiusInput   = 2.f;
inline constexpr float     radiusControl = 4.f;
inline constexpr float     radiusPopup   = 6.f;
inline constexpr float     borderThin    = 1.f;
inline constexpr glm::vec2 shadowOffset  = { 0.f, 4.f };
inline constexpr float     shadowBlur    = 12.f;
inline constexpr glm::vec4 shadowColor   = srgb(0x000000, 0.50f);

// ── D5a 여백 · 높이 토큰 (1x 기준 px, UI 배율 s() 를 곱해 사용) ───────────────
inline constexpr float     space2        = 2.f;
inline constexpr float     space4        = 4.f;
inline constexpr float     space8        = 8.f;
inline constexpr float     space12       = 12.f;
inline constexpr float     space16       = 16.f;
inline constexpr float     space24       = 24.f;

inline constexpr float     rowH          = 24.f;   // 아웃라이너 · 디테일 · 콘텐츠 브라우저 행
inline constexpr float     toolbarH      = 36.f;   // 메인 툴바
inline constexpr float     menuBarH      = 26.f;   // 상단 메뉴 바
inline constexpr float     tabH          = 28.f;   // 하단 탭 바
inline constexpr float     headerH       = 26.f;   // 섹션 · 컴포넌트 머리
inline constexpr float     panelHeaderH  = 34.f;   // 패널 상단 액션 줄 ("+ Add Empty" 등)

} // namespace EditorTheme
