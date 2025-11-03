#include "AudioComponent.hpp"
#include "../audio/AudioSystem.hpp"
#include <filesystem>

void AudioComponent::onDestroy() {
    if (sound_ && audioSys_) {
        audioSys_->destroySound(sound_);
        sound_    = nullptr;
        audioSys_ = nullptr;
    }
}

std::vector<PropDesc> AudioComponent::buildProps() {
    auto* p = this; return { AUDIO_PROPS(p) };
}

std::string AudioComponent::readOnlyInfo() const {
    if (filePath.empty()) return "(no file)";
    return std::filesystem::path(filePath).filename().string();
}

void AudioComponent::play() {
    if (audioSys_) audioSys_->playSound(sound_);
}

void AudioComponent::stop() {
    if (audioSys_) audioSys_->stopSound(sound_);
}

bool AudioComponent::isPlaying() const {
    return audioSys_ && audioSys_->isSoundPlaying(sound_);
}
