#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "AudioSystem.hpp"
#include "../components/AudioComponent.hpp"
#include "../core/World.hpp"
#include "../core/Actor.hpp"
#include "../components/CameraComponent.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

#include <iostream>
#include <cstring>

void AudioSystem::startup() {
    auto* eng = new ma_engine();
    ma_engine_config cfg = ma_engine_config_init();
    cfg.channels    = 2;
    cfg.sampleRate  = 48000;

    if (ma_engine_init(&cfg, eng) != MA_SUCCESS) {
        std::cerr << "[Audio] ma_engine_init failed\n";
        delete eng;
        return;
    }
    engine_ = eng;
    std::cout << "[Audio] miniaudio initialized (48 kHz stereo)\n";
}

void AudioSystem::shutdown() {
    if (!engine_) return;
    auto* eng = static_cast<ma_engine*>(engine_);
    ma_engine_uninit(eng);
    delete eng;
    engine_ = nullptr;
}

void AudioSystem::tick(float /*dt*/) {
    if (!engine_ || !world_) return;
    auto* eng = static_cast<ma_engine*>(engine_);

    // Update 3D listener from active camera
    if (auto* cam = world_->getActiveCamera(); cam && cam->owner) {
        glm::vec3 pos = cam->owner->worldPosition();
        ma_engine_listener_set_position(eng, 0, pos.x, pos.y, pos.z);
    }

    // Initialize any new AudioComponents and update 3D positions
    for (auto& actorPtr : world_->getActors()) {
        Actor* a = actorPtr.get();
        auto* ac = a->getComponent<AudioComponent>();
        // activeInHierarchy() 게이트(A-2): 비활성 액터(또는 비활성 부모 아래)의 사운드는
        // 초기화·autoPlay·3D 갱신을 하지 않아 렌더·틱과 일관됩니다. 한계: 이미 재생 중인
        // 사운드를 플레이 도중 setActive(false)로 끄는 경로는 아직 정지시키지 않습니다.
        if (!ac || !ac->isEnabled() || !a->activeInHierarchy()) continue;

        // First-time initialization
        if (!ac->sound_ && !ac->filePath.empty() && !ac->loadFailed_) {
            void* snd = nullptr;
            if (initSound(ac->filePath.c_str(), ac->loop, &snd)) {
                ac->sound_    = snd;
                ac->audioSys_ = this;
                ma_sound_set_volume(static_cast<ma_sound*>(snd), ac->volume);
                ma_sound_set_spatialization_enabled(static_cast<ma_sound*>(snd), MA_TRUE);
                if (ac->autoPlay)
                    ma_sound_start(static_cast<ma_sound*>(snd));
            } else {
                std::cerr << "[Audio] Failed to load: " << ac->filePath << "\n";
                ac->loadFailed_ = true;
            }
        }

        // Sync 3D position
        if (ac->sound_) {
            glm::vec3 pos = a->worldPosition();
            ma_sound_set_position(static_cast<ma_sound*>(ac->sound_), pos.x, pos.y, pos.z);
            ma_sound_set_volume(static_cast<ma_sound*>(ac->sound_), ac->volume);
        }
    }
}

void AudioSystem::onBeginPlay() {
    if (!world_) return;
    for (auto& ap : world_->getActors()) {
        auto* ac = ap->getComponent<AudioComponent>();
        if (ac && ac->sound_ == nullptr)
            ac->loadFailed_ = false;
    }
}

bool AudioSystem::initSound(const char* filePath, bool loop, void** outSound) {
    if (!engine_) return false;
    auto* snd = new ma_sound();
    uint32_t flags = MA_SOUND_FLAG_ASYNC;
    if (ma_sound_init_from_file(static_cast<ma_engine*>(engine_),
                                filePath, flags, nullptr, nullptr, snd) != MA_SUCCESS) {
        delete snd;
        return false;
    }
    ma_sound_set_looping(snd, loop ? MA_TRUE : MA_FALSE);
    *outSound = snd;
    return true;
}

void AudioSystem::destroySound(void* maSound) {
    if (!maSound) return;
    auto* snd = static_cast<ma_sound*>(maSound);
    ma_sound_uninit(snd);
    delete snd;
}

void AudioSystem::playSound(void* maSound) {
    if (maSound) ma_sound_start(static_cast<ma_sound*>(maSound));
}

void AudioSystem::stopSound(void* maSound) {
    if (maSound) ma_sound_stop(static_cast<ma_sound*>(maSound));
}

bool AudioSystem::isSoundPlaying(void* maSound) const {
    if (!maSound) return false;
    return ma_sound_is_playing(static_cast<ma_sound*>(maSound)) == MA_TRUE;
}
