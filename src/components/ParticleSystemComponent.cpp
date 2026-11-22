#include "ParticleSystemComponent.hpp"
#include "../core/Profiler.hpp"
#include "../scene/Actor.hpp"
#include "../scene/ComponentFields.hpp"
#include "../scene/Reflect.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <type_traits>

namespace {
const char* const kBlendNames[]  = { "additive", "alpha" };
const char* const kSpaceNames[]  = { "world", "local" };
const char* const kShapeNames[]  = { "cone", "sphere" };
const char* const kSpriteNames[] = { "softCircle", "circle", "ring", "star" };

template <size_t N>
int indexOf(const char* const (&names)[N], const std::string& v) {
    for (size_t i = 0; i < N; ++i) if (v == names[i]) return (int)i;
    return -1;
}
} // namespace

void ParticleSystemComponent::play() {
    sim_.reset(desc);
    instances_.clear();
    emitting_ = true;
    started_  = true;
}

void ParticleSystemComponent::onStart() {
    desc.clampValues();
    if (!started_) {
        sim_.reset(desc);
        emitting_ = autoPlay;
        started_  = true;
    }
}

void ParticleSystemComponent::tick(float dt) {
    HPROF_SCOPE("Particles.Simulate");
    if (!owner) return;
    const glm::mat4 m = owner->worldMatrix();
    center_ = glm::vec3(m[3]);
    sim_.step(desc, dt, m, emitting_);
    sim_.buildInstances(desc, m, instances_);
}

