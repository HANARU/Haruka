#pragma once
#include "../core/Component.hpp"
#include "../audio/AudioAttenuation.hpp"
#include <string>

class AudioSystem;

// 액터 위치에서 소리를 내는 컴포넌트입니다. 실제 사운드(ma_sound)는 플레이 중에
// AudioSystem::tick 이 처음 만나는 순간 만들고, 그 뒤 매 틱 아래 값을 사운드에 반영합니다.
// 그래서 플레이 중에 디테일 패널에서 바꾼 값(Loop·감쇠 등)도 바로 들립니다.
class AudioComponent : public Component {
public:
    std::string filePath;
    float       volume   = 1.f;
    bool        loop     = false;
    bool        autoPlay = true;
    AudioAttenuationSettings attenuation;   // 3D 재생 설정(Spatialize·감쇠 모델·거리)

    // 꺼지거나(enabled) 액터가 비활성이 되면 멈추고 처음으로 되감습니다. 다시 켜지면
    // autoPlay 사운드를 처음부터 재생합니다(Unity 의 Play On Awake 와 같은 규칙).
    void onEnable()  override;
    void onDisable() override;
    void onDestroy() override;

    void play();   // 재생합니다. 끝까지 간 사운드는 처음부터 다시 재생합니다.
    void stop();   // 멈추고 처음으로 되감습니다. 다음 play() 는 처음부터입니다.
    bool isPlaying() const;   // 에디터 일시정지 중에도 true 입니다(재생 상태는 유지됩니다)

    // 재생할 파일을 바꿉니다. 이미 만든 사운드는 정리하고, 다음 틱에 새 파일로 다시 만듭니다.
    void setFile(const std::string& path);

    bool editorIcon(glm::vec4& c) const override { c = {0.3f, 0.85f, 0.4f, 1.f}; return true; }

    const char*           typeName()     const override { return "AudioComponent"; }
    std::vector<PropDesc> buildProps()         override;
    std::string           readOnlyInfo() const override;

    // AudioSystem 이 관리합니다. 밖에서는 setFile 로만 바꾸십시오.
    void*        sound_      = nullptr;
    AudioSystem* audioSys_   = nullptr;
    bool         loadFailed_ = false;   // true 면 다음 플레이 시작 전까지 다시 시도하지 않습니다

private:
    void releaseSound();
};
