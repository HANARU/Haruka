#include "SceneSerializer.hpp"
#include "../core/SceneLog.hpp"
#include "../core/TextFloat.hpp"
#include "AssetManager.hpp"
#include "../core/Actor.hpp"
#include "../core/World.hpp"
#include "../components/StaticMeshComponent.hpp"
#include "../components/LightComponent.hpp"
#include "../components/CameraComponent.hpp"
#include "../components/AudioComponent.hpp"
#include "../components/RigidBodyComponent.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <sstream>
#include <algorithm>
#include <unordered_map>

static glm::vec3 quatToEulerDeg(const glm::quat& q) {
    return glm::degrees(glm::vec3(glm::pitch(q), glm::yaw(q), glm::roll(q)));
}

static glm::vec3 parseVec3(const std::string& s) {
    std::istringstream ss(s); glm::vec3 v{};
    ss >> v.x >> v.y >> v.z; return v;
}

// 실수는 전부 이 함수로 씁니다. 예전에는 iostream 기본값(유효 숫자 6자리)으로 써서,
// Play→Stop 이나 저장·불러오기를 거칠 때마다 위치·색 같은 값이 조금씩 바뀌었습니다.
static std::string F(float v) { return floatToText(v); }

// ── writeActor ─────────────────────────────────────────────────────────────────
void SceneSerializer::writeActor(std::ostream& os, const Actor* actor, int parentIndex) {
    const glm::vec3 pos   = actor->transform().position();
    const glm::vec3 euler = quatToEulerDeg(actor->transform().rotation());
    const glm::vec3 scale = actor->transform().scale();
    os << "POS:"   << F(pos.x)   << " " << F(pos.y)   << " " << F(pos.z)   << "\n";
    os << "ROT:"   << F(euler.x) << " " << F(euler.y) << " " << F(euler.z) << "\n";
    os << "SCALE:" << F(scale.x) << " " << F(scale.y) << " " << F(scale.z) << "\n";
    os << "ACTIVE:" << (actor->activeSelf() ? 1 : 0) << "\n";
    if (parentIndex >= 0)
        os << "PARENT:" << parentIndex << "\n";

    // 컴포넌트는 등록된 타입마다 COMPONENT 블록으로 씁니다(v4, DEF-22). 예전에는 다섯 종을 여기서
    // LIGHT: · CAMERA: · RIGID: 같은 줄로 손으로 썼고, 새 컴포넌트는 저장할 수 없었습니다.
    ComponentSerializer::write(os, *actor);
}

