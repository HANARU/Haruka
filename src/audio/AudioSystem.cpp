// miniaudio 구현부는 MiniaudioImpl.cpp 에 있습니다. 여기서는 선언만 씁니다.
#include "miniaudio.h"

#include "AudioSystem.hpp"
#include "../components/AudioComponent.hpp"
#include "../core/World.hpp"
#include "../core/Actor.hpp"
#include "../components/CameraComponent.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

#include "../core/HLog.hpp"

static constinit HLogCategory LogAudio{ "Audio", ELogVerbosity::Log };

// EAudioAttenuation 은 ma_attenuation_model 값을 그대로 씁니다(applySettings 가 캐스트만 합니다).
static_assert((int)EAudioAttenuation::Inverse     == (int)ma_attenuation_model_inverse);
static_assert((int)EAudioAttenuation::Linear      == (int)ma_attenuation_model_linear);
static_assert((int)EAudioAttenuation::Exponential == (int)ma_attenuation_model_exponential);

static ma_engine*      asEngine(void* p) { return static_cast<ma_engine*>(p); }
static ma_sound*       asSound (void* p) { return static_cast<ma_sound*>(p); }
static ma_sound_group* asGroup (void* p) { return static_cast<ma_sound_group*>(p); }

void AudioSystem::startup() {
    auto* eng = new ma_engine();
    ma_engine_config cfg = ma_engine_config_init();
    cfg.channels   = kChannels;
    cfg.sampleRate = kSampleRate;
    cfg.noDevice   = headless_ ? MA_TRUE : MA_FALSE;

    if (const ma_result r = ma_engine_init(&cfg, eng); r != MA_SUCCESS) {
        HLOG(LogAudio, Error) << "ma_engine_init failed: " << ma_result_description(r);
        delete eng;
        return;
    }
    engine_ = eng;

    // 그룹은 기본으로 재생 상태이고 공간화가 꺼져 있어, 섞인 소리를 다시 감쇠하지 않습니다.
    // 피치도 끕니다(NO_PITCH). 켜 두면 그룹이 섞인 소리 전체를 배율 1 로 리샘플링하느라
    // 연산을 쓰고, 그 리샘플러가 한 프레임 늦게 내보내서 멈춘 소리의 마지막 샘플이 다음
    // 블록 첫머리에 남습니다(HarukaAudioTest 5절이 잡았습니다).
    auto* grp = new ma_sound_group();
    if (const ma_result r = ma_sound_group_init(eng, MA_SOUND_FLAG_NO_PITCH, nullptr, grp); r != MA_SUCCESS) {
        // 그룹이 없으면 사운드는 엔진 출력에 바로 붙습니다. 재생은 되고 일시정지만 듣지 않습니다.
        HLOG(LogAudio, Error) << "world sound group init failed: " << ma_result_description(r)
                              << " (pause will not silence sounds)";
        delete grp;
    } else {
        group_ = grp;
    }
    HLOG(LogAudio, Log) << "miniaudio initialized (48 kHz stereo" << (headless_ ? ", no device" : "") << ")";
}

void AudioSystem::shutdown() {
    if (!engine_) return;
    // 엔진보다 사운드를 먼저 정리합니다. World 는 Engine 이 소멸할 때 컴포넌트 onDestroy 없이
    // 해제되므로, 여기서 정리하지 않으면 해제된 엔진에 붙은 사운드와 사라질 이 객체를 가리키는
    // audioSys_ 가 컴포넌트에 남습니다.
    releaseWorldSounds();
    if (group_) {
        ma_sound_group_uninit(asGroup(group_));
        delete asGroup(group_);
        group_ = nullptr;
    }
    ma_engine_uninit(asEngine(engine_));
    delete asEngine(engine_);
    engine_ = nullptr;
    paused_ = false;
}

void AudioSystem::releaseWorldSounds() {
    if (!world_) return;
    for (auto& ap : world_->getActors())
        if (auto* ac = ap->getComponent<AudioComponent>(); ac && ac->sound_) {
            destroySound(ac->sound_);
            ac->sound_    = nullptr;
            ac->audioSys_ = nullptr;
        }
}

void AudioSystem::updateListener() {
    CameraComponent* cam = world_->getActiveCamera();
    if (!cam || !cam->owner) return;
    const glm::vec3 pos = cam->owner->worldPosition();
    const glm::vec3 fwd = cam->forward();
    const glm::vec3 up  = cam->up();
    ma_engine_listener_set_position(asEngine(engine_), 0, pos.x, pos.y, pos.z);
    // 예전에는 위치만 넘겨서 청취자가 늘 -Z 를 보고 있었고, 카메라를 돌려도 소리의 좌우가
    // 바뀌지 않았습니다. miniaudio 는 방향 × 위 벡터로 오른쪽을 정합니다. 카메라 자세가 액터
    // 회전이 된 뒤로(DEF-21) 롤이 있을 수 있으므로 월드 +Y 가 아니라 카메라의 위 벡터를 넘깁니다.
    ma_engine_listener_set_direction(asEngine(engine_), 0, fwd.x, fwd.y, fwd.z);
    ma_engine_listener_set_world_up(asEngine(engine_), 0, up.x, up.y, up.z);
}

