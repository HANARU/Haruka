#include "DefaultGameModule.hpp"

// ── 시작 씬 ───────────────────────────────────────────────────────────────────
// 예전에는 이 함수가 코드에 박힌 시작 씬(assets/scenes/LightingProbe.hscene)을 읽었습니다. 2026-10-15 부터 시작 씬은
// 프로젝트 설정(assets/project.ini 의 startScene)에서 정하고, 처음 열 씬은 엔진 앞단이 고릅니다. 에디터는 마지막으로 연
// 씬이 있으면 그것, 없으면 시작 씬이고(그래야 지금 열린 파일을 알고 저장할 수 있음), 게임 실행 파일은 GameFrontend 가
// 시작 씬을 엽니다(2026-10-19). 이 함수는 게임 규칙을 넣을 자리로 남겨 둡니다.
void DefaultGameModule::onStart(EngineContext& /*ctx*/) {}
