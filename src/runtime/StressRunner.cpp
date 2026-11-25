#include "StressRunner.hpp"

#include "../assets/AssetManager.hpp"
#include "../components/CameraComponent.hpp"
#include "../components/LightComponent.hpp"
#include "../components/ParticleSystemComponent.hpp"
#include "../components/RenderExtract.hpp"
#include "../components/StaticMeshComponent.hpp"
#include "../components/UIPanelComponent.hpp"
#include "../core/AppPaths.hpp"
#include "../core/HLog.hpp"
#include "../core/Subtitles.hpp"
#include "../scene/Actor.hpp"
#include "../scene/World.hpp"

#define GLM_FORCE_RADIANS
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

static constinit HLogCategory LogStress{ "Stress", ELogVerbosity::Log };

void StressRunner::init(const hstress::StressSpec& spec, World& world, AssetManager& assets, Renderer& renderer,
                        SubtitleQueue* subtitles) {
    spec_     = spec;
    world_    = &world;
    assets_   = &assets;
    renderer_ = &renderer;

    records_.clear();
    csvRows_.clear();
    stepActors_.clear();
    csvRows_.push_back("axis,n,gpu_total_ms,delta_ms,shadow_ms,main_ms,ao_ms,bloom_ms,tonemap_ms,ui_ms,cpu_work_ms,frame_ms,tris,draws,instances");

    setupBaseScene();
    schedule_.begin(spec_);
    spawnStepActors(schedule_.currentN);

    stepStart_      = std::chrono::steady_clock::now();
    discardCleared_ = false;
    gpuAccum_.reset();
    cpuWorkSumMs_ = 0.0;
    frameSumMs_   = 0.0;
    cpuFrames_    = 0;

    HLOG(LogStress, Log) << "Stress ramp start: axis=" << hstress::axisName(spec_.axis)
                         << " [" << spec_.start << ":" << spec_.step << ":" << spec_.max << "]"
                         << ", step=" << spec_.effectiveStepSeconds() << "s"
                         << ". Look straight ahead and hold still (정면을 보고 가만히 있으세요).";
    if (subtitles) {
        SubtitleLine sl;
        sl.text     = HText::fromString("정면을 보고 가만히 있으세요");
        sl.duration = 3.5f;
        subtitles->push(std::move(sl));
    }
}

void StressRunner::setupBaseScene() {
    std::vector<Actor*> existing;
    existing.reserve(world_->getActors().size());
    for (const auto& up : world_->getActors()) existing.push_back(up.get());
    for (Actor* a : existing) {
        if (a) world_->destroyActor(a);
    }

    Actor* camActor = world_->spawnActor("StressCamera");
    camActor->setLocalPosition({ 0.f, 1.7f, 0.f });
    camActor->addComponent<CameraComponent>();

    Actor* sunActor = world_->spawnActor("StressSun");
    sunActor->setLocalRotation(glm::quat(glm::radians(glm::vec3(-45.f, 25.f, 0.f))));
    auto* sun = sunActor->addComponent<LightComponent>();
    sun->lightType   = LightType::Directional;
    sun->intensity   = 3.0f;
    sun->castShadows = true;

    Actor* floorActor = world_->spawnActor("StressFloor");
    floorActor->setLocalPosition({ 0.f, 0.f, 0.f });
    floorActor->setLocalScale({ 40.f, 1.f, 40.f });
    auto* floorSm = floorActor->addComponent<StaticMeshComponent>();
    PrimitiveDesc pd;
    pd.shape = EPrimitiveShape::Plane;
    pd.segU  = 1;
    pd.segV  = 1;
    floorSm->assignMeshFromPair(assets_->acquirePrimitive(pd));
    floorSm->material = assets_->primitiveMaterial(0x70747AFFu);
}

void StressRunner::clearStepActors() {
    for (Actor* a : stepActors_) {
        if (a) world_->destroyActor(a);
    }
    stepActors_.clear();
}

