#pragma once
#include "../scene/Component.hpp"
#include "AudioAttenuation.hpp"
#include "AudioPropagation.hpp"
#include <string>

class AudioSystem;
struct AudioVoice;

// 액터 위치에서 소리를 내는 컴포넌트입니다. 실제 소리(보이스: ma_sound 와 뒤에 이어지는 노드)는 플레이 중에
// AudioSystem::tick 이 처음 만나는 순간 만들고, 그 뒤 매 틱 아래 값을 사운드에 반영합니다.
// 그래서 플레이 중에 디테일 패널에서 바꾼 값(Loop·감쇠 등)도 바로 들립니다.
class AudioComponent : public Component {
public:
    std::string filePath;
    float       volume   = 1.f;
    bool        loop     = false;
    bool        autoPlay = true;
    AudioAttenuationSettings attenuation;   // 3D 재생 설정(Spatialize·감쇠 모델·거리)
    AudioOcclusionSettings   occlusion;     // 차폐(2026-11-06, 기본 끔). 충돌체가 청취자와의 사이를 막으면 줄이고 먹먹하게
    AudioPropagationSettings propagation;   // 도플러 · 공기 흡수(2026-11-17, 기본 끔)
    // 사운드 단계 4(2026-11-17): 버스(볼륨 묶음), 긴 음악 스트리밍(통째로 디코딩하지 않고 재생하며 풂), 지향성 콘.
    EAudioBus         bus    = EAudioBus::Sfx;
    bool              stream = false;   // 바꾸면 다음 소리 만들 때(setFile · 다음 Play) 반영
    AudioConeSettings cone;
    // 잔향(2026-11-06, Audio_Design.md 9-5). 청취자가 잔향 구역 안에 있을 때 공용 리버브로 보내는 양과 거리 규칙입니다.
    // 2D 소리(Spatialize 끔)는 보내지 않습니다(UE 의 2D 기본 센드 0 과 같음).
    float reverbSend  = 1.f;     // 0 ~ 2
    float reverbReach = 0.5f;    // 0 = 직접음 대 잔향 비율 일정, 1 = 잔향 크기가 거리와 무관(hrev::sendGain)
    static constexpr float kMaxReverbSend = 2.f;
    // 자막(2026-11-21, Docs/Accessibility.md D3): 재생을 시작할 때 이 글을 월드의 자막 큐에 넣습니다. subtitleKey 가 "ns:key" 면
    // 번역 표에서 찾고(없으면 subtitle 원문), subtitleSpeaker 는 화자입니다. subtitle 이 비면 아무것도 넣지 않습니다. 같은 컴포넌트가
    // 다시 재생하면 그 줄을 바꿉니다(id = audio:<액터 id>). 길이는 글자 수로 자동입니다.
    std::string subtitle;
    std::string subtitleKey;
    std::string subtitleSpeaker;
    void emitSubtitle();

    // 꺼지거나(enabled) 액터가 비활성이 되면 멈추고 처음으로 되감습니다. 다시 켜지면
    // autoPlay 사운드를 처음부터 재생합니다(Unity 의 Play On Awake 와 같은 규칙).
    void onEnable()  override;
    void onDisable() override;
    void onDestroy() override;

    void play();   // 재생합니다. 끝까지 간 사운드는 처음부터 다시 재생합니다.
    void stop();   // 멈추고 처음으로 되감습니다. 다음 play() 는 처음부터입니다.
    bool isPlaying() const;   // 에디터 일시정지 중에도 true 입니다(재생 상태는 유지됩니다)
    // 지금 차폐 정도(0 = 열림, 1 = 완전히 막힘, 평활 뒤). 사운드가 없거나 차폐가 꺼졌으면 0 입니다.
    float occlusionAmount() const;

    // 재생할 파일을 바꿉니다. 이미 만든 사운드는 정리하고, 다음 틱에 새 파일로 다시 만듭니다.
    void setFile(const std::string& path);

    const char*           typeName()     const override { return "AudioComponent"; }
    // 직렬화 (DEF-22). 이름은 멤버 이름을 따릅니다.
    void writeFields(ComponentFieldWriter& w) const override;
    bool readField(const std::string& key, const std::string& value) override;
    std::vector<PropDesc> buildProps()         override;
    std::string           readOnlyInfo() const override;

    // AudioSystem 이 관리합니다. 밖에서는 setFile 로만 바꾸십시오.
    AudioVoice*  voice_      = nullptr;
    AudioSystem* audioSys_   = nullptr;
    bool         loadFailed_ = false;   // true 면 다음 플레이 시작 전까지 다시 시도하지 않습니다

private:
    void releaseSound();
};
