#pragma once

// 엔진 기본 컴포넌트 다섯을 ComponentRegistry 에 등록합니다(DEF-22). Engine 이 시작할 때 한 번 부르고,
// 여러 번 불러도 이미 있는 이름은 건너뜁니다. 등록 순서가 에디터의 컴포넌트 추가 버튼 순서입니다.
void registerCoreComponents();
