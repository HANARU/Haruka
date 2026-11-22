#include "AudioComponent.hpp"
#include "../vfs/LogicalPath.hpp"
#include "../scene/Actor.hpp"
#include "AudioSystem.hpp"
#include "../scene/ComponentFields.hpp"
#include "../scene/World.hpp"
#include "../core/HText.hpp"
#include "../core/Subtitles.hpp"
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

    // 재생할 파일(H3b, 에셋 경로 칸): 소리 · 소리 큐. 바꾸면 setFile(지금 소리를 놓고 다음 틱에 새 파일로 다시 만듦, 콘텐츠 브라우저의
    // 소리 지정과 같은 길). 기본값은 두지 않습니다(지정한 파일이 그 액터의 값).
    props.push_back(makePropAssetPath("File", "Sound,Sound Cue", [p] { return p->filePath; },
                                      [p](const std::string& v) { if (v != p->filePath) p->setFile(v); }));
    props.push_back(makePropFloat("Volume", PropColor::Neutral, 0.01f,
        [p]{ return p->volume; }, [p](float v){ p->volume = std::clamp(v, 0.f, 2.f); }, PropUnit::Ratio).withDefault(1.f));
    props.push_back(makePropToggle("Loop",
        [p]{ return p->loop; }, [p](bool v){ p->loop = v; }).withDefault(false));
    props.push_back(makePropToggle("AutoPlay",
        [p]{ return p->autoPlay; }, [p](bool v){ p->autoPlay = v; }).withDefault(true));
    props.push_back(makePropEnum("Bus", { "SFX", "Music", "Voice", "Ambient", "UI" },
        [p]{ return (int)p->bus; }, [p](int v){ p->bus = (EAudioBus)std::clamp(v, 0, kAudioBusCount - 1); }).withDefault(EAudioBus::Sfx));
    props.push_back(makePropToggle("Stream",
        [p]{ return p->stream; }, [p](bool v){ p->stream = v; }).withDefault(false));

    // Spatialize 를 끄면 위치와 무관한 2D 소리라 아래 거리 행은 의미가 없어 숨깁니다.
    // 토글 행이 바뀌면 패널을 다시 만들므로 행이 곧바로 나타나고 사라집니다.
    props.push_back(makePropToggle("Spatialize",
        [p]{ return p->attenuation.spatialize; }, [p](bool v){ p->attenuation.spatialize = v; }).withDefault(true));
    if (attenuation.spatialize) {
        // 드롭다운 순서는 EAudioAttenuation 값(1부터)에서 1을 뺀 것입니다.
        props.push_back(makePropEnum("Attenuation", {"Inverse", "Linear", "Exponential"},
            [p]{ return (int)p->attenuation.model - 1; },
            [p](int v){ p->attenuation.model = (EAudioAttenuation)(std::clamp(v, 0, 2) + 1); }).withDefault(0.f));
        // Min 과 Max 는 서로를 넘지 못합니다. miniaudio 는 min >= max 이면 감쇠를 끕니다.
        props.push_back(makePropFloat("Min Distance", PropColor::Neutral, 0.1f,
            [p]{ return p->attenuation.minDistance; },
            [p](float v){
                p->attenuation.minDistance = std::clamp(v, S::kMinDistanceFloor,
                    std::max(S::kMinDistanceFloor, p->attenuation.maxDistance - S::kMinGap));
            }, PropUnit::Size).withDefault(1.f));
        props.push_back(makePropFloat("Max Distance", PropColor::Neutral, 1.f,
            [p]{ return p->attenuation.maxDistance; },
            [p](float v){ p->attenuation.maxDistance = std::max(p->attenuation.minDistance + S::kMinGap, v); }, PropUnit::Size).withDefault(100.f));
        props.push_back(makePropFloat("Rolloff", PropColor::Neutral, 0.05f,
            [p]{ return p->attenuation.rolloff; },
            [p](float v){ p->attenuation.rolloff = std::max(0.f, v); }).withDefault(1.f));
        props.push_back(makePropToggle("Silent Beyond Max",
            [p]{ return p->attenuation.silentBeyondMax; }, [p](bool v){ p->attenuation.silentBeyondMax = v; }).withDefault(false));

        // 잔향 센드(2026-11-06). 잔향 구역(Reverb Zone)이 없으면 들리지 않습니다.
        props.push_back(makePropFloat("Reverb Send", PropColor::Neutral, 0.01f,
            [p]{ return p->reverbSend; },
            [p](float v){ p->reverbSend = std::clamp(v, 0.f, kMaxReverbSend); }, PropUnit::Ratio).withDefault(1.f));
        props.push_back(makePropFloat("Reverb Reach", PropColor::Neutral, 0.01f,
            [p]{ return p->reverbReach; },
            [p](float v){ p->reverbReach = std::clamp(v, 0.f, 1.f); }, PropUnit::Ratio).withDefault(0.5f));

        // 차폐(2026-11-06). UE 의 Attenuation > Occlusion 과 같은 항목이고, 끄면 아래 행을 숨깁니다.
        using O = AudioOcclusionSettings;
        props.push_back(makePropToggle("Occlusion",
            [p]{ return p->occlusion.enabled; }, [p](bool v){ p->occlusion.enabled = v; }).withDefault(false));
        if (occlusion.enabled) {
            props.push_back(makePropFloat("Occluded Low Pass", PropColor::Neutral, 50.f,
                [p]{ return p->occlusion.lowPassHz; },
                [p](float v){ p->occlusion.lowPassHz = std::clamp(v, O::kMinHz, O::kMaxHz); }).withDefault(1200.f));
            props.push_back(makePropFloat("Occluded Volume", PropColor::Neutral, 0.01f,
                [p]{ return p->occlusion.volume; },
                [p](float v){ p->occlusion.volume = std::clamp(v, 0.f, 1.f); }, PropUnit::Ratio).withDefault(0.4f));
            props.push_back(makePropFloat("Occlusion Time", PropColor::Neutral, 0.01f,
                [p]{ return p->occlusion.smoothTime; },
                [p](float v){ p->occlusion.smoothTime = std::clamp(v, 0.f, O::kMaxSmooth); }).withDefault(0.1f));
            props.push_back(makePropFloat("Source Radius", PropColor::Neutral, 0.05f,
                [p]{ return p->occlusion.radius; },
                [p](float v){ p->occlusion.radius = std::clamp(v, 0.f, O::kMaxRadius); }, PropUnit::Size).withDefault(0.f));
            props.push_back(makePropFloat("Full Block Thickness", PropColor::Neutral, 0.01f,
                [p]{ return p->occlusion.fullThickness; },
                [p](float v){ p->occlusion.fullThickness = std::clamp(v, 0.f, O::kMaxThickness); }, PropUnit::Size).withDefault(0.f));
            props.push_back(makePropToggle("Rotate Samples",
                [p]{ return p->occlusion.rotateSamples; }, [p](bool v){ p->occlusion.rotateSamples = v; }).withDefault(false));
        }

        // 지향성 콘(사운드 단계 4): 안쪽 각 360 이면 끔.
        props.push_back(makePropFloat("Cone Inner", PropColor::Neutral, 1.f,
            [p]{ return p->cone.innerDeg; },
            [p](float v){ p->cone.innerDeg = std::clamp(v, 0.f, 360.f); p->cone.outerDeg = std::max(p->cone.outerDeg, p->cone.innerDeg); }).withDefault(360.f));
        props.push_back(makePropFloat("Cone Outer", PropColor::Neutral, 1.f,
            [p]{ return p->cone.outerDeg; },
            [p](float v){ p->cone.outerDeg = std::clamp(v, p->cone.innerDeg, 360.f); }).withDefault(360.f));
        props.push_back(makePropFloat("Cone Outer Gain", PropColor::Neutral, 0.01f,
            [p]{ return p->cone.outerGain; },
            [p](float v){ p->cone.outerGain = std::clamp(v, 0.f, 1.f); }, PropUnit::Ratio).withDefault(0.f));

        // 전파(2026-11-17, S1 · S2): 도플러와 공기 흡수. 기본은 둘 다 꺼져 예전 소리 그대로입니다.
        using P = AudioPropagationSettings;
        props.push_back(makePropFloat("Doppler", PropColor::Neutral, 0.05f,
            [p]{ return p->propagation.dopplerFactor; },
            [p](float v){ p->propagation.dopplerFactor = std::clamp(v, 0.f, P::kMaxDoppler); }, PropUnit::Ratio).withDefault(0.f));
        props.push_back(makePropToggle("Air Absorption",
            [p]{ return p->propagation.airAbsorption; }, [p](bool v){ p->propagation.airAbsorption = v; }).withDefault(false));
        if (propagation.airAbsorption)
            props.push_back(makePropFloat("Air Absorption Hz", PropColor::Neutral, 50.f,
                [p]{ return p->propagation.airAbsorptionHz; },
                [p](float v){ p->propagation.airAbsorptionHz = std::clamp(v, P::kMinHz, P::kMaxHz); }).withDefault(3000.f));
    }
    return props;
}

