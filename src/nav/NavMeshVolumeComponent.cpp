#include "NavMeshVolumeComponent.hpp"
#include "NavLog.hpp"

#include "../core/FieldParse.hpp"
#include "../scene/Actor.hpp"
#include "../scene/ComponentFields.hpp"
#include "../scene/Reflect.hpp"
#include "../vfs/Vfs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

void NavMeshVolumeComponent::writeFields(ComponentFieldWriter& w) const {
    w.v3("halfExtent", halfExtent);
    w.f ("agentRadius", settings.agentRadius);
    w.f ("agentHeight", settings.agentHeight);
    w.f ("maxSlope", settings.maxSlopeDeg);
    w.f ("maxClimb", settings.maxClimb);
    w.f ("cellSize", settings.cellSize);
    w.f ("cellHeight", settings.cellHeight);
    if (!data.empty()) w.s("data", data);
}

bool NavMeshVolumeComponent::readField(const std::string& key, const std::string& value) {
    bool ok = true;
    auto num = [&](float& dst) {
        float v = 0.f;
        ok = fieldparse::toFloat(value, v) && std::isfinite(v);
        if (ok) dst = v;
    };
    if (key == "halfExtent") {
        glm::vec3 v;
        ok = fieldparse::toVec3(value, v) && std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        if (ok) halfExtent = glm::max(v, glm::vec3(0.f));
    }
    else if (key == "agentRadius") num(settings.agentRadius);
    else if (key == "agentHeight") num(settings.agentHeight);
    else if (key == "maxSlope")    num(settings.maxSlopeDeg);
    else if (key == "maxClimb")    num(settings.maxClimb);
    else if (key == "cellSize")    num(settings.cellSize);
    else if (key == "cellHeight")  num(settings.cellHeight);
    else if (key == "data")        data = value;
    else return false;
    if (!ok) reportBadField(key, value);
    return true;
}

void NavMeshVolumeComponent::onFieldsLoaded(const ComponentLoadContext&) {
    settings.sanitize();
    loadData();
}

void NavMeshVolumeComponent::onStart() {
    // 코드로 만든 볼륨(씬 파일을 거치지 않음)도 데이터가 있으면 읽습니다.
    if (!triedLoad_ && !data.empty()) loadData();
}

void NavMeshVolumeComponent::worldBounds(glm::vec3& mn, glm::vec3& mx) const {
    const glm::vec3 c = owner ? owner->worldPosition() : glm::vec3(0.f);
    const glm::vec3 h = halfExtent * (owner ? glm::abs(owner->worldScale()) : glm::vec3(1.f));
    mn = c - h;
    mx = c + h;
}

bool NavMeshVolumeComponent::contains(const glm::vec3& p, float verticalSlack) const {
    glm::vec3 mn, mx;
    worldBounds(mn, mx);
    return p.x >= mn.x && p.x <= mx.x && p.z >= mn.z && p.z <= mx.z && p.y >= mn.y - verticalSlack && p.y <= mx.y + verticalSlack;
}

bool NavMeshVolumeComponent::loadBytes(const std::vector<uint8_t>& bytes, std::string& err) {
    NavFile f;
    mesh_.clear();
    loadedHash_ = 0;
    if (!hnav::readFile(bytes.data(), bytes.size(), f, err) || !mesh_.load(f.tile, err)) {
        status_ = "load failed: " + err;
        return false;
    }
    loadedHash_ = f.inputHash;
    status_ = std::to_string(mesh_.polygonCount()) + " polygons";
    return true;
}

bool NavMeshVolumeComponent::loadData() {
    triedLoad_ = true;
    mesh_.clear();
    loadedHash_ = 0;
    if (data.empty()) { status_ = "no data path"; return false; }
    const char* who = owner ? owner->name.c_str() : "?";
    std::vector<uint8_t> bytes;
    std::string err;
    if (!hvfs::readAll(data, bytes, &err)) {
        status_ = "not built (" + data + " is missing)";
        HLOG(LogNav, Warning) << "Nav mesh volume '" << who << "': " << data
                              << " is missing; run nav.build in the editor console (agents in this volume cannot find paths)";
        return false;
    }
    if (!loadBytes(bytes, err)) {
        HLOG(LogNav, Warning) << "Nav mesh volume '" << who << "': " << data << " could not be read (" << err << ")";
        return false;
    }
    HLOG(LogNav, Log) << "Nav mesh volume '" << who << "': loaded " << data << " (" << mesh_.polygonCount() << " polygons)";
    return true;
}

std::vector<PropDesc> NavMeshVolumeComponent::buildProps() {
    auto* p = this;
    NavBuildSettings* s = &settings;
    return {
        XPROP_Vec3Min(p, "Extent", 0.1f, halfExtent, 0.f, PropUnit::Scalar).withDefault(glm::vec3(10.f, 3.f, 10.f)),
        makePropFloat("Agent Radius", PropColor::Neutral, 0.01f, [s]{ return s->agentRadius; }, [s](float v){ s->agentRadius = std::clamp(v, 0.f, 10.f); }, PropUnit::Size).withDefault(0.3f),
        makePropFloat("Agent Height", PropColor::Neutral, 0.01f, [s]{ return s->agentHeight; }, [s](float v){ s->agentHeight = std::clamp(v, 0.1f, 20.f); }, PropUnit::Size).withDefault(1.8f),
        makePropFloat("Max Slope", PropColor::Neutral, 1.f, [s]{ return s->maxSlopeDeg; }, [s](float v){ s->maxSlopeDeg = std::clamp(v, 0.f, 89.f); }, PropUnit::Angle).withDefault(45.f),
        makePropFloat("Max Climb", PropColor::Neutral, 0.01f, [s]{ return s->maxClimb; }, [s](float v){ s->maxClimb = std::clamp(v, 0.f, 10.f); }, PropUnit::Size).withDefault(0.35f),
        makePropFloat("Cell Size", PropColor::Neutral, 0.01f, [s]{ return s->cellSize; }, [s](float v){ s->cellSize = std::clamp(v, 0.01f, 5.f); }, PropUnit::Size).withDefault(0.1f),
        makePropFloat("Cell Height", PropColor::Neutral, 0.01f, [s]{ return s->cellHeight; }, [s](float v){ s->cellHeight = std::clamp(v, 0.01f, 5.f); }, PropUnit::Size).withDefault(0.05f),
        // 구운 데이터(.hnav, H3b 에셋 경로 칸). 바꾸면 그 자리에서 다시 읽습니다(loadData, 씬을 읽을 때와 같은 길). 굽기(nav.build)는 그대로.
        makePropAssetPath("Data", "Nav Data", [p]{ return p->data; }, [p](const std::string& v){ if (v != p->data) { p->data = v; p->loadData(); } }),
    };
}

std::string NavMeshVolumeComponent::readOnlyInfo() const {
    return "Data: " + (data.empty() ? std::string("(none, nav.build <path> sets it)") : data) + "\nNav mesh: " + status_;
}
