#pragma once
#include "../core/IEngineModule.hpp"

class World;

// miniaudio-based audio system.
// Manages the ma_engine and acts as the intermediary for AudioComponents.
// AudioComponent initialization and 3D listener update both happen in tick().
class AudioSystem : public IEngineModule {
public:
    void        startup()      override;
    void        shutdown()     override;
    void        tick(float dt) override;
    void        onBeginPlay()  override;
    const char* name() const   override { return "AudioSystem"; }

    void setWorld(World* w) { world_ = w; }

    // Called by AudioComponent::play() / stop()
    void playSound (void* maSound);
    void stopSound (void* maSound);
    bool isSoundPlaying(void* maSound) const;

    // Creates and destroys ma_sound. Called by AudioSystem when initializing components.
    bool initSound (const char* filePath, bool loop, void** outSound);
    void destroySound(void* maSound);

private:
    void* engine_ = nullptr;  // ma_engine* (opaque — miniaudio.h only in .cpp)
    World* world_ = nullptr;
};
