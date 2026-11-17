#include "AudioComponent.hpp"
#include "../audio/AudioSystem.hpp"
#include "../core/ComponentFields.hpp"
#include <algorithm>
#include <filesystem>

void AudioComponent::onEnable() {
    // 사운드가 아직 없으면(부착 직후·플레이 전) 아무 일도 없고, AudioSystem::tick 이
    // 사운드를 만들면서 autoPlay 를 처리합니다.
    if (autoPlay) play();
}

void AudioComponent::onDisable() {
    // 예전에는 플레이 중 setActive(false) 를 해도 이미 나오던 소리가 계속 났습니다.
    stop();
}

void AudioComponent::onDestroy() {
    releaseSound();
}

void AudioComponent::releaseSound() {
    if (voice_ && audioSys_) audioSys_->destroyVoice(voice_);
    voice_    = nullptr;
    audioSys_ = nullptr;
}

void AudioComponent::setFile(const std::string& path) {
    releaseSound();
    filePath    = path;
    loadFailed_ = false;
}

std::vector<PropDesc> AudioComponent::buildProps() {
    using S = AudioAttenuationSettings;
    auto* p = this;
    std::vector<PropDesc> props;

    props.push_back(makePropFloat("Volume", PropColor::Neutral, 0.01f,
        [p]{ return p->volume; }, [p](float v){ p->volume = std::clamp(v, 0.f, 2.f); }, PropUnit::Ratio));
    props.push_back(makePropToggle("Loop",
        [p]{ return p->loop; }, [p](bool v){ p->loop = v; }));
    props.push_back(makePropToggle("AutoPlay",
        [p]{ return p->autoPlay; }, [p](bool v){ p->autoPlay = v; }));

    // Spatialize 를 끄면 위치와 무관한 2D 소리라 아래 거리 행은 의미가 없어 숨깁니다.
    // 토글 행이 바뀌면 패널을 다시 만들므로 행이 곧바로 나타나고 사라집니다.
    props.push_back(makePropToggle("Spatialize",
        [p]{ return p->attenuation.spatialize; }, [p](bool v){ p->attenuation.spatialize = v; }));
    if (attenuation.spatialize) {
        // 드롭다운 순서는 EAudioAttenuation 값(1부터)에서 1을 뺀 것입니다.
        props.push_back(makePropEnum("Attenuation", {"Inverse", "Linear", "Exponential"},
            [p]{ return (int)p->attenuation.model - 1; },
            [p](int v){ p->attenuation.model = (EAudioAttenuation)(std::clamp(v, 0, 2) + 1); }));
        // Min 과 Max 는 서로를 넘지 못합니다. miniaudio 는 min >= max 이면 감쇠를 끕니다.
        props.push_back(makePropFloat("Min Distance", PropColor::Neutral, 0.1f,
            [p]{ return p->attenuation.minDistance; },
            [p](float v){
                p->attenuation.minDistance = std::clamp(v, S::kMinDistanceFloor,
                    std::max(S::kMinDistanceFloor, p->attenuation.maxDistance - S::kMinGap));
            }, PropUnit::Size));
        props.push_back(makePropFloat("Max Distance", PropColor::Neutral, 1.f,
            [p]{ return p->attenuation.maxDistance; },
            [p](float v){ p->attenuation.maxDistance = std::max(p->attenuation.minDistance + S::kMinGap, v); }, PropUnit::Size));
        props.push_back(makePropFloat("Rolloff", PropColor::Neutral, 0.05f,
            [p]{ return p->attenuation.rolloff; },
            [p](float v){ p->attenuation.rolloff = std::max(0.f, v); }));
        props.push_back(makePropToggle("Silent Beyond Max",
            [p]{ return p->attenuation.silentBeyondMax; }, [p](bool v){ p->attenuation.silentBeyondMax = v; }));

        // 잔향 센드(2026-11-06). 잔향 구역(Reverb Zone)이 없으면 들리지 않습니다.
        props.push_back(makePropFloat("Reverb Send", PropColor::Neutral, 0.01f,
            [p]{ return p->reverbSend; },
            [p](float v){ p->reverbSend = std::clamp(v, 0.f, kMaxReverbSend); }, PropUnit::Ratio));
        props.push_back(makePropFloat("Reverb Reach", PropColor::Neutral, 0.01f,
            [p]{ return p->reverbReach; },
            [p](float v){ p->reverbReach = std::clamp(v, 0.f, 1.f); }, PropUnit::Ratio));

        // 차폐(2026-11-06). UE 의 Attenuation > Occlusion 과 같은 항목이고, 끄면 아래 행을 숨깁니다.
        using O = AudioOcclusionSettings;
        props.push_back(makePropToggle("Occlusion",
            [p]{ return p->occlusion.enabled; }, [p](bool v){ p->occlusion.enabled = v; }));
        if (occlusion.enabled) {
            props.push_back(makePropFloat("Occluded Low Pass", PropColor::Neutral, 50.f,
                [p]{ return p->occlusion.lowPassHz; },
                [p](float v){ p->occlusion.lowPassHz = std::clamp(v, O::kMinHz, O::kMaxHz); }));
            props.push_back(makePropFloat("Occluded Volume", PropColor::Neutral, 0.01f,
                [p]{ return p->occlusion.volume; },
                [p](float v){ p->occlusion.volume = std::clamp(v, 0.f, 1.f); }, PropUnit::Ratio));
            props.push_back(makePropFloat("Occlusion Time", PropColor::Neutral, 0.01f,
                [p]{ return p->occlusion.smoothTime; },
                [p](float v){ p->occlusion.smoothTime = std::clamp(v, 0.f, O::kMaxSmooth); }));
            props.push_back(makePropFloat("Source Radius", PropColor::Neutral, 0.05f,
                [p]{ return p->occlusion.radius; },
                [p](float v){ p->occlusion.radius = std::clamp(v, 0.f, O::kMaxRadius); }, PropUnit::Size));
        }
    }
    return props;
}

