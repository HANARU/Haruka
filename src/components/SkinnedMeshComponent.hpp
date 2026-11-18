#pragma once
#include "../scene/Component.hpp"
#include "../renderer/RenderSceneInput.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <string>
#include <vector>

struct MeshHandle;
struct MaterialHandle;
struct MeshSourceVertex;
struct SkinSourceData;

// 뼈대로 움직이는 메시(2026-11-07, Docs/Hand_Grasp_Design.md 4-1). glTF 의 skin 이 있는 모델을 그리고, 관절마다 로컬 회전을
// 바꿔 자세를 정합니다. 매 프레임 렌더러가 펜스를 기다린 뒤 지금 자세로 CPU 스키닝해 메가 버퍼의 그 프레임 구간에 직접
// 씁니다(GpuScene::skinArena). 깊이 · 섀도 · 색 패스와 셰이더는 그대로입니다.
//
// 자세는 쉬는 자세(모델의 관절 로컬 TRS)에서 관절별 로컬 회전만 바꿉니다. 이동 · 배율은 쉬는 자세 그대로입니다. 손은
// HandPoseComponent 가 이 컴포넌트의 관절 회전을 정합니다.
class SkinnedMeshComponent : public Component, public IRenderSkinSource {
public:
    bool        visible     = true;
    bool        transparent = false;
    uint8_t     lightChannels = 1;   // 라이트 채널(LightChannels.hpp, 2026-11-17)
    std::string modelPath;
    int         meshIndex   = 0;   // AssetManager::loadAll() 결과 안의 번호. 스킨이 있는 메시여야 합니다

    // 불러온 결과. baseMesh 는 쉬는 자세로 스키닝해 올린 메시이고, 인덱스 구간 · 바운드에 씁니다.
    MeshHandle*     baseMesh  = nullptr;
    MaterialHandle* material  = nullptr;
    int             vertCount = 0;
    int             triCount  = 0;

    bool hasSkin() const { return skin_ && bind_; }        // 자세 · 스키닝 계산이 되는지
    bool loaded()  const { return baseMesh && hasSkin(); } // 그릴 수 있는지(GPU 메시까지 있음)
    int  jointCount() const;
    int  findJoint(const std::string& name) const;   // 없으면 -1
    const SkinSourceData* skin() const { return skin_.get(); }

    // 관절 로컬 회전. 범위 밖 번호는 무시합니다.
    const glm::quat& jointRotation(int joint) const;
    const glm::quat& restRotation(int joint) const;
    void setJointRotation(int joint, const glm::quat& q);
    void resetPose();   // 모든 관절을 쉬는 자세로

    // 관절 전역 행렬(이 컴포넌트의 모델 공간 = 액터 로컬). 자세가 바뀌었으면 다시 계산합니다.
    const std::vector<glm::mat4>& jointGlobals() const;

    // 렌더러가 부릅니다: 지금 자세로 스키닝한 정점 vertCount 개를 out 에 쓰고 모델 공간 AABB 를 돌려줍니다.
    // out 은 매핑된 정점 구간이라 읽지 않습니다. 불러오지 못했으면 false.
    bool skinInto(MeshSourceVertex* out, glm::vec3& bmin, glm::vec3& bmax) const override;

    // 시험 · 도구용: 스킨 데이터를 직접 넣습니다(AssetManager 없이). baseMesh 가 null 이어도 skinInto 는 됩니다.
    void setSkinData(std::shared_ptr<const SkinSourceData> skin,
                     std::shared_ptr<const std::vector<MeshSourceVertex>> bindVertices);

    bool localBounds(glm::vec3& outMin, glm::vec3& outMax) const override;

    const char*           typeName()     const override { return "SkinnedMeshComponent"; }
    std::vector<PropDesc> buildProps()         override;
    std::string           readOnlyInfo() const override;

    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;
    void onFieldsLoaded(const ComponentLoadContext& ctx) override;

private:
    std::shared_ptr<const SkinSourceData>                skin_;
    std::shared_ptr<const std::vector<MeshSourceVertex>> bind_;
    std::vector<glm::quat>                               pose_;
    mutable std::vector<glm::mat4>                       globals_;
    mutable std::vector<glm::mat4>                       palette_;
    mutable bool                                         dirty_ = true;
    bool                                                 transparentLoaded_ = false;
};