std::string AudioComponent::readOnlyInfo() const {
    if (filePath.empty()) return "(no file)";
    return std::filesystem::path(filePath).filename().string();
}

void AudioComponent::play() {
    if (audioSys_) {
        audioSys_->playVoice(voice_);
        emitSubtitle();   // 소리 장치가 없거나 사운드를 아직 만들지 않았어도 Play 중이면 자막은 냄
    }
}

void AudioComponent::emitSubtitle() {
    if (subtitle.empty() || !owner || !owner->world()) return;
    SubtitleQueue* q = owner->world()->service<SubtitleQueue>();
    if (!q) return;
    SubtitleLine l;
    const size_t colon = subtitleKey.find(':');
    l.text = colon != std::string::npos && colon > 0 && colon + 1 < subtitleKey.size()
           ? HText::localized(subtitleKey.substr(0, colon), subtitleKey.substr(colon + 1), subtitle)
           : HText::fromString(subtitle);
    if (!subtitleSpeaker.empty()) l.speaker = HText::fromString(subtitleSpeaker);
    l.id = "audio:" + std::to_string(owner->id);
    q->push(std::move(l));
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
    w.s("file",        hvfs::toCanonical(filePath));
    w.f("volume",      volume);
    w.b("loop",        loop);
    w.b("autoPlay",    autoPlay);
    w.s("attenuation", attenuation.toText());
    if (attenuation.silentBeyondMax) w.b("silentBeyondMax", true);   // 2026-11-17. 켰을 때만(기존 씬 파일 그대로)
    w.s("occlusion",   occlusion.toText());   // 2026-11-06. 옛 파일에는 없고, 없으면 기본(끔)입니다
    if (!occlusion.extraIsDefault()) w.s("occlusionExtra", occlusion.extraToText());       // 2026-11-17. 기본값이면 쓰지 않음
    if (!(propagation == AudioPropagationSettings{})) w.s("propagation", propagation.toText());
    if (bus != EAudioBus::Sfx) w.s("bus", audioBusName(bus));        // 2026-11-17 사운드 단계 4. 기본값이면 쓰지 않음
    if (stream) w.b("stream", true);
    if (cone.enabled()) w.s("cone", cone.toText());
    w.f("reverbSend",  reverbSend);           // 2026-11-06. 없으면 기본값
    w.f("reverbReach", reverbReach);
    if (!subtitle.empty())        w.s("subtitle", subtitle);   // 2026-11-21 접근성. 비면 쓰지 않음(기존 씬 파일 그대로)
    if (!subtitleKey.empty())     w.s("subtitleKey", subtitleKey);
    if (!subtitleSpeaker.empty()) w.s("subtitleSpeaker", subtitleSpeaker);
}