MeshHandle* StressRunner::buildUvSphere(uint32_t segU, uint32_t segV) {
    segU = std::max(4u, segU);
    segV = std::max(4u, segV);
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    verts.reserve(static_cast<size_t>(segU + 1) * (segV + 1));
    indices.reserve(static_cast<size_t>(segU) * segV * 6);

    constexpr float kPi = 3.14159265358979323846f;
    for (uint32_t y = 0; y <= segV; ++y) {
        const float v   = static_cast<float>(y) / static_cast<float>(segV);
        const float phi = v * kPi;
        const float sinP = std::sin(phi);
        const float cosP = std::cos(phi);
        for (uint32_t x = 0; x <= segU; ++x) {
            const float u     = static_cast<float>(x) / static_cast<float>(segU);
            const float theta = u * 2.f * kPi;
            const glm::vec3 n(sinP * std::cos(theta), cosP, sinP * std::sin(theta));
            Vertex vt{};
            vt.pos[0] = n.x * 0.5f; vt.pos[1] = n.y * 0.5f; vt.pos[2] = n.z * 0.5f;
            vt.normal[0] = n.x; vt.normal[1] = n.y; vt.normal[2] = n.z;
            vt.uv[0] = u; vt.uv[1] = v;
            vt.tangent[0] = -std::sin(theta); vt.tangent[1] = 0.f; vt.tangent[2] = std::cos(theta); vt.tangent[3] = 1.f;
            verts.push_back(vt);
        }
    }
    for (uint32_t y = 0; y < segV; ++y) {
        for (uint32_t x = 0; x < segU; ++x) {
            const uint32_t i0 = y * (segU + 1) + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + (segU + 1);
            const uint32_t i3 = i2 + 1;
            indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
            indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
        }
    }
    MeshBounds mb{};
    mb.center  = { 0.f, 0.f, 0.f };
    mb.radius  = 0.5f;
    mb.aabbMin = { -0.5f, -0.5f, -0.5f };
    mb.aabbMax = {  0.5f,  0.5f,  0.5f };
    return renderer_->uploadMesh(verts, indices, &mb);
}

