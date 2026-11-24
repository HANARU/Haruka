#pragma once
#include "../scene/Component.hpp"
#include "../vfx/ParticleSim.hpp"

#include <string>
#include <vector>

// ── 파티클 이미터 (2026-11-20, Docs/Particles_And_Decals.md) ────────────────────────────────────────────
// CPU 이미터 하나입니다. 시뮬레이션은 vfx::ParticleSim(순수 계산)이 하고, 이 컴포넌트는 월드 틱(시간 배율이 곱해진 dt,
// 일시정지면 틱 없음)에 맞춰 step 을 부르고 그릴 인스턴스(월드 공간)를 만들어 둡니다. 렌더 추출은 그 목록을 가리킵니다.
// PostPhysics 틱이라 물리로 움직이는 액터에 붙어도 이번 틱의 자리에서 생성합니다(D5).
//
// 게임 코드: play()(처음부터 다시) · stop()(새 생성만 멈춤, 남은 파티클은 수명대로) · clear()(모두 지움).
class ParticleSystemComponent : public Component {
public:
    vfx::EmitterDesc desc;
    bool             autoPlay = true;   // 시작할 때 생성을 시작함

    ParticleSystemComponent() { tickGroup = ETickGroup::Display; }

    void play();
    void stop()  { emitting_ = false; }
    void clear() { sim_.reset(desc); instances_.clear(); }
    bool emitting() const { return emitting_; }

    const vfx::ParticleSim&                   sim() const       { return sim_; }
    const std::vector<vfx::ParticleInstance>& instances() const { return instances_; }   // 마지막 틱의 그릴 목록(월드)
    glm::vec3                                 center() const    { return center_; }      // 이미터 월드 위치(정렬)

    void onStart() override;
    void tick(float dt) override;

    const char*           typeName() const override { return "ParticleSystemComponent"; }
    void                  writeFields(ComponentFieldWriter& w) const override;
    bool                  readField(const std::string& key, const std::string& value) override;
    std::vector<PropDesc> buildProps() override;
    std::string           readOnlyInfo() const override;
    void                  onPropsCommitted() override { desc.clampValues(); }

private:
    vfx::ParticleSim                   sim_;
    std::vector<vfx::ParticleInstance> instances_;
    glm::vec3                          center_{ 0.f };
    bool                               emitting_ = false;
    bool                               started_  = false;
};
