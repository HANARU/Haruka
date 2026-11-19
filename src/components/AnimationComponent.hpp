#pragma once
#include "../scene/Component.hpp"
#include "../assets/Animation.hpp"
#include "../assets/AnimStateMachine.hpp"
#include "../core/HDelegate.hpp"

#include <memory>
#include <string>
#include <vector>

class SkinnedMeshComponent;

// 클립 재생기(2026-11-20, Docs/Animation_System.md A3 · D4 · D5). 같은 액터의 SkinnedMeshComponent 가 읽은 모델의 클립을
// 샘플링해 자세(setPose)를 정합니다. 바닥 층은 지금 클립 + 크로스페이드로 사라지는 이전 클립, 그 위에 가산 층 하나가
// 있습니다(기준 = 가산 클립의 첫 프레임). 상태 기계를 붙이면 그것이 재생할 클립을 정합니다.
//
// 틱은 Play 동안만 돕니다(World::tick). 에디터에서는 디테일의 Preview Time 으로 그 시간의 자세를 봅니다. 같은 액터에
// HandPoseComponent 가 있으면 손 자세가 관절을 정하므로 이 컴포넌트는 자세를 쓰지 않고 한 번 알립니다.
class AnimationComponent : public Component {
public:
    // ── 저장하는 값 ──
    std::string clip;              // 처음 재생할 클립 이름(비면 모델의 첫 클립)
    bool        autoPlay   = true; // Play 를 시작하면 바로 재생
    bool        loop       = true;
    float       speed      = 1.f;  // 음수면 거꾸로
    bool        rootMotion = false;// 뿌리 관절의 이동을 액터 이동으로 옮김(D5)

    float       previewTime = 0.f; // 에디터 미리보기 시간(저장하지 않음)

    // ── 재생 API ──
    // 이름의 클립을 fade 초 동안 크로스페이드해 재생합니다(0 이면 바로). 반복 · 속도는 위 필드를 씁니다. 없는 이름이면 false.
    bool play(const std::string& name, float fadeSeconds = 0.f);
    void stop();                            // 재생을 멈추고 지금 자세를 둡니다
    void setTime(float seconds);            // 지금 클립의 시간(반복이면 감음)
    float time() const { return cur_.time; }
    bool  isPlaying() const { return cur_.clip >= 0 && !cur_.finished && !stopped_; }
    std::string currentClip() const;
    float clipDuration(const std::string& name) const;   // 없으면 0
    std::vector<std::string> clipNames() const;

    // 가산 층: name 클립의 첫 프레임과의 차이를 weight 만큼 더합니다(반복 재생). 빈 이름이면 끕니다.
    bool playAdditive(const std::string& name, float weight);
    void setAdditiveWeight(float w) { addWeight_ = w; }

    // 이벤트(D5): 클립 name 의 time 초를 지날 때 onNotify(eventName) 를 한 번 알립니다(반복하면 바퀴마다).
    void addNotify(const std::string& clipName, float time, const std::string& eventName);
    HEvent<const std::string&> onNotify;
    HEvent<const std::string&> onFinished;   // 한 번 재생 클립이 끝남(클립 이름)

    // 상태 기계(A4). 붙이면 다음 틱에 시작 상태부터 재생합니다. null 이면 떼어 냄.
    void setStateMachine(std::shared_ptr<hanim::StateMachine> sm);
    hanim::StateMachine* stateMachine() const { return sm_.get(); }

    // 지금 자세를 계산해 메시에 넣습니다(틱 · 미리보기 · 시험이 부름). 메시가 없으면 false.
    bool applyPose();
    // 에디터 미리보기: 고른 클립의 previewTime 자세.
    void applyPreview();

    void tick(float dt) override;
    void onStart() override {}

    const char*           typeName()     const override { return "AnimationComponent"; }
    std::vector<PropDesc> buildProps()         override;
    std::string           readOnlyInfo() const override;
    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;

private:
    struct Layer {
        int           clip     = -1;
        float         time     = 0.f;
        bool          loop     = true;
        float         speed    = 1.f;
        bool          finished = false;
        bool          fresh    = true;   // 재생을 시작하고 아직 한 번도 진행하지 않음(0 초 이벤트 포함)
        int           wraps    = 0;      // 이번 진행에서 감은 바퀴 수(부호 = 방향)
        hanim::Cursor cursor;
    };
    struct Notify {
        std::string clipName;
        float       time = 0.f;
        std::string name;
    };

    SkinnedMeshComponent*           mesh() const;
    const std::vector<hanim::Clip>* clips() const;
    int  clipIndex(const std::string& name) const;
    bool playIndex(int idx, float fade, bool loop, float speed);
    // 레이어를 dt 만큼 진행합니다. fire 면 지나간 이벤트를 알립니다. 반복으로 한 바퀴 넘거나 한 번 재생이 끝나면 true.
    bool advance(Layer& L, float dt, bool fire);
    void fireRange(int clipIdx, float t0, float t1, bool loop, bool inclusiveStart);
    int  rootJoint() const;
    glm::vec3 rootTranslation(int clipIdx, float t) const;
    void applyRootMotion();

    Layer cur_, prev_, add_;
    float fadeDur_ = 0.f, fadeT_ = 0.f;
    bool  fading_  = false;
    float addWeight_ = 0.f;
    hanim::Pose addRef_;
    bool  started_ = false;
    bool  stopped_ = false;
    bool  warnedHand_ = false;
    glm::vec3 lastRoot_{ 0.f };
    bool  rootValid_ = false;
    std::vector<Notify> notifies_;
    std::shared_ptr<hanim::StateMachine> sm_;
    hanim::Pose scratchA_, scratchB_;
    std::vector<float> weightsA_, weightsB_;
    std::vector<std::string> optionNames_;   // 디테일 목록(const char* 의 저장소)
};