// ── parseActorLine ────────────────────────────────────────────────────────────────
void SceneSerializer::parseActorLine(const std::string& line, ActorData& d) {
    if (d.components.feed(line, d.name)) return;   // COMPONENT 블록(v4)
    const auto colon = line.find(':');
    if (colon == std::string::npos) return;
    parseLine(line.substr(0, colon), line.substr(colon + 1), d);
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
    else if (key == "PRIM") {
        // 값이 깨졌으면 메시 없는 액터가 됩니다. 다른 도형으로 대체하지 않습니다(설계 D4).
        d.hasPrim = PrimitiveDesc::fromKey(val, d.prim);
        if (!d.hasPrim)
            HLOG(LogScene, Error) << "actor '" << d.name << "': invalid PRIM value '" << val
                                  << "'; the actor gets no mesh";
    } else if (key == "PRIM_MAT") {
        // 머티리얼이 깨진 것은 도형을 버릴 이유가 아닙니다. 기본 머티리얼로 두고 알립니다.
        if (!PrimitiveMaterialDesc::fromText(val, d.primMat)) {
            HLOG(LogScene, Error) << "actor '" << d.name << "': invalid PRIM_MAT value '" << val
                                  << "'; using the default primitive material";
            d.primMat = PrimitiveMaterialDesc{};
        }
    } else if (key == "LIGHT") {
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
    } else if (key == "AUDIO_3D") {
        // 3D 설정이 깨진 것은 소리를 버릴 이유가 아닙니다. 기본 설정으로 두고 알립니다.
        if (!AudioAttenuationSettings::fromText(val, d.audioAtten)) {
            HLOG(LogScene, Error) << "actor '" << d.name << "': invalid AUDIO_3D value '" << val
                                  << "'; using the default 3D sound settings";
            d.audioAtten = AudioAttenuationSettings{};
        }
    } else if (key == "RIGID") {
        // 이름과 옛 순번(0~2)을 모두 받습니다. 모르는 값은 기본값(box·dynamic)으로 두고
        // 알립니다. 예전에는 범위 검사 없이 캐스트했습니다(DEF-44).
        d.hasRigid = true;
        std::istringstream ss(val);
        std::string shapeTok, motionTok;
        ss >> shapeTok >> motionTok;
        EPhysicsShape shape  = EPhysicsShape::Box;
        EMotionType   motion = EMotionType::Dynamic;
        if (!RigidBodyComponent::shapeFromText(shapeTok, shape))
            HLOG(LogScene, Error) << "actor '" << d.name << "': invalid RIGID shape '" << shapeTok
                                  << "'; using box";
        if (!RigidBodyComponent::motionFromText(motionTok, motion))
            HLOG(LogScene, Error) << "actor '" << d.name << "': invalid RIGID motion '" << motionTok
                                  << "'; using dynamic";
        d.rigidShape  = (int)shape;
        d.rigidMotion = (int)motion;
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

    if (d.hasPrim) {
        if (!d.model.empty())
            HLOG(LogScene, Warning) << "actor '" << d.name << "' has both PRIM and MODEL; using PRIM";
        try {
            // 실패하면 AssetManager 가 이유를 남기고 빈 쌍을 돌려줍니다. 그때는 메시를 붙이지 않습니다.
            AssetPair pair = assets.acquirePrimitive(d.prim);
            if (pair.mesh) {
                auto* mc              = a->addComponent<StaticMeshComponent>();
                mc->mesh              = pair.mesh;
                mc->material          = assets.acquirePrimitiveMaterial(d.primMat);
                mc->primitive         = d.prim;
                mc->primitiveMaterial = d.primMat;
                mc->vertCount         = pair.vertCount;
                mc->triCount          = pair.triCount;
                mc->visible           = (d.meshVisible != 0);
                mc->setEnabled(d.meshEnabled != 0);
            }
        } catch (const std::exception& ex) {
            HLOG(LogScene, Error) << "Primitive create failed (" << d.name << "): " << ex.what();
        }
    } else if (!d.model.empty()) {
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
        // 반경은 0 보다 커야 합니다(DEF-35). Point·Spot 에서 0 이하(또는 NaN)가 들어오면
        // 최솟값으로 고치고 알립니다. Directional 은 반경을 쓰지 않으므로 조용히 고칩니다.
        if (!(lc->attenuationRadius >= LightComponent::kMinAttenuationRadius)) {
            if (lc->lightType != LightType::Directional)
                HLOG(LogScene, Warning) << "Light on '" << a->name << "' has attenuation radius "
                    << d.lightRange << "; clamped to " << LightComponent::kMinAttenuationRadius << " m";
            lc->attenuationRadius = LightComponent::kMinAttenuationRadius;
        }
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
        ac->attenuation = d.audioAtten;
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
    // v4 의 COMPONENT 블록(DEF-22). 등록된 타입이면 만들어 값을 읽고, 아니면 원문을 보존합니다.
    ComponentLoadContext ctx;
    ctx.assets = &assets;
    ComponentSerializer::spawn(*a, d.components.blocks(), ctx);
    return a;
}

// ── 씬 단위(.hscene) ───────────────────────────────────────────────────────────
// 예전에는 EditorApp 에만 있어서, 게임 모듈이 시작 씬을 파일에서 읽을 수 없었습니다.
// v3: PRIM/PRIM_MAT 추가, 실수를 정밀도를 잃지 않는 형식으로 기록. 이어서 AUDIO_3D 도 v3 에
// 넣었습니다(값이 없으면 기본값이라 판을 올리지 않음).
// v4(2026-10-14): 컴포넌트를 COMPONENT 블록으로 씁니다(DEF-22). v3 이하 파일도 계속 읽습니다. v4 를
// 모르는 옛 엔진은 COMPONENT 블록의 줄을 모르는 키로 건너뛰므로 컴포넌트 없이 액터만 읽습니다.
std::string SceneSerializer::writeScene(const World& world, const Actor* skip) {
    std::ostringstream oss;
    oss << "# Haruka Scene v4\n";

    // 직렬화 세트(skip 제외)에서 각 액터에 순번을 매겨, 부모를 인덱스로 참조합니다.
    // 부모가 세트 밖(예: 에디터 카메라)이면 -1 로 기록해 링크를 남기지 않습니다.
    std::unordered_map<const Actor*, int> indexOf;
    int n = 0;
    for (auto& ap : world.getActors()) {
        if (ap.get() == skip) continue;
        indexOf[ap.get()] = n++;
    }

    for (auto& ap : world.getActors()) {
        const Actor* a = ap.get();
        if (a == skip) continue;

        int parentIdx = -1;
        if (const Actor* p = a->getParent()) {
            auto it = indexOf.find(p);
            if (it != indexOf.end()) parentIdx = it->second;
        }

        oss << "ACTOR:" << a->name << "\n";
        writeActor(oss, a, parentIdx);
        oss << "END_ACTOR\n";
    }
    return oss.str();
}

std::vector<Actor*> SceneSerializer::readScene(const std::string& text, World& world, AssetManager& assets) {
    // 직렬화 순서대로 블록마다 정확히 한 슬롯을 만듭니다(스폰 실패 시 nullptr 자리표시자).
    // writeScene 이 액터마다 위치 인덱스를 매기므로, 이름 빈 블록 등으로 스폰이
    // 실패해도 슬롯을 예약해야 그 뒤 PARENT 인덱스가 밀리지 않습니다. 부모 배선은 전부
    // 스폰된 뒤에 하며, 파일에서 부모가 자식보다 뒤에 올 수도 있어 전방 참조를 허용합니다.
    std::vector<Actor*> spawned;   // 블록당 한 항목, 스폰 실패면 nullptr
    std::vector<int>    parentOf;
    bool                inBlock = false;
    ActorData           cur;

    auto closeBlock = [&] {
        if (!inBlock) return;
        Actor* a = spawn(cur, world, assets);
        spawned.push_back(a);
        parentOf.push_back(a ? cur.parentIndex : -1);
        cur     = ActorData{};
        inBlock = false;
    };

    std::string line;
    std::istringstream iss(text);
    while (std::getline(iss, line)) {
        // 텍스트 모드가 아닌 경로(다른 플랫폼, 메모리에서 읽은 파일)에서는 CRLF 의 \r 이
        // 값 끝에 남아, 경로와 PRIM 값이 조용히 어긋납니다.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        // END_ACTOR · ACTOR: 는 COMPONENT 블록보다 먼저 봅니다. END_COMPONENT 가 빠진 블록이 있어도
        // 다음 액터를 그 블록의 값으로 삼키지 않습니다.
        if (line == "END_ACTOR") { closeBlock(); continue; }
        if (line.rfind("ACTOR:", 0) == 0) {
            closeBlock();          // END_ACTOR 없이 다음 블록이 와도 이전 블록을 닫음
            inBlock  = true;
            cur.name = line.substr(6);
            continue;
        }
        parseActorLine(line, cur);
    }
    closeBlock();                  // END_ACTOR 없이 끝나는 마지막 블록

    // 부모 배선: PARENT 인덱스는 직렬화(블록) 순서이며 spawned 인덱스와 1:1 대응입니다.
    // attachTo 가 사이클을 거부하고 실효 활성을 다시 맞추므로, 비활성 부모 아래 자식은
    // 여기서 꺼지고, 순환 참조가 있어도 크래시하지 않습니다. null 슬롯은 건너뜁니다.
    for (size_t i = 0; i < spawned.size(); ++i) {
        int p = parentOf[i];
        if (p >= 0 && p < (int)spawned.size()
            && spawned[i] && spawned[p] && spawned[p] != spawned[i])
            spawned[i]->attachTo(spawned[p]);
    }
    return spawned;
}
