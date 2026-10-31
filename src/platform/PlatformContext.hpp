#pragma once

// ── 플랫폼 진입점이 엔진에 넘기는 것 (2026-10-31, 패키징 3단계) ─────────────────────────────────
// Android(Quest)는 NativeActivity 의 android_main 이 엔진을 만듭니다. 창 이벤트를 받는 android_app, OpenXR 로더가
// 런타임 브로커를 찾을 때 쓰는 JavaVM 과 액티비티를 여기에 담아 넘깁니다. 엔진 · XR 코드가 NDK 헤더에 기대지 않도록
// void* 로 둡니다. 데스크톱은 모두 null 입니다.
struct PlatformContext {
    void* androidApp = nullptr;   // android_app*
    void* javaVM     = nullptr;   // JavaVM*
    void* activity   = nullptr;   // jobject(액티비티)
};
