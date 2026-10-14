#pragma once

// ── 등록되지 않은 컴포넌트의 자리표시자 (DEF-22) ──────────────────────────────
// 씬 · 프리팹 파일에 등록되지 않은 타입의 COMPONENT 블록이 있으면(예: 그 타입을 등록하는 Layer 2 모듈 없이
// 씬을 엶) 경고를 남기고 이 컴포넌트에 원문을 그대로 담아 둡니다. 저장할 때 원래 이름과 값을 그대로 다시
// 쓰므로 데이터가 사라지지 않습니다(사용자 결정, Unity 의 Missing Script 와 같은 방식). 동작은 없습니다.

#include <string>
#include <utility>
#include <vector>

#include "Component.hpp"
#include "ComponentFields.hpp"

class UnknownComponent : public Component {
public:
    std::string originalType;                                  // 파일에 적힌 타입 이름
    std::vector<std::pair<std::string, std::string>> rawFields;   // enabled 를 포함한 원래 값(순서 그대로)

    const char* typeName() const override { return "UnknownComponent"; }
    std::string readOnlyInfo() const override {
        return "Unregistered type '" + originalType + "' (" + std::to_string(rawFields.size())
             + " values kept as is)";
    }
    void writeFields(ComponentFieldWriter& w) const override {
        for (const auto& kv : rawFields) w.put(kv.first, kv.second);
    }
};
