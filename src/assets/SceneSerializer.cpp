#include "SceneSerializer.hpp"
#include "../core/SceneLog.hpp"
#include "AssetManager.hpp"
#include "../core/Actor.hpp"
#include "../core/World.hpp"
#include "../components/StaticMeshComponent.hpp"
#include "../components/LightComponent.hpp"
#include "../components/CameraComponent.hpp"
#include "../components/AudioComponent.hpp"
#include "../components/RigidBodyComponent.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <sstream>
#include <algorithm>

static glm::vec3 quatToEulerDeg(const glm::quat& q) {
    return glm::degrees(glm::vec3(glm::pitch(q), glm::yaw(q), glm::roll(q)));
}

static glm::vec3 parseVec3(const std::string& s) {
    std::istringstream ss(s); glm::vec3 v{};
    ss >> v.x >> v.y >> v.z; return v;
}

// ── writeActor ─────────────────────────────────────────────────────────────────
void SceneSerializer::writeActor(std::ostream& os, const Actor* actor, int parentIndex) {
    os << "POS:"   << actor->transform().position().x << " "
                   << actor->transform().position().y << " "
                   << actor->transform().position().z << "\n";
    glm::vec3 euler = quatToEulerDeg(actor->transform().rotation());
    os << "ROT:"   << euler.x << " " << euler.y << " " << euler.z << "\n";
    os << "SCALE:" << actor->transform().scale().x << " "
                   << actor->transform().scale().y << " "
                   << actor->transform().scale().z << "\n";
    os << "ACTIVE:" << (actor->activeSelf() ? 1 : 0) << "\n";
    if (parentIndex >= 0)
        os << "PARENT:" << parentIndex << "\n";

    if (auto* mc = actor->getComponent<StaticMeshComponent>(); mc && !mc->modelPath.empty()) {
        os << "MODEL:" << mc->modelPath << "\n";
        os << "MESH_IDX:" << mc->meshIndex << "\n";
        os << "MESH_STATE:" << (mc->visible ? 1 : 0)
                            << " " << (mc->isEnabled() ? 1 : 0) << "\n";
    }

    if (auto* lc = actor->getComponent<LightComponent>())
        os << "LIGHT:" << lc->color.r << " " << lc->color.g << " " << lc->color.b
                       << " " << lc->intensity
                       << " " << 0.f    // 예약 슬롯(과거 ambient). 포맷 안정용으로 자리만 유지.
                       << " " << (lc->isEnabled() ? 1 : 0)
                       << " " << (int)lc->lightType
                       << " " << lc->attenuationRadius
                       << " " << lc->innerConeAngle << " " << lc->outerConeAngle
                       << " " << lc->direction.x << " " << lc->direction.y << " " << lc->direction.z
                       << " " << (lc->useTemperature ? 1 : 0) << " " << lc->temperatureK
                       << " " << (lc->castShadows ? 1 : 0)
                       << "\n";

    if (auto* cc = actor->getComponent<CameraComponent>())
        os << "CAMERA:" << cc->fovY << " " << cc->zNear << " " << cc->zFar
                        << " " << (cc->primary ? 1 : 0)
                        << " " << (cc->isEnabled() ? 1 : 0) << "\n";

    if (auto* ac = actor->getComponent<AudioComponent>())
        os << "AUDIO:" << ac->filePath << "\n"
           << "AUDIO_VOL:" << ac->volume << " " << (ac->loop ? 1 : 0)
                           << " " << (ac->autoPlay ? 1 : 0)
                           << " " << (ac->isEnabled() ? 1 : 0) << "\n";

    if (auto* rb = actor->getComponent<RigidBodyComponent>())
        os << "RIGID:" << (int)rb->shape << " " << (int)rb->motionType << "\n"
           << "RIGID_PROPS:" << rb->friction << " " << rb->restitution << " " << rb->mass
                             << " " << rb->halfExtent.x << " " << rb->halfExtent.y << " " << rb->halfExtent.z
                             << " " << (rb->isEnabled() ? 1 : 0) << "\n";
}

