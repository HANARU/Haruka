#pragma once
#include "../core/IEngineModule.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

class World;
class AudioComponent;

// 소리 출력 장치 하나(2026-10-16).
struct AudioOutputDevice {
    std::string id;                  // 다시 고를 때 쓰는 문자열. WASAPI 는 끝점 ID, 다른 백엔드는 장치 이름
    std::string name;                // 운영체제가 알려 주는 이름(UTF-8, 한국어 Windows 면 한글이 섞임)
    bool        isDefault = false;   // 지금 시스템 기본 장치인지
};

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

    // 청취자를 활성 카메라 대신 이 월드 자세(위치 + 방향, -Z 가 정면 · +Y 가 위)에 둡니다. XR 세션이 입력을
    // 받는 동안 Engine 이 매 프레임 헤드셋(두 눈 사이)의 자세를 넘깁니다. 카메라는 XR 원점일 뿐이라, 넘기지
    // 않으면 고개를 돌려도 소리의 좌우가 바뀌지 않습니다. clearListenerPose 뒤에는 다시 카메라를 따릅니다.
    void setListenerPose(const glm::mat4& worldPose) { listenerPose_ = worldPose; hasListenerPose_ = true; }
    void clearListenerPose()                          { hasListenerPose_ = false; }

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

    // ── 출력 장치 (2026-10-16) ────────────────────────────────────────────────
    // 에디터 설정(그리고 나중에 게임의 설정 화면)이 고르는 출력 장치입니다. 엔진(ma_engine)은 이 시스템이 직접 만든 장치에
    // 연결되어 있어, 장치를 바꿔도 엔진 · 사운드는 그대로 두고 장치만 다시 엽니다(재생이 이어짐). UE 의 출력 장치 바꾸기
    // (Swap Audio Output Device), Godot 의 AudioServer.output_device 와 같은 역할입니다.
    //   setOutputDevice("")   시스템 기본 장치(운영체제에서 기본 장치를 바꾸면 따라감)
    //   setOutputDevice(id)   그 장치. 목록에 없으면(뽑힘 등) 경고하고 시스템 기본 장치로 엽니다
    //   setOutputOverride(id) 잠시 다른 장치로(XR Play 동안 헤드셋). 비우면 setOutputDevice 로 고른 장치로 돌아갑니다
    // headless 면 값만 기억하고 장치는 열지 않습니다.
    std::vector<AudioOutputDevice> outputDevices() const;
    void setOutputDevice(const std::string& id);
    void setOutputOverride(const std::string& id);
    const std::string& outputDevice() const   { return requestedId_; }
    const std::string& outputOverride() const { return overrideId_; }
    std::string        activeOutputName() const;   // 지금 소리가 나가는 장치 이름(장치가 없으면 빈 문자열)

    static constexpr uint32_t kSampleRate = 48000;
    static constexpr uint32_t kChannels   = 2;

private:
    // 지금 원하는 장치(덮어쓰기가 있으면 그것)를 엽니다. 이미 그 장치가 열려 있으면 아무것도 하지 않습니다.
    void reopenDevice();
    void updateListener();
    // 컴포넌트 값(볼륨·Loop·3D 설정)을 사운드에 반영합니다. 매 틱 부르므로 플레이 중에
    // 디테일 패널에서 바꾼 값도 바로 적용됩니다.
    static void applySettings(void* maSound, const AudioComponent& ac);
    // 월드에 남은 컴포넌트 사운드를 전부 정리하고 컴포넌트의 포인터를 비웁니다.
    void releaseWorldSounds();

    void*  engine_   = nullptr;  // ma_engine*      (miniaudio.h 는 .cpp 에서만 포함)
    void*  group_    = nullptr;  // ma_sound_group*  월드 사운드가 모두 붙는 그룹
    void*  out_      = nullptr;  // 출력 장치 상태(ma_context · ma_device, .cpp 의 OutputState). headless 면 없음
    std::string requestedId_;    // setOutputDevice 로 고른 장치(빈 문자열 = 시스템 기본)
    std::string overrideId_;     // setOutputOverride 로 잠시 쓰는 장치
    World* world_    = nullptr;
    bool   headless_ = false;
    bool   paused_   = false;

    glm::mat4 listenerPose_{ 1.f };
    bool      hasListenerPose_ = false;
};
