#pragma once
#include "../core/IEngineModule.hpp"
#include <cstdint>

class World;
class AudioComponent;

// miniaudio 기반 오디오 시스템입니다. ma_engine 을 소유하고 AudioComponent 와 사운드를 잇습니다.
// 컴포넌트 사운드 생성, 3D 위치·설정 반영, 청취자(활성 카메라) 갱신은 모두 tick() 에서 합니다.
//
// 월드 사운드는 전부 그룹 하나(월드 그룹)에 붙습니다. 일시정지는 그 그룹을 멈추는 것이라,
// 그룹 아래 사운드는 믹싱에서 읽히지 않아 재생 위치가 그대로 멈춰 있다가 재개하면 이어집니다.
// 나중에 에디터 미리듣기나 UI 소리를 넣는다면 이 그룹 밖에 붙여 일시정지와 무관하게 둡니다.
class AudioSystem : public IEngineModule {
public:
    // headless 면 출력 장치 없이 엔진만 만들고, 출력은 renderHeadless 로 직접 당깁니다
    // (오디오 테스트·오프라인 처리용). Engine 은 기본값인 장치 모드로 만듭니다.
    explicit AudioSystem(bool headless = false) : headless_(headless) {}

    void        startup()      override;
    void        shutdown()     override;
    void        tick(float dt) override;
    void        onBeginPlay()  override;
    void        onEndPlay()    override;
    void        onPauseChanged(bool paused) override;
    const char* name() const   override { return "AudioSystem"; }

    void setWorld(World* w) { world_ = w; }
    bool isPaused() const   { return paused_; }

    // AudioComponent::play()/stop() 이 부릅니다. stopSound 는 처음으로 되감습니다.
    void playSound (void* maSound);
    void stopSound (void* maSound);
    bool isSoundPlaying(void* maSound) const;

    // ma_sound 를 만들어 월드 그룹에 붙이고, 정리합니다. 만들기에 실패하면 이유를 로그로
    // 남기고 false 를 돌려줍니다.
    bool initSound (const char* filePath, bool loop, void** outSound);
    void destroySound(void* maSound);

    // headless 일 때만 동작합니다. 스테레오 interleaved float 를 frameCount 프레임 채웁니다.
    bool renderHeadless(float* outStereo, uint32_t frameCount);

    static constexpr uint32_t kSampleRate = 48000;
    static constexpr uint32_t kChannels   = 2;

private:
    void updateListener();
    // 컴포넌트 값(볼륨·Loop·3D 설정)을 사운드에 반영합니다. 매 틱 부르므로 플레이 중에
    // 디테일 패널에서 바꾼 값도 바로 적용됩니다.
    static void applySettings(void* maSound, const AudioComponent& ac);
    // 월드에 남은 컴포넌트 사운드를 전부 정리하고 컴포넌트의 포인터를 비웁니다.
    void releaseWorldSounds();

    void*  engine_   = nullptr;  // ma_engine*      (miniaudio.h 는 .cpp 에서만 포함)
    void*  group_    = nullptr;  // ma_sound_group*  월드 사운드가 모두 붙는 그룹
    World* world_    = nullptr;
    bool   headless_ = false;
    bool   paused_   = false;
};