std::vector<PropDesc> ParticleSystemComponent::buildProps() {
    auto* p = this;
    vfx::EmitterDesc* d = &desc;
    std::vector<PropDesc> props;
    props.push_back(makePropToggle("Auto Play", [p]{ return p->autoPlay; }, [p](bool v){ p->autoPlay = v; }).withDefault(true));
    props.push_back(makePropFloat("Rate", PropColor::Neutral, 1.f,
        [d]{ return d->rate; }, [d](float v){ d->rate = v; d->clampValues(); }).withDefault(20.f));
    props.push_back(makePropFloat("Burst", PropColor::Neutral, 1.f,
        [d]{ return (float)d->burst; }, [d](float v){ d->burst = (uint32_t)std::max(0.f, std::round(v)); d->clampValues(); }).withDefault(0.f));
    props.push_back(makePropFloat("Duration", PropColor::Neutral, 0.1f,
        [d]{ return d->duration; }, [d](float v){ d->duration = v; d->clampValues(); }).withDefault(2.f));
    props.push_back(makePropToggle("Loop", [d]{ return d->loop; }, [d](bool v){ d->loop = v; }).withDefault(true));
    props.push_back(makePropFloat("Max Particles", PropColor::Neutral, 10.f,
        [d]{ return (float)d->maxParticles; },
        [d](float v){ d->maxParticles = (uint32_t)std::max(1.f, std::round(v)); d->clampValues(); }).withDefault(1000.f));
    props.push_back(makePropFloat("Seed", PropColor::Neutral, 1.f,
        [d]{ return (float)d->seed; }, [d](float v){ d->seed = (uint32_t)std::clamp(std::round(v), 0.f, 1.0e9f); }).withDefault(0.f));
    props.push_back(makePropEnum("Space", { "World", "Local" },
        [d]{ return (int)d->space; }, [d](int v){ d->space = (vfx::Space)std::clamp(v, 0, 1); }).withDefault(vfx::Space::World));
    props.push_back(makePropFloat("Life Min", PropColor::Neutral, 0.1f,
        [d]{ return d->lifeMin; }, [d](float v){ d->lifeMin = v; d->clampValues(); }).withDefault(1.f));
    props.push_back(makePropFloat("Life Max", PropColor::Neutral, 0.1f,
        [d]{ return d->lifeMax; }, [d](float v){ d->lifeMax = v; d->clampValues(); }).withDefault(2.f));
    props.push_back(makePropFloat("Speed Min", PropColor::Neutral, 0.1f,
        [d]{ return d->speedMin; }, [d](float v){ d->speedMin = v; d->clampValues(); }).withDefault(1.f));
    props.push_back(makePropFloat("Speed Max", PropColor::Neutral, 0.1f,
        [d]{ return d->speedMax; }, [d](float v){ d->speedMax = v; d->clampValues(); }).withDefault(2.f));
    props.push_back(makePropEnum("Shape", { "Cone", "Sphere" },
        [d]{ return (int)d->shape; }, [d](int v){ d->shape = (vfx::Shape)std::clamp(v, 0, 1); }).withDefault(vfx::Shape::Cone));
    if (desc.shape == vfx::Shape::Cone)
        props.push_back(makePropFloat("Cone Angle", PropColor::Neutral, 1.f,
            [d]{ return d->coneAngle; }, [d](float v){ d->coneAngle = v; d->clampValues(); }, PropUnit::Angle).withDefault(25.f));
    props.push_back(makePropFloat("Radius", PropColor::Neutral, 0.01f,
        [d]{ return d->radius; }, [d](float v){ d->radius = v; d->clampValues(); }, PropUnit::Size).withDefault(0.f));
    props.push_back(makePropFloat("Size", PropColor::Neutral, 0.01f,
        [d]{ return d->size; }, [d](float v){ d->size = v; d->clampValues(); }, PropUnit::Size).withDefault(0.1f));
    props.push_back(makePropFloat("Spin Min", PropColor::Neutral, 5.f,
        [d]{ return d->spinMin; }, [d](float v){ d->spinMin = v; d->clampValues(); }).withDefault(0.f));
    props.push_back(makePropFloat("Spin Max", PropColor::Neutral, 5.f,
        [d]{ return d->spinMax; }, [d](float v){ d->spinMax = v; d->clampValues(); }).withDefault(0.f));
    props.push_back(makePropVec3("Gravity", 0.1f,
        [d]{ return d->gravity; }, [d](const glm::vec3& v){ d->gravity = glm::clamp(v, glm::vec3(-1000.f), glm::vec3(1000.f)); })
        .withDefault(glm::vec3(0.f)));
    props.push_back(makePropFloat("Drag", PropColor::Neutral, 0.05f,
        [d]{ return d->drag; }, [d](float v){ d->drag = v; d->clampValues(); }).withDefault(0.f));
    props.push_back(makePropEnum("Blend", { "Additive", "Alpha" },
        [d]{ return (int)d->blend; }, [d](int v){ d->blend = (vfx::Blend)std::clamp(v, 0, 1); }).withDefault(vfx::Blend::Additive));
    props.push_back(makePropEnum("Sprite", { "Soft Circle", "Circle", "Ring", "Star" },
        [d]{ return (int)d->sprite; }, [d](int v){ d->sprite = (vfx::Sprite)std::clamp(v, 0, 3); }).withDefault(vfx::Sprite::SoftCircle));
    return props;
}

std::string ParticleSystemComponent::readOnlyInfo() const {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Particles %zu / %u. Color and size curves: scene file (colorCurve, sizeCurve)",
                  sim_.count(), desc.maxParticles);
    return buf;
}

void ParticleSystemComponent::writeFields(ComponentFieldWriter& w) const {
    w.b ("autoPlay",     autoPlay);
    w.f ("rate",         desc.rate);
    w.i ("burst",        (int)desc.burst);
    w.f ("duration",     desc.duration);
    w.b ("loop",         desc.loop);
    w.i ("maxParticles", (int)desc.maxParticles);
    w.i ("seed",         (int)desc.seed);
    w.s ("space",        kSpaceNames[(int)desc.space]);
    w.f ("lifeMin",      desc.lifeMin);
    w.f ("lifeMax",      desc.lifeMax);
    w.f ("speedMin",     desc.speedMin);
    w.f ("speedMax",     desc.speedMax);
    w.s ("shape",        kShapeNames[(int)desc.shape]);
    w.f ("coneAngle",    desc.coneAngle);
    w.f ("radius",       desc.radius);
    w.f ("size",         desc.size);
    w.f ("spinMin",      desc.spinMin);
    w.f ("spinMax",      desc.spinMax);
    w.v3("gravity",      desc.gravity);
    w.f ("drag",         desc.drag);
    w.s ("sizeCurve",    vfx::curveToText(desc.sizeCurve));
    w.s ("colorCurve",   vfx::curveToText(desc.colorCurve));
    w.s ("blend",        kBlendNames[(int)desc.blend]);
    w.s ("sprite",       kSpriteNames[(int)desc.sprite]);
}

