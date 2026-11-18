#pragma once
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <functional>
#include <typeindex>
#include <unordered_map>

#include "../scene/Component.hpp"   // typeid(c) 에 완전한 형이 필요

// ── 컴포넌트 시각화 등록 (2026-11-18, 엔진 구조 안정화 S4) ─────────────────────────────────────────
// 에디터 뷰포트에서 컴포넌트를 보이고 잡히게 하는 아이콘과, 선택했을 때 그리는 범위 표시를 컴포넌트 형마다 등록합니다. UE 의
// FComponentVisualizer(에디터 모듈에 형별로 등록) · Godot 의 3D 기즈모 플러그인과 같은 자리입니다. 예전에는 Component 기반
// 클래스의 가상 함수(editorIcon · editorIconShape · drawEditorVisual)라, 게임 실행 파일에도 에디터 그리기 코드가 들어갔습니다.
// 형은 정확히 맞춰 찾습니다(typeid). 등록한 형을 상속한 컴포넌트는 따로 등록하십시오.
struct ComponentVisualizer {
    // 솔리드 바운드가 없는 엔티티(라이트 · 카메라 · 소리)를 뷰포트에서 보이고 잡히게 하는 아이콘. 액터 원점에 화면 고정 크기로
    // 그리고 화면 원판 판정으로 픽합니다.
    bool      hasIcon   = false;
    glm::vec4 iconColor { 0.f };
    int       iconShape = 0;   // 0 사각형, 1 라이트, 2 카메라, 3 소리, 4 구역, 5 안개, 6 반사 캡처(2026-11-17)
    // 선택했을 때의 범위 표시(라이트 범위 · 원뿔, 카메라 절두체, 소리 반경, 충돌체 등). hdebug 선으로 그립니다.
    std::function<void(const Component&)> drawSelected;
};

class ComponentVisualizers {
public:
    ComponentVisualizers();   // 엔진 컴포넌트를 등록합니다(ComponentVisualizers.cpp)

    template<class T>
    void add(ComponentVisualizer v, void (*draw)(const T&) = nullptr) {
        if (draw) v.drawSelected = [draw](const Component& c) { draw(static_cast<const T&>(c)); };
        map_[std::type_index(typeid(T))] = std::move(v);
    }
    const ComponentVisualizer* find(const Component& c) const {
        auto it = map_.find(std::type_index(typeid(c)));
        return it != map_.end() ? &it->second : nullptr;
    }
    // 아이콘이 있으면 색(과 모양)을 돌려줍니다. 예전 Component::editorIcon · editorIconShape.
    bool icon(const Component& c, glm::vec4& color, int* shape = nullptr) const {
        const ComponentVisualizer* v = find(c);
        if (!v || !v->hasIcon) return false;
        color = v->iconColor;
        if (shape) *shape = v->iconShape;
        return true;
    }
    // 선택했을 때의 범위 표시. 예전 Component::drawEditorVisual.
    void drawSelected(const Component& c) const {
        if (const ComponentVisualizer* v = find(c); v && v->drawSelected) v->drawSelected(c);
    }

private:
    std::unordered_map<std::type_index, ComponentVisualizer> map_;
};