std::string AudioComponent::readOnlyInfo() const {
    if (filePath.empty()) return "(no file)";
    return std::filesystem::path(filePath).filename().string();
}

void AudioComponent::play() {
    if (audioSys_) audioSys_->playVoice(voice_);
}

void AudioComponent::stop() {
    if (audioSys_) audioSys_->stopVoice(voice_);
}

bool AudioComponent::isPlaying() const {
    return audioSys_ && audioSys_->isVoicePlaying(voice_);
}

float AudioComponent::occlusionAmount() const {
    return audioSys_ ? audioSys_->voiceOcclusion(voice_) : 0.f;
}

// ── 직렬화 (DEF-22) ─────────────────────────────────────────────────────────────
// 3D 설정은 AudioAttenuationSettings 의 텍스트 형식 그대로입니다(감쇠 모델은 이름).
void AudioComponent::writeFields(ComponentFieldWriter& w) const {
    w.s("file",        filePath);
    w.f("volume",      volume);
    w.b("loop",        loop);
    w.b("autoPlay",    autoPlay);
    w.s("attenuation", attenuation.toText());
    if (attenuation.silentBeyondMax) w.b("silentBeyondMax", true);   // 2026-11-17. 켰을 때만(기존 씬 파일 그대로)
    w.s("occlusion",   occlusion.toText());   // 2026-11-06. 옛 파일에는 없고, 없으면 기본(끔)입니다
    w.f("reverbSend",  reverbSend);           // 2026-11-06. 없으면 기본값
    w.f("reverbReach", reverbReach);
}

bool AudioComponent::readField(const std::string& key, const std::string& value) {
    bool ok = true;
    if      (key == "file")     filePath = value;
    else if (key == "volume")   ok = fieldparse::toFloat(value, volume);
    else if (key == "loop")     ok = fieldparse::toBool (value, loop);
    else if (key == "autoPlay") ok = fieldparse::toBool (value, autoPlay);
    else if (key == "attenuation") {
        // 3D 설정이 깨진 것은 소리를 버릴 이유가 아닙니다. 기본 설정으로 두고 알립니다.
        AudioAttenuationSettings a;
        a.silentBeyondMax = attenuation.silentBeyondMax;   // 다른 키라 줄 순서와 상관없이 지킴
        if (AudioAttenuationSettings::fromText(value, a)) attenuation = a;
        else ok = false;
    }
    else if (key == "silentBeyondMax") ok = fieldparse::toBool(value, attenuation.silentBeyondMax);
    else if (key == "occlusion") {
        AudioOcclusionSettings o;
        if (AudioOcclusionSettings::fromText(value, o)) occlusion = o;
        else ok = false;
    }
    else if (key == "reverbSend" || key == "reverbReach") {
        float v;
        const float hi = key == "reverbSend" ? kMaxReverbSend : 1.f;
        ok = fieldparse::toFloat(value, v) && v >= 0.f && v <= hi;
        if (ok) (key == "reverbSend" ? reverbSend : reverbReach) = v;
    }
    else return false;
    if (!ok) reportBadField(key, value);
    return true;
}