bool ParticleSystemComponent::readField(const std::string& key, const std::string& value) {
    bool  ok = true;
    float f  = 0.f;
    int   n  = 0;
    auto readF = [&](float& dst, float lo, float hi) {
        ok = fieldparse::toFloat(value, f) && std::isfinite(f) && f >= lo && f <= hi;
        if (ok) dst = f;
    };
    auto readEnum = [&](auto& dst, int idx) {
        ok = idx >= 0;
        if (ok) dst = static_cast<std::remove_reference_t<decltype(dst)>>(idx);
    };
    if      (key == "autoPlay")     ok = fieldparse::toBool(value, autoPlay);
    else if (key == "rate")         readF(desc.rate, 0.f, 100000.f);
    else if (key == "burst")        { ok = fieldparse::toInt(value, n) && n >= 0 && n <= (int)vfx::kMaxParticles; if (ok) desc.burst = (uint32_t)n; }
    else if (key == "duration")     readF(desc.duration, 0.01f, 3600.f);
    else if (key == "loop")         ok = fieldparse::toBool(value, desc.loop);
    else if (key == "maxParticles") { ok = fieldparse::toInt(value, n) && n >= 1 && n <= (int)vfx::kMaxParticles; if (ok) desc.maxParticles = (uint32_t)n; }
    else if (key == "seed")         { ok = fieldparse::toInt(value, n) && n >= 0; if (ok) desc.seed = (uint32_t)n; }
    else if (key == "space")        readEnum(desc.space, indexOf(kSpaceNames, value));
    else if (key == "lifeMin")      readF(desc.lifeMin, 0.01f, 600.f);
    else if (key == "lifeMax")      readF(desc.lifeMax, 0.01f, 600.f);
    else if (key == "speedMin")     readF(desc.speedMin, 0.f, 1000.f);
    else if (key == "speedMax")     readF(desc.speedMax, 0.f, 1000.f);
    else if (key == "shape")        readEnum(desc.shape, indexOf(kShapeNames, value));
    else if (key == "coneAngle")    readF(desc.coneAngle, 0.f, 180.f);
    else if (key == "radius")       readF(desc.radius, 0.f, 1000.f);
    else if (key == "size")         readF(desc.size, 0.f, 100.f);
    else if (key == "spinMin")      readF(desc.spinMin, -3600.f, 3600.f);
    else if (key == "spinMax")      readF(desc.spinMax, -3600.f, 3600.f);
    else if (key == "gravity") {
        glm::vec3 g;
        ok = fieldparse::toVec3(value, g) && std::isfinite(g.x) && std::isfinite(g.y) && std::isfinite(g.z);
        if (ok) desc.gravity = g;
    }
    else if (key == "drag")         readF(desc.drag, 0.f, 100.f);
    else if (key == "sizeCurve")    ok = vfx::curveFromText(value, desc.sizeCurve);
    else if (key == "colorCurve")   ok = vfx::curveFromText(value, desc.colorCurve);
    else if (key == "blend")        readEnum(desc.blend, indexOf(kBlendNames, value));
    else if (key == "sprite")       readEnum(desc.sprite, indexOf(kSpriteNames, value));
    else return false;
    if (!ok) reportBadField(key, value);
    desc.clampValues();   // min > max 같은 짝을 맞춤
    return true;
}