bool AudioComponent::readField(const std::string& key, const std::string& value) {
    bool ok = true;
    if      (key == "file")     filePath = value;
    else if (key == "volume")   ok = fieldparse::toFloat(value, volume);
    else if (key == "loop")     ok = fieldparse::toBool (value, loop);
    else if (key == "autoPlay") ok = fieldparse::toBool (value, autoPlay);
    else if (key == "subtitle")        subtitle = value;
    else if (key == "subtitleKey")     subtitleKey = value;
    else if (key == "subtitleSpeaker") subtitleSpeaker = value;
    else if (key == "attenuation") {
        // 3D 설정이 깨진 것은 소리를 버릴 이유가 아닙니다. 기본 설정으로 두고 알립니다.
        // fromText 는 결과 전체를 새 값으로 채우므로, 다른 키(silentBeyondMax)로 이미 읽은 값을 읽은 뒤에 되돌려 놓습니다
        // (줄 순서와 상관없이 지킴).
        AudioAttenuationSettings a;
        if (AudioAttenuationSettings::fromText(value, a)) {
            a.silentBeyondMax = attenuation.silentBeyondMax;
            attenuation = a;
        } else ok = false;
    }
    else if (key == "silentBeyondMax") ok = fieldparse::toBool(value, attenuation.silentBeyondMax);
    else if (key == "occlusion") {
        AudioOcclusionSettings o;
        if (AudioOcclusionSettings::fromText(value, o)) {
            o.fullThickness = occlusion.fullThickness;   // 다른 키(occlusionExtra)라 줄 순서와 상관없이 지킴
            o.rotateSamples = occlusion.rotateSamples;
            occlusion = o;
        } else ok = false;
    }
    else if (key == "occlusionExtra") ok = occlusion.extraFromText(value);
    else if (key == "propagation") ok = AudioPropagationSettings::fromText(value, propagation);
    else if (key == "bus")         ok = audioBusFromName(value, bus);
    else if (key == "stream")      ok = fieldparse::toBool(value, stream);
    else if (key == "cone")        ok = AudioConeSettings::fromText(value, cone);
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

