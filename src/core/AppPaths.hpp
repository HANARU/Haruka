#pragma once
#include <filesystem>
#include <string>
#include <vector>

// ── 실행 폴더와 쓰기 위치 (2026-10-19) ────────────────────────────────────────────
// 엔진이 읽는 패키지 파일(셰이더 · 실행 폴더의 에셋 복사본 · 기본 설정)은 작업 폴더가 아니라 실행 파일이 있는 폴더를
// 기준으로 찾습니다. 예전에는 모두 작업 폴더 기준이라 run.bat 의 pushd 덕분에만 맞았고, 바로가기나 다른 폴더에서
// 실행하면 셰이더를 찾지 못했습니다.
//
// 실행 중에 쓰는 파일(설정 · 로그 · 캐시)은 쓰기 루트 아래에 둡니다(2026-10-19 사용자 결정).
//   에디터(HarukaEngine)    실행 폴더. 점검 도구와 배치 파일이 이 자리를 씁니다(예전과 같음).
//   게임(HarukaGame)        Windows %LOCALAPPDATA%\Haruka\<실행 파일 이름>. Program Files 에 설치해도 쓸 수 있고 사용자마다
//                           따로입니다(UE 의 Saved, Unity 의 LocalLow 와 같은 자리). Android 는 외부 앱 폴더(Android 단계).
namespace happ {

// 실행 파일이 있는 폴더. 처음 부를 때 한 번 구합니다.
const std::filesystem::path& exeDir();
// 실행 파일 이름(확장자 없음). 예: HarukaEngine, HarukaGame
const std::string& exeStem();

// 쓰기 루트를 정합니다. main() 이 로그를 열기 전에 한 번 부릅니다. 사용자 폴더를 쓰기로 했는데 그 폴더를 얻거나
// 만들지 못하면 실행 폴더로 내려가고 false 를 돌려줍니다(호출자가 로그로 알림).
bool initWriteRoot(bool userDataFolder);
// 쓰기 루트. initWriteRoot 전에는 실행 폴더입니다.
const std::filesystem::path& writeRoot();
// 쓰기 루트가 실행 폴더와 같은지(에디터). 같으면 패키지 기본 설정과 사용자 설정이 한 파일입니다.
bool writeRootIsExeDir();

// Android(2026-10-31, 패키징 3단계): 실행 파일 경로가 /system/bin/app_process64 라 쓸 수 없으므로, 진입점이 콘텐츠 폴더
// (패키지 기본 설정 · 낱개 파일 · 묶음 파일을 찾는 자리)와 쓰기 루트, 이름을 정합니다. 둘 다 앱의 외부 파일 폴더
// (/sdcard/Android/data/<패키지>/files)이고, 게임처럼 쓰기 루트가 따로 있는 것으로 봅니다. 로그를 열기 전에 부릅니다.
void setPlatformDirs(const std::filesystem::path& contentDir, const std::filesystem::path& writeRoot, const std::string& stem);
// 묶음 파일을 더 찾을 폴더(Android 의 OBB 폴더: adb push 가 files/ 에 막힐 때의 대안). 콘텐츠 폴더 다음 순서입니다.
void addPackDir(const std::filesystem::path& dir);
// 묶음 파일을 찾을 폴더 목록. 첫째는 언제나 실행(콘텐츠) 폴더입니다.
std::vector<std::filesystem::path> packDirs();

// 실행 폴더 기준 경로. 절대 경로는 그대로 둡니다.
inline std::filesystem::path inExeDir(const std::filesystem::path& rel) {
    return rel.is_absolute() ? rel : exeDir() / rel;
}
// 쓰기 루트 기준 경로.
inline std::filesystem::path inWriteRoot(const std::filesystem::path& rel) {
    return rel.is_absolute() ? rel : writeRoot() / rel;
}

} // namespace happ
