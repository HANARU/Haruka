#pragma once
#include "../platform/PlatformContext.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// 실행 옵션(명령줄). main() 이 채워 Engine 에 넘깁니다. 해석 규칙은 EngineOptions.cpp 이고, 창 없이 시험합니다
// (HarukaHeadlessTest, headless_render).
struct EngineOptions {
    // --xr · --no-xr : OpenXR 를 준비할지(런타임이 없으면 데스크톱). 둘 다 없으면 앞단 정책을 따릅니다. 에디터는 끔,
    // 게임 실행 파일은 프로젝트 설정 xr.startInVr(2026-10-19). 에디터는 Play 동안만, 게임은 시작부터 헤드셋에 그립니다.
    std::optional<bool> xr;
    bool emulateStereo = false;   // --emulate-stereo : 헤드셋 없이 두 눈 경로 확인(DEF-09)
    // --loose : 실행 폴더의 낱개 파일을 묶음 파일보다 먼저 찾음(게임, 개발 중 묶음을 다시 만들지 않고 에셋을 바꿔 보기).
    bool loose = false;

    // ── 창 없는 실행과 자동 확인 (2026-10-31, 패키징 2단계) ─────────────────────────────────────────
    // 배포 실행 파일에도 남깁니다(사용자 결정). 패키지 점검 스크립트가 같은 옵션으로 ZIP 의 렌더까지 확인합니다.
    bool        headless      = false;   // --headless : OS 창 없이 실행(게임 실행 파일만, 에디터는 무시)
    bool        syntheticEyes = false;   // --synthetic-eyes[=WxH] : 헤드셋 없이 XR 프레임 경로로 두 눈을 그림
    uint32_t    syntheticEyeW = 512;
    uint32_t    syntheticEyeH = 512;
    uint32_t    frames        = 0;       // --frames=N : 그린 프레임이 N 이 되면 끝냄(0 = 끝없이)
    float       timeoutSec    = 0.f;     // --timeout=S : S 초가 지나면 실패로 끝냄(0 이면 frames 가 있을 때 60초)
    std::string capturePath;             // --capture=path : 끝낼 때 합성 눈 왼쪽 층을 PNG 로 저장
    bool        vkNoSync2     = false;   // --vk-no-sync2 : synchronization2 가 있어도 1.0 배리어로 기록(DEF-04 대체 경로 시험)
    bool        vkDedicatedMsaa = false; // --vk-dedicated-msaa : MSAA 컬러를 핵심 힙 밖 별도 할당으로(DEF-06 경로 시험)
    // --ssao · --no-ssao : 화면 공간 AO 를 켜거나 끔(AO 를 뺀 비교 실측 · 끔 경로 시험). 없으면 프로젝트 설정
    // render.ao. 진단용이라 프로젝트 설정을 덮어씁니다.
    std::optional<bool> ssao;

    // ── 플랫폼 진입점이 정하는 것 (명령줄 아님, 2026-10-31 패키징 3단계) ────────────────────────────
    PlatformContext platform;            // Android: android_app · JavaVM · 액티비티
    // XR 이 없으면 시작하지 않음(Quest 는 데스크톱으로 계속할 화면이 없음). 런타임 · 헤드셋을 못 쓰거나 세션을 못 만들면
    // 이유를 남기고 멈춥니다. 연결이 잠시 끊기는 것(LOSS_PENDING)은 예전처럼 다시 엽니다.
    bool            xrRequired = false;
    bool            noAudio    = false;  // 소리 장치를 열지 않음(Quest 첫 실행, 2026-10-31 결정)
};

namespace hopt {

// 모르는 인자와 값이 잘못된 인자는 unknown 에 담깁니다(경고만 하고 무시).
EngineOptions parse(const std::vector<std::string>& args, std::vector<std::string>& unknown);

// 창 없는 자동 실행의 종료 코드. 1 은 FATAL(예외)입니다.
inline constexpr int kExitOk         = 0;
inline constexpr int kExitTimeout    = 2;   // 시간 안에 원한 프레임 수를 못 그림
inline constexpr int kExitValidation = 3;   // 검증 레이어 오류(Debug 빌드)
inline constexpr int kExitBlank      = 4;   // 저장한 화면이 비어 있음

// 자동 확인 실행인지(프레임 수 · 시간 제한 · 캡처 중 하나). 이때만 종료 코드가 결과를 알립니다.
inline bool isAutomated(const EngineOptions& o) {
    return o.frames > 0 || o.timeoutSec > 0.f || !o.capturePath.empty();
}

// wanted 가 0 이면 프레임 수는 보지 않습니다.
inline int headlessExitCode(uint32_t drawn, uint32_t wanted, bool timedOut, uint32_t validationErrors, bool blank) {
    if (timedOut || (wanted > 0 && drawn < wanted)) return kExitTimeout;
    if (validationErrors > 0) return kExitValidation;
    if (blank) return kExitBlank;
    return kExitOk;
}

// RGBA8 화면이 비었는지. 밝은 화소(RGB 중 하나라도 16 초과)가 1% 이하면 빈 화면으로 봅니다.
inline bool looksBlank(const std::vector<uint8_t>& rgba) {
    const size_t pixels = rgba.size() / 4;
    if (pixels == 0) return true;
    size_t lit = 0;
    for (size_t i = 0; i < pixels; ++i) {
        const uint8_t* p = &rgba[i * 4];
        if (p[0] > 16 || p[1] > 16 || p[2] > 16) ++lit;
    }
    return lit * 100 <= pixels;
}

} // namespace hopt
