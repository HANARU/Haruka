#pragma once
#include "../core/Component.hpp"
#include <string>

class AudioSystem;

#define AUDIO_PROPS(p) \
    XPROP_FloatClamp(p, "Volume",  PropColor::Neutral, 0.01f, volume,  0.f, 2.f), \
    XPROP_Toggle    (p, "Loop",    loop),                                          \
    XPROP_Toggle    (p, "AutoPlay",autoPlay)

class AudioComponent : public Component {
public:
    std::string filePath;
    float       volume   = 1.f;
    bool        loop     = false;
    bool        autoPlay = true;

    void onDestroy() override;

    void play();
    void stop();
    bool isPlaying() const;

    bool editorIcon(glm::vec4& c) const override { c = {0.3f, 0.85f, 0.4f, 1.f}; return true; }

    const char*           typeName()     const override { return "AudioComponent"; }
    std::vector<PropDesc> buildProps()         override;
    std::string           readOnlyInfo() const override;

    // Managed by AudioSystem — do not touch from outside.
    // Reset loadFailed_ to false after changing filePath to trigger a retry.
    void*        sound_      = nullptr;
    AudioSystem* audioSys_   = nullptr;
    bool         loadFailed_ = false;
};
