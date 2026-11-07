#pragma once
#include "../core/Component.hpp"
#include "../xr/HandSkeleton.hpp"

#include <string>
#include <vector>

class SkinnedMeshComponent;

// 손 메시의 자세(2026-11-07, Docs/Hand_Grasp_Design.md 4-1 · 4-2). 같은 액터의 SkinnedMeshComponent 관절을 OpenXR 26관절
// 이름(hhand::kHandJointNames)으로 찾아, 손가락마다 굽힘 값 0(편 손) ~ 1(쥔 손)을 관절 회전으로 바꿉니다.
// 각 굽힘 관절(hhand::kFingerFlexJoints)을 쉬는 자세에서 로컬 X 둘레로 -(굽힘 x 최대 각) 만큼 돌립니다(OpenXR 축 규약에서
// 손바닥 쪽으로 굽힘). 손 모델의 관절 로컬 축이 규약과 다르면(자식 관절이 로컬 -Z 쪽에 있지 않으면) 한 번 경고합니다.
//
// hand 가 Left · Right 이면 Play 중 그 손의 컨트롤러 입력(트리거 · 그립 · 정전식 터치)이 굽힘 값을 정합니다(B 단계,
// hhand::curlTargets · smoothCurl). 입력이 없으면(데스크톱 · XR 세션이 입력을 받지 않음) 굽힘 값을 그대로 둡니다.
// hand 가 None 이면 굽힘 값을 속성으로 직접 정합니다(손 확인 씬 등). 같은 액터에서 SkinnedMeshComponent 뒤에 두어야 씬을
// 불러올 때 바로 자세가 들어갑니다(Play 중에는 매 tick 적용).
enum class EHandSide : int { None = 0, Left = 1, Right = 2 };

class HandPoseComponent : public Component {
public:
    float     curl[hhand::kFingerCount] = { 0.f, 0.f, 0.f, 0.f, 0.f };
    float     maxFlexDeg[hhand::kFingerCount][3];
    EHandSide hand = EHandSide::None;   // 입력을 읽을 손

    // 입력으로 굽힘 값을 dt 초만큼 갱신합니다(tick 이 부름, 시험이 직접 부름). 그 손의 입력이 없으면 false 이고 값은 그대로.
    bool updateFromInput(float dt);

    HandPoseComponent();

    // 지금 굽힘 값을 메시 관절에 씁니다. 메시가 없거나 아직 불러오지 않았으면 아무것도 하지 않고 false.
    bool apply();

    // 진단(시험 · 에디터 정보): 찾은 관절 수(최대 26), 축 규약 위반 관절 이름들.
    int mappedJoints() const { return mapped_; }
    const std::vector<std::string>& axisViolations() const { return axisViolations_; }

    void tick(float dt) override;
    void onStart() override { apply(); }
    void onPropsCommitted() override { apply(); }

    const char*           typeName()     const override { return "HandPoseComponent"; }
    std::vector<PropDesc> buildProps()         override;
    std::string           readOnlyInfo() const override;

    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;
    void onFieldsLoaded(const ComponentLoadContext& ctx) override;

private:
    bool map(SkinnedMeshComponent& mesh);   // 관절 번호 대응 · 축 확인. 같은 스킨이면 다시 하지 않음

    const void*              mappedSkin_ = nullptr;   // 대응을 만든 스킨(SkinSourceData*)
    int                      jointMap_[hhand::kJointCount];
    int                      mapped_ = 0;
    std::vector<std::string> axisViolations_;
};
