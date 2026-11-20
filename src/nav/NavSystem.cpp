#include "NavSystem.hpp"

#include "NavAgentComponent.hpp"
#include "NavGeometry.hpp"
#include "NavLog.hpp"
#include "NavMeshVolumeComponent.hpp"

#include "../core/CVar.hpp"
#include "../core/DebugDraw.hpp"
#include "../core/ProjectPaths.hpp"
#include "../scene/Actor.hpp"
#include "../scene/World.hpp"
#include "../vfs/PathUtf8.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

float NavPath::length() const {
    float l = 0.f;
    for (size_t i = 1; i < points.size(); ++i) l += glm::length(points[i] - points[i - 1]);
    return l;
}

NavMeshVolumeComponent* NavSystem::volumeFor(const glm::vec3& p) const {
    if (!world_) return nullptr;
    const auto& vols = world_->componentsOf<NavMeshVolumeComponent>();
    for (NavMeshVolumeComponent* v : vols)
        if (v->isLive() && v->loaded() && v->contains(p)) return v;
    glm::vec3 q;
    for (NavMeshVolumeComponent* v : vols)
        if (v->isLive() && v->loaded() && v->mesh().projectPoint(p, queryExtents, q)) return v;
    return nullptr;
}

ENavPathResult NavSystem::findPath(const glm::vec3& start, const glm::vec3& end, NavPath& out) const {
    out.points.clear();
    out.result = ENavPathResult::NoPath;
    if (const NavMeshVolumeComponent* v = volumeFor(start)) out.result = v->mesh().findPath(start, end, queryExtents, out.points);
    return out.result;
}

bool NavSystem::projectPoint(const glm::vec3& p, glm::vec3& out) const {
    const NavMeshVolumeComponent* v = volumeFor(p);
    return v && v->mesh().projectPoint(p, queryExtents, out);
}

bool NavSystem::moveAlongSurface(const glm::vec3& from, const glm::vec3& to, glm::vec3& out) const {
    const NavMeshVolumeComponent* v = volumeFor(from);
    return v && v->mesh().moveAlongSurface(from, to, queryExtents, out);
}

NavSystem::BuildReport NavSystem::build(bool writeFiles, const std::string& dataPath) {
    BuildReport rep;
    if (!world_) { rep.text = "no world"; return rep; }
    std::ostringstream text;
    std::vector<NavMeshVolumeComponent*> vols;
    for (NavMeshVolumeComponent* v : world_->componentsOf<NavMeshVolumeComponent>())
        if (v->isEnabled() && v->owner && v->owner->activeInHierarchy()) vols.push_back(v);
    rep.volumes = (int)vols.size();
    if (vols.empty()) { rep.text = "no Nav Mesh Volume in the scene"; return rep; }
    if (!dataPath.empty()) {
        int unnamed = 0;
        for (auto* v : vols) unnamed += v->data.empty() ? 1 : 0;
        if (unnamed == 1) { for (auto* v : vols) if (v->data.empty()) v->data = dataPath; }
        else if (unnamed > 1) text << "the path was not used: " << unnamed << " volumes have no data path\n";
    }
    for (NavMeshVolumeComponent* v : vols) {
        const std::string who = v->owner->name;
        glm::vec3 mn, mx;
        v->worldBounds(mn, mx);
        NavInputGeometry geo;
        const size_t colliders = hnav::collectStaticGeometry(*world_, mn, mx, geo);
        v->settings.sanitize();
        const NavBuildResult r = hnav::build(geo, v->settings, mn, mx);
        char ms[32];
        std::snprintf(ms, sizeof ms, "%.1f", r.milliseconds);
        if (!r.ok) {
            ++rep.failed;
            text << "'" << who << "': failed (" << r.error << ")\n";
            continue;
        }
        NavFile f;
        f.settings  = v->settings;
        f.inputHash = hnav::inputHash(geo, v->settings, mn, mx);
        f.boundsMin = mn;
        f.boundsMax = mx;
        f.polygons  = r.polygons;
        f.tile      = r.tile;
        const std::vector<uint8_t> bytes = hnav::writeFile(f);
        std::string err;
        v->loadBytes(bytes, err);
        ++rep.built;
        text << "'" << who << "': " << r.polygons << " polygons from " << colliders << " colliders (" << geo.triangleCount()
             << " triangles) in " << ms << " ms";
        if (writeFiles) {
            if (v->data.empty()) {
                text << "; not saved (no data path; use nav.build /Game/scenes/<name>.hnav)";
            } else {
                std::filesystem::path disk;
                hpak::pathFromUtf8(hproj::resolve(v->data), disk);
                std::error_code ec;
                if (disk.has_parent_path()) std::filesystem::create_directories(disk.parent_path(), ec);
                std::ofstream o(disk, std::ios::binary | std::ios::trunc);
                o.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
                if (o.good()) text << "; wrote " << v->data << " (" << bytes.size() << " bytes)";
                else { text << "; could not write " << v->data; ++rep.failed; }
            }
        }
        text << "\n";
    }
    rep.text = text.str();
    if (!rep.text.empty() && rep.text.back() == '\n') rep.text.pop_back();
    return rep;
}

