#pragma once
#include <filesystem>
#include <string>

// 프로젝트 에셋의 실제 위치(2026-10-15 사용자 결정: 에디터는 프로젝트 폴더를 직접 읽고 씀).
//
// 엔진은 에셋을 "assets/models/model.glb" 같은 논리 경로로 가리키고, 씬 · 프리팹 · 컴포넌트에도 그 논리
// 경로를 저장합니다. 예전에는 논리 경로를 작업 디렉터리(실행 폴더) 기준으로 열었는데, 실행 폴더의 assets 는
// 빌드할 때마다 소스 트리에서 덮어쓰는 복사본이라 에디터에서 저장한 프리팹이 소스 트리에 남지 않고 다음 빌드에
// 덮어써졌습니다. UE(Content) · Unity(Assets) · Godot(res://) 처럼 에디터 빌드는 프로젝트 폴더를 직접 씁니다.
//
// 루트가 정해지면 "assets/" 로 시작하는 경로만 그 아래로 바꿉니다. 셰이더 · Config · Logs 는 실행 폴더에 그대로
// 둡니다. 캐시(assets/cache)는 원본에서 만든 파생 데이터라 소스 트리를 더럽히지 않도록 실행 폴더에 둡니다.
// 루트가 비어 있으면(배포된 실행 파일 등) 지금처럼 실행 폴더 기준입니다.
namespace hproj {

inline std::filesystem::path& rootStorage() {
    static std::filesystem::path root;
    return root;
}

inline void setRoot(const std::filesystem::path& root) { rootStorage() = root; }
inline const std::filesystem::path& root()             { return rootStorage(); }

// 논리 경로를 실제로 열 경로로 바꿉니다. "assets/" 밖(임시 파일 · 절대 경로 등)과 캐시는 그대로 둡니다.
inline std::string resolve(const std::string& logical) {
    const std::filesystem::path& r = rootStorage();
    if (r.empty()) return logical;
    if (logical.rfind("assets/", 0) != 0) return logical;
    if (logical.rfind("assets/cache/", 0) == 0 || logical == "assets/cache") return logical;
    return (r / logical).generic_string();
}

} // namespace hproj
