#pragma once

// ── COMPONENT 블록 읽기 · 쓰기 (DEF-22) ─────────────────────────────────────────
// 씬 v4 · 프리팹 v2 의 액터 블록 안에서 컴포넌트마다 다음 블록을 씁니다.
//
//   COMPONENT:LightComponent
//   enabled=1
//   type=point
//   intensity=3
//   END_COMPONENT
//
// 타입 이름으로 ComponentRegistry 에서 컴포넌트를 만들고, 값은 Component::readField 가 이름으로 읽습니다.
// 등록되지 않은 타입은 경고하고 UnknownComponent 에 원문을 담아 두어 다시 저장할 때 그대로 씁니다.
// SceneSerializer(씬) 와 PrefabManager(프리팹)가 함께 쓰고, 렌더러 · 에셋에 기대지 않아 화면 없이
// 검산합니다(scene_components 테스트). 메시처럼 에셋이 필요한 컴포넌트는 ComponentLoadContext 로 받습니다.

#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "../core/ComponentFields.hpp"

class Actor;

namespace ComponentSerializer {

struct Block {
    std::string type;
    std::vector<std::pair<std::string, std::string>> fields;   // 파일 순서 그대로
};

// 액터의 컴포넌트를 붙은 순서대로 블록으로 씁니다. 등록되지 않은 타입은 쓰지 못하므로 타입마다 한 번 경고합니다.
void write(std::ostream& os, const Actor& actor);

// 액터 블록의 줄을 하나씩 받아 COMPONENT 블록을 모읍니다. COMPONENT: · END_COMPONENT 와 블록 안의 줄을
// 받았으면 true, 그 밖의 줄(액터 수준의 KEY:VAL)이면 false 를 돌려줍니다.
class BlockParser {
public:
    bool feed(const std::string& line, const std::string& actorName);
    const std::vector<Block>& blocks() const { return blocks_; }
    bool open() const { return open_; }

private:
    std::vector<Block> blocks_;
    bool               open_ = false;
};

// 블록마다 컴포넌트를 붙이고 값을 읽습니다. enabled 는 모든 컴포넌트에 공통이라 여기서 다룹니다.
// 만든 컴포넌트 수를 돌려줍니다(UnknownComponent 포함).
int spawn(Actor& actor, const std::vector<Block>& blocks, const ComponentLoadContext& ctx);

} // namespace ComponentSerializer
