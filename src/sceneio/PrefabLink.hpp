#pragma once
// 링크된 프리팹(2026-11-21, Docs/Linked_Prefabs.md). 씬 · 프리팹 글의 인스턴스 블록을 펼치고(읽기), 액터 세트를 인스턴스 블록 +
// 덮어쓰기로 씁니다(쓰기). 글 단계에서 다루므로 컴포넌트가 무엇인지 몰라도 되고, 등록되지 않은 컴포넌트도 같은 규칙입니다.
//
//   PREFAB:/Game/prefabs/Lamp.hprefab          인스턴스 블록(블록 순번 하나)
//   GUID:<인스턴스 루트 GUID>
//   PARENT:<블록 순번> 또는 PARENT_GUID:<16진수>
//   OVERRIDE:<짝> <경로>=<값>                  경로 = POS · NAME · @PARENT · LightComponent[0].intensity 등(D4)
//   REMOVE:<짝>  /  REMOVE:<짝> <형>[<순번>]    지운 액터 · 지운 컴포넌트(D5)
//   MEMBER_GUID:<짝> <GUID>                    파생 값과 다른 GUID(붙여넣기 등)
//   END_PREFAB
//
// 짝 = 펼친 원본 안의 GUID, 인스턴스 안 GUID = derive(루트 GUID, 짝)(D3). 펼친 글은 보통 ACTOR 블록에 PREFAB_SOURCE:(루트) ·
// PREFAB_PAIR:(소속) · PREFAB_RAW:(펼치지 못한 루트의 원래 줄) 줄을 더한 "전체" 형식이고, SceneSerializer 가 그대로 읽습니다.

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../core/Guid.hpp"

class Actor;
class World;