// ── parseLine ──────────────────────────────────────────────────────────────────
bool SceneSerializer::parseLine(const std::string& key, const std::string& val, ActorData& d) {
    if      (key == "POS")        { d.pos   = parseVec3(val); }
    else if (key == "ROT")        { d.euler = parseVec3(val); }
    else if (key == "SCALE")      { d.scale = parseVec3(val); }
    else if (key == "ACTIVE")     { std::istringstream ss(val); ss >> d.active; }
    else if (key == "PARENT")     { std::istringstream ss(val); ss >> d.parentIndex; }
    else if (key == "MODEL")      { d.model = val; }
    else if (key == "MESH_IDX")   { std::istringstream ss(val); ss >> d.meshIndex; }
    else if (key == "MESH_STATE") { std::istringstream ss(val); ss >> d.meshVisible >> d.meshEnabled; }
    else if (key == "LIGHT") {
        d.hasLight = true;
        std::istringstream ss(val);
        ss >> d.lightColor.r >> d.lightColor.g >> d.lightColor.b
           >> d.lightIntensity >> d.lightAmbient >> d.lightEnabled
           // 아래는 Phase 0 확장분. 옛 파일엔 없어 읽기가 실패하면 ActorData 기본값이 유지됩니다.
           >> d.lightType >> d.lightRange >> d.lightSpotInner >> d.lightSpotOuter
           >> d.lightDir.x >> d.lightDir.y >> d.lightDir.z
           // Phase 2 색온도 확장분(다시 trailing). 옛 파일엔 없어 기본값 유지.
           >> d.lightUseTemp >> d.lightTempK
           // per-light Cast Shadows(또 trailing). 옛 파일엔 없어 기본값 1(켜짐) 유지.
           >> d.lightCastShadows;
    } else if (key == "CAMERA") {
        d.hasCamera = true;
        std::istringstream ss(val);
        ss >> d.camFovY >> d.camZNear >> d.camZFar >> d.camPrimary >> d.camEnabled;
    } else if (key == "AUDIO") {
        d.hasAudio  = true;
        d.audioFile = val;
    } else if (key == "AUDIO_VOL") {
        std::istringstream ss(val);
        ss >> d.audioVol >> d.audioLoop >> d.audioAutoPlay >> d.audioEnabled;
    } else if (key == "RIGID") {
        d.hasRigid = true;
        std::istringstream ss(val);
        ss >> d.rigidShape >> d.rigidMotion;
    } else if (key == "RIGID_PROPS") {
        std::istringstream ss(val);
        ss >> d.rigidFric >> d.rigidRest >> d.rigidMass
           >> d.rigidHX >> d.rigidHY >> d.rigidHZ >> d.rigidEnabled;
    } else {
        return false;
    }
    return true;
}

// ── spawn ──────────────────────────────────────────────────────────────────────
Actor* SceneSerializer::spawn(const ActorData& d, World& world, AssetManager& assets) {
    if (d.name.empty()) return nullptr;

    Actor* a = world.spawnActor(d.name);
    // 컴포넌트를 붙이기 전에 활성 상태를 먼저 정합니다. 비활성으로 저장된 액터라면,
    // 아직 컴포넌트가 없어 setActive 는 플래그만 바꾸고, 이후 addComponent 의 syncLive
    // 가 처음부터 비활성 기준으로 판정하므로 불필요한 onEnable→onDisable 이 없습니다.
    // (부모는 여기서 알 수 없어 호출자가 나중에 attachTo 로 배선합니다.)
    a->setActive(d.active != 0);
    a->setLocalPosition(d.pos);
    a->setLocalRotation(glm::quat(glm::radians(d.euler)));
    a->setLocalScale(d.scale);

    if (!d.model.empty()) {
        try {
            auto pairs = assets.loadAll(d.model);
            if (!pairs.empty()) {
                int idx = std::min(d.meshIndex, (int)pairs.size() - 1);
                auto* mc        = a->addComponent<StaticMeshComponent>();
                mc->mesh        = pairs[idx].mesh;
                mc->material    = pairs[idx].material;
                mc->modelPath   = d.model;
                mc->meshIndex   = idx;
                mc->transparent = pairs[idx].isBlend;
                mc->vertCount   = pairs[idx].vertCount;
                mc->triCount    = pairs[idx].triCount;
                mc->visible     = (d.meshVisible != 0);
                mc->setEnabled(d.meshEnabled != 0);
            }
        } catch (const std::exception& ex) {
            HLOG(LogScene, Error) << "Mesh load failed: " << ex.what();
        }
    }
    if (d.hasLight) {
        auto* lc            = a->addComponent<LightComponent>();
        lc->color           = d.lightColor;
        lc->intensity       = d.lightIntensity;
        lc->lightType       = static_cast<LightType>(d.lightType);
        lc->attenuationRadius = d.lightRange;
        lc->innerConeAngle  = d.lightSpotInner;
        lc->outerConeAngle  = d.lightSpotOuter;
        lc->direction       = d.lightDir;
        lc->useTemperature  = (d.lightUseTemp != 0);
        lc->temperatureK    = d.lightTempK;
        lc->castShadows     = (d.lightCastShadows != 0);
        lc->setEnabled(d.lightEnabled != 0);
        // d.lightAmbient 는 예약 슬롯이라 적용하지 않습니다(사문 필드 제거됨).
    }
    if (d.hasCamera) {
        auto* cc    = a->addComponent<CameraComponent>();
        cc->fovY    = d.camFovY;
        cc->zNear   = d.camZNear;
        cc->zFar    = d.camZFar;
        cc->primary = (d.camPrimary != 0);
        cc->setEnabled(d.camEnabled != 0);
    }
    if (d.hasAudio) {
        auto* ac     = a->addComponent<AudioComponent>();
        ac->filePath = d.audioFile;
        ac->volume   = d.audioVol;
        ac->loop     = (d.audioLoop     != 0);
        ac->autoPlay = (d.audioAutoPlay != 0);
        ac->setEnabled(d.audioEnabled != 0);
    }
    if (d.hasRigid) {
        auto* rb        = a->addComponent<RigidBodyComponent>();
        rb->shape       = static_cast<EPhysicsShape>(d.rigidShape);
        rb->motionType  = static_cast<EMotionType>(d.rigidMotion);
        rb->friction    = d.rigidFric;
        rb->restitution = d.rigidRest;
        rb->mass        = d.rigidMass;
        rb->halfExtent  = {d.rigidHX, d.rigidHY, d.rigidHZ};
        rb->setEnabled(d.rigidEnabled != 0);
    }
    return a;
}