void StressRunner::spawnStepActors(uint32_t n) {
    clearStepActors();
    if (n == 0) {
        lastTris_ = countWorldTris(*world_);
        return;
    }

    const hstress::StressVec3 origin{ 0.f, 1.7f, 0.f };
    const hstress::StressVec3 fwd{ 0.f, 0.f, -1.f };
    const hstress::StressVec3 right{ 1.f, 0.f, 0.f };
    const hstress::StressVec3 up{ 0.f, 1.f, 0.f };

    PrimitiveDesc boxDesc;
    boxDesc.shape = EPrimitiveShape::Box;

    switch (spec_.axis) {
        case hstress::StressAxis::Empty:
            break;

        case hstress::StressAxis::Draws: {
            const AssetPair boxPair = assets_->acquirePrimitive(boxDesc);
            for (uint32_t i = 0; i < n; ++i) {
                const hstress::StressVec3 p = hstress::placeStressGrid(i, n, origin, fwd, right, up);
                Actor* a = world_->spawnActor("StressDraw_" + std::to_string(i));
                a->setLocalPosition({ p.x, p.y, p.z });
                a->setLocalScale({ 0.25f, 0.25f, 0.25f });
                auto* sm = a->addComponent<StaticMeshComponent>();
                sm->assignMeshFromPair(boxPair);
                // 서로 다른 머티리얼로 DEF-16 인스턴싱 묶음을 피합니다.
                const uint32_t c = ((i * 2654435761u) & 0xFFFFFF00u) | 0x000000FFu;
                sm->material = assets_->primitiveMaterial(c);
                stepActors_.push_back(a);
            }
            break;
        }

        case hstress::StressAxis::Instances: {
            const AssetPair boxPair = assets_->acquirePrimitive(boxDesc);
            MaterialHandle* sharedMat = assets_->primitiveMaterial(0x44AAEEFFu);
            for (uint32_t i = 0; i < n; ++i) {
                const hstress::StressVec3 p = hstress::placeStressGrid(i, n, origin, fwd, right, up);
                Actor* a = world_->spawnActor("StressInst_" + std::to_string(i));
                a->setLocalPosition({ p.x, p.y, p.z });
                a->setLocalScale({ 0.18f, 0.18f, 0.18f });
                auto* sm = a->addComponent<StaticMeshComponent>();
                sm->assignMeshFromPair(boxPair);
                sm->material = sharedMat;
                stepActors_.push_back(a);
            }
            break;
        }

        case hstress::StressAxis::Tris: {
            constexpr uint32_t kFixedDraws = 16;
            MeshHandle* sphereMesh = buildUvSphere(n, std::max(4u, n / 2u));
            MaterialHandle* mat    = assets_->primitiveMaterial(0xDD8844FFu);
            for (uint32_t i = 0; i < kFixedDraws; ++i) {
                const hstress::StressVec3 p = hstress::placeStressGrid(i, kFixedDraws, origin, fwd, right, up);
                Actor* a = world_->spawnActor("StressTris_" + std::to_string(i));
                a->setLocalPosition({ p.x, p.y, p.z });
                a->setLocalScale({ 0.6f, 0.6f, 0.6f });
                auto* sm = a->addComponent<StaticMeshComponent>();
                sm->mesh     = sphereMesh;
                sm->material = mat;
                stepActors_.push_back(a);
            }
            break;
        }

        case hstress::StressAxis::Lights: {
            constexpr uint32_t kReceivers = 16;
            const AssetPair boxPair = assets_->acquirePrimitive(boxDesc);
            MaterialHandle* mat     = assets_->primitiveMaterial(0xCCCCCCFFu);
            for (uint32_t i = 0; i < kReceivers; ++i) {
                const hstress::StressVec3 p = hstress::placeStressGrid(i, kReceivers, origin, fwd, right, up);
                Actor* a = world_->spawnActor("StressLightBox_" + std::to_string(i));
                a->setLocalPosition({ p.x, p.y, p.z });
                a->setLocalScale({ 0.5f, 0.5f, 0.5f });
                auto* sm = a->addComponent<StaticMeshComponent>();
                sm->assignMeshFromPair(boxPair);
                sm->material = mat;
                stepActors_.push_back(a);
            }
            for (uint32_t i = 0; i < n; ++i) {
                const hstress::StressVec3 p = hstress::placeStressGrid(i, n, origin, fwd, right, up);
                Actor* a = world_->spawnActor("StressLight_" + std::to_string(i));
                a->setLocalPosition({ p.x, p.y, p.z });
                auto* lc = a->addComponent<LightComponent>();
                lc->lightType         = LightType::Point;
                lc->castShadows       = false;
                lc->attenuationRadius = 6.0f;
                lc->intensity         = 2.0f;
                lc->color             = { 1.0f, 0.85f, 0.7f };
                stepActors_.push_back(a);
            }
            break;
        }

        case hstress::StressAxis::Overdraw: {
            PrimitiveDesc planeDesc;
            planeDesc.shape = EPrimitiveShape::Plane;
            planeDesc.segU  = 1;
            planeDesc.segV  = 1;
            const AssetPair planePair = assets_->acquirePrimitive(planeDesc);
            MaterialHandle* transMat  = assets_->primitiveMaterial(0x88CCFF66u);
            const glm::quat faceCam   = glm::quat(glm::radians(glm::vec3(90.f, 0.f, 0.f)));
            for (uint32_t i = 0; i < n; ++i) {
                const float t = (n <= 1u) ? 0.5f : static_cast<float>(i) / static_cast<float>(n - 1u);
                const float z = -3.0f - t * 4.5f;
                Actor* a = world_->spawnActor("StressOverdraw_" + std::to_string(i));
                a->setLocalPosition({ 0.f, 1.7f, z });
                a->setLocalRotation(faceCam);
                a->setLocalScale({ 8.f, 1.f, 8.f });
                auto* sm = a->addComponent<StaticMeshComponent>();
                sm->assignMeshFromPair(planePair);
                sm->material    = transMat;
                sm->transparent = true;
                stepActors_.push_back(a);
            }
            break;
        }

        case hstress::StressAxis::Particles: {
            const uint32_t clamped = std::min(n, vfx::kMaxParticles);
            Actor* a = world_->spawnActor("StressParticles");
            a->setLocalPosition({ 0.f, 1.7f, -4.5f });
            auto* ps = a->addComponent<ParticleSystemComponent>();
            ps->desc.rate         = 0.f;
            ps->desc.burst        = clamped;
            ps->desc.maxParticles = clamped;
            ps->desc.duration     = 60.f;
            ps->desc.loop         = true;
            ps->desc.lifeMin      = 60.f;
            ps->desc.lifeMax      = 60.f;
            ps->desc.speedMin     = 0.f;
            ps->desc.speedMax     = 0.f;
            ps->desc.shape        = vfx::Shape::Sphere;
            ps->desc.radius       = 1.5f;
            ps->desc.size         = 0.25f;
            ps->desc.seed         = 0x5EED1234u;
            ps->play();
            stepActors_.push_back(a);
            break;
        }

        case hstress::StressAxis::Panels: {
            for (uint32_t i = 0; i < n; ++i) {
                const hstress::StressVec3 p = hstress::placeStressGrid(i, n, origin, fwd, right, up);
                Actor* a = world_->spawnActor("StressPanel_" + std::to_string(i));
                a->setLocalPosition({ p.x, p.y, p.z });
                auto* upc = a->addComponent<UIPanelComponent>();
                upc->width          = 0.5f;
                upc->height         = 0.35f;
                upc->pixelsPerMeter = 400.f;
                upc->content        = "Demo";
                upc->applyTextureSize();
                stepActors_.push_back(a);
            }
            break;
        }
    }
    lastTris_ = countWorldTris(*world_);
}

