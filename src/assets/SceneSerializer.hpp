#pragma once
#include <string>
#include <ostream>
#include <vector>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "../geometry/PrimitiveDesc.hpp"
#include "../audio/AudioAttenuation.hpp"
#include "ComponentSerializer.hpp"

class Actor;
class World;
class AssetManager;

// ── SceneSerializer ───────────────────────────────────────────────────────────
// Actor 단위 텍스트 직렬화의 단일 구현. .hscene(EditorApp·게임 모듈)과 .hprefab(PrefabManager)
// 양쪽에서 공용으로 사용한다. 액터 단위 함수에서는 이름 헤더(ACTOR:/NAME:)와 블록 구분이
// 호출자 책임이고, 씬 단위 함수(writeScene/readScene)는 .hscene 의 블록 규칙까지 맡는다.
//
// v4(2026-10-14, DEF-22): 컴포넌트는 COMPONENT 블록으로 쓴다(ComponentSerializer). 타입은 등록된
// 이름(ComponentRegistry), 값은 이름 붙은 `이름=값` 줄이다. 그래서 새 컴포넌트(Layer 2 포함)도 등록만
// 하면 저장 · 불러오기가 되고, 이 파일을 고칠 필요가 없다. v3 이하의 LIGHT: · CAMERA: · RIGID: 같은 줄은
// 옛 파일을 읽으려고 계속 받는다(아래 ActorData 의 v3 필드). 쓰기는 v4 만 한다.
class SceneSerializer {
public:
    // 파싱된 액터 필드를 스폰 전까지 담아두는 중간 표현
    struct ActorData {
        std::string name;
        glm::vec3   pos{}, euler{}, scale{1.f};
        // QUAT: 줄(회전의 정확한 값, DEF-108). 없거나 ROT 를 손으로 고쳤으면 euler 를 씁니다.
        glm::quat   quat{ 1.f, 0.f, 0.f, 0.f };
        bool        hasQuat     = false;
        int         active      = 1;   // 액터 activeSelf (기본 활성)
        int         parentIndex = -1;  // 직렬화 순서상 부모 인덱스, 부모 없으면 -1
        // v4 의 COMPONENT 블록들(DEF-22).
        ComponentSerializer::BlockParser components;

        // ── 아래는 v3 이하 파일을 읽을 때만 씁니다(컴포넌트 다섯을 필드로 받던 예전 형식) ──
        std::string model;
        int         meshIndex   = 0;
        int         meshVisible = 1, meshEnabled = 1;
        // 절차 도형(PRIM:/PRIM_MAT:). MODEL 과 배타입니다. 값이 깨졌으면 hasPrim 이 거짓이라
        // 메시 없는 액터가 됩니다. 다른 도형으로 대체하지 않습니다(설계 D4).
        bool                  hasPrim = false;
        PrimitiveDesc         prim;
        PrimitiveMaterialDesc primMat;
        bool        hasLight = false;
        glm::vec3   lightColor{1.f};
        float       lightIntensity = 0.85f;
        // 예약 슬롯: 과거 LightComponent::ambient. 지금은 사문 필드라 컴포넌트에서 제거됐지만,
        // 옛 씬 파일과의 토큰 위치 호환을 위해 LIGHT 라인의 자리는 유지하고 읽기만 합니다(미적용).
        float       lightAmbient = 0.15f;
        int         lightEnabled = 1;
        // Phase 0: 예전에는 저장 안 돼 저장·로드 시 Point/Spot 이 Directional 로 리셋됐습니다.
        // 옛 파일과의 호환을 위해 기존 값 뒤에 덧붙이고, 값이 없으면 아래 기본값이 유지됩니다.
        int         lightType      = 0;      // 0=Directional 1=Point 2=Spot
        float       lightRange     = 10.f;   // → attenuationRadius
        float       lightSpotInner = 15.f, lightSpotOuter = 30.f;   // → inner/outerConeAngle
        glm::vec3   lightDir{ 1.f, 2.f, 1.5f };   // 로컬 방향(worldDirection 이 정규화)
        // Phase 2 색온도. 다시 trailing 확장이라 옛 파일엔 없어 기본값(꺼짐·6500K)이 유지됩니다.
        int         lightUseTemp = 0;
        float       lightTempK   = 6500.f;
        // per-light Cast Shadows(DEF-26/27 후 도입). 기본 1(켜짐)이라 옛 파일도 그림자를 유지.
        int         lightCastShadows = 1;
        bool        hasCamera = false;
        float       camFovY = 45.f, camZNear = 0.1f, camZFar = 100.f;
        int         camPrimary = 0, camEnabled = 1;
        bool        hasAudio = false;
        std::string audioFile;
        float       audioVol = 1.f;
        int         audioLoop = 0, audioAutoPlay = 1, audioEnabled = 1;
        // AUDIO_3D(2026-10-02 추가). 옛 파일엔 없어 기본값(3D·Inverse·1~100 m·rolloff 1)이 유지됩니다.
        AudioAttenuationSettings audioAtten;
        bool        hasRigid = false;
        int         rigidShape = 0, rigidMotion = 1;
        float       rigidFric = 0.5f, rigidRest = 0.3f, rigidMass = 1.f;
        float       rigidHX = 0.5f, rigidHY = 0.5f, rigidHZ = 0.5f;
        int         rigidEnabled = 1;
    };

    // 트랜스폼(KEY:VAL 줄) + 부착된 모든 컴포넌트(COMPONENT 블록)를 기록 (이름 라인 제외).
    // parentIndex 가 0 이상이면 PARENT 라인을 기록합니다(직렬화 세트 안의 부모 인덱스).
    // 단일 액터를 저장하는 프리팹은 부모가 없으므로 기본값 -1 로 호출합니다.
    static void writeActor(std::ostream& os, const Actor* actor, int parentIndex = -1);

    // key:value 한 줄을 data에 반영. 인식한 키면 true.
    // 이름 키(ACTOR/NAME)는 블록 구분 의미가 포맷마다 달라 여기서 다루지 않는다.
    static bool parseLine(const std::string& key, const std::string& val, ActorData& data);
    // 액터 블록 안의 줄 하나(ACTOR: · NAME: · END_ACTOR 제외)를 반영합니다. COMPONENT 블록과 KEY:VAL
    // 줄을 모두 다룹니다. readScene 과 PrefabManager 가 같은 규칙으로 읽도록 둘 다 이것을 씁니다.
    static void parseActorLine(const std::string& line, ActorData& data);

    // 파싱 결과로 액터 생성 + 컴포넌트 부착. name이 비어 있으면 nullptr.
    static Actor* spawn(const ActorData& data, World& world, AssetManager& assets);

    // ── 씬 단위(.hscene) ─────────────────────────────────────────────────────
    // 액터마다 "ACTOR:이름" 으로 시작해 "END_ACTOR" 로 끝나는 블록이고, 부모는 블록 순서의
    // 인덱스(PARENT:)로 가리킨다. skip 은 저장하지 않을 액터(에디터 카메라)이며, 그 액터를
    // 부모로 둔 자식은 부모 없이 기록된다.
    static std::string writeScene(const World& world, const Actor* skip = nullptr);
    // 블록마다 한 슬롯을 돌려준다(스폰 실패면 nullptr). 부모 배선까지 마친다. 기존 액터는
    // 건드리지 않으므로, 씬을 갈아 끼우려면 호출자가 먼저 비운다.
    static std::vector<Actor*> readScene(const std::string& text, World& world, AssetManager& assets);
};
