#pragma once
// 내비게이션 월드 서비스(2026-11-21, Docs/Navigation_AI.md D4 · D7). 엔진이 하나 소유하고 world.setService<NavSystem> 로 넣습니다
// (UE UNavigationSystemV1 · Godot NavigationServer3D 자리). 질의는 시작점을 담은 볼륨의 메시를 쓰고, 담은 볼륨이 없으면 메시에 점을
// 투영할 수 있는 첫 볼륨을 씁니다. 메인 스레드 전용입니다.
#include "NavMesh.hpp"

#include <string>
#include <vector>

class World;
class CVarRegistry;
class NavMeshVolumeComponent;

struct NavPath {
    ENavPathResult         result = ENavPathResult::NoPath;
    std::vector<glm::vec3> points;   // 시작(메시 위로 옮긴 점) ~ 끝
    float length() const;
};

class NavSystem {
public:
    void   setWorld(World* w) { world_ = w; }
    World* world() const { return world_; }

    // 점을 메시에 맞출 때 찾는 범위(반 크기, m). 기본은 가로 1 m · 세로 2 m.
    glm::vec3 queryExtents{ 1.f, 2.f, 1.f };

    NavMeshVolumeComponent* volumeFor(const glm::vec3& p) const;
    ENavPathResult findPath(const glm::vec3& start, const glm::vec3& end, NavPath& out) const;
    bool projectPoint(const glm::vec3& p, glm::vec3& out) const;
    bool moveAlongSurface(const glm::vec3& from, const glm::vec3& to, glm::vec3& out) const;

    // 월드의 모든 켜진 볼륨을 굽습니다(D4). writeFiles 면 data 경로(hproj::resolve 로 디스크 경로)에 쓰고, 어느 경우든 곧바로 다시
    // 읽어 질의에 씁니다. dataPath 가 있으면 data 가 빈 볼륨(하나일 때)에 그 경로를 줍니다. 결과는 볼륨마다 한 줄입니다.
    struct BuildReport {
        int         volumes = 0, built = 0, failed = 0;
        std::string text;
    };
    BuildReport build(bool writeFiles, const std::string& dataPath = {});
    // 볼륨마다 상태 한 줄(다각형 수 · 구운 입력과 지금 월드가 같은지).
    std::string info() const;

    // 콘솔 명령 nav.build [경로] · nav.info 와 변수 nav.debug(디버그 선).
    void registerCommands(CVarRegistry& cvars);
    // nav.debug 가 켜져 있으면 볼륨 범위 · 메시 변 · 에이전트 경로를 디버그 선으로 그립니다(엔진이 프레임마다 부름).
    void drawDebug() const;
    bool debugDraw = false;

private:
    World* world_ = nullptr;
};