bool StressRunner::onFrame(float cpuWorkMs, float frameMs, float refreshHz, bool overlayOn) {
    if (schedule_.finished) return true;
    if (refreshHz > 0.f) lastRefreshHz_ = refreshHz;

    const auto now = std::chrono::steady_clock::now();
    const float elapsed = std::chrono::duration<float>(now - stepStart_).count();
    const float stepSec = spec_.effectiveStepSeconds();

    if (hstress::StressSchedule::inDiscardWindow(elapsed, stepSec)) {
        return false;
    }
    if (!discardCleared_) {
        discardCleared_ = true;
        gpuAccum_.reset();
        cpuWorkSumMs_ = 0.0;
        frameSumMs_   = 0.0;
        cpuFrames_    = 0;
    }

    const Renderer::GpuTimings t  = renderer_->getTimings();
    const Renderer::FrameStats fs = renderer_->getFrameStats();
    gpuAccum_.add({ t.shadowMs, t.mainMs, t.aoMs, t.bloomMs, t.tonemapMs, t.uiMs, t.totalMs });
    cpuWorkSumMs_ += cpuWorkMs;
    frameSumMs_   += frameMs;
    ++cpuFrames_;
    lastDraws_ = fs.drawCalls;
    lastInsts_ = fs.instances;

    if (elapsed < stepSec) return false;

    const hgpu::GpuLogAverage avg        = gpuAccum_.flush();
    const float               cpuWorkAvg = cpuFrames_ > 0 ? static_cast<float>(cpuWorkSumMs_ / cpuFrames_) : 0.f;
    const float               frameAvg   = cpuFrames_ > 0 ? static_cast<float>(frameSumMs_ / cpuFrames_) : 0.f;
    const uint32_t            n          = schedule_.currentN;
    const float               deltaMs    = schedule_.hasBase ? (avg.totalMs - schedule_.baseGpuMs) : 0.f;

    char line[368];
    std::snprintf(
        line, sizeof(line),
        "Stress step: axis=%s n=%u, GPU ms %.3f (%+.3f ms) (shadow %.3f main %.3f ao %.3f bloom %.3f tonemap %.3f), CPU work ms %.3f, frame ms %.3f, tris %u, draws %u%s",
        hstress::axisName(spec_.axis), n, avg.totalMs, deltaMs, avg.shadowMs, avg.mainMs, avg.aoMs, avg.bloomMs, avg.tonemapMs,
        cpuWorkAvg, frameAvg, lastTris_, lastDraws_, overlayOn ? ", overlay on" : "");
    HLOG(LogStress, Log) << line;

    char csv[288];
    std::snprintf(csv, sizeof(csv), "%s,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%u",
                  hstress::axisName(spec_.axis), n, avg.totalMs, deltaMs, avg.shadowMs, avg.mainMs, avg.aoMs, avg.bloomMs,
                  avg.tonemapMs, avg.uiMs, cpuWorkAvg, frameAvg, lastTris_, lastDraws_, lastInsts_);
    csvRows_.emplace_back(csv);
    records_.push_back({ n, lastTris_, avg.totalMs });

    const float budgetMs = (!spec_.budgetMsSet && lastRefreshHz_ > 0.f) ? (1000.f / lastRefreshHz_) : spec_.budgetMs;
    const float hzUsed   = (!spec_.budgetMsSet && lastRefreshHz_ > 0.f) ? lastRefreshHz_ : (1000.f / budgetMs);

    if (schedule_.advanceAfterStep(avg.totalMs, budgetMs, spec_)) {
        spawnStepActors(schedule_.currentN);
        stepStart_      = std::chrono::steady_clock::now();
        discardCleared_ = false;
        gpuAccum_.reset();
        cpuWorkSumMs_ = 0.0;
        frameSumMs_   = 0.0;
        cpuFrames_    = 0;
        return false;
    }

    const float baseMs = schedule_.hasBase ? schedule_.baseGpuMs : (records_.empty() ? 0.f : records_.front().gpuTotalMs);
    const hstress::StressRegression reg = hstress::computeRegression(records_, spec_.axis, budgetMs, baseMs);

    char unitBuf[48] = "n/a";
    if (reg.hasSlope) {
        const char* unitLabel = (spec_.axis == hstress::StressAxis::Tris) ? "100k tris" : "100";
        std::snprintf(unitBuf, sizeof(unitBuf), "%.3f ms/%s", reg.unitCostMs, unitLabel);
    }
    char estBuf[48] = "none";
    if (reg.overAtBase) {
        std::snprintf(estBuf, sizeof(estBuf), "over at base");
    } else if (reg.hasEstLimit) {
        std::snprintf(estBuf, sizeof(estBuf), "%.0f", reg.estLimit);
    }

    const hstress::KneePoint kp = hstress::computeKneePoint(records_, budgetMs);
    const std::string underStr = kp.hasLastUnder
        ? (std::to_string(kp.lastUnderN) + " (" + std::to_string(kp.lastUnderMs).substr(0, 5) + " ms)")
        : std::string("none");
    const std::string overStr  = kp.hasFirstOver
        ? (std::to_string(kp.firstOverN) + " (" + std::to_string(kp.firstOverMs).substr(0, 5) + " ms)")
        : std::string("none");

    char res[320];
    std::snprintf(res, sizeof(res),
                  "Stress result: axis=%s, budget %.1f ms @ %.0f Hz, base %.3f ms, unit %s, est limit = %s, last under = %s, first over = %s",
                  hstress::axisName(spec_.axis), budgetMs, hzUsed, baseMs, unitBuf, estBuf, underStr.c_str(), overStr.c_str());
    HLOG(LogStress, Log) << res;

    writeCsv();
    clearStepActors();
    return true;
}

void StressRunner::writeCsv() const {
    const std::filesystem::path dir = happ::inWriteRoot("Saved/Stress");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const std::time_t now = std::time(nullptr);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &now);
#else
    localtime_r(&now, &tmBuf);
#endif
    char ts[32];
    std::snprintf(ts, sizeof(ts), "%04d%02d%02d_%02d%02d%02d",
                  tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
                  tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);
    const std::filesystem::path path = dir / (std::string(hstress::axisName(spec_.axis)) + "_" + ts + ".csv");
    std::ofstream f(path);
    for (const std::string& row : csvRows_) f << row << "\n";
    if (f.good())
        HLOG(LogStress, Log) << "Stress CSV written: " << path.generic_string();
    else
        HLOG(LogStress, Warning) << "Failed to write Stress CSV: " << path.generic_string();
}