namespace hprefablink {

// 읽은 프리팹 글을 경로별로 둡니다(D2). 에디터가 월드 서비스로 붙이고(World::setService), 덮어쓰기를 구하는 기준이 됩니다.
// 없으면(게임 실행 파일) 파일에서 바로 읽습니다.
class PrefabSourceCache {
public:
    // 캐시에 있으면 그 글, 없으면 가상 파일 시스템에서 읽어 기억합니다.
    bool get(const std::string& path, std::string& out);
    bool peek(const std::string& path, std::string* out = nullptr) const;
    void set(const std::string& path, std::string text);
    void erase(const std::string& path);
    void clear() { texts_.clear(); }
    size_t size() const { return texts_.size(); }

private:
    std::unordered_map<std::string, std::string> texts_;   // 키 = 정규 경로(hvfs::toCanonical)
};

using Loader = std::function<bool(const std::string& path, std::string& text)>;

// 월드에 캐시 서비스가 있으면 그것을, 없으면 파일을 읽는 함수.
Loader loaderFor(const World* world);

// 인스턴스 안 GUID(D3). 0 이 되지 않습니다.
HGuid derive(HGuid root, HGuid source);

// ── 글 블록 모델 ──────────────────────────────────────────────────────────────
struct TComp {
    std::string type;
    std::vector<std::pair<std::string, std::string>> fields;
};
struct TBlock {
    bool        instance = false;   // PREFAB: 블록
    std::string name;               // ACTOR: 이름
    std::string source;             // PREFAB: 경로
    HGuid       guid;
    int         parent = -1;        // 세트 안 블록 순번
    HGuid       parentGuid;         // PARENT_GUID:
    std::vector<std::pair<std::string, std::string>> props;   // 그 밖의 KEY:VAL(파일 순서)
    std::vector<TComp>       comps;
    std::vector<std::string> lines;  // 인스턴스 블록의 OVERRIDE · REMOVE · MEMBER_GUID 원문
    std::string              linkSource;   // PREFAB_SOURCE:
    HGuid                    linkPair;     // PREFAB_PAIR:
    std::vector<std::string> linkRaw;      // PREFAB_RAW:
};
std::vector<TBlock> parseBlocks(const std::string& text);
std::string         writeBlocks(const std::vector<TBlock>& blocks);

// 글에 인스턴스 블록 · PARENT_GUID 가 있는지.
bool needsExpand(const std::string& text);

// 인스턴스 블록을 모두 펼친 전체 형식 글(머리 줄 없음). errors 에 사람이 읽을 오류(없는 원본 · 순환 등)를 더합니다.
std::string expandText(const std::string& text, const Loader& load, std::vector<std::string>* errors = nullptr);

// 원본 하나를 루트 GUID root 로 펼칩니다(소속 액터의 PREFAB_PAIR · 루트의 PREFAB_SOURCE 까지). 실패면 false.
bool expandSource(const std::string& path, HGuid root, const Loader& load, std::vector<TBlock>& out,
                  std::vector<std::string>* errors = nullptr);

// source 를 펼칠 때 target 이 쓰이는지(source == target 포함). 순환 · 없는 파일에서도 멈춥니다.
bool dependsOn(const std::string& source, const std::string& target, const Loader& load);

// ── 소속 ─────────────────────────────────────────────────────────────────────
// 액터가 속한 인스턴스 루트(루트면 자신). 소속이 아니면 nullptr. 짝이 있어도 조상 중 루트가 없으면(인스턴스 밖으로 옮김) 소속이 아닙니다.
Actor*       instanceRootOf(Actor* a);
const Actor* instanceRootOf(const Actor* a);
// 루트와 소속 액터(전위 순회, 0 번 = 루트). 더한 액터 · 그 아래 다른 인스턴스는 빠집니다.
std::vector<Actor*> membersOf(Actor* root);

// ── 쓰기 ─────────────────────────────────────────────────────────────────────
// 액터 세트(전위 순회, parentIndex = 세트 안 부모)를 인스턴스 블록으로 씁니다(머리 줄 없음). flattenRoot 가 세트 0 번의
// 원본과 같으면 그 인스턴스는 풀어 보통 액터로 씁니다(인스턴스를 고쳐 같은 프리팹으로 저장, D8).
std::string writeCompact(const std::vector<const Actor*>& set, const std::vector<int>& parentIndex, const Loader& load,
                         const std::string& flattenRoot = {}, std::vector<std::string>* errors = nullptr);

// 인스턴스 하나의 덮어쓰기 줄(OVERRIDE · REMOVE · MEMBER_GUID). 펼칠 수 없으면 ok = false.
std::vector<std::string> overridesOf(const Actor& root, const Loader& load, bool* ok = nullptr);

// 소속 액터 하나의 덮어쓴 경로들(OVERRIDE 경로, 지운 컴포넌트는 "<형>[<순번>]"). 디테일 패널 표시용.
std::vector<std::string> overridePathsOf(const Actor& member, const Loader& load);

// 소속 액터 하나의 덮어쓰기 줄(그 액터 것만, 지운 액터 줄 없음). 원본에 적용이 씁니다.
std::vector<std::string> overrideLinesOf(const Actor& member, const Loader& load);

// 소속 액터 하나의 원본 값(액터 블록 글, ACTOR: ~ END_ACTOR). 되돌리기가 씁니다. 실패면 false.
bool baselineBlock(const Actor& member, const Loader& load, std::string& out);

// 원본 프리팹 글에 덮어쓰기 줄(인스턴스 쪽 짝 기준)을 넣습니다(D9 원본에 적용). 중첩 인스턴스의 액터면 안쪽 인스턴스 블록의
// 덮어쓰기로 넣습니다. 넣은 줄 수, 실패한 줄은 errors.
int applyToSourceText(std::string& text, const std::string& sourcePath, const std::vector<std::string>& lines,
                      const Loader& load, std::vector<std::string>* errors = nullptr);

// Fresh(붙여넣기 · 복제)에서 쓸 GUID 표. 보통 액터는 새 값, 세트 안 인스턴스 소속 액터는 derive(새 루트, 짝)(D3).
std::unordered_map<HGuid, HGuid> freshMapping(const std::string& text, const std::function<bool(HGuid)>& inUse);

} // namespace hprefablink

struct ComponentLoadContext;

namespace hprefablink {

// 액터 하나를 액터 블록 글로(링크 줄 · 부모 없음). 되돌리기 커맨드의 전후 상태.
std::string actorBlock(const Actor& a);
// 액터 블록 글의 이름 · 트랜스폼 · 활성 · 태그 · 컴포넌트를 그 액터에 넣습니다(GUID · 부모 · 링크는 그대로). 글이 틀리면 false.
bool applyActorBlock(Actor& a, const std::string& text, const ComponentLoadContext& ctx);

} // namespace hprefablink
