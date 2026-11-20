#pragma once
// 내비 메시 볼륨(2026-11-21, Docs/Navigation_AI.md D3 · D4). 범위 · 에이전트 설정 · 구운 데이터 경로(.hnav)를 들고, 씬을 읽을 때
// 그 파일을 읽어 실행 중 메시(NavMesh)를 만듭니다. 굽기는 NavSystem::build(콘솔 nav.build)가 합니다. 게임은 읽기만 합니다.
//
// 범위는 액터 월드 위치를 중심으로 halfExtent × |월드 크기| 인 월드 축 정렬 상자입니다(회전은 무시, Unreal NavMeshBoundsVolume 과 같음).
#include "../scene/Component.hpp"
#include "NavMesh.hpp"
#include "NavMeshBuild.hpp"

#include <string>

class NavMeshVolumeComponent : public Component {
public:
    glm::vec3        halfExtent{ 10.f, 3.f, 10.f };
    NavBuildSettings settings;
    std::string      data;   // 논리 경로(예: /Game/scenes/NavProbe.hnav). 비었으면 굽기 · 읽기를 하지 않음

    const char* typeName() const override { return "NavMeshVolumeComponent"; }
    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;
    void onFieldsLoaded(const ComponentLoadContext& ctx) override;
    void onStart() override;
    std::vector<PropDesc> buildProps() override;
    std::string readOnlyInfo() const override;

    // 월드 범위.
    void worldBounds(glm::vec3& mn, glm::vec3& mx) const;
    bool contains(const glm::vec3& p, float verticalSlack = 0.5f) const;

    // data 경로의 파일을 읽습니다(가상 파일 시스템). 없거나 깨졌으면 경고하고 빈 메시입니다.
    bool loadData();
    // 구운 파일 내용을 바로 넣습니다(굽기 직후 · 시험).
    bool loadBytes(const std::vector<uint8_t>& bytes, std::string& err);

    const NavMesh& mesh() const { return mesh_; }
    bool     loaded() const { return mesh_.valid(); }
    uint64_t loadedHash() const { return loadedHash_; }
    const std::string& status() const { return status_; }

private:
    NavMesh     mesh_;
    uint64_t    loadedHash_ = 0;
    bool        triedLoad_  = false;
    std::string status_ = "not built";
};
