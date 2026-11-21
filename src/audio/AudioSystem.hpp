#pragma once
#include "../core/IEngineModule.hpp"
#include "AudioReverb.hpp"
#include "AudioHrtf.hpp"
#include "AudioPropagation.hpp"
#include "AudioMix.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <glm/glm.hpp>

class World;
class Actor;
class AudioComponent;
class PhysicsSystem;
class CVarRegistry;
// 소리 하나(보이스). ma_sound 와 그 뒤에 이어지는 Haruka 노드들을 묶은 것으로, 정의는 AudioSystem.cpp 에 있습니다
// (miniaudio.h 는 .cpp 에서만 포함). AudioComponent 는 이 포인터만 들고 AudioSystem 의 함수로 다룹니다(2026-11-06).
struct AudioVoice;

// HRTF 를 켜는 때(2026-11-06, Docs/Audio_Design.md 9-6). HRTF 는 헤드폰 · 헤드셋용이라 스피커로 들으면 오히려 어색합니다.
//   Auto  XR 세션이 있는 동안(헤드셋으로 들음)만. 기본값
//   On    언제나(데스크톱에서 헤드폰으로 들을 때)
//   Off   쓰지 않음(패닝)
enum class EHrtfMode : uint8_t { Auto = 0, On = 1, Off = 2 };

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
    explicit AudioSystem(bool headless = false);

    void        startup()      override;
    void        shutdown()     override;
    void        tick(float dt) override;
    void        onBeginPlay()  override;
    void        onEndPlay()    override;
    void        onPauseChanged(bool paused) override;
    const char* name() const   override { return "AudioSystem"; }

    // 월드를 정하고 그 월드의 서비스로 자신을 넣습니다(게임 코드: world.service<AudioSystem>(), 2026-11-21). shutdown 이 뺍니다.
    void setWorld(World* w);
    bool isPaused() const   { return paused_; }

    // 차폐 판정에 쓰는 물리(2026-11-06). 없으면 차폐를 켠 소리도 열린 것으로 둡니다. 판정은 게임 스레드의 tick 에서
    // PhysicsSystem::raycast 로 하고(오디오 스레드에서 쏘지 않음, Steam Audio 권고와 같음), 결과만 보이스 필터에 넘깁니다.
    void setPhysics(const PhysicsSystem* p) { physics_ = p; }
    // 보이스의 지금 차폐 정도(0 = 열림, 1 = 완전히 막힘, 평활 뒤 값). 차폐가 꺼졌거나 보이스가 없으면 0 입니다.
    float voiceOcclusion(const AudioVoice* voice) const;
    // 지금 넣은 도플러 음높이 배율(S1, 쓰지 않으면 1). 시험 · 디버그용.
    float voicePitch(const AudioVoice* voice) const;

    // 잔향(2026-11-06, Docs/Audio_Design.md 9-5). 공용 리버브 하나가 월드 그룹 안에 있고, 청취자가 있는 잔향 구역이 매개변수를 정합니다.
    // 구역 밖이면 크기가 0 으로 줄어든 뒤 리버브를 멈춥니다(그동안 CPU 를 쓰지 않고, 출력이 잔향이 없던 때와 비트 단위로 같음).
    const hrev::ReverbParams& reverbParams() const { return reverbCur_; }   // 평활 뒤 지금 값
    bool                      reverbRunning() const { return reverbRunning_; }

    // HRTF(2026-11-06, Docs/Audio_Design.md 9-6). 켜지면 공간화한 소리마다 HRTF 노드를 사슬에 넣고(보이스 필터 → HRTF → 월드 그룹)
    // miniaudio 의 좌우 패닝을 끕니다(거리 감쇠 · 콘 · 도플러는 그대로). 꺼지면 노드를 사슬에서 빼고 패닝으로 돌아가며, 그동안의
    // 출력은 HRTF 가 없던 때와 같습니다. 구현은 처음 켜질 때 Steam Audio 를 열어 만들고, 열지 못하면 경고하고 패닝으로 둡니다.
    void      setHrtfMode(EHrtfMode m);
    EHrtfMode hrtfMode() const { return hrtfMode_; }
    // Engine 이 XR 세션을 만들고 지울 때 알립니다(Auto 모드가 따름).
    void      setXrActive(bool active);
    bool      hrtfActive() const { return hrtfActive_; }          // 지금 HRTF 로 소리를 내는지(모드 · XR · 구현이 모두 맞을 때)
    bool      hrtfAvailable() const { return hrtf_ != nullptr; }  // 구현이 있는지(Steam Audio 를 열었거나 넣어 줌)
    std::string hrtfStatus() const;   // 설정 화면용 짧은 ASCII 상태("on (Steam Audio 4.8.1)" · "off" · "unavailable (see the log)")
    // 구현을 바꿔 끼웁니다(시험용 구현 · 다른 HRTF). 이미 만든 HRTF 노드는 모두 사슬에서 빼고 지운 뒤 바꿉니다. nullptr 면 구현
    // 없음(패닝)이고, 그 뒤로는 Steam Audio 를 열지 않습니다.
    void      setHrtfBackend(std::unique_ptr<hhrtf::Backend> backend);
    bool      voiceHrtfRouted(const AudioVoice* voice) const;   // 이 보이스가 HRTF 노드를 거치는지
    uint64_t  voiceHrtfBlocks(const AudioVoice* voice) const;   // HRTF 노드가 처리한 블록 수(소리가 끝나고 꼬리를 다 내면 멈춤)
    static const char* hrtfModeName(EHrtfMode m);                // "auto" · "on" · "off"(설정 파일의 값)
    static bool        hrtfModeFromText(const std::string& text, EHrtfMode& out);

    // 청취자를 활성 카메라 대신 이 월드 자세(위치 + 방향, -Z 가 정면 · +Y 가 위)에 둡니다. XR 세션이 입력을
    // 받는 동안 Engine 이 매 프레임 헤드셋(두 눈 사이)의 자세를 넘깁니다. 카메라는 XR 원점일 뿐이라, 넘기지
    // 않으면 고개를 돌려도 소리의 좌우가 바뀌지 않습니다. clearListenerPose 뒤에는 다시 카메라를 따릅니다.
    // 청취자가 될 액터를 고르는 함수(2026-11-18, 엔진 구조 안정화 S7). 엔진이 화면 카메라의 액터를 고르는 함수를 넘깁니다
    // (CameraComponent::activeIn). 소리 시스템은 카메라 형을 모르고, 방향은 그 액터의 월드 회전(-Z 정면, +Y 위)으로 정합니다.
    // 없거나 nullptr 을 돌려주면 청취자를 옮기지 않습니다. tick 마다(청취자를 옮길 때) 부릅니다.
    void setListenerSelector(std::function<Actor*(const World&)> select) { listenerSelector_ = std::move(select); }
    void setListenerPose(const glm::mat4& worldPose) { listenerPose_ = worldPose; hasListenerPose_ = true; }
    void clearListenerPose()                          { hasListenerPose_ = false; }

    // AudioComponent::play()/stop() 이 부릅니다. stopVoice 는 처음으로 되감습니다.
    void playVoice (AudioVoice* voice);
    void stopVoice (AudioVoice* voice);
    bool isVoicePlaying(const AudioVoice* voice) const;   // 가상 보이스(아래)도 재생 중으로 봅니다
    // 들리지 않아(예상 게인 −60 dB 미만, AudioAttenuation.hpp hvirt) 믹싱을 멈추고 재생 위치만 세는 중인지(S3, 2026-11-17).
    bool voiceVirtual(const AudioVoice* voice) const;

    // 보이스를 만들어 월드 그룹에 붙이고, 정리합니다. 만들기에 실패하면 이유를 로그로 남기고 nullptr 를 돌려줍니다.
    // 사슬은 ma_sound → 보이스 필터 → (HRTF 노드) → 월드 그룹입니다(AudioSystem.cpp 의 "음원별 노드 사슬").
    // stream 이면 통째로 디코딩하지 않고 재생하며 풉니다(긴 음악, MA_SOUND_FLAG_STREAM).
    AudioVoice* createVoice (const char* filePath, bool loop, bool stream = false, int bus = 0);

    // ── 사운드 단계 4(2026-11-17): 버스 · 마스터 볼륨 · 한 번 재생 · 보이스 예산 ──────────────────────────────
    // 마스터는 엔진 출력 전체(UI 소리 포함), 버스는 그 버스의 소리에 곱합니다. 0 ~ 2, 기본 1(예전 소리 그대로).
    void  setMasterVolume(float v);
    float masterVolume() const { return masterVolume_; }
    void  setBusVolume(EAudioBus bus, float v) { setBusVolume((int)bus, v); }
    float busVolume(EAudioBus bus) const       { return busVolume((int)bus); }

    // ── 버스 그래프 · 스냅숏 · 더킹 · 리미터 · 사운드 큐 (2026-11-21, Docs/Audio_Design.md 12절) ─────────────────────
    // 버스마다 노드가 하나 있고(D1) 사슬은 보이스 → 버스 → (부모 버스) → 월드 그룹 → 리미터 → 엔진 출력입니다. 표준 5버스는 id 0 ~ 4
    // (EAudioBus 와 같음)이고 부모가 없습니다. 버스 게인 = 볼륨 × CVar 배율 × 스냅숏 × 더킹, 음소거면 0(D2). 게인이 1 인 버스는 소리를
    // 건드리지 않습니다(바이트 동일).
    static constexpr int kMaxBuses = 32;
    int   addBus(const std::string& name, const std::string& parent = "");   // 새 id. 이름이 틀렸거나 · 있거나 · 부모가 없으면 -1
    int   findBus(std::string_view name) const;                               // 없으면 -1
    int   busCount() const { return (int)buses_.size(); }
    const std::string& busName(int id) const;
    int   busParent(int id) const;                                            // 없으면 -1(월드 그룹)
    void  setBusVolume(int id, float v);                                      // 사용자 볼륨(설정 화면), 0 ~ 2
    float busVolume(int id) const;
    void  setBusMuted(int id, bool muted);
    bool  busMuted(int id) const;
    float busGain(int id) const;            // 이 버스 노드에 넣는 게인
    float busEffectiveGain(int id) const;   // 부모까지 곱한 게인(들리는 크기 계산에 씀)
    float busPeak(int id) const;            // 마지막으로 렌더한 블록들의 버스 출력 피크(더킹용). 틱 사이에 렌더가 없었으면 직전 값(DEF-176)
    float busMeter(int id) const;           // 미터 표시용 피크: 가장 큰 값을 0.3 초 붙잡은 뒤 내려감(콘솔 `audio.buses`)
    float busDuckGain(int id) const;        // 더킹이 이 버스에 곱하는 값(1 = 그대로)
    float busSnapshotGain(int id) const;    // 스냅숏이 이 버스에 곱하는 값

    // 스냅숏(D3). 같은 이름이면 바꿉니다. 볼륨 표의 버스 id 가 틀리면 false.
    bool  defineSnapshot(const hmix::Snapshot& s);
    bool  setSnapshotActive(const std::string& name, bool on);   // 없는 이름이면 false
    bool  snapshotActive(const std::string& name) const;         // 켜져 있음(페이드 인 중 포함)
    float snapshotWeight(const std::string& name) const;         // 0 ~ 1

    // 더킹(D4). 규칙 번호를 돌려주고, 버스 id 가 틀리면 -1.
    int   addDuck(const hmix::DuckRule& rule);
    void  clearDucks() { ducks_.clear(); }

    // 마스터 리미터(D5). 기본 끔. 한도는 최종 출력(마스터 볼륨 뒤) 기준 dBFS.
    void  setLimiter(bool enabled, float ceilingDb = -1.f, float releaseSec = 0.1f);
    bool  limiterEnabled() const   { return limiterOn_; }
    float limiterCeilingDb() const { return limiterCeilingDb_; }

    // 사운드 큐(D6 ~ D9). 위치에서 큐를 한 번 냅니다(한 번 재생과 같은 규칙에 큐의 변형 · 범위 · 동시 재생 규칙). 낸 인스턴스 번호를,
    // 못 내면 0 을 돌려줍니다(큐를 못 읽음 · 재시작 간격 · rejectNew · 들리지 않음 · 한 번 재생 상한).
    uint32_t playCue(const std::string& cuePath, const glm::vec3& position, float volume = 1.f,
                     const AudioAttenuationSettings& attenuation = {});
    bool     setCueSeed(const std::string& cuePath, uint64_t seed);   // 큐를 못 읽으면 false
    size_t   cueInstanceCount(const std::string& cuePath);            // 지금 재생 중인 인스턴스 수
    bool     oneShotPlaying(uint32_t instance) const;                 // playCue 가 돌려준 번호가 아직 울리는지
    const hmix::SoundCue* cue(const std::string& cuePath);            // 읽은 큐(없으면 읽어 봄, 실패하면 nullptr)
    static bool isCuePath(std::string_view path);                      // .hcue 로 끝나는지
    // 보이스가 큐에서 뽑은 값(시험 · 디버그용). 큐가 아니면 -1 · 1 · 1.
    int   voiceCueVariant(const AudioVoice* voice) const;
    float voiceCuePitch(const AudioVoice* voice) const;
    float voiceCueVolume(const AudioVoice* voice) const;
    double clock() const { return clock_; }   // tick 의 dt 를 더한 시각(초, 일시정지 동안 멈춤)

    // CVar · 콘솔 명령(D10). Engine 이 시작할 때 한 번 부릅니다. 그 뒤 addBus 한 버스도 CVar 가 생깁니다.
    void registerCVars(CVarRegistry& reg);
    std::string busReport() const;   // `audio.buses` 의 글
    // 한 번 재생: 위치에서 소리를 한 번 내고 끝나면 스스로 정리합니다(총소리 · 발소리 · 부딪힘). 엔진이 소유하므로 컴포넌트가
    // 필요 없습니다. 차폐 · HRTF · 잔향은 걸지 않습니다(짧은 효과음용). 들리지 않을 만큼 멀거나(−60 dB 아래) 동시에
    // kMaxOneShots 개가 이미 울리면 내지 않고 false. 월드 그룹에 붙어 일시정지를 따릅니다.
    bool  playOneShot(const char* filePath, const glm::vec3& position, float volume = 1.f, EAudioBus bus = EAudioBus::Sfx,
                      const AudioAttenuationSettings& attenuation = {});
    size_t oneShotCount() const { return oneShots_.size(); }
    static constexpr size_t kMaxOneShots = 32;
    // 보이스 예산: 동시에 실제로 섞는 3D 소리 수의 상한. 넘으면 들리는 크기가 작은 것부터 가상화합니다(재생 위치는 셈, S3 와
    // 같은 길). 0 = 제한 없음. 기본 64(Quest 에서 HRTF · 필터를 거는 보이스가 늘면 이 값으로 비용을 묶음).
    void     setVoiceBudget(uint32_t n) { voiceBudget_ = n; }
    uint32_t voiceBudget() const { return voiceBudget_; }
    void        destroyVoice(AudioVoice* voice);

    // headless 일 때만 동작합니다. 스테레오 interleaved float 를 frameCount 프레임 채웁니다.
    bool renderHeadless(float* outStereo, uint32_t frameCount);

    // ── 출력 장치 (2026-10-16) ────────────────────────────────────────────────
    // 에디터 설정(그리고 나중에 게임의 설정 화면)이 고르는 출력 장치입니다. 엔진(ma_engine)은 이 시스템이 직접 만든 장치에
    // 연결되어 있어, 장치를 바꿔도 엔진 · 사운드는 그대로 두고 장치만 다시 엽니다(재생이 이어짐). UE 의 출력 장치 바꾸기
    // (Swap Audio Output Device), Godot 의 AudioServer.output_device 와 같은 역할입니다.
    //   setOutputDevice("")   시스템 기본 장치(운영체제에서 기본 장치를 바꾸면 따라감)
    //   setOutputDevice(id)   그 장치. 목록에 없거나 열리지 않으면(뽑힘 · 다른 앱의 독점 모드 등) 경고하고 시스템 기본 장치로 엽니다
    //   setOutputOverride(id) 잠시 다른 장치로(XR Play 동안 헤드셋). 없으면 고른 장치, 그것도 없으면 시스템 기본입니다.
    //                         비우면 setOutputDevice 로 고른 장치로 돌아갑니다
    // 연 장치를 실행 중에 잃으면 Play 동안 tick 이 간격을 두고 같은 순서로 다시 엽니다(DEF-145).
    // headless 면 값만 기억하고 장치는 열지 않습니다.
    std::vector<AudioOutputDevice> outputDevices() const;
    void setOutputDevice(const std::string& id);
    void setOutputOverride(const std::string& id);
    const std::string& outputDevice() const   { return requestedId_; }
    const std::string& outputOverride() const { return overrideId_; }
    std::string        activeOutputName() const;   // 지금 소리가 나가는 장치 이름(장치가 없으면 빈 문자열)
    bool               overrideActive() const;     // 덮어쓰기 장치(헤드셋)가 실제로 열려 있는지(DEF-147)
    // 열어 볼 장치 순서: 덮어쓰기 → 고른 장치 → 시스템 기본(빈 문자열). 같은 장치는 한 번만 넣습니다.
    static std::vector<std::string> outputCandidates(const std::string& overrideId, const std::string& requestedId);

    static constexpr uint32_t kSampleRate = 48000;
    static constexpr uint32_t kChannels   = 2;

