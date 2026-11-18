#pragma once

// XR 리그 바닥 찾기(hxr::floorBelow, XrComponents.hpp)에서 빼는 컴포넌트의 표지(2026-11-18, 엔진 구조 안정화 S4).
// 바닥 찾기는 컴포넌트의 로컬 바운드(Component::localBounds)를 밟을 수 있는 면으로 봅니다. 바운드가 있어도 서 있을 곳이 아닌
// 컴포넌트(3D UI 패널 등)가 Component 와 함께 상속합니다. 예전에는 Component 기반 클래스의 가상 함수 blocksFloor 였습니다.
class INotFloorSurface {
public:
    virtual ~INotFloorSurface() = default;
};
