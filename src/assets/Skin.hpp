#pragma once
// 스킨(뼈대로 움직이는 메시) 데이터와 CPU 스키닝(2026-11-07, Docs/Hand_Grasp_Design.md 4-1).
//
// glTF 의 skin 을 읽은 결과입니다. 정점은 바인드 공간(스킨 메시 노드의 트랜스폼은 glTF 규약대로 무시)에 있고, 관절마다
// 쉬는 자세의 로컬 TRS 와 inverseBindMatrix 를 가집니다. 스키닝 행렬은 관절 전역 행렬 x inverseBind 이며, 쉬는 자세가
// 바인드 자세와 같으면 단위 행렬이 됩니다. GPU 의존이 없어 장치 없이 시험할 수 있습니다.

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "AnimClip.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct MeshSourceVertex;   // assets/MeshSource.hpp. 렌더러의 Vertex 와 배치가 같습니다(GpuScene 이 static_assert 로 확인)

struct SkinJoint {
    std::string name;
    int         parent = -1;              // joints 안의 번호. -1 = 뿌리 관절(부모 노드가 관절이 아님)
    glm::vec3   restT{ 0.f };             // 쉬는 자세의 로컬 이동 · 회전 · 배율(부모 관절 기준)
    glm::quat   restR{ 1.f, 0.f, 0.f, 0.f };
    glm::vec3   restS{ 1.f };
    glm::mat4   inverseBind{ 1.f };
    glm::mat4   rootParent{ 1.f };        // parent == -1 일 때만: 관절이 아닌 조상 노드들의 전역 행렬
};

// 관절 하나의 로컬 자세(부모 관절 기준 이동 · 회전 · 배율, 2026-11-20 애니메이션 D2).
struct JointPose {
    glm::vec3 t{ 0.f };
    glm::quat r{ 1.f, 0.f, 0.f, 0.f };
    glm::vec3 s{ 1.f };
};

struct SkinSourceData {
    std::vector<SkinJoint> joints;
    std::vector<uint16_t>  jointIndex;    // 정점마다 4개(joints 안의 번호)
    std::vector<float>     jointWeight;   // 정점마다 4개, 합 1
    std::vector<int>       order;         // 부모가 자식보다 앞서는 평가 순서. buildOrder 가 채웁니다
    std::vector<hanim::Clip> clips;       // 이 스킨의 관절을 움직이는 glTF 애니메이션(2026-11-20, Docs/Animation_System.md)
    // 모프 타깃(2026-11-20, 애니메이션 A5 · D7): 타깃마다 정점별 위치 · 노멀 차이(바인드 공간). 배치는 [타깃][정점][xyz].
    // 스킨이 없는 모프 메시는 관절 하나(단위 행렬, rootParent = 노드 행렬)짜리 스킨으로 만듭니다.
    uint32_t                 morphCount = 0;
    std::vector<float>       morphPos;            // morphCount x 정점 수 x 3
    std::vector<float>       morphNrm;            // 비면 노멀 차이 없음
    std::vector<float>       defaultWeights;      // glTF mesh.weights(없으면 0), 크기 morphCount
};

namespace hskin {

constexpr int kInfluences = 4;

// 부모가 자식보다 앞서는 순서를 order 에 채웁니다. 부모 번호가 범위를 벗어나거나 순환이 있으면 false.
bool buildOrder(SkinSourceData& skin);

// 관절 이름 → 번호(없으면 -1).
int findJoint(const SkinSourceData& skin, const std::string& name);

// 관절 전역 행렬(모델 공간). localRot 가 null 이면 쉬는 자세, 아니면 관절마다 그 로컬 회전을 씁니다(이동 · 배율은 쉬는 자세).
void computeGlobals(const SkinSourceData& skin, const glm::quat* localRot, std::vector<glm::mat4>& outGlobal);

// 관절 전역 행렬(모델 공간). pose 는 관절마다 로컬 TRS 입니다(애니메이션, 2026-11-20). null 이면 쉬는 자세.
void computeGlobals(const SkinSourceData& skin, const JointPose* pose, std::vector<glm::mat4>& outGlobal);

// 쉬는 자세(두 판 중 어느 쪽인지 모호한 nullptr 인자용).
inline void computeGlobals(const SkinSourceData& skin, std::nullptr_t, std::vector<glm::mat4>& outGlobal) {
    computeGlobals(skin, static_cast<const glm::quat*>(nullptr), outGlobal);
}

// 쉬는 자세를 out 에 채웁니다(관절 수만큼).
void restPose(const SkinSourceData& skin, std::vector<JointPose>& out);

// 모프: bind 정점 n 개에 weights(크기 morphCount)만큼 차이를 더해 out 에 씁니다. 노멀은 다시 정규화합니다.
// 무게가 모두 0 이면 false 를 돌려주고 out 을 건드리지 않습니다(그때는 bind 를 그대로 쓰면 됨).
bool applyMorph(const MeshSourceVertex* bind, size_t n, const SkinSourceData& skin, const float* weights, MeshSourceVertex* out);

// 스키닝 행렬 = 전역 x inverseBind.
void computePalette(const SkinSourceData& skin, const std::vector<glm::mat4>& globals, std::vector<glm::mat4>& outPalette);

// 바인드 정점 n 개를 palette 로 변형해 out 에 씁니다(선형 혼합). 노멀 · 탄젠트는 회전 부분으로 돌리고 다시 정규화합니다
// (관절에 비균등 배율이 없다고 가정). 변형한 위치의 AABB 를 bmin · bmax 로 돌려줍니다. n 이 0 이면 bmin = bmax = 0.
// out 은 렌더러의 매핑된 정점 구간일 수 있습니다(MeshSourceVertex 와 Vertex 는 배치가 같음). 이 함수는 out 을 읽지 않습니다.
void skinVertices(const MeshSourceVertex* bind, size_t n, const SkinSourceData& skin,
                  const glm::mat4* palette, MeshSourceVertex* out, glm::vec3& bmin, glm::vec3& bmax);

} // namespace hskin
