#pragma once
// 내비 메시 굽기 · .hnav 파일(2026-11-21, Docs/Navigation_AI.md D1 · D5).
//
// 입력은 월드 공간 삼각형(NavInputGeometry)이고, Recast 로 복셀화 → 영역 → 윤곽 → 다각형 메시 → 높이 상세를 만든 뒤 Detour 타일
// 데이터 하나(solo 메시)로 돌려줍니다. Recast · Detour 형은 .cpp 안에만 있습니다. 같은 입력 · 설정이면 같은 바이트입니다(난수 없음).
// 메인 스레드가 아니어도 됩니다(전역 상태 없음).

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// 에이전트 · 복셀 설정(Unreal RecastNavMesh · Unity NavMeshSurface 의 같은 이름 값). 길이는 m, 각도는 도.
struct NavBuildSettings {
    float agentRadius   = 0.3f;    // 벽에서 떨어질 거리
    float agentHeight   = 1.8f;    // 지나갈 수 있는 최소 높이
    float maxSlopeDeg   = 45.f;    // 걸을 수 있는 최대 경사
    float maxClimb      = 0.35f;   // 올라설 수 있는 턱 높이
    float cellSize      = 0.1f;    // 가로 복셀 크기(반경의 1/2 ~ 1/3 권장)
    float cellHeight    = 0.05f;   // 세로 복셀 크기
    float regionMinSize = 8.f;     // 이보다 작은 섬(셀 수의 제곱근)은 지움
    float regionMergeSize = 20.f;  // 이보다 작은 영역은 이웃과 합침
    float edgeMaxLen    = 12.f;    // 윤곽 변 최대 길이(m)
    float edgeMaxError  = 1.3f;    // 윤곽 단순화 허용 오차(셀)
    float detailSampleDist = 6.f;  // 높이 상세 표본 간격(셀, 0.9 미만이면 끔)
    float detailSampleMaxError = 1.f;   // 높이 상세 허용 오차(셀 높이)

    // 범위를 벗어나는 값을 쓸 수 있는 값으로 고칩니다(씬 파일에서 읽은 값).
    void sanitize();
};

struct NavInputGeometry {
    std::vector<glm::vec3> verts;
    std::vector<uint32_t>  tris;   // 3개씩, 앞면(위를 보는 바닥이면 법선 +Y)이 바깥

    void   addTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);
    size_t triangleCount() const { return tris.size() / 3; }
    void   clear() { verts.clear(); tris.clear(); }
};

struct NavBuildResult {
    bool                 ok = false;
    std::string          error;
    std::vector<uint8_t> tile;        // Detour 타일 데이터(dtCreateNavMeshData 결과), 다각형이 없으면 비어 있음
    int                  polygons = 0;
    int                  vertices = 0;
    double               milliseconds = 0.0;
};

// .hnav 한 개의 내용(D5).
struct NavFile {
    static constexpr uint32_t kMagic   = 0x56414E48u;   // "HNAV"
    static constexpr uint32_t kVersion = 1;
    NavBuildSettings     settings;
    uint64_t             inputHash = 0;
    glm::vec3            boundsMin{ 0.f }, boundsMax{ 0.f };
    int32_t              polygons = 0;
    std::vector<uint8_t> tile;
};

namespace hnav {

// 셀 수 상한(가로 × 세로). 넘으면 굽지 않습니다(셀 크기를 키우거나 볼륨을 줄일 것, 타일 나누기는 남음).
constexpr int64_t kMaxCells = 16 * 1024 * 1024;

// bmin ~ bmax(월드 축 정렬 상자) 안을 굽습니다. 다각형이 하나도 없으면 ok 이고 tile 은 비어 있습니다.
NavBuildResult build(const NavInputGeometry& in, const NavBuildSettings& s, const glm::vec3& bmin, const glm::vec3& bmax);

// 입력 삼각형 · 설정 · 범위의 FNV-1a 64비트(D5). 같은 입력이면 같은 값입니다.
uint64_t inputHash(const NavInputGeometry& in, const NavBuildSettings& s, const glm::vec3& bmin, const glm::vec3& bmax);

std::vector<uint8_t> writeFile(const NavFile& f);
// 머리(마법 값 · 판 · 크기)와 Detour 타일 머리를 확인합니다. 틀리면 false 와 err.
bool readFile(const uint8_t* data, size_t size, NavFile& out, std::string& err);

} // namespace hnav