private:
    // 지금 원하는 장치(덮어쓰기가 있으면 그것)를 엽니다. 이미 그 장치가 열려 돌고 있으면 아무것도 하지 않습니다.
    // 장치가 돌기 시작하면 true. quiet 면 실패 경고를 남기지 않습니다(tick 의 재시도).
    bool reopenDevice(bool quiet = false);
    // 연 장치가 멈췄는지(잃음) 보고, 멈췄으면 kOutputRetrySec 간격으로 다시 엽니다.
    void checkOutput(float dt);
    static constexpr float kOutputRetrySec = 2.f;
    bool  outputLost_  = false;
    float outputRetry_ = 0.f;
    void updateListener();
    // 차폐(2026-11-06): 소리마다 kRetraceSec 간격으로 판정하고(처음 만들 때는 바로 판정해 그 값으로 시작) 매 틱 평활해
    // 보이스 필터에 넘깁니다.
    void  updateOcclusion(AudioVoice& voice, const AudioComponent& ac, const glm::vec3& sourcePos, float dt);
    float traceOcclusion(const AudioComponent& ac, const glm::vec3& sourcePos);   // 막힌 표본의 비율
    bool  segmentBlocked(const glm::vec3& from, const glm::vec3& to) const;
    // 선분을 막은 물체의 두께(m, S4): 양쪽 끝에서 레이를 쏴 처음 닿는 거리를 빼서 구합니다. 벽이 여럿이면 그 사이 공간까지 두께로
    // 칩니다(보수적으로 더 막힘). 막히지 않으면 0.
    float segmentThickness(const glm::vec3& from, const glm::vec3& to) const;
    // 차폐 정도 · 공기 흡수를 합쳐 보이스 필터 값을 넣습니다(S2). first 면 보간 없이 그 값으로 시작.
    void  applyFilter(AudioVoice& voice, const AudioComponent& ac, const glm::vec3& sourcePos, bool first);
    // 도플러(S1): 위치 차분으로 속도를 추정해 음높이 배율을 넣습니다.
    void  updateDoppler(AudioVoice& voice, const AudioComponent& ac, const glm::vec3& sourcePos, float dt);
    // 가상화(S3): 예상 게인과, 그에 따라 멈추고 · 위치를 세고 · 이어 내기.
    float audibleGain(const AudioVoice& voice, const AudioComponent& ac, const glm::vec3& sourcePos) const;
    void  updateVirtual(AudioVoice& voice, const AudioComponent& ac, const glm::vec3& sourcePos, float dt);
    const PhysicsSystem* physics_ = nullptr;
    glm::vec3            listenerPos_{ 0.f };
    glm::vec3            listenerPrevPos_{ 0.f };   // 도플러용 청취자 속도 추정(S1)
    glm::vec3            listenerVel_{ 0.f };
    bool                 listenerHasPrev_ = false;
    Actor*               listenerActor_ = nullptr;   // 레이에서 뺄 청취자 쪽 액터(활성 카메라의 액터)
    std::function<Actor*(const World&)> listenerSelector_;   // setListenerSelector
    std::vector<Actor*>    ignore_;                   // 판정마다 다시 채우는 제외 목록(할당을 재사용)
    std::vector<glm::vec3> samples_;
    uint32_t               voiceSerial_ = 0;          // 판정 시점을 엇갈리게 하는 일련번호

    // 사운드 단계 4
    float    masterVolume_ = 1.f;
    uint32_t voiceBudget_  = 64;
    struct OneShot { AudioVoice* voice; glm::vec3 pos; AudioAttenuationSettings at; float volume; int bus; };
    std::vector<OneShot> oneShots_;
    void tickOneShots();
    void releaseOneShots();
    // 한 번 재생 공통(playOneShot · playCue). 낸 보이스, 못 내면 nullptr.
    AudioVoice* startOneShot(const char* filePath, const glm::vec3& position, float volume, int bus,
                             const AudioAttenuationSettings& attenuation, float pitch);

    // 버스 그래프(12절)
    struct BusState {
        std::string name;
        int   parent     = -1;
        float volume     = 1.f;   // 사용자 볼륨(setBusVolume)
        float cvarVolume = 1.f;   // audio.bus.<이름>.volume
        bool  muted      = false;
        bool  cvarMuted  = false;
        float snap       = 1.f;   // 스냅숏 섞은 값
        float duck       = 1.f;   // 더킹 값
        float peak       = 0.f;   // 마지막으로 렌더한 블록들의 출력 피크(DEF-176)
        float meter      = 0.f;   // 표시용: 붙잡은 피크
        float meterHold  = 0.f;   // meter 를 붙잡아 둘 남은 시간(초)
        void* node       = nullptr;   // .cpp 의 BusNode(엔진이 없으면 없음)
    };
    std::vector<BusState> buses_;
    bool  createBusNode(int id);
    void  destroyBusNodes();
    void  pushBusGains();             // 게인을 노드에 넘김
    void  updateMix(float dt);        // 미터 읽기 · 스냅숏 · 더킹 · 게인
    void  registerBusCVars(int id);
    void* busSink(int bus) const;     // 이 버스의 보이스가 붙는 노드(없으면 월드 그룹)
    void  routeVoice(AudioVoice& v, int bus);   // 보이스 사슬의 끝을 다른 버스로 옮김
    std::vector<hmix::Snapshot>       snapshots_;
    std::vector<hmix::ActiveSnapshot> activeSnaps_;
    uint64_t                          snapOrder_ = 0;
    uint64_t                          meterFrame_ = ~0ull;   // 지난 미터 읽기 때의 엔진 시각(PCM 프레임)
    struct Duck { hmix::DuckRule rule; hmix::DuckState st; };
    std::vector<Duck> ducks_;
    void* limiter_          = nullptr;   // .cpp 의 LimiterNode
    bool  limiterOn_        = false;
    float limiterCeilingDb_ = -1.f;
    float limiterRelease_   = 0.1f;
    bool  limiterRouted_    = false;     // 리미터가 지금 사슬에 들어 있는지(켜져 있을 때만 넣음)
    void  pushLimiter();
    double clock_ = 0.0;
    CVarRegistry* cvars_ = nullptr;
    uint32_t instanceSerial_ = 0;

    // 사운드 큐(12절 D6 ~ D9). 큐 파일 경로(정식 경로)마다 정의 · 랜덤 상태 · 재생 중 인스턴스.
    struct CueInstance { AudioVoice* voice = nullptr; double start = 0.0; bool oneShot = false; };
    struct CueState {
        bool                     ok = false;
        hmix::SoundCue           def;
        uint64_t                 rng = 0;
        double                   lastStart = -1e30;
        std::vector<CueInstance> instances;
    };
    std::map<std::string, CueState> cues_;
    CueState* loadCue(const std::string& cuePath);
    // 시작해도 되는지 보고(재시작 간격 · 상한 · 넘을 때 규칙), 되면 인스턴스로 넣습니다.
    bool admitCue(CueState& c, AudioVoice* voice, bool oneShot);
    void pruneCue(CueState& c);
    void forgetCueVoice(AudioVoice* voice);   // destroyVoice 가 부름
    // 큐를 가리키는 컴포넌트의 보이스를 만듭니다(변형 뽑기). 큐를 못 읽으면 nullptr.
    AudioVoice* createCueVoice(const AudioComponent& ac);
    int   componentBus(const AudioComponent& ac, const AudioVoice* v) const;
    void applyBudget(std::vector<std::pair<AudioVoice*, float>>& audible);

    // 잔향(2026-11-06): 구역 고르기 · 매개변수 평활 · 리버브 시작과 멈춤, 소리마다 센드 양.
    void updateReverb(float dt);
    void stopReverb();
    void updateSend(AudioVoice& voice, const AudioComponent& ac, const glm::vec3& sourcePos);
    void*              reverb_ = nullptr;   // 리버브 노드(.cpp 의 ReverbNode). 월드 그룹이 없으면 없음
    hrev::ReverbParams reverbCur_;          // 평활 뒤 지금 값(level 0 이면 멈춤)
    hrev::ReverbParams reverbTarget_;
    float              reverbFade_    = 0.5f;
    bool               reverbRunning_ = false;
    // HRTF(2026-11-06): 모드 · XR · 구현으로 hrtfActive_ 를 정하고(처음 필요할 때 Steam Audio 를 엶), 소리마다 노드를 넣고 빼며
    // 방향을 넘깁니다.
    void refreshHrtf();
    void updateHrtf(AudioVoice& voice, const AudioComponent& ac, const glm::vec3& sourcePos);
    bool createHrtfNode(AudioVoice& voice);
    void dropHrtfNodes();   // 모든 보이스의 HRTF 노드를 사슬에서 빼고 지움(구현을 바꾸기 전)
    std::unique_ptr<hhrtf::Backend> hrtf_;
    EHrtfMode hrtfMode_    = EHrtfMode::Auto;
    bool      xrActive_    = false;
    bool      hrtfActive_  = false;
    bool      hrtfTried_   = false;   // Steam Audio 열기를 해 봤는지(실패해도 되풀이하지 않음)
    bool      hrtfWarned_  = false;   // 노드 만들기 실패 경고를 남겼는지
    glm::vec3 listenerFwd_{ 0.f, 0.f, -1.f };
    glm::vec3 listenerUp_ { 0.f, 1.f, 0.f };
    void* sinkNode() const;   // 버스 노드가 붙는 곳(월드 그룹, 없으면 엔진 출력)

    // 컴포넌트 값(볼륨·Loop·3D 설정)을 사운드에 반영합니다. 매 틱 부르므로 플레이 중에
    // 디테일 패널에서 바꾼 값도 바로 적용됩니다.
    void applySettings(AudioVoice& voice, const AudioComponent& ac) const;
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
