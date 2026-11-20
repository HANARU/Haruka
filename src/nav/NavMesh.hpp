#pragma once
// 실행 중 내비 메시 하나 + Detour 질의(2026-11-21, Docs/Navigation_AI.md D7). Detour 형은 .cpp 안에만 있습니다.
// 볼륨 컴포넌트가 하나씩 들고, 월드 서비스 NavSystem 이 볼륨을 골라 부릅니다. 메인 스레드 전용입니다(질의 객체의 노드 풀을 공유).

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// 경로 결과(D7). Partial = 끝점에 닿을 수 없어 가장 가까운 닿는 점까지(Unreal bAllowPartialPaths 와 같음).
enum class ENavPathResult : uint8_t { NoPath = 0, Partial = 1, Complete = 2 };
const char* navPathResultName(ENavPathResult r);

class NavMesh {
public:
    NavMesh();
    ~NavMesh();
    NavMesh(const NavMesh&) = delete;
    NavMesh& operator=(const NavMesh&) = delete;

    // Detour 타일 데이터(.hnav 의 tile)를 복사해 메시를 만듭니다. 비어 있으면 빈 메시(질의는 모두 실패)로 true.
    bool load(const std::vector<uint8_t>& tile, std::string& err);
    void clear();
    bool valid() const;          // 다각형이 있는 메시
    int  polygonCount() const;

    // start · end 를 extents(반 크기) 안의 가장 가까운 다각형으로 옮겨 찾습니다. out 은 꺾이는 점(시작 · 끝 포함)입니다.
    ENavPathResult findPath(const glm::vec3& start, const glm::vec3& end, const glm::vec3& extents,
                            std::vector<glm::vec3>& out) const;
    // p 에 가장 가까운 메시 위 점. 없으면 false.
    bool projectPoint(const glm::vec3& p, const glm::vec3& extents, glm::vec3& out) const;
    // from(메시 위로 옮긴 뒤)에서 to 쪽으로 메시 경계를 따라 미끄러진 점. 높이는 메시 높이입니다. from 근처에 메시가 없으면 false.
    bool moveAlongSurface(const glm::vec3& from, const glm::vec3& to, const glm::vec3& extents, glm::vec3& out) const;
    // 다각형 변(디버그 선). 같은 변은 한 번만.
    void edges(std::vector<std::pair<glm::vec3, glm::vec3>>& out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