void AudioSystem::applySettings(void* maSound, const AudioComponent& ac) {
    ma_sound* s = asSound(maSound);
    const AudioAttenuationSettings& at = ac.attenuation;
    ma_sound_set_volume(s, ac.volume);
    ma_sound_set_looping(s, ac.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_spatialization_enabled(s, at.spatialize ? MA_TRUE : MA_FALSE);
    ma_sound_set_attenuation_model(s, (ma_attenuation_model)at.model);
    ma_sound_set_min_distance(s, at.minDistance);
    ma_sound_set_max_distance(s, at.maxDistance);
    ma_sound_set_rolloff(s, at.rolloff);
}

void AudioSystem::tick(float /*dt*/) {
    if (!engine_ || !world_) return;
    updateListener();

    for (auto& actorPtr : world_->getActors()) {
        Actor* a = actorPtr.get();
        auto* ac = a->getComponent<AudioComponent>();
        // 실효 활성(컴포넌트 enabled && 액터 activeInHierarchy)일 때만 다룹니다. 렌더·틱과
        // 같은 기준이며, 꺼질 때 나오던 소리는 AudioComponent::onDisable 이 멈춥니다.
        if (!ac || !ac->isLive()) continue;

        bool created = false;
        if (!ac->sound_ && !ac->filePath.empty() && !ac->loadFailed_) {
            void* snd = nullptr;
            if (initSound(ac->filePath.c_str(), ac->loop, &snd)) {
                ac->sound_    = snd;
                ac->audioSys_ = this;
                created       = true;
            } else {
                ac->loadFailed_ = true;   // 다음 플레이 시작(onBeginPlay)까지 다시 시도하지 않습니다
            }
        }
        if (!ac->sound_) continue;

        const glm::vec3 pos = a->worldPosition();
        ma_sound_set_position(asSound(ac->sound_), pos.x, pos.y, pos.z);
        applySettings(ac->sound_, *ac);
        // 위치와 설정을 넣은 뒤에 시작해야 첫 소리가 원점에서 나지 않습니다.
        if (created && ac->autoPlay) ma_sound_start(asSound(ac->sound_));
    }
}

void AudioSystem::onBeginPlay() {
    // 지난 플레이에서 불러오기에 실패한 파일을 이번 플레이에서 다시 시도합니다.
    if (world_)
        for (auto& ap : world_->getActors()) {
            auto* ac = ap->getComponent<AudioComponent>();
            if (ac && ac->sound_ == nullptr) ac->loadFailed_ = false;
        }
    onPauseChanged(false);   // 어떤 경로로든 멈춘 그룹이 남아 있으면 풉니다
}

void AudioSystem::onEndPlay() {
    // 플레이가 끝나면 남은 소리를 멈춥니다. 에디터는 Stop 에서 월드를 되돌리며 사운드를 이미
    // 정리하므로 보통은 할 일이 없지만, 다른 경로로 끝나도 소리가 남지 않게 합니다.
    if (world_)
        for (auto& ap : world_->getActors())
            if (auto* ac = ap->getComponent<AudioComponent>(); ac && ac->sound_)
                stopSound(ac->sound_);
    // Pause 상태에서 Stop 하면 그룹이 멈춘 채 남아 다음 플레이가 무음이 됩니다. 여기서 풉니다.
    onPauseChanged(false);
}

void AudioSystem::onPauseChanged(bool paused) {
    if (!group_ || paused == paused_) return;
    if (paused) ma_sound_group_stop(asGroup(group_));
    else        ma_sound_group_start(asGroup(group_));
    paused_ = paused;
}

bool AudioSystem::initSound(const char* filePath, bool loop, void** outSound) {
    if (!engine_) return false;
    auto* snd = new ma_sound();
    // ASYNC 여도 miniaudio 는 디코더를 초기화할 때까지 기다렸다가 돌아옵니다(WAIT_INIT 강제).
    // 그래서 없는 파일과 모르는 형식은 여기서 바로 실패하고, 디코딩은 믹싱 중에 합니다.
    const ma_result r = ma_sound_init_from_file(asEngine(engine_), filePath, MA_SOUND_FLAG_ASYNC,
                                                asGroup(group_), nullptr, snd);
    if (r != MA_SUCCESS) {
        HLOG(LogAudio, Error) << "failed to load '" << filePath << "': " << ma_result_description(r);
        delete snd;
        return false;
    }
    ma_sound_set_looping(snd, loop ? MA_TRUE : MA_FALSE);
    *outSound = snd;
    return true;
}

void AudioSystem::destroySound(void* maSound) {
    if (!maSound) return;
    auto* snd = asSound(maSound);
    ma_sound_uninit(snd);
    delete snd;
}

void AudioSystem::playSound(void* maSound) {
    if (maSound) ma_sound_start(asSound(maSound));
}

void AudioSystem::stopSound(void* maSound) {
    if (!maSound) return;
    // ma_sound_stop 은 재생 위치를 그대로 두는 일시정지입니다. 엔진의 stop 은 처음으로
    // 되감는 것이 규칙이라(Unity·UE 와 같음) 되감기를 함께 겁니다. 되감기는 다음 믹싱에서
    // 적용됩니다.
    ma_sound_stop(asSound(maSound));
    ma_sound_seek_to_pcm_frame(asSound(maSound), 0);
}

bool AudioSystem::isSoundPlaying(void* maSound) const {
    if (!maSound) return false;
    return ma_sound_is_playing(asSound(maSound)) == MA_TRUE;
}

bool AudioSystem::renderHeadless(float* outStereo, uint32_t frameCount) {
    if (!engine_ || !headless_) return false;
    ma_uint64 read = 0;
    const ma_result r = ma_engine_read_pcm_frames(asEngine(engine_), outStereo, frameCount, &read);
    return r == MA_SUCCESS && read == frameCount;
}