std::string NavSystem::info() const {
    if (!world_) return "no world";
    std::ostringstream text;
    int n = 0;
    for (NavMeshVolumeComponent* v : world_->componentsOf<NavMeshVolumeComponent>()) {
        if (!v->owner) continue;
        ++n;
        glm::vec3 mn, mx;
        v->worldBounds(mn, mx);
        NavInputGeometry geo;
        hnav::collectStaticGeometry(*world_, mn, mx, geo);
        NavBuildSettings s = v->settings;
        s.sanitize();
        const bool current = v->loaded() && hnav::inputHash(geo, s, mn, mx) == v->loadedHash();
        text << "'" << v->owner->name << "': " << v->status() << (v->loaded() ? (current ? ", up to date" : ", out of date (run nav.build)") : "")
             << "\n";
    }
    if (n == 0) text << "no Nav Mesh Volume in the scene\n";
    char buf[160];
    for (NavAgentComponent* a : world_->componentsOf<NavAgentComponent>()) {
        if (!a->owner) continue;
        const glm::vec3 p = a->owner->worldPosition(), g = a->goal();
        std::snprintf(buf, sizeof buf, "agent '%s': %s at (%.2f, %.2f, %.2f), goal (%.2f, %.2f, %.2f), %d paths",
                      a->owner->name.c_str(), navMoveStatusName(a->status()), p.x, p.y, p.z, g.x, g.y, g.z, a->repathCount());
        text << buf << "\n";
    }
    std::string s = text.str();
    if (!s.empty()) s.pop_back();
    return s;
}

void NavSystem::registerCommands(CVarRegistry& cvars) {
    cvars.addCommand("nav.build", "nav.build [/Game/scenes/<name>.hnav]: bake every Nav Mesh Volume and save its .hnav",
                     [this](const std::vector<std::string>& args) {
                         const BuildReport r = build(true, args.empty() ? std::string() : args[0]);
                         HLOG(LogNav, Log) << "nav.build: " << r.built << " built, " << r.failed << " failed";
                         return r.text;
                     });
    cvars.addCommand("nav.info", "nav.info: nav mesh volumes and whether their baked data matches the scene",
                     [this](const std::vector<std::string>&) { return info(); });
    cvars.addBool("nav.debug", false, "Draw nav mesh edges, volume bounds and agent paths",
                  [this](const CVarRegistry::CVar& v) { debugDraw = v.asBool(); });
}

void NavSystem::drawDebug() const {
    if (!debugDraw || !world_) return;
    const glm::vec4 edgeColor{ 0.2f, 0.9f, 0.9f, 1.f }, boundsColor{ 0.9f, 0.6f, 0.1f, 1.f }, pathColor{ 1.f, 0.9f, 0.2f, 1.f };
    std::vector<std::pair<glm::vec3, glm::vec3>> edges;
    for (NavMeshVolumeComponent* v : world_->componentsOf<NavMeshVolumeComponent>()) {
        if (!v->owner) continue;
        glm::vec3 mn, mx;
        v->worldBounds(mn, mx);
        hdebug::box(glm::mat4(1.f), (mn + mx) * 0.5f, (mx - mn) * 0.5f, boundsColor);
        v->mesh().edges(edges);
        const glm::vec3 lift{ 0.f, 0.02f, 0.f };
        for (const auto& e : edges) hdebug::line(e.first + lift, e.second + lift, edgeColor);
    }
    for (NavAgentComponent* a : world_->componentsOf<NavAgentComponent>()) {
        const auto& pts = a->path().points;
        for (size_t i = 1; i < pts.size(); ++i) hdebug::line(pts[i - 1], pts[i], pathColor);
    }
}
