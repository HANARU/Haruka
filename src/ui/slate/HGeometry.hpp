#pragma once
#include "HSlateTypes.hpp"

// ── HGeometry (Slate의 FGeometry) ─────────────────────────────────────────────
// 위젯의 렌더 타겟 기준 절대 위치 + 크기
struct HGeometry {
    glm::vec2 position = {};  // 렌더 타겟 좌상단 기준 절대 좌표 (px)
    glm::vec2 size     = {};  // 위젯 크기 (px)
    float     scale    = 1.f; // HiDPI 배율 (기본 1.0)

    // 자식 위젯의 Geometry 생성: localOffset은 이 위젯 내 로컬 좌표
    HGeometry makeChild(glm::vec2 localOffset, glm::vec2 childSize, float childScale = 1.f) const {
        HGeometry g;
        g.position = position + localOffset;
        g.size     = childSize;
        g.scale    = scale * childScale;
        return g;
    }

    // 패딩을 제거한 콘텐츠 영역
    HGeometry deflate(const HPadding& p) const {
        HGeometry g;
        g.position = position + glm::vec2(p.left, p.top);
        g.size     = glm::max(size - glm::vec2(p.totalH(), p.totalV()), glm::vec2(0.f));
        g.scale    = scale;
        return g;
    }

    bool contains(glm::vec2 point) const {
        return point.x >= position.x && point.x < position.x + size.x &&
               point.y >= position.y && point.y < position.y + size.y;
    }

    // HAlign 적용: 주어진 contentSize를 this->size 안에서 정렬
    float alignH(float contentWidth, EHAlign h) const {
        switch (h) {
            case EHAlign::Left:   return position.x;
            case EHAlign::Right:  return position.x + size.x - contentWidth;
            case EHAlign::Center: return position.x + (size.x - contentWidth) * 0.5f;
            default:              return position.x; // Fill
        }
    }
    float alignV(float contentHeight, EVAlign v) const {
        switch (v) {
            case EVAlign::Top:    return position.y;
            case EVAlign::Bottom: return position.y + size.y - contentHeight;
            case EVAlign::Center: return position.y + (size.y - contentHeight) * 0.5f;
            default:              return position.y; // Fill
        }
    }
};
