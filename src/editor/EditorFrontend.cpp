// 에디터 실행 파일(HarukaEngine)의 엔진 앞단 선택(2026-10-19, 런타임 분리). 게임 실행 파일은 이 파일 대신
// game/GameFrontend.cpp 를 링크합니다.
#include "EditorApp.hpp"

IEngineFrontend::Policy EditorApp::policy() const {
    Policy p;
    // 에디터는 프로젝트 폴더의 assets/ 를 직접 읽고 씁니다(2026-10-15, core/ProjectPaths.hpp). 소스 트리 경로는 에디터
    // 실행 파일에만 넣습니다. 그 폴더가 없으면(다른 PC 로 옮긴 실행 파일 등) 엔진이 실행 파일 옆의 복사본을 씁니다.
#ifdef HARUKA_PROJECT_DIR
    p.projectDir = HARUKA_PROJECT_DIR;
#endif
    // 나머지는 기본값입니다: VR 은 --xr 로만 준비하고 Play 동안만 헤드셋에 그림(2026-10-15), 헤드셋을 벗어도 Play 는 계속,
    // 런타임이 세션을 끝내면 데스크톱으로 계속하고 다음 Play 에서 다시 엶, 설정 · 로그는 실행 폴더.
    return p;
}

std::unique_ptr<IEngineFrontend> createEngineFrontend() {
    return std::make_unique<EditorApp>();
}
