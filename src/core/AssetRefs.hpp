#pragma once
// 에셋 참조 색인 · 검사 · 옮기기(참조 체계 A-1 ~ A-3, Docs/Object_References.md).
//
// UE 의 Asset Registry + Fix Up Redirectors 와 같은 일을 글 형식에 맞게 합니다. 에셋은 계속 논리 경로(/Game/...)로 가리키고
// (.meta · UID 파일을 만들지 않음, 결정 D5), 대신
//   - 씬 · 프리팹 · ini 의 값 중 논리 경로인 것을 모아 "누가 무엇을 쓰는지" 알고,
//   - 없는 파일을 가리키는 참조를 찾고,
//   - 에디터 · 도구에서 에셋을 옮길 때 참조하는 파일의 그 값만 고쳐 쓴 뒤 파일을 옮깁니다.
// 탐색기에서 옮긴 것은 따라가지 못하므로 검사(missing)가 찾습니다.
//
// 참조로 보는 값(결정 D6): .hscene · .hprefab · .ini 의 한 줄에서 처음 나오는 '=' 또는 ':' 뒤의 값을 정규화(hvfs::toCanonical)한
// 결과가 /Game/ 또는 /Engine/ 로 시작하면 참조입니다(옛 "assets/..." 값도 /Game/ 으로 인식). ACTOR · NAME · GUID 줄은 빼고,
// '#' · ';' 로 시작하는 줄은 주석입니다. .gltf 의 상대 "uri" 는 읽기 전용 참조로 모읍니다(그 값은 고치지 않으므로, 옮기면
// 상대 위치가 바뀌는 경우 옮기기를 거절합니다).
//
// 엔진 · 에디터 · HarukaPak 이 함께 씁니다. 화면 없이 검산합니다(HarukaRefTest).

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace hrefs {

// 논리 접두("/Game" · "/Config" · "/Engine")와 디스크 폴더.
struct Root {
    std::string           logical;
    std::filesystem::path disk;
};

struct Ref {
    std::string file;              // 참조하는 파일(논리 경로)
    int         line = 0;          // 1 부터
    std::string key;               // 값 앞의 이름(model · MODEL · startScene · uri 등)
    std::string target;            // 가리키는 에셋(정규화한 논리 경로)
    bool        readOnly = false;  // glTF 의 상대 uri(고쳐 쓰지 않음)
    std::string uri;               // readOnly 일 때 원래 uri(퍼센트 풀기 전)
    // 고쳐 쓸 때 쓰는 자리: 줄의 값이 파일 안에서 차지하는 바이트 범위.
    size_t      valueBegin = 0, valueEnd = 0;
};

struct Index {
    std::vector<std::string> assets;   // 루트 아래 모든 파일(논리 경로, 정렬)
    std::vector<Ref>         refs;     // 파일 순서 · 줄 순서
};

// 루트 아래를 훑어 색인을 만듭니다. 읽을 수 없는 파일은 건너뜁니다.
Index scan(const std::vector<Root>& roots);

// 한 파일의 글에서 참조를 뽑습니다(scan 이 씁니다. 시험용으로도 엶). file 은 그 파일의 논리 경로.
std::vector<Ref> refsInText(const std::string& file, const std::string& bytes);

// 논리 경로 → 디스크 경로. 맞는 루트가 없으면 false.
bool toDisk(const std::vector<Root>& roots, const std::string& logical, std::filesystem::path& out);

// target 을 가리키는 참조(대소문자 무시). folder 가 참이면 target 폴더 아래 전부.
std::vector<Ref> referencesTo(const Index& index, const std::string& target, bool folder = false);

// 없는 파일을 가리키는 참조. 루트가 없는 접두(/Engine 을 주지 않은 경우 등)는 판단하지 않습니다.
std::vector<Ref> missing(const Index& index, const std::vector<Root>& roots);

// ── 옮기기 ──────────────────────────────────────────────────────────────────────────────────
struct Edit {
    std::string                         file;    // 고칠 파일(논리 경로, 옮기기 전 자리)
    std::vector<std::pair<Ref, std::string>> values;   // 참조와 새 값
};

struct MovePlan {
    std::string              from, to;       // 정규화한 논리 경로
    bool                     folder = false;
    std::vector<Edit>        edits;          // 고쳐 쓸 파일들
    std::vector<std::string> codeWarnings;   // 소스 코드 안의 같은 문자열("파일:줄")
    std::string              error;          // 비어 있지 않으면 옮길 수 없음
    bool ok() const { return error.empty(); }
    int  valueCount() const { int n = 0; for (const auto& e : edits) n += (int)e.values.size(); return n; }
};

// from(파일 또는 폴더)을 to 로 옮기는 계획. /Game 아래만 옮깁니다. sourceDirs 는 경로 문자열을 찾을 C++ 소스 폴더(보고만 함).
MovePlan planMove(const Index& index, const std::vector<Root>& roots, const std::string& from, const std::string& to,
                  const std::vector<std::filesystem::path>& sourceDirs = {});

// 계획대로 참조 파일을 고쳐 쓰고(바이트 그대로, 값 글자만 바꿈) 파일을 옮깁니다. 옮기기가 실패하면 고친 파일을 되돌립니다.
bool applyMove(const MovePlan& plan, const std::vector<Root>& roots, std::string* error = nullptr);

// 경로 old 를 plan 에 따라 바꾼 값(해당 없으면 그대로).
std::string mapPath(const MovePlan& plan, const std::string& path);

// 한 파일의 글(file 은 형식을 정하는 논리 경로)에서 plan 에 걸리는 참조 값만 바꾼 글. 바꾼 수를 *count 에.
// 에디터가 열린 씬(메모리)을 디스크와 같게 고칠 때 씁니다(참조 체계 D7).
std::string rewriteText(const std::string& file, const std::string& bytes, const MovePlan& plan, int* count = nullptr);

} // namespace hrefs
