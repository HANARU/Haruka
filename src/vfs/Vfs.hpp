#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hpak { class PakReader; }

// ── 가상 파일 시스템 (2026-10-24, Docs/Packaging_Design.md 5절) ──────────────────────────────
// 엔진이 읽는 패키지 파일(셰이더 · 에셋)을 논리 경로("assets/models/model.glb", "shaders/pbr.frag.spv")로 찾습니다.
// 마운트는 낱개 폴더와 묶음 파일(.hpak) 두 가지이고, 우선순위가 높은 쪽이 이깁니다(같으면 나중에 마운트한 쪽).
//   에디터            실행 폴더의 shaders/ + 프로젝트 폴더의 assets/. 실행 폴더의 assets/ 복사본은 보지 않습니다(DEF-124).
//                     프로젝트 폴더가 없으면(다른 PC 로 옮긴 실행 파일) 실행 폴더 전체
//   게임 실행 파일    실행 폴더 + 그 폴더의 묶음 파일(위). --loose 를 주면 낱개 파일이 모든 묶음보다 위
// 경로 찾기는 정규화(hpak::normalizePath) 뒤 ASCII 대소문자를 무시합니다. 경로 문자열은 UTF-8 로 읽습니다(코드 페이지와
// 상관없음). 절대 경로(시스템 글꼴, 시험의 임시 파일)는 마운트를 거치지 않고 그 파일을 직접 읽습니다.
//
// 마운트는 시작할 때 한 번 하고 그 뒤로는 바꾸지 않습니다. 읽기는 여러 스레드에서 함께 불러도 됩니다(오디오 비동기 로딩).
// 읽기 함수(exists · stat · readAll · readText · contentHash · diskPath)는 예외를 던지지 않습니다(DEF-123). 올바르지 않은
// UTF-8 경로는 "없음", 메모리 부족 같은 실패는 false(diskPath 는 빈 경로)입니다. 마운트 함수는 시작할 때만 부르며 던질 수 있습니다.
namespace hvfs {

// 게임 묶음의 우선순위: game.hpak = base, dlc_*.hpak = base + 1, game_p<N>.hpak = base + 2 + min(N, kMaxPatchNumber).
constexpr int kMaxPatchNumber = 1000000;
// --loose 의 낱개 폴더. 어떤 묶음(패치 번호가 커도)보다 위입니다(DEF-126).
constexpr int kLoosePriority  = 0x7FFFFFFF;

struct Stat {
    uint64_t size        = 0;
    uint64_t mtime       = 0;       // 낱개 파일의 수정 시각(묶음 안이면 0)
    bool     inPack      = false;
    uint64_t contentHash = 0;       // 묶음 안이면 목차의 XXH3-64(낱개 파일은 contentHash() 로 계산)
};

void reset();   // 모든 마운트 해제(엔진 부팅 · 시험)
// root 아래 낱개 파일을 마운트합니다. prefix 가 비어 있지 않으면 그 접두어("assets/")로 시작하는 경로만 찾습니다.
void mountDirectory(const std::filesystem::path& root, const std::string& prefix, int priority);
bool mountPack(const std::filesystem::path& file, int priority, std::string& err);
bool mountPack(std::unique_ptr<hpak::PakReader> reader, const std::string& name, int priority);   // 시험 · 메모리 묶음

// dir 의 게임 묶음을 파일 이름 규칙으로 마운트합니다. game.hpak(기본) < dlc_*.hpak < game_p<N>.hpak(N 이 클수록 위).
// 같은 단계는 이름순으로 나중 것이 이깁니다. 우선순위는 basePriority 부터 올라갑니다(위 kMaxPatchNumber).
struct MountReport {
    std::vector<std::string> mounted;   // 파일 이름
    std::vector<std::string> failed;    // "이름: 이유"
};
MountReport mountGamePacks(const std::filesystem::path& dir, int basePriority);
// dir 에 .hpak 파일이 하나라도 있는지(마운트하지 않음). 엔진이 쓰지 않는 묶음 폴더를 알릴 때 씁니다(DEF-149).
bool hasPackFiles(const std::filesystem::path& dir);

bool exists(std::string_view path);
bool stat(std::string_view path, Stat& out);
bool readAll(std::string_view path, std::vector<uint8_t>& out, std::string* err = nullptr);
// readAll 과 같고 문자열로 돌려줍니다. 줄 끝은 그대로입니다(파서가 '\r' 을 지움).
bool readText(std::string_view path, std::string& out, std::string* err = nullptr);
// 내용의 XXH3-64. 묶음 안이면 목차 값, 낱개 파일이면 읽어서 계산합니다.
bool contentHash(std::string_view path, uint64_t& out);
// 낱개 파일이면 실제 경로, 묶음 안이거나 없으면 빈 경로. 에디터처럼 파일 자체가 필요한 곳만 씁니다.
std::filesystem::path diskPath(std::string_view path);
// 마운트 목록(로그용, 우선순위 높은 순).
std::vector<std::string> describe();

} // namespace hvfs
