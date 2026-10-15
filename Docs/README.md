# Haruka 문서 색인

이 폴더의 문서를 범주별로 정리한 지도입니다. 문서끼리 상대 경로 마크다운 링크로 촘촘히
얽혀 있어, 활발히 참조되는 활성 문서는 **최상위에 그대로 두고** 이 색인으로 범주를 부여합니다.
신뢰도가 낮은 외부 엔진 분석만 [`archive/`](archive/)로 물리 이동했습니다.

정본 우선순위: 진행할 일은 [Roadmap.md](Roadmap.md), 결함은 [Core_Stabilization_Spec.md](Core_Stabilization_Spec.md)
와 [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md)가 정본입니다.

---

## 색인 · 진입점

| 문서 | 내용 |
|---|---|
| [Engine_Specification.md](Engine_Specification.md) | 엔진 사양 명세서: 개발 이력 · 구현 기능 · 미구현 기능 · 열린 결함 · 우려되는 점 종합 |
| [Roadmap.md](Roadmap.md) | 진행 예정 작업의 우선순위 정본 |
| [CHANGES.md](CHANGES.md) | 변경 이력 |
| [Architecture_Overview.md](Architecture_Overview.md) | 엔진 아키텍처 개요 |
| [Layer_Architecture.md](Layer_Architecture.md) | 코어(Layer 1)와 도메인 경계, 단위 규약 |

## 활성 설계 정본

| 문서 | 내용 |
|---|---|
| [Lighting_UE_Design.md](Lighting_UE_Design.md) | 라이팅 UE 용어·설계(색온도·Cast Shadows 포함) |
| [Editor_Layout_Design.md](Editor_Layout_Design.md) | 에디터 레이아웃 구조(UE5 기반): 5분할 + 메뉴바/상태바/뷰포트툴바/아웃라이너 계층 |
| [Log_Category_Design.md](Log_Category_Design.md) | 로그 카테고리: 기록 수준과 표시 필터 분리, Output Log 필터 |
| [Transform_Design.md](Transform_Design.md) | 트랜스폼·기즈모, 월드 행렬 캐시 제거 |
| [Spatial_Representation_Design.md](Spatial_Representation_Design.md) | 엔티티 무관 공간 표현, Transform 캡슐화 |
| [Transparency_Temporal_Design.md](Transparency_Temporal_Design.md) | 반투명(볼록 2패스)·TAA 처리 |
| [Viewport_Render_Design.md](Viewport_Render_Design.md) | 뷰포트 렌더(renderExtent 분리)·좌표 단일화 |
| [RendererRefactor.md](RendererRefactor.md) | 렌더러 구조: RenderContext·GpuScene·FrameResources·passes |
| [Primitive_And_Lighting_Design.md](Primitive_And_Lighting_Design.md) | 절차 도형·라이팅 규약(단위·머티리얼) |
| [Audio_Design.md](Audio_Design.md) | 오디오: 재생 규칙(일시정지·비활성), 청취자·3D 감쇠 설정, 씬 형식, 시험 음원 |
| [Core_Stabilization_Spec.md](Core_Stabilization_Spec.md) | 결함 심층 분석 + 마스터 체크리스트(정본) |
| [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) | 결함 작업 지시서(정본, Stabilization 과 상태 일치) |
| [Editor_QA_Checklist.md](Editor_QA_Checklist.md) | 에디터 실기 체크리스트: 준비·결함 재현·기능 회귀·결과 기록, 자동 조작 요령 |

## 결정 기록 (ADR)

| 문서 | 내용 |
|---|---|
| [Vulkan_Single_RHI_Decision.md](Vulkan_Single_RHI_Decision.md) | Vulkan 단일 RHI 채택 근거 |
| [Memory_Allocator_Decision.md](Memory_Allocator_Decision.md) | 메모리 할당자(존 기반) 결정 |
| [ECS_Decision.md](ECS_Decision.md) | ECS 도입 보류 근거와 재검토 트리거 |
| [Engine_Strategy_Evaluation.md](Engine_Strategy_Evaluation.md) | 엔진 전략 평가(범용 코어 + 첫 도메인) |
| [Threading.md](Threading.md) | 스레드 소유권 규칙(계층 무관 전역 규약) |

## VR · 이식 (계획 문서)

2026-10-04 에 PC OpenXR 연결과 두 눈 렌더(VR 첫걸음)를 넣었고, 헤드셋 확인과 Quest 이식이 남았습니다. 아래 두 문서는
착수 전에 쓴 사전 조사 · 계획이라 현재 구현과 다른 부분이 있습니다. 현재 상태는 [Engine_Specification.md](Engine_Specification.md) 를 보십시오. 사운드는 2026-10-02 에
기초를 정리했고, 정본은 위의 [Audio_Design.md](Audio_Design.md) 입니다.

| 문서 | 내용 |
|---|---|
| [VR_Transition_Masterplan.md](VR_Transition_Masterplan.md) | VR 전환 마스터플랜(스테레오·OpenXR·Reverse-Z 등) |
| [Deep_Space_VR_Specification.md](Deep_Space_VR_Specification.md) | 심우주 VR 도메인 사양 |

## 아카이브 — 외부 엔진 분석 (신뢰도 낮음, 참고용)

외부 엔진(Carbon/Trinity) 소스를 분석한 문서입니다. **구체 수치에 다수 오류가 있어** 근거로
직접 인용하지 말고 참고용으로만 봅니다(메모리 `carbon-docs-reliability` 참조). Haruka 설계 정본과
섞이지 않도록 [`archive/`](archive/)로 분리했습니다.

| 문서 | 내용 |
|---|---|
| [archive/Carbon_Source_Audit.md](archive/Carbon_Source_Audit.md) | 1차 자료 검증(다른 Carbon 문서 정정의 근거) |
| [archive/Carbon_Engine_Math_Analysis.md](archive/Carbon_Engine_Math_Analysis.md) | Carbon 수학 분석 |
| [archive/Carbon_Proprietary_Exclusions.md](archive/Carbon_Proprietary_Exclusions.md) | 차용 제외 대상 판정 |
| [archive/Deep_Carbon_Network_Analysis.md](archive/Deep_Carbon_Network_Analysis.md) | 네트워크(HNet) 사전 조사 — 범위 밖 |
| [archive/Full_Carbon_Ecosystem_Analysis.md](archive/Full_Carbon_Ecosystem_Analysis.md) | Carbon 생태계 전반 분석 |
| [archive/Trinity_Rendering_Analysis.md](archive/Trinity_Rendering_Analysis.md) | Trinity 렌더링(Reverse-Z 전환 참고) |

## 브랜드 자산

| 위치 | 내용 |
|---|---|
| [Logo/](Logo/) | 로고 마스터 `Logo_Final.png`(1658px)가 그림의 정본입니다. `Logo.pptx` 는 도형 원본이지만 어두운 배경 대응용 흰 원판이 빠져 있으니 여기서 아이콘을 다시 만들지 마십시오. `build_icon.py` 가 마스터에서 실행 파일 아이콘 [`res/Haruka.ico`](../res/Haruka.ico)(16~256px 7종)를 만들고, 리소스 등록은 [`res/Haruka.rc`](../res/Haruka.rc) 에 있습니다. |
