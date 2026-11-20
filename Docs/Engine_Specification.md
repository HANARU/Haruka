# Haruka 엔진 사양 명세서

> **문서 지위**: 엔진의 정체성 · 개발 이력 · 현재 구현된 기능 · 미구현 기능 · 우려되는 점을 한곳에 모은 **종합 요약 문서**입니다.
> 개별 항목의 정본은 각 설계 · 결함 문서이며, 이 문서와 정본이 어긋나면 정본을, 정본과 코드가 어긋나면 코드를 따릅니다.
>
> **기준 시점**: 커밋 `5a9cb51`(2026-10-15). 수치와 기능 서술은 이 시점의 `src/` · `shaders/` · `tests/` 코드와 빌드 결과를
> 직접 확인해 적었습니다. 이 문서를 갱신할 때도 문서가 아니라 코드를 먼저 확인하십시오.
>
> **갱신(2026-10-15)**: 에디터의 액터 생성 · 삭제 Undo 를 4-11 · 4-12 · 5-4 · 7장에, XR 머리 자세 청취자를 4-8 · 5-3 에, 프로젝트 폴더 에셋 루트를 4-6 에 반영하고, 1-3 · 3-2 의 소스 · 테스트 수치를 이
> 변경까지 다시 셌습니다. 커밋 수와 결함 수는 기준 시점 그대로입니다.
>
> **갱신(2026-10-15, 엔진 기본 기능 보강)**: 커밋 `4cc931a` 뒤의 씬 관리 · 편집 기본기 · 플레이어와 입력 작업을 반영했습니다
> (정본 [Engine_Basics_Design.md](Engine_Basics_Design.md)). 1-3 · 3-2 의 소스 · 테스트 수치를 이 작업까지 다시 셌습니다.
>
> **갱신(2026-10-16)**: 헤드셋 2차 확인(DEF-110 · DEF-106 · 플레이어 리그)과 "XR 에는 언제나 리그" 결정을 2-2 · 2-3 · 4-5 · 7 · 8장에 반영하고
> 수치를 다시 셌습니다. 같은 날의 소리 출력 장치 선택은 4-8 · 4-11 에 있습니다.
>
> **갱신(2026-10-24, 패키징 1단계)**: 런타임 분리(게임 실행 파일 `HarukaGame`, DEF-12 해결) · 실행 폴더 기준 경로와 쓰기 루트 · 배포 ZIP ·
> 자체 묶음 파일(.hpak)과 가상 파일 시스템을 1-1 · 1-3 · 2장 · 3-2 · 3-3 · 4-1 · 4-6 · 4-12 · 5-1 · 6 · 7 · 8장에 반영하고 수치를 다시
> 셌습니다(정본 [Packaging_Design.md](Packaging_Design.md)). 이 작업에서 찾은 DEF-112 ~ 119 를 등재했습니다.
>
> **갱신(2026-10-31, 오류 검토와 패키징 마무리 묶음)**: 미커밋 작업 전체의 오류 검토에서 나온 DEF-120 ~ 147 을 등재하고, 그중 패키징
> 마무리 묶음 DEF-121 ~ 128 을 고친 결과를 1-3 · 2장 · 4-12 · 6 · 7 · 8장에 반영했습니다.
>
> **갱신(2026-10-31, 패키징 2단계 · 3단계 진행)**: Vulkan 1.1 기준선 · 확장 함수 · 배리어 분기 · 창 없는 렌더 경로 · 핵심 힙 · MSAA
> 지연 할당(DEF-04 · 06 · 42 · 97 · 103 · 118 · 120 해결)과 Android 빌드 · APK(DEF-08 빌드 부분)를 1-1 · 1-3 · 2장 · 4-3 · 4-4 ·
> 4-12 · 5-1 · 6 · 7 · 8장에 반영했습니다(정본 [Packaging_Design.md](Packaging_Design.md) 6 · 7절). 새 결함 DEF-148 을 등재했습니다.
>
> **갱신(2026-11-01, Android 오류 검출과 수정)**: 패키징 관점의 Android 오류 검출에서 나온 DEF-149 ~ 156 과 확인 중 찾은 Windows 쪽
> DEF-157 을 등재하고 모두 고쳤습니다(묶음과 실행 파일의 짝 확인 · configChanges · APK 고지 · 설정 파일 인코딩 · 파이프라인 캐시 파일 ·
> 종료 순서 · XR 손실 · SSAO 깊이 샘플러 · `_HAS_EXCEPTIONS`). 1-3 · 4-12 의 수치를 고쳤습니다(정본 [Packaging_Design.md](Packaging_Design.md) 7-1).

---

## 1. 개요

### 1-1. 정체성

**Haruka 는 Vulkan 기반 VR 엔진입니다.** 우주 · 무중력 기능은 그 코어 위에 얹을 첫 번째 도메인(Layer 2)이며, 엔진의 정의가
아닙니다. 코어(Layer 1)는 우주 도메인을 걷어내도 그 자체로 쓸 수 있는 범용 VR 엔진이어야 합니다. 정본은
[Layer_Architecture.md](Layer_Architecture.md) 와 [Engine_Strategy_Evaluation.md](Engine_Strategy_Evaluation.md) 입니다.

| 항목 | 내용 |
|---|---|
| 최종 목표 기기 | Meta Quest Pro(Snapdragon XR2+ Gen 1) 단독 실행 VR |
| 현재 실행 형태 | Windows 데스크톱 실행 파일 둘(2026-10-19): 에디터 `HarukaEngine`(엔진과 에디터가 같은 창), 게임만 실행하는 `HarukaGame`(에디터 코드 없음, 배포 ZIP 으로 내보냄) |
| VR 상태 | PC OpenXR 연결(`--xr`, Quest Link 등)과 두 눈 렌더. 2026-10-15 Meta Quest Pro(Quest Link)에서 두 눈 · 머리 추적 · 컨트롤러 · 72Hz 를 확인했습니다. 헤드셋에는 Play 동안만 그립니다. Quest 단독 실행은 APK 까지 만들었고(2026-10-31) 헤드셋 첫 실행이 남았습니다 |
| 언어 · API | C++20, Vulkan 1.1(데스크톱 · Quest 같은 경로, 2026-10-31), GLSL(빌드 시 SPIR-V 1.0 으로 컴파일) |
| 개발 형태 | 1인 개발. 외부 엔진 프레임워크 없이 렌더러 · GPU 메모리 할당자 · UI 위젯 시스템 · 에디터를 직접 구현 |
| 저장소 | `github.com/HANARU/Haruka`(공개). 라이선스 파일 없음 |

### 1-2. 설계 원칙

- **안전은 규율이 아니라 구조로 만듭니다.** "호출부가 잊지 않는 것"에 걸린 안전을 타입 · 이벤트 · 메커니즘으로 강제합니다
  (예: 월드 행렬 캐시 제거, `Transform` 필드 캡슐화, 커맨드 스택의 액터 참조 자동 제거). [Roadmap.md](Roadmap.md) 머리말.
- **고치기 전에 다른 엔진의 해법을 조사합니다.** UE · Unity · Godot · Bevy 등의 방법과 Haruka 에 맞는지를 먼저 확인하고, 사용자가
  결정한 뒤 구현합니다. 2026-10-04 의 사용자 지시 이후 결함 수정은 이 순서를 따랐습니다. 예외로, 2026-10-14 일괄 점검에서
  찾은 DEF-105 ~ 109 는 사용자 지시("내 판단 없이 스스로 작업 진행해")에 따라 조사한 뒤 사용자 결정 없이 바로 고쳤습니다.
- **모바일 XR 예산을 먼저 따집니다.** 포워드 렌더링(G-buffer 없음), 한 렌더 패스 안의 서브패스 3개(타일 GPU), 1GB 단일 힙은
  Quest 를 전제로 한 선택입니다.
- **단위는 1 unit = 1 m, 1 kg** 입니다(2026-08-25 확정). OpenXR 과 Jolt 가 미터 기준이기 때문입니다.
- **엔진 데이터는 메인 스레드가 소유합니다.** [Threading.md](Threading.md).
- **엔티티 모델은 Unity 식, UI 와 이름은 UE 식**입니다. Actor = GameObject, Component 조합, `activeSelf` / `activeInHierarchy` 는
  Unity 를 따르고, 에디터 UI(HSlate)와 명칭(Actor · tick · BeginPlay, 라이트 용어)은 UE 를 따릅니다.

### 1-3. 규모 (2026-10-31)

| 항목 | 값 |
|---|---|
| 소스 | `src/` 의 `.cpp` · `.hpp` 179 파일, 30,275 줄 |
| 셰이더 | GLSL 20 파일(셰이더 19 + 공용 헤더 1), 841 줄(2026-11-02). 컴퓨트 셰이더 없음 |
| 자동 테스트 | 44개 실행 파일, 검사 3,577개(`ctest -C Release`, 2026-11-21 내비게이션 · AI 뒤) |
| 문서 | `Docs/` 활성 문서 28개(색인 `README.md` 제외, 이 문서 포함) + 아카이브 6개(외부 엔진 분석, 신뢰도 낮음) |
| 커밋 | 44개. 기준 시점에는 17개가 원격에 없었으나, 2026-10-15 에 그 뒤 커밋까지 모두 원격에 올렸습니다 |
| 결함 대장 | 기준 시점 109건 등재: 해결 94 · 열림 9 · 보류 3 · 닫음 3(자동 조작 환경의 착시). 그 뒤 DEF-110 · 111(2026-10-15)과 DEF-112 ~ 119(패키징 1단계, 2026-10-19 ~ 24)를 등재하고 DEF-12 를 해결했습니다. 2026-10-31 오류 검토에서 DEF-120 ~ 147(28건)을 등재하고 그중 8건(DEF-121 ~ 128)을 고쳤습니다. 같은 날 패키징 2단계에서 7건(DEF-04 · 06 · 42 · 97 · 103 · 118 · 120)을 고치고 DEF-148 을 등재했습니다. 2026-11-01 Android 오류 검출에서 DEF-149 ~ 157(9건)을 등재하고 모두 고쳤습니다. 같은 날 빛 · 그림자 조사 후속에서 DEF-148 을 고치고 DEF-158 ~ 161(4건)을 등재해 모두 고쳤습니다. 2026-11-02 헤드셋 확인에서 DEF-162 · 163, 마지막 점검에서 DEF-164 를 등재해 모두 고쳤습니다. 2026-11-06 검토 결함 묶음 DEF-130 ~ 147(18건)을 모두 고쳤고, 같은 날 임포트 검토에서 DEF-165(텍스처 밉맵)를, 공간 음향 조사에서 DEF-166(패닝 문서 오류)을 등재해 고쳐, 지금은 166건 중 해결 155 · 열림 5 · 보류 3 · 닫음 3 입니다. 그 뒤 DEF-167(보류, 2026-11-16)과 DEF-168 ~ 172(2026-11-17 ~ 18, 모두 해결)를 등재해 172건 중 해결 160 · 열림 5 · 보류 4 · 닫음 3 입니다 |

---

## 2. 개발 이력

### 2-1. 저장소 기록에 대한 주의

처음 13개 커밋(`762aa6f` ~ `fc9113b`)은 날짜가 2025-06-03 부터 2026-06-03 까지 **매월 3일로 고르게** 찍혀 있고, 빌드 · 할당자 ·
코어 · 렌더러 · 에셋처럼 구성 요소 단위로 나뉘어 있습니다. 이어지는 두 커밋도 같은 형식이고, 2026-07-03 커밋의 "도킹 Phase 0"은
문서상 2026-08-27 에 끝난 작업입니다. 따라서 초기 커밋은 실제 작업 순서와 날짜를 기록한 것이 아니라 저장소에 올리면서 정리한 것으로
보입니다. 아래 연표의 날짜는 커밋 날짜가 아니라 설계 · 결함 문서와 작업 기록에 적힌 날짜를 따릅니다.

### 2-2. 연표

| 시기 | 작업 | 결과 |
|---|---|---|
| 초기(저장소 기록상 2025-06 ~ 2026-06) | 빌드 체계, GPU 코어 힙 할당자, 창 · 입력 추상화, 씬 모델(World · Actor · Component), Vulkan 렌더러 기반, glTF 가져오기와 바이너리 캐시, 오디오 · 물리 모듈, 절차 도형, 렌더 파이프라인, HSlate UI, 씬 직렬화, UE5 식 에디터 셸, 첫 단위 테스트 | 엔진의 뼈대 |
| 2026-05-12 ~ 05-14 | HSlate Phase 1 · 2, 에디터의 컴포넌트 UI · 삭제 · 빈 액터 · 기즈모 · F2 리네임 | [CHANGES.md](CHANGES.md) |
| 2026-06-11 | 렌더러 패스 구조 리팩터링(RenderContext · GpuScene · passes · HPass 수명주기 · RenderView), 스레딩 규칙 명문화 | [RendererRefactor.md](RendererRefactor.md), [Threading.md](Threading.md) |
| 2026-08-24 | 계층 아키텍처 기준 문서, 외부 엔진(Carbon) 분석 문서 정정, 결함 대장 DEF-01 ~ 22 | Layer 1 / Layer 2 경계 확정 |
| 2026-08-25 | 결함 11건 해결(섀도 비교 규약 반전으로 직접광이 사라지던 DEF-36, glTF 팩터 미적용 DEF-47 등), 단위 = 미터 확정 | 화면이 처음으로 의도대로 보임 |
| 2026-08-26 | GameObject 견고화(파괴 멱등 · 커맨드 스택 정리 · 활성 모델 · 계층 직렬화), 트랜스폼 설계(DEF-49 ~ 54), 엔티티 공간 표현 | [Transform_Design.md](Transform_Design.md), [Spatial_Representation_Design.md](Spatial_Representation_Design.md) |
| 2026-08-27 | 렌더링 결함 묶음(SSAO 밴딩 · 블룸 · 가짜 TAA 비활성 · GPU 컬링 중단), 뷰포트 렌더 분리, 라이팅 UE 용어 Phase 0 ~ 2(색온도 · 라이트별 그림자), `Engine` 클래스 부트스트랩, 에디터 Phase 0(도킹) | [Lighting_UE_Design.md](Lighting_UE_Design.md) |
| 2026-09-03 ~ 09-09 | 에디터 Phase 2(뷰포트 툴바 · 스냅) · Phase 3(아웃라이너 트리) · Phase 1(상태바), ECS 도입 보류 결정, HSlate 위젯 결함 수정 | [Editor_Layout_Design.md](Editor_Layout_Design.md), [ECS_Decision.md](ECS_Decision.md) |
| 2026-09-17 ~ 09-25 | 로그 카테고리 체계 L0 ~ L4(기록 수준과 표시 필터 분리, Output Log 필터, UTF-8 로그 파일), 실행 파일 아이콘 | [Log_Category_Design.md](Log_Category_Design.md) |
| 2026-10-02 ~ 10-03 | 절차 도형 직렬화, 사운드 기초, **종합 오류 검출**(DEF-76 ~ 94 등재, 잠재 DEF-91 을 뺀 전부 수정), 기반 공사(클리핑 · 위젯 휠 · 키 반복 · 글자 폭 실측) | [Audio_Design.md](Audio_Design.md), [Editor_QA_Checklist.md](Editor_QA_Checklist.md) |
| 2026-10-04 | **남은 결함 정리** 1 · 2단계(셰이더 의존성 · 감마 이중 적용 · 깊이 규약 · 역제곱 감쇠 · 숫자 칸 정밀도), **VR 첫걸음**(OpenXR 연결 · 두 눈 렌더 · 두 눈 컬링 · VR 입력 · 물리 API) | 커밋 `cbae320`, `5d3a86b` |
| 2026-10-14 ~ 10-15 | 남은 결함 정리 "나머지": 자동 인스턴싱(DEF-16), 렌더 확장 지점(DEF-17 · 18), 직렬화 타입 등록과 씬 v4(DEF-22), 라이트 선택(DEF-33) · 상한 설정 파일(DEF-10), **일괄 점검**과 그 과정에서 찾은 DEF-105 ~ 109 수정 | 커밋 `744cc69` ~ `5a9cb51` |
| 2026-10-15 | 헤드셋 확인(Quest Link), VR 은 Play 동안만(DEF-111), **엔진 기본 기능 보강**: 씬 관리(씬 파일 선택 · 변경 표시 · 프로젝트 설정), 편집 기본기(ID 기반 Undo · 다중 선택 · 복사와 붙여넣기 · 부모 바꾸기 · 화면 맞추기 · 계층 프리팹), 플레이어와 입력(리그 · 추적 자세 · 이동 · 잡기 · 키와 마우스 축 · 햅틱) | 헤드셋 확인 · DEF-111 은 커밋 `4cc931a`, 기본 기능은 [Engine_Basics_Design.md](Engine_Basics_Design.md) |
| 2026-10-16 | 헤드셋 2차 확인(DEF-110 검증 메시지 0, DEF-106 최소화 중 헤드셋 유지, 플레이어 리그 · 잡기), XR 에는 언제나 리그(보이지 않는 리그 · 리그 높이 모드), 소리 출력 장치 선택(에디터 Edit > Audio Output, XR Play 동안 헤드셋 자동) | [Engine_Basics_Design.md](Engine_Basics_Design.md) 2-5, [Audio_Design.md](Audio_Design.md) 8절, [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) "헤드셋 2차 확인" |
| 2026-10-17 ~ 10-24 | **패키징 1단계**(목표: Windows · Android XR 배포): 열린 결함과 패키징 조사 6건, 런타임 분리(빌드 타깃 `HarukaRuntime` · `HarukaEngine` · `HarukaGame`, 엔진 앞단 인터페이스, DEF-12), 게임 실행 규칙(VR 시작 설정 · 런타임 종료 처리 · 초점 일시정지), 실행 폴더 기준 경로와 쓰기 루트(`%LOCALAPPDATA%`), 배포 ZIP(CMake install · CPack), 자체 묶음 파일 `.hpak` v1 · 가상 파일 시스템 · 묶음 도구 `HarukaPak`, 서드파티 라이선스 고지, 이 과정에서 찾은 DEF-112 ~ 119 | [Packaging_Design.md](Packaging_Design.md) |
| 2026-10-31 | 미커밋 작업 전체의 **오류 검토**(결함마다 검증 두 표, DEF-120 ~ 147 등재), **패키징 마무리 묶음**(UTF-8 묶음 도구 · 조작한 목차 검사 · C 콜백 예외 경계 · 에디터의 옛 에셋 복사본 · glTF `../` · `--loose` 우선순위 · 숨김 폴더 · jsoncpp 고지, DEF-121 ~ 128), 패키징 2단계 계획 | [Packaging_Design.md](Packaging_Design.md) 5-2, [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) 끝 절 |
| 2026-10-31 | **패키징 2단계**: Vulkan 1.1 기준선(장치 계획 · 확장 함수 포인터 · XR 런타임 범위), 배리어 분기(sync2 또는 1.0), 창 없는 렌더 경로(`--headless` · 합성 눈 · 자동 확인 종료 코드), 핵심 힙 1GB 고정 · MSAA 지연 할당(DEF-04 · 06 · 42 · 97 · 103 · 118 · 120). **3단계 진행**: Android 빌드(NDK, Gradle 없이) · `android_main` · OpenXR 로더(공유) · APK | [Packaging_Design.md](Packaging_Design.md) 6 · 7절, [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) 끝 절 |
| 2026-11-06 | **공간 음향** 조사(차폐 · 회절 · 잔향 · HRTF · 기본 기능 4갈래) → 사용자 결정(단계 0 ~ 3) → 단계 0 기반(음원별 노드 사슬 · 레이캐스트 제외 액터 · DEF-166 패닝 문서 정정), 단계 1 차폐 + 회절 근사(표본 9개 일부 차폐), 단계 2 잔향(verblib 공용 리버브 · 음원별 센드 · 잔향 구역 컴포넌트), 단계 3 HRTF(Steam Audio 4.8.1 바이노럴 노드, 기본은 XR Play 동안, 2026-11-07 배포 확인까지 마침) | [Audio_Design.md](Audio_Design.md) 9절 |
| 2026-11-17 | **수학 보강**([Math_Reinforcement.md](Math_Reinforcement.md)): 던지기 속도(DEF-168) · 셰이더 시간 · 법선 여인수와 접선 변환(DEF-169) · PCF 4탭 · 화면 크기 LOD · 작은 물체 컬링 · 연속 충돌 · 볼록 껍질과 메시 충돌체 · 소리 가상화 · 최대 거리 밖 무음. 3D UI P1 · P2 창 · 헤드셋 확인 통과 |
| 2026-11-18 | **성능 안정화**: 프레임 시간 분포 · 끊김 로그 · 시작 시간, `--gpu=N`, 하늘 IBL 굽기 면별 스레드, 형별 컴포넌트 목록 캐시(`World::componentsOf`), 머티리얼 업로드 묶기(DEF-172), Quest 품질 단계(`project.ini` 의 `.android` 접미사 · `--settings-platform`) | [Roadmap.md](Roadmap.md) 같은 날 항목 |
| 2026-11-18 | **멀티뷰**(DEF-09 둘째 단계): 두 눈의 메인 패스를 viewMask 0b11 로 한 번에 그림(장면 타깃 2층 배열, 렌더 패스 · 파이프라인 두 벌, 셰이더 `HARUKA_EYE_INDEX`), 멀티뷰 기능 필수, `--no-multiview` · `--capture-eye=right` | [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) "멀티뷰" 절 |
| 2026-11-17 | **구현 목록 전체 진행**: 거울 변환(DEF-170), `src/math`, 물리 P3 ~ P5 · 캐릭터 컨트롤러 · 텔레포트, 월드 행렬 캐시 · 16비트 인덱스 · 3 행 트랜스폼 · QTangent, Reverse-Z, 소리 단계 4 · 전파 효과(DEF-171), 라이팅(채널 · 노출 · CSM · 접촉 그림자 · 반사 캡처 · 높이 안개), 손 쥐기 C · D, 에디터 시각화 · 여러 액터 편집 · 도형 머티리얼 편집 | [Math_Reinforcement.md](Math_Reinforcement.md) 4절 |

### 2-3. 주요 결정 기록

| 결정 | 시기 | 내용 | 정본 |
|---|---|---|---|
| Vulkan 단일 RHI | 초기 | 다른 그래픽 API 를 추상화하지 않습니다 | [Vulkan_Single_RHI_Decision.md](Vulkan_Single_RHI_Decision.md) |
| 자체 메모리 할당자 | 초기, 결정은 보류 | VMA 도입 여부는 아직 보류입니다. 그동안 1GB 단일 힙을 세 구역으로 나눈 `HarukaAllocator`(옵션 B)를 운용합니다 | [Memory_Allocator_Decision.md](Memory_Allocator_Decision.md) |
| 포워드 렌더링 | 초기 | 디퍼드는 MSAA 와 맞지 않고 모바일 타일 GPU 의 대역폭을 많이 써서 배제합니다 | [Architecture_Overview.md](Architecture_Overview.md), [Core_Stabilization_Spec.md](Core_Stabilization_Spec.md) 2-K |
| 단위 = 미터 | 2026-08-25 | 1cm 단위 안을 검토한 뒤 미터를 유지했습니다 | [Layer_Architecture.md](Layer_Architecture.md) §2 |
| 라이팅은 "UE 명칭만" | 2026-08-27 | UE 이름 · 드롭다운만 빌리고 값은 상대값입니다. 물리 단위 · 노출계는 쓰지 않습니다. 점광원 감쇠 모양만 역제곱으로 바꿨습니다(2026-10-04) | [Lighting_UE_Design.md](Lighting_UE_Design.md) |
| ECS 도입 보류 | 2026-09-08 | Actor + Component 를 유지합니다. 측정상 프레임은 후처리(AO · Bloom)가 지배하는 GPU 바운드였고, 대규모 파편장을 막던 것은 데이터 배치가 아니라 GPU 제출 경로(인스턴싱 부재)였습니다. 재검토 트리거를 수치로 정해 두었습니다 | [ECS_Decision.md](ECS_Decision.md) |
| 물리 규칙은 UE 식 | 2026-10-04 | 힘은 스텝에 나눠 넣고, 위치를 바꾸면 순간 이동 + 속도 유지, XR 중 물리 스텝 = 헤드셋 주사율 | [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) "3단계 VR 첫걸음" |
| 렌더 확장은 고정 지점 | 2026-10-14 | 렌더 그래프 대신 고정 지점 4곳 + 좁은 인터페이스 | 같은 문서 "DEF-17 · 18" |
| 컴포넌트 명시 등록 | 2026-10-14 | 전역 객체 자동 등록 대신 시작할 때 명시적으로 등록합니다(정적 라이브러리에서 링커가 버릴 수 있음) | 같은 문서 "DEF-22" |
| 자동 인스턴싱만 | 2026-10-14 | 명시적 인스턴싱 컴포넌트는 파편장 착수 때 만듭니다 | 같은 문서 "DEF-16" |
| 3D UI 는 render-to-texture | 2026-08-26 (2026-10-15 재확인, 2026-11-15 상세 결정) | `UIPanelComponent` 와 기존 `HWidget` 트리, HSlate 오프스크린 렌더. 패널은 장면 패스에 그리되 그 화소는 톤매핑 · AO · 블룸을 건너뜀, OpenXR quad 레이어는 나중 선택지. P0(HSlate 문맥 · 출력 분리) · P1(패널 보이기) · P2(마우스 · XR 컨트롤러 입력, 게임 API, 따라오는 패널, 2026-11-16) 구현, 헤드셋 확인(P3)부터 남음 | [Spatial_Representation_Design.md](Spatial_Representation_Design.md) 10절 |
| 라이트 방향은 회전에서만 | 2026-10-15 | `direction` 필드를 없애고 액터 회전의 -Z 를 씁니다. 2026-11-15 구현(옛 씬은 읽을 때 회전에 접음) | [Lighting_UE_Design.md](Lighting_UE_Design.md) 6절 4번 |
| VR 은 Play 동안만 | 2026-10-15 | `--xr` 은 OpenXR 를 준비만 하고, 세션은 Play 때 만들고 Stop 때 끝냅니다(UE VR Preview · Unity 와 같음) | [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) "헤드셋 확인 결과" |
| 할당자는 이식 때 조사 | 2026-10-15 | VMA 대 자체 할당자는 Quest 이식 착수 때 정합니다. 2026-10-19 GPU 힙 조사에서 판단 근거(메모리 종류를 여럿 다뤄야 함)가 더해졌습니다 | [Roadmap.md](Roadmap.md), [Packaging_Design.md](Packaging_Design.md) 2절 |
| 런타임 분리는 빌드 타깃 | 2026-10-19 | 에디터를 뺀 엔진을 `HarukaRuntime` 정적 라이브러리로 묶고 에디터 · 게임 실행 파일이 각각 링크합니다(UE 의 Game · Editor 타깃, O3DE 와 같음). 엔진은 앞단 인터페이스로만 에디터 · 게임을 부릅니다 | [Packaging_Design.md](Packaging_Design.md) 3 · 4-1절 |
| 게임 실행 규칙 | 2026-10-19 | VR 시작은 `project.ini` 의 `xr.startInVr` + 명령줄 `--xr` · `--no-xr`. 시작 때 헤드셋이 없으면 데스크톱, 실행 중 런타임이 끝내면(EXITING) 종료, 연결이 끊기면 데스크톱으로 계속하며 다시 연결. 헤드셋을 벗으면 게임만 일시정지(에디터 Play 는 그대로) | 같은 문서 3 · 4-2절 |
| 쓰기 위치 | 2026-10-19 | 게임의 설정 · 로그 · 캐시는 `%LOCALAPPDATA%\Haruka\<실행 파일 이름>`(Android 는 외부 앱 폴더), 에디터는 실행 폴더. 설정은 패키지 기본값 + 사용자 덮어쓰기 두 겹 | 같은 문서 3 · 4-3절 |
| 자체 묶음 파일 | 2026-10-19 결정, 2026-10-24 형식 | 게임 배포물의 에셋 · 셰이더는 `.hpak` 하나로 묶습니다(사용자: "실제 게임 개발에 들어가면 커진다, 규약을 하나 만들어 두자"). LZ4 64KiB 블록 · XXH3-64 · 대소문자 무시 찾기 · 16B/4KiB 정렬 · 이름 규칙에 따른 여러 묶음 | 같은 문서 5절 |
| 프로젝트 설정 파일 | 2026-10-15 | 시작 씬 · 입력 바인딩은 프로젝트 폴더의 `assets/project.ini`(Godot 의 `project.godot` 자리), 마지막에 연 씬은 실행 폴더의 `Config/editor_state.txt` 에 둡니다 | [Engine_Basics_Design.md](Engine_Basics_Design.md) 2-2 |
| Undo 커맨드는 ID 기반 | 2026-10-15 | 액터는 ID, 컴포넌트는 타입 이름 + 순번으로 찾습니다. 삭제를 되돌리면 그 전 편집 이력도 이어집니다(같은 날 정한 "삭제 전 이력은 버림"을 뒤집음). 저장 여부는 Undo 상태 표지로 판정합니다 | 같은 문서 2-2 · 2-4 |
| 소리 출력 장치는 에디터 설정 | 2026-10-16 | 기계별 값(`Config/editor_state.txt`), Edit 메뉴의 목록 대화상자로 고름. XR Play 동안 헤드셋 장치로 자동 전환(기본 켜짐). 패키징된 빌드는 같은 API 를 설정 화면이 씀(UE 의 에디터 Audio 설정 · Swap Audio Output Device, Godot 의 `AudioServer.output_device` 와 같은 구분) | [Audio_Design.md](Audio_Design.md) 8절 |
| XR 에는 언제나 리그 | 2026-10-16 | 씬 리그가 없으면 카메라 아래 바닥에 보이지 않는 리그를 붙입니다. 리그 높이는 실제 키(LOCAL_FLOOR) 또는 고정 눈높이(Unity XR Origin 의 Floor · Device 모드와 같은 구분). 데스크톱 카메라는 그대로 | [Engine_Basics_Design.md](Engine_Basics_Design.md) 2-5 |
| 플레이어 기초는 코어에 | 2026-10-15 | 리그 · 추적 자세 · 기본 이동 · 잡기 컴포넌트만 Layer 1 에 두고 도메인 이동은 Layer 2 가 붙입니다. 잡기는 Kinematic 전환 + 손 속도이고, 속도 추적 잡기 · 텔레포트 · 캐릭터 컨트롤러는 미룹니다 | 같은 문서 2절 |

---

## 3. 구조

### 3-1. 계층

```
[Layer 2] 우주 · 무중력 도메인 (미착수: ZeroGController · DebrisField · 생존 규칙 · 우주 비주얼 · 정적 기지 건설)
            │ 단방향 의존 (EngineContext 로만 코어에 접근)
            v
[Layer 1] Haruka Core
   Engine(부트스트랩 · 메인 루프)
   ├─ 코어 서비스(고정 순서): 창 → (XR) → 렌더러 → 에셋 관리자
   ├─ 동적 모듈(ModuleManager): AudioSystem, PhysicsSystem
   ├─ 에디터(EditorApp, HSlate)
   └─ 게임 모듈(IGameModule, 기본은 빈 DefaultGameModule. 시작 씬은 에디터가 엶)
```

Layer 2 는 `EngineContext` 로 월드 · 에셋 · 창 · 모듈 · 렌더 확장 등록(`render`) · 컴포넌트 등록(`components`)에만 닿습니다.

### 3-2. 소스 구성

| 디렉터리 | 파일 · 줄 | 내용 |
|---|---|---|
| `src/`(루트) | 2 · 306 | `main.cpp`(진입점: 실행 파일의 앞단을 만들어 쓰기 루트를 정하고, 콘솔 없이 직접 실행할 때 C stdio 출력을 `Logs/stdout.txt` 로 돌린 뒤 `Engine` 을 만들고 돌림. 에디터 · 게임 실행 파일이 함께 씀), `AndroidMain.cpp`(Android `android_main`) |
| `src/core/` | 18 · 1,850 | 기반: `HLog`, `EngineSettings`, 값 해석(`FieldParse.hpp`), 프로젝트 설정(`ProjectSettings.hpp`), 실행 폴더 · 쓰기 루트(`AppPaths`), 원자적 저장(`FileUtil.hpp`), 디버그 선(`DebugDraw.hpp`), 에셋 루트(`ProjectPaths.hpp`), 계측(`FrameTimings` · `FramePacing`, CPU 구간 기록 `Profiler` · 추적 파일 `TraceRecorder`), `Transform`, 모듈 인터페이스(`IEngineModule`) |
| `src/scene/` | 11 · 949 | 장면 모델: `World`(월드 단위 서비스 포함) · `Actor` · `Component`, 컴포넌트 필드 · `ComponentRegistry` · 리플렉션(`Reflect.hpp`) |
| `src/runtime/` | 18 · 1,936 | 조립: `Engine`(프레임 단계), 실행 옵션, 엔진 앞단 인터페이스(`IEngineFrontend.hpp`), 게임 모듈 인터페이스, `ModuleManager`, XR 세션 제어 · 다시 열기 · 원점, 월드 UI 포인터, 프레임 통계, 셰이더 묶음 짝 확인 |
| `src/sceneio/` | 11 · 2,225 | 씬 · 프리팹 직렬화(`SceneSerializer` · `PrefabManager`), 컴포넌트 블록 직렬화, 회전 글자 표현 |
| `src/renderer/` | 24 · 4,223 | `Renderer`(프레임 구성), `RenderContext`, `GpuScene`, `FrameResources`, 스왑체인, 인스턴싱 묶기 · 라이트 선택 · 절두체 컬링 헤더, 렌더 확장 |
| `src/renderer/passes/` | 11 · 2,106 | Shadow · Geometry · AO · Bloom · Post 패스(`HPass` 수명주기) |
| `src/memory/` | 2 · 388 | `HarukaAllocator`(1GB 단일 힙, 세 구역) |
| `src/assets/` | 10 · 1,669 | `AssetManager`, glTF 로더, `.gasset` 캐시, 스킨 |
| `src/geometry/` | 5 · 1,321 | 절차 도형 기술자와 메시 생성기 |
| `src/components/` | 24 · 3,316 | Light · Camera · StaticMesh · SkinnedMesh · HeightFog · ReflectionCapture · UIPanel · HandPose · ParticleSystem · Decal, XR 리그 · 추적 자세 · 이동 · 잡기(`XrComponents`), 기본 컴포넌트 등록 |
| `src/vfx/` | 2 · 436 | 파티클 시뮬레이션(순수 계산, 2026-11-20): `ParticleSim`(이미터 설정 · 상태 · 인스턴스 · 정렬 · 모양 아틀라스) |
| `src/physics/` | 8 · 2,196 | `PhysicsSystem`(Jolt), `RigidBodyComponent`, `JointComponent`, 충돌 레이어 표(`PhysicsLayers.hpp`), 접촉 알림 형(`PhysicsEvents.hpp`) |
| `src/audio/` | 15 · 3,106 | `AudioSystem`(miniaudio, 출력 장치 선택, 가상 파일 시스템 파일 콜백, 음원별 노드 사슬 · 차폐 · 공용 리버브 · HRTF 노드), 3D 감쇠 · 차폐 설정, 차폐 · 잔향 계산(`AudioOcclusion` · `AudioReverb`), verblib 설정, HRTF 인터페이스(`AudioHrtf`)와 Steam Audio 구현(`SteamAudioHrtf`, 라이브러리를 실행 중에 엶), `AudioComponent` · `ReverbZoneComponent` |
| `src/input/` | 3 · 1,150 | `HInputSystem`(액션 · 축 바인딩, 키 · 마우스 · 게임패드, 설정 줄 바인딩, 수정자, 입력 컨텍스트, 사용자 재지정, 햅틱), XR 입력 상태 |
| `src/xr/` | 2 · 894 | `XrSystem`(OpenXR 인스턴스 · 세션 · 스왑체인 · 입력 · 진동 · 바닥 기준 공간 · 세션을 잃은 이유) |
| `src/platform/` | 5 · 602 | `IWindow`, GLFW 구현, Android 스텁 |
| `src/ui/slate/` | 26 · 3,161 | HSlate: 위젯 14종, 배치 · 페인트 · 폰트 아틀라스 · 렌더러 |
| `src/editor/` | 18 · 7,285 | `EditorApp`(엔진 앞단 구현, 씬 파일 · 대화상자 `EditorApp_scene.cpp`, 다중 선택 · 클립보드 · 부모 바꾸기 `EditorApp_edit.cpp`), 에디터 앞단 선택(`EditorFrontend.cpp`), 기즈모, 커맨드 스택과 ID 기반 Undo 커맨드, 씬 문서(`SceneDocument.hpp`), 액터 스냅샷, 도킹, 셰이더 핫 리로드. 에디터 실행 파일에만 링크 |
| `src/game/` | 3 · 118 | `DefaultGameModule`(빈 게임 모듈), 게임 실행 파일의 앞단(`GameFrontend.cpp`: 시작 씬 · 처음부터 Play · 기본 카메라) |
| `src/vfs/` | 8 · 1,022 | 묶음 파일 `.hpak` v1 형식 · 쓰기 · 읽기(`PakFormat` · `PakWriter` · `PakReader`), 가상 파일 시스템(`Vfs`: 낱개 폴더 · 묶음 마운트와 우선순위). `HarukaVfs` 라이브러리 |
| `src/tools/` | 1 · 293 | 묶음 도구 `HarukaPak`(build · list · verify · extract) |

### 3-3. 부팅 · 메인 루프 · 종료

**앞단**(2026-10-19): 엔진은 에디터를 직접 알지 않고 `IEngineFrontend` 로만 부릅니다. 에디터 실행 파일은 `EditorApp`, 게임 실행
파일은 `GameFrontend` 를 링크하고, 앞단의 정책(프로젝트 폴더 · VR 시작 · 초점 일시정지 · 런타임 종료 처리 · 쓰기 루트 · 묶음 마운트)이
아래 순서의 갈림길을 정합니다. `main()` 이 앞단을 먼저 만들어 쓰기 루트를 정한 뒤 로그를 엽니다.

**부팅 순서**(`Engine::Engine`):
1. 컴포넌트 등록(기본 5종 + XR 5종) → 에셋 루트(에디터는 프로젝트 폴더, 그 밖에는 실행 폴더) → 가상 파일 시스템 마운트(실행 폴더의 낱개
   파일 위에 에디터는 프로젝트 폴더의 `assets/`, 게임은 실행 폴더의 묶음 파일, `--loose` 면 낱개 파일이 맨 위) → 프로젝트 설정
   `assets/project.ini` → `Config/engine.ini`(실행 폴더의 패키지 기본값 위에 쓰기 루트의 사용자 값, 에디터는 없으면 기본값으로 생성).
2. GLFW 창(1600x900) → VR 을 원하면 OpenXR 초기화(명령줄 `--xr` · `--no-xr`, 없으면 게임은 `xr.startInVr`. 렌더러보다 먼저, OpenXR 이
   GPU 를 고름) → 렌더러(에디터는 XR 세션을 Play 때, 게임은 시작하자마자 만듦) → `--emulate-stereo` 처리 → 에셋 관리자.
3. 동적 모듈(Audio, Physics) 등록 → 앞단 초기화 → 입력 바인딩(프로젝트 설정, 없으면 엔진 기본값) → 게임 모듈 `onStart` → 앞단이 처음 씬을
   엶(에디터: 마지막에 연 씬, 없으면 `startScene`, 그것도 없으면 빈 씬. 게임: `startScene` 을 열고 카메라가 없으면 기본 카메라를 둔 뒤 Play).

**메인 루프**(`Engine::run`, 프레임마다): 창 닫기 요청 확인(에디터는 저장하지 않은 씬이 있으면 확인 창을 띄우고 닫기를 미룸, 게임은 바로 끝냄)
→ 창 이벤트 → 최소화 확인 → Play 상태에 맞춰 XR 세션 열기 · 닫기(게임은 연결이 끊긴 뒤 2초마다 다시 연결 확인) → 리그 높이 모드에 맞춰
기준 공간을 고름(화면 카메라가 씬 리그의 머리면 그 리그의 값, 아니면 프로젝트 설정) → XR 프레임 시작(세션이 돌면 헤드셋이 루프 속도를 정함.
런타임이 세션을 끝내면 에디터는 데스크톱으로 계속, 게임은 EXITING 이면 종료 · 연결 끊김이면 데스크톱으로 계속) → 카메라에 XR 원점을 넘김(씬
리그가 있으면 그 리그, 없으면 XR 세션 동안은 카메라 아래 바닥의 보이지 않는 리그, 세션이 없으면 카메라 위치 + 요) → 소리 청취자 갱신(XR
중에는 머리 자세) → Play 상태 전이(BeginPlay · EndPlay · 일시정지 통지. 게임은 헤드셋이 입력 초점을 잃으면 일시정지를 겹침) → 앞단 tick →
디버그 선 비움 → 입력 시스템 갱신 → **Play 중에만** 월드 · 게임 모듈 · 동적 모듈 tick → 마우스 시선 양 비움 → 앞단 입력 처리 → 렌더(XR 이면
두 눈 + 거울 화면, UI 가 없으면 렌더러가 스왑체인 이미지를 표시 레이아웃으로 전환) → 제목 표시줄(앞단 제목, VR 을 쓸 수 없거나 끊겼으면
알림, FPS). 최소화 중에는 앞단 UI 를 건너뛰고 약 60Hz 로 쉽니다.

**종료**는 대체로 부팅의 역순입니다. 실제 순서는 게임 모듈 → 동적 모듈 → 에셋 → 앞단 → XR 세션 → 렌더러 → XR 인스턴스이며,
앞단은 부팅 때 동적 모듈보다 늦게 만들지만 정리는 에셋 뒤에 합니다.

### 3-4. 스레드

| 스레드 | 역할 | 엔진 데이터 접근 |
|---|---|---|
| 메인 | 입력 · 틱 · 에디터 · 렌더 전체 | 전부(소유자) |
| 셰이더 감시 | `.spv` 변경 감시(`ReadDirectoryChangesW`) | atomic 플래그와 로그만 |
| Jolt 작업자 | 물리 스텝 내부 병렬화 | 없음(스텝 안에서 합류) |
| 오디오 | miniaudio 장치 콜백 믹싱 | 없음 |

에셋 로딩은 메인 스레드에서 동기로 합니다.

### 3-5. 외부 의존성

라이브러리는 모두 CMake FetchContent 로 구성 시점에 내려받습니다. 미리 설치할 도구는 Visual Studio(MSVC), CMake 3.21 이상,
Git(FetchContent 가 저장소를 받을 때 씀), Vulkan SDK(1.4.x, `glslc` 포함)입니다.

| 라이브러리 | 버전 | 용도 |
|---|---|---|
| GLFW | 3.4 | 창 · 입력(데스크톱) |
| glm | 1.0.1 | 수학(`GLM_FORCE_DEPTH_ZERO_TO_ONE` 전역) |
| tinygltf | v2.9.3 | glTF 2.0 읽기 |
| stb | `master`(고정되지 않음) | 폰트 래스터라이즈(stb_truetype)에만 씁니다. 이미지 디코드는 tinygltf 에 들어 있는 `stb_image.h` 가 맡으므로 tinygltf 버전과 함께 고정됩니다 |
| miniaudio | 0.11.21 | 오디오 장치 · 디코드(OGG 는 함께 들어 있는 stb_vorbis). 잔향은 함께 들어 있는 verblib(Freeverb 기반, 2026-11-06) |
| Steam Audio | 4.8.1 | HRTF(바이노럴, Apache-2.0, 2026-11-06). FetchContent 가 아니라 `cmake/SteamAudio.cmake` 가 릴리스 ZIP 을 받아 SHA-256 을 확인하고 헤더 · 그 플랫폼의 라이브러리만 풉니다. 링크하지 않고 실행 중에 엶, `-DHARUKA_STEAM_AUDIO=OFF` 로 뺄 수 있음 |
| JoltPhysics | v5.1.0 | 물리 |
| OpenXR-SDK | release-1.1.63 | OpenXR 로더(정적 링크) |
| LZ4 | v1.10.0 | 묶음 파일 압축(`lib/lz4.c` · `lz4hc.c` 만, BSD-2, 2026-10-24) |
| xxHash | v0.8.3 | 묶음 파일 · glTF 캐시의 내용 해시 XXH3-64(헤더 하나, BSD-2, 2026-10-24) |

---

## 4. 구현된 기능

### 4-1. 플랫폼 · 빌드

- Windows x64 + MSVC(Visual Studio 18 2026, 없으면 2022). CMake 3.21 이상(셰이더 include 추적에 depfile 사용).
- `build.bat [Debug|Release] [clean]`, `run.bat [Config] [--xr | --emulate-stereo]`. 빌드 로그는 `Logs/build_log.txt`.
- 셰이더는 빌드 시 `glslc` 로 SPIR-V 컴파일, include 의존성 추적, 목록 밖 셰이더 파일은 구성 단계에서 경고합니다.
- 실행 파일은 콘솔 창 없이(`/SUBSYSTEM:WINDOWS`) 뜨고, 콘솔이 필요하면 `-DHARUKA_CONSOLE=ON` 으로 구성합니다. 실행 파일 · 창
  아이콘이 리소스로 들어 있습니다.
- 빌드 결과(2026-10-19 ~ 24): 에디터를 뺀 엔진 라이브러리 `HarukaRuntime`, 에디터 `HarukaEngine.exe`, 게임 `HarukaGame.exe`, 묶음 도구
  `HarukaPak.exe`, 게임 묶음 `build/pack/game.hpak`(목록 `Packaging/game.pack.txt`). 실행 파일 매니페스트가 프로세스 코드 페이지를
  UTF-8 로 둡니다(한글 사용자 폴더 경로).
- 게임 배포: `cmake --build build --config Release --target PACKAGE` 가 `build/HarukaGame-<버전>-win64.zip` 을 만듭니다. 들어가는 것은
  `HarukaGame.exe` · `game.hpak` · VC++ 런타임 DLL · `THIRD_PARTY_NOTICES.txt` 이고, 압축을 풀면 바로 실행됩니다.
- 경로: 셰이더 · 에셋은 작업 폴더가 아니라 실행 파일 기준으로 찾고, 실행 중에 쓰는 파일은 쓰기 루트(에디터 실행 폴더, 게임
  `%LOCALAPPDATA%\Haruka\HarukaGame`)에 둡니다.
- 경고 수준 `/W4`, 빌드 경고 0 을 유지합니다.

### 4-2. 코어

| 기능 | 내용 |
|---|---|
| 엔티티 모델 | `World` 가 `Actor` 를, `Actor` 가 `Component` 를 소유(`unique_ptr`). 컴포넌트 조회는 `type_index` 해시 캐시 |
| 활성 모델 | `activeSelf` / `activeInHierarchy`(부모 체인), 전이할 때만 서브트리에 `onEnable` · `onDisable` |
| 계층 | 부모 · 자식(`attachTo` · `detach`, 사이클 방지), 재귀 파괴, 틱 중 파괴는 지연 큐 |
| 트랜스폼 | 로컬 위치 · 쿼터니언 회전 · 스케일, 필드 비공개(세터로만 변경), 월드 행렬은 캐시 없이 매번 계산 |
| 파괴 감시 | `World::onActorDestroyed` 이벤트. 에디터가 구독해 커맨드 스택 · 선택 · 디테일 패널을 정리 |
| 모듈 | `IEngineModule`(startup · tick · BeginPlay · EndPlay · 일시정지 통지 · shutdown)을 `ModuleManager` 가 관리 |
| 게임 모듈 | `IGameModule`(onStart · onBeginPlay · onTick · onEndPlay · onShutdown), `EngineContext` 로 코어 접근 |
| 컴포넌트 등록 | `ComponentRegistry`(타입 이름 → 생성 · 찾기 함수, 옛 이름, 에디터 버튼 라벨). Layer 2 는 `EngineContext::components` |
| 리플렉션 | 디테일 패널용 속성 기술자(`Reflect.hpp`), 단위 종류(거리 · 크기 · 각도 · 비율)별 드래그 스텝 |
| 로그 | 카테고리별 기록 수준(Off · Error · Warning · Log · Verbose), `Config/log_levels.txt` 로 덮어쓰기, UTF-8 파일 `Logs/HarukaEngine.log`(줄마다 flush), 어느 스레드에서나 안전 |
| 상한 설정 | `Config/engine.ini`: `render.maxDrawRows`(기본 16384, 1024 ~ 131072) · `render.maxLights`(256, 1 ~ 4096) · `physics.maxBodies`(4096, 256 ~ 65536). 시작할 때 한 번 읽고, 범위 밖 · 깨진 값은 경고 |
| 계측 | CPU 구간 타이머(폴링 · 틱 · 입력 · 기록 · 제출 · 표시 등)와 GPU 타임스탬프(Shadow · Main · AO · Bloom · Tonemap · UI). 2026-11-20 부터 이름 붙인 중첩 스코프 `HPROF_SCOPE` · `HPROF_FUNCTION` 을 스레드별로 기록해 Chrome `trace.json`(쓰기 루트 `Traces/`)으로 씁니다: `--trace=<파일>` · `--trace-frames=<a>-<b>` · 콘솔 `trace.start` · `trace.stop`, 컴파일 스위치 `HARUKA_PROFILER`(기본 켬, 기록하지 않을 때 스코프 하나 약 1 ns). [CPU_Profiling.md](CPU_Profiling.md) |

### 4-3. 메모리

- 장치 메모리를 `vkAllocateMemory` 한 번으로 1GB 선점하고 전 범위를 지속 매핑합니다. 구역은 A 64MB(엔진 구조체, bump) ·
  B 896MB(에셋, free-list. 이미지는 시작 주소를 `bufferImageGranularity` 에 맞춰 정렬) · C 64MB(프레임별 동적 데이터, ring)이고 합이 1GB 임을 `static_assert` 로
  강제합니다.
- 메모리 타입은 `DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT`(UMA · ReBAR)를 먼저 고르고, 없으면 `HOST_VISIBLE | HOST_COHERENT` 로
  내려갑니다(이때 이미지는 별도 `DEVICE_LOCAL` 할당). 두 단계 모두 힙이 1GB 이상이어야 합니다. 1GB 를 한 번에 잡고, 못 잡으면
  필요한 크기 · 고른 타입 · 힙 크기 · VkResult · 최대 단일 할당을 적은 예외로 멈춥니다(DEF-42, 2026-10-31. 예전의 75% 상한과 절반씩
  줄이는 재시도는 Zone 오프셋을 넘어 지웠습니다). 1GB 가 힙의 75% 를 넘으면 경고만 합니다.
- MSAA 컬러(TRANSIENT 첨부)는 지연 할당(LAZILY_ALLOCATED) 타입이 있으면 핵심 힙 밖에 따로 놓습니다(DEF-06, Quest). 데스크톱에는
  그 타입이 없어 Zone B 이고, `--vk-dedicated-msaa` 로 별도 할당 경로를 시험합니다.
- 1GB 힙이 엔진 장치 메모리의 전부는 아닙니다. HSlate 의 폰트 아틀라스 · 흰 텍스처 · 정점/인덱스 버퍼(와 업로드용 스테이징),
  라인 · 기즈모 정점 버퍼, 위의 대체 경로 이미지, 지연 할당 MSAA 컬러는 힙 밖에서 `vkAllocateMemory` 로 따로 할당합니다.

### 4-4. 렌더러

**프레임 구성**: 다시 그릴 월드 UI 패널을 먼저 텍스처에 그리고(밉 포함), 섀도는 프레임에 한 번 그립니다. 그다음 눈마다 메인 패스(서브패스 3개:
깊이 프리패스 → 불투명 PBR → 반투명 PBR · 파티클 · UI 패널 · 라인 · 기즈모, MSAA 를 HDR 로 resolve) → SSAO → Bloom → 톤매핑을 반복합니다. XR 이면
왼쪽 눈을 에디터 뷰포트에 거울 화면으로 한 번 더 톤매핑하고, 마지막에 UI 를 그립니다.

| 기능 | 사양 |
|---|---|
| 장치 요구 | Vulkan 1.1 이상, 확장 `VK_KHR_push_descriptor`(필수) · `VK_KHR_swapchain`(창이 있을 때) · `VK_KHR_synchronization2`(있으면 씀, 없으면 1.0 배리어), 기능 `samplerAnisotropy`(있으면). 장치 버전이 높아도 같은 경로입니다(2026-10-31, `renderer/VulkanBaseline.hpp`) |
| 프레임 | 프레임 인 플라이트 2, MSAA 4x(장치가 못 하면 2x → 1x), HDR `R16G16B16A16_SFLOAT` |
| 셰이딩 | 메탈릭 · 러프니스 PBR(GGX · Smith · Schlick), 텍스처 5종(알베도 · 노멀 · 금속/거칠기 · AO · 이미시브) |
| 라이트 | Directional · Point · Spot, 색온도(Kelvin) 틴트, 라이트별 Cast Shadows. 점광원 · 스팟 감쇠는 역제곱 × 창 함수 `(1-(d/r)⁴)²`(UE · Frostbite · Filament) |
| 라이트 선택 | 어느 눈의 절두체와도 영향 구가 겹치지 않는 점광원 · 스팟은 올리지 않습니다(화면 불변). 상한 초과 시 방향광 우선, 나머지는 거리/반경 순 |
| 그림자 | `castShadows` 가 켜진 첫 Directional 라이트 하나. 머리 중심 구를 덮는 직교 섀도맵, 캐스케이드 2단(가까운 단 6 m · 먼 단 20 m, 칸마다 PC 4096² · Quest 2048², 가로 두 칸 아틀라스), 3x3 PCF(같은 커널을 쌍선형 4탭으로, 2026-11-17). 라이트 절두체로 캐스터를 따로 고르므로 화면 밖 물체도 그림자를 던집니다. 품질 단계로 1단 · 그림자 거리를 바꿉니다 |
| 품질 단계 | `assets/project.ini` 의 `render.csmCascades` · `render.shadowDistance` · `render.contactShadows` · `render.heightFog` · `render.reflectionCapture` · `render.lightLimit`(그리고 `render.ao` · `render.bloom`). 기본값은 PC 화면이고, 이름 끝에 `.android` 를 붙인 줄이 Quest 에서만 덮어씁니다(줄 순서와 무관, UE 의 플랫폼 ini 와 같은 자리). 저장소의 Quest 값: AO · 접촉 그림자 끔, 캐스케이드 1단 · 12 m, 라이트 32개. PC 에서 `--settings-platform=android` 로 시험합니다(섀도 해상도는 빌드 플랫폼 것, 2026-11-18) |
| 간접광 | IBL: 하늘 그라디언트로 CPU 에서 절차 생성한 조도 큐브맵 · 프리필터 큐브맵(5 밉) + GPU 로 구운 BRDF LUT(512², R16G16) |
| SSAO | 반해상도, 선형 깊이 → 반구 커널 64 샘플 |
| Bloom | 문턱값(1/2 해상도) → 다운샘플 4회(1/32 까지, 5레벨) → 업샘플 4회(스킵 연결). 결과는 1/2 해상도 |
| 톤매핑 | ACES(Narkowicz 근사), 노출 1.0, sRGB 인코딩(스왑체인 형식에 따라 셰이더 또는 GPU). HDR 알파는 후처리 가중치입니다(2026-11-16): 장면은 1(장면 파이프라인은 알파를 쓰지 않음), UI 패널 화소는 0 이라 노출 · AO · 블룸 · 톤매핑을 건너뜁니다. 렌더 확장도 알파를 그대로 두어야 합니다 |
| 월드 UI 패널 | `UIPanelComponent`(4-10 절). 패널마다 sRGB 텍스처(알파를 곱한 색, GPU 블릿 밉)를 두고, 프레임마다 그리기 목록을 만들어 해시가 바뀐 패널만 GPU 로 다시 그립니다. 텍스처는 지연 삭제(제출 번호 + 2 프레임 뒤)합니다. 반투명 서브패스에서 반투명 물체 뒤에 그리며 장면 깊이에 가려집니다(깊이는 쓰지 않음). 조명을 받지 않고, 샘플러는 가장자리 고정 · 밉 선형 · 이방성 |
| 반투명 | 메인 패스 세 번째 서브패스. 뒤에서 앞으로 정렬한 목록 전체를 뒷면만 한 번, 앞면만 한 번 그립니다(볼록 메시 하나의 앞뒤를 맞추는 방식이며, 메시마다 묶지 않으므로 서로 겹친 반투명 물체 사이의 순서는 어긋날 수 있음). 반투명도 그림자를 던집니다(알파 무시) |
| 파티클 | `ParticleSystemComponent`(2026-11-20, [Particles_And_Decals.md](Particles_And_Decals.md)). CPU 이미터(순수 계산 `src/vfx`: 비율 · 터뜨리기 · 수명 · 원뿔/구 속도 · 중력 · 감쇠 · 색/크기 수명 곡선 · 회전 · 상한 1 ~ 65,536 · 로컬/월드 · 결정적 시드), 월드 시간 배율을 따름. 반투명 서브패스에서 반투명 메시 뒤 · UI 패널 앞에 빌보드 인스턴싱(인스턴스 48 B, 프레임 슬롯마다 호스트 버퍼, 두 눈 가운데를 바라봄), 엔진 모양 아틀라스(부드러운 원 · 원 · 고리 · 별) × 파티클 색, 가산 · 알파(알파를 곱한 색, HDR 알파는 그대로), 깊이 시험만. 알파는 이미터끼리 + 이미터 안 먼 것부터 정렬, 합이 상한을 넘으면 먼 이미터부터 잘림. 조명 · 안개 · 부드러운 파티클은 없음 |
| 데칼 | `DecalComponent`(2026-11-20). 포워드 상자 데칼: pbr.frag 가 조명 전에 화소를 상자 로컬로 옮겨 알베도를 바꿈(Replace · Multiply, 거칠기 선택), 로컬 −Y 로 투영, 법선 각 페이드, 모양은 셰이더 계산(원 · 부드러운 원 · 고리 · 사각). 한 프레임 최대 8개(FrameUBO, 눈 가운데에서 가까운 것부터), 없으면 반복하지 않음. 데칼 텍스처는 없음 |
| 컬링 | CPU 에서 바운딩 구 대 6평면. 두 눈 각각 판정해 하나라도 통과하면 그림 |
| 인스턴싱 | 같은 지오메트리 · 머티리얼의 불투명 메시를 자동으로 묶어 `instanceCount = N` 드로우 하나로(셰이더는 `gl_InstanceIndex` 로 행을 읽음). 반투명은 묶지 않음 |
| 오류 머티리얼 | 머티리얼이 없는 메시는 자홍색으로 그리고 액터마다 한 번 경고 |
| 렌더 확장 | 고정 지점 4곳(불투명 끝 · 반투명 끝 · 메인 패스 뒤 · 톤매핑 뒤)에 Layer 2 가 그리기를 끼워 넣음. 기본 패스 5개와 확장을 한 목록(패스 레지스트리)으로 수명주기 관리 |
| 뷰포트 | 씬 렌더 해상도(`renderExtent`)를 스왑체인과 분리하고 에디터 뷰포트 사각형에 합성 |
| 스테레오 | 두 눈의 메인 패스(깊이 · 불투명 · 반투명 · 패널 · 선)를 멀티뷰(viewMask 0b11)로 한 번 그림(2026-11-18). XR · 스테레오 흉내 동안 장면 타깃(깊이 · MSAA · HDR)은 2층 배열이고 층 v 가 눈 v. AO · 블룸 · 톤매핑은 눈마다 그 층을 읽음. 렌더 패스와 파이프라인은 단안 · 멀티뷰 두 벌, 셰이더는 한 벌(눈 번호 = 푸시 상수 + `gl_ViewIndex`). `--no-multiview` 와 멀티뷰를 모르는 렌더 확장이 메인 패스 안에 있으면 눈마다 그림. `--emulate-stereo` 는 헤드셋 없이 뷰포트를 좌우로 나눠 두 눈을 그림 |
| 핫 리로드 | 실행 폴더 `shaders/` 의 `.spv` 가 바뀌면 파이프라인을 다시 만듦. 에셋(2026-11-20, [Asset_Hot_Reload.md](Asset_Hot_Reload.md)): 에디터가 프로젝트 폴더를 감시해(Windows `ReadDirectoryChangesW`, 300 ms 모으기) 올라간 모델 · 그 모델의 .gltf 옆 파일(.bin · 이미지)이 바뀌면 다시 가져와 정적 · 스킨 메시 컴포넌트에 바꿔 끼우고 옛 메시 · LOD · 머티리얼을 은퇴 목록으로 보냄. 연 씬이 밖에서 바뀌면 수정이 없을 때 다시 열기를 묻고(`--auto-reload-scene` 이면 바로), 수정 중 · Play 중이면 알림만. `/Config/import.ini` 가 바뀌면 결과 설정이 달라진 모델만 다시 가져옴. 창 없는 확인 `--touch-asset-at=<프레임>:<경로>` |
| 텍스처 압축 | 2026-11-21([Texture_Compression.md](Texture_Compression.md)): HarukaPak 이 묶을 때 모델 텍스처를 KTX2 로 압축(PC = BC7 sRGB · BC5 노멀 · BC7, Android = ASTC 6x6 · 5x5 노멀 · 6x6, 밉 미리 생성, `textures/<대상>/<열쇠>.ktx2`, 캐시 `build/texture_cache`). 엔진은 묶은 창 게임에서(또는 `--texture-compression=bc|astc`, `--texture-dir`) 워커가 KTX2 를 붙이고, 그 대상의 압축 기능만 켜며, 형식을 샘플링할 수 있으면 단계별 복사(블릿 없음), 아니면 RGBA8 원본. 노멀 B = 0 이면 셰이더가 Z 복원. 텍스처 메모리 · Zone B 로그 |
| 가져오기 설정 | `/Config/import.ini`(프로젝트 `Config/`, 2026-11-20): `[경로 패턴]` 절에 `scale`(정적 · 모프 메시) · `autoLod` · `srgb`(albedo · emissive 를 선형으로 보고 sRGB 로 인코딩). 뒤 절이 이기고, 결과 해시가 `.gasset` 캐시 키(버전 12, 의존 파일 크기 · 시각 · 해시도 기록)에 들어감. 에디터 · 게임 · Android 가 시작할 때 읽음. 텍스처 압축 칸은 없음 |
| 최소화 | 창이 최소화되면 데스크톱 화면만 건너뛰고(XR 프레임이면 두 눈은 그림) 스왑체인은 복원 뒤 다시 만듦 |
| 통계 | 뷰포트 오버레이(FPS · Draw · Inst · Culled · Dropped · Transp · 재생성 횟수 · GPU/CPU/LOOP 구간)와 GPU Stats 패널(Lights 올린 수 / 켜진 수 포함) |
| 프레임 시간 기록 | XR 세션 · 합성 눈 프레임의 루프 시간과 GPU 합계를 모아, 세션 끝 · 자동 실행 끝에 중앙값 · 99 분위 · 최악과 예산(헤드셋 주사율, 모르면 90 Hz) 초과 수를 한 줄로 남깁니다. 예산을 넘은 프레임은 처음 20 개까지 구간(입력 · XR 대기 · 틱 · 월드 · 모듈 · 물리 · 펜스 · 반사 캡처 · extract · 기록 · 제출 · 표시 · GPU)을 남깁니다. 시작 시간("engine ready", 첫 프레임)도 남깁니다(2026-11-18, `core/FramePacing.hpp`) |

### 4-5. VR (OpenXR)

| 기능 | 사양 |
|---|---|
| 런타임 연결 | `--xr` 로 켬. `XR_KHR_vulkan_enable2` 필수, `XR_FB_display_refresh_rate` 있으면 사용. 런타임 · 헤드셋이 없으면 경고 하나 남기고 데스크톱으로 계속. Quest Link(Oculus 1.208, Meta Quest Pro)에서 확인(2026-10-15) |
| 세션 수명 | Play 에 들어가면 세션을 만들고 Stop 하면 `xrRequestExitSession` 으로 끝냄. 편집 중에는 헤드셋에 그리지 않음(UE VR Preview · Unity 와 같음, 2026-10-15 사용자 결정, DEF-111). 일시정지 중에는 유지. 런타임이 Play 중에 끝내면 Play 는 데스크톱으로 계속하고 다음 Play 에서 다시 엶. 헤드셋을 쓰지 않는 동안 런타임은 세션을 IDLE 로 둠 |
| 공간 · 원점 | XR 에는 언제나 리그가 있습니다(2026-10-16). 화면 카메라가 씬 리그의 머리면 원점 = 그 리그, 아니면 카메라 아래 바닥에 붙인 보이지 않는 리그(수평 위치 · 요는 카메라). 리그 높이가 실제 키면 바닥 기준 공간(LOCAL_FLOOR, `XR_EXT_local_floor`), 고정 눈높이면 LOCAL + 눈높이(런타임에 바닥 공간이 없을 때도). 두 공간을 함께 만들어 두고 프레임마다 고름. XR 세션이 없으면 카메라 위치 + 요(yaw) |
| 프레임 | `xrWaitFrame` 이 루프 속도를 정함. 한 프레임에 자세 하나로 컬링 · 렌더 · 제출. 헤드셋이 화면을 보이지 않으면 빈 프레임 제출 |
| 스왑체인 | 두 층 배열 스왑체인 하나(눈마다 한 층). 오른쪽 눈을 먼저 그리고 왼쪽 눈을 에디터 뷰포트에 거울 화면으로 비춤 |
| 입력 | 기본 액션 세트 하나. Touch 컨트롤러 · simple 컨트롤러 프로필(Quest Pro 컨트롤러도 Touch 프로필로 연결됨). 머리 · 양손 자세와 손 속도, 트리거 · 그립 · 스틱 · 버튼 → `HInputSystem` 의 XR 버튼 · 축, 손 진동(`xrApplyHapticFeedback`) |
| 물리 | XR 세션 동안 물리 스텝을 헤드셋 주사율에 맞춤(30 ~ 240Hz 범위) |
| 플레이어 | 리그 · 추적 자세 · 스틱 이동 · 45° 스냅 회전 · 그립 잡기와 던지기(아래 4-9, [Engine_Basics_Design.md](Engine_Basics_Design.md) 5절). 표준 구성 `XrPlayer` 프리팹, 시연 씬 `XrDemo` |
| 시각화 | 씬의 라인 오버레이로 그려 헤드셋 두 눈과 거울 화면에 모두 보입니다. 손 자세에 10cm 축 세 개, 가리키는 방향에 선 하나(트리거 → 색, 그립 → 길이). 손 액터의 Tracked Pose(`showDebug`)가 그리고, 손 추적 컴포넌트가 없는 씬에서만 에디터가 예전처럼 카메라 원점 기준으로 그림 |

### 4-6. 에셋 · 씬

| 기능 | 사양 |
|---|---|
| 에셋 루트 | 에셋은 `assets/...` 논리 경로로 가리키고 씬 · 프리팹에도 논리 경로를 저장합니다. 에디터 빌드는 프로젝트 폴더(구성할 때 넣은 소스 트리 경로)의 `assets/` 를 직접 읽고 씁니다(2026-10-15, UE · Unity · Godot 과 같음). 그 폴더가 없으면 실행 파일 옆 복사본을 씁니다 |
| 가상 파일 시스템 | 엔진이 읽는 셰이더 · 에셋은 논리 경로로 가상 파일 시스템에서 찾습니다(2026-10-24). 낱개 폴더와 묶음 파일을 우선순위대로 겹치고, 찾기는 대소문자를 무시합니다. 여러 스레드가 함께 읽을 수 있습니다(오디오 비동기 로딩). 절대 경로(시스템 글꼴)는 그 파일을 직접 읽습니다 |
| 묶음 파일 | `.hpak` v1(2026-10-24): 128B 머리 + 데이터 + 끝 목차, 파일마다 그대로 저장 또는 LZ4 64KiB 독립 블록, 머리 · 목차 · 내용의 XXH3-64, 16B(64KiB 이상 4KiB) 정렬. 실행 폴더의 `game.hpak` < `dlc_*.hpak` < `game_p<N>.hpak` 순으로 위에 올라감. 묶음 도구가 같은 입력에서 같은 바이트를 만들고 텍스트 에셋의 줄 끝을 LF 로 맞춤 |
| glTF | `.glb` · `.gltf` 정적 메시, 텍스처 5종. 스칼라 팩터는 읽을 때 텍스처에 구워 넣음. `alphaMode = BLEND` → 반투명. 본문은 메모리에서 파싱하고 외부 파일도 가상 파일 시스템으로 읽음 |
| 메시 캐시 | `.gasset` 바이너리 캐시 v3(에디터는 실행 폴더의 `assets/cache/`, 게임은 쓰기 루트의 `Cache/models`). 파일 이름은 논리 경로에서 나오고, 원본 크기와 수정 시각 또는 내용 해시(XXH3-64, 원본이 묶음 안이면 목차 값)가 같으면 재사용 |
| 절차 도형 | Box · Plane · Sphere · Cylinder · Cone · Capsule · Torus · Arrow · Icosphere 9종. 머티리얼은 체커 또는 단색(색 · 금속도 · 거칠기) |
| 씬 형식 | 텍스트 `.hscene` v4. 액터마다 `POS` · `ROT`(오일러 각) · `QUAT`(정확한 회전) · `SCALE` · `ACTIVE` · `PARENT`, 컴포넌트마다 `COMPONENT:타입` … `END_COMPONENT` 블록에 `이름=값`. 실수는 다시 읽으면 같은 값이 되는 가장 짧은 십진수로 씀 |
| 호환 | v3 이하 씬 · v1 · v2 프리팹을 계속 읽음. 등록되지 않은 컴포넌트 타입은 원문 그대로 보존해 다시 저장할 때 씀 |
| 프리팹 | `.hprefab` v3, 서브트리(씬 블록, 첫 블록이 루트. 2026-10-15). 에디터에서 저장 · 콘텐츠 브라우저에서 꺼내기. v1 · v2 도 읽음. **v6(2026-11-21)**: 꺼내면 링크된 인스턴스가 되고, 씬 v7 은 인스턴스를 `PREFAB:` 블록(원본 경로 + 원본과 다른 값만 적은 덮어쓰기)으로 저장합니다. 원본을 고치면 덮어쓰지 않은 값만 따라오고, 프리팹 안 프리팹(중첩) · 순환 거부 · 되돌리기 · 원본에 적용 · 링크 끊기가 됩니다([Linked_Prefabs.md](Linked_Prefabs.md)) |
| 씬 파일 | 프로젝트 폴더의 `assets/scenes/` 아래 논리 경로로 열고 저장. 임시 파일에 쓴 뒤 바꾸는 원자적 저장(2026-10-15) |
| 시작 씬 | `assets/project.ini` 의 `startScene`(기본 `assets/scenes/LightingProbe.hscene`, 라이팅 확인용 절차 도형 씬, v4). 에디터는 마지막에 연 씬을 먼저 엶 |
| 추적 에셋 | 모델 1(`model.glb`), 시험 음원 3, 씬 2(`LightingProbe` · `XrDemo`), 프리팹 2(`AudioProbe` · `XrPlayer`), 프로젝트 설정 `project.ini` |

**컴포넌트 10종**

| 컴포넌트 | 주요 값 |
|---|---|
| Light | type · color · intensity · attenuationRadius · inner/outerConeAngle · useTemperature · temperatureK · castShadows. 방향은 액터 회전(-Z), 옛 `direction` 키는 읽을 때 회전에 접음 |
| Camera | fovY · zNear · zFar · primary. 방향은 액터 회전 |
| StaticMesh | visible · transparent · 절차 도형(prim · primMaterial) 또는 모델(model · meshIndex) |
| Audio | file · volume · loop · autoPlay · attenuation(공간화 · 감쇠 모델 · 최소/최대 거리 · 롤오프) · silentBeyondMax(최대 거리 밖 무음, 켰을 때만 씀) |
| RigidBody | shape(Box · Sphere · Capsule · Convex Hull · Mesh) · motion(Static · Dynamic · Kinematic) · friction · restitution · mass · halfExtent · continuous(연속 충돌, 켰을 때만 씀) |
| XR Rig | height(tracked · fixed) · eyeHeight. 플레이어 원점(바닥) |
| Tracked Pose | source(head · left · right) · showDebug |
| Locomotion | moveSpeed · snapTurn · flying · lookSensitivity |
| Grabbable | throwable |
| Grab Hand | radius |

### 4-7. 물리 (Jolt)

- 고정 스텝 1/60 초(XR 중에는 헤드셋 주사율), 바디 상한은 설정 파일.
- 콜라이더 크기는 월드 스케일을 반영합니다. 구 · 캡슐은 균등 스케일만 지원(비균등이면 가장 큰 값으로 근사하고 경고). 얇은 상자는
  모서리 반경을 두께에 맞춰 줄이고, 만들 수 없는 크기는 경고 후 바디를 만들지 않습니다.
- API: 힘 · 위치 지정 힘 · 토크 · 충격량 · 선속도 · 각속도 · 중력 설정, 레이캐스트, 구 겹침 질의, 바디 상태 조회, 마찰 · 반발 즉시 반영,
  바디를 다시 만들지 않는 Dynamic ↔ Kinematic 전환(잡기, 2026-10-15). 질량은 Static 이 아닌 모든 바디에 적용합니다.
- 바디는 Play 중 지연 생성되고, 끄면 지우며 주변 잠든 바디를 깨웁니다. 편집 확정 때만 다시 만듭니다.
- 수학 보강(2026-11-17, [Math_Reinforcement.md](Math_Reinforcement.md)): 볼록 껍질 · 메시 충돌체(같은 액터의 메시 형상, 비균등 크기 정확,
  Mesh 는 Static · Kinematic 전용), 연속 충돌(`continuous`, 던진 물체는 자동), 던지기 속도 = 손 속도 평균 + ω × (질량 중심 − 손) + 리그 이동(DEF-168).
- 조인트 · 충돌 이벤트 · 레이어(2026-11-20, [Physics_Joints_And_Events.md](Physics_Joints_And_Events.md)):
  - 충돌 레이어 16 슬롯(0 = Default)과 쌍별 충돌 표. `project.ini` 의 `physics.layer.<1~15> = 이름` · `physics.ignore = 이름, 이름` 으로 적고,
    기본은 모두 부딪힘(예전 동작). `RigidBodyComponent.layer`(씬 파일 `layer`, 디테일 칸은 레이어 이름 목록). 레이캐스트 · 겹침 · 스윕은 레이어 마스크를 받습니다.
  - 트리거(`trigger`, Jolt 센서): 충돌 반응 없이 들어감 · 나감을 알리고 질의(레이캐스트 · 스윕 · 겹침 · 캐릭터 이동)에 맞지 않습니다. Static 트리거는
    깨어 있는 키네마틱 센서로 만들어 잠든 몸도 봅니다.
  - 접촉 알림(`contactEvents` 를 켠 몸): 시작 · 유지(틱마다 한 번) · 끝. 물리 스레드는 큐에만 넣고, 메인 스레드가 스텝마다 바디 쌍 단위로 세어 물리 틱 끝에
    `RigidBodyComponent` 의 함수 칸(`onTriggerEnter` · `onTriggerExit` · `onContactBegin` · `onContactStay` · `onContactEnd`)과 이벤트 버스
    `PhysicsContactEvent` 로 보냅니다. 몸이 잠들어 빠진 접촉은 끝이 아니고, 몸을 지우면 끝을 보냅니다(상대는 nullptr). 알림 안에서 액터를 지워도 됩니다.
  - `JointComponent`: fixed · hinge(각도 한계, 도) · slider(거리 한계, m) · distance(최소 · 최대 거리), 상대는 `HActorRef`(비면 월드). 앵커 · 축은
    소유 액터 로컬, 값 0 은 만든 순간의 자세. 어느 쪽 몸이든 지워지면 조인트도 지웁니다. 에디터는 앵커 · 축 · 한계를 선으로 그립니다.
  - 셰이프 캐스트 `PhysicsSystem::shapeCast`(구 · 상자 · 캡슐, 회전, 레이어 마스크, 무시 액터, 시작부터 겹치면 거리 0).
- 내비게이션 · AI(2026-11-21, [Navigation_AI.md](Navigation_AI.md), recastnavigation 1.6.0 의 Recast · Detour):
  - `NavMeshVolumeComponent` 가 범위(월드 축 정렬) · 에이전트 반경 · 높이 · 최대 경사 · 턱 높이 · 셀 크기와 `data`(`.hnav` 논리 경로)를 듭니다. 입력은 활성 액터의
    Static 충돌체(트리거 제외)이고, 콘솔 `nav.build [경로]` 가 구워 디스크에 쓰고 곧바로 다시 읽습니다. 게임은 씬을 읽을 때 `.hnav` 를 읽기만 합니다.
  - 월드 서비스 `NavSystem`: `findPath`(완전 · 부분 = 가장 가까운 닿는 점까지 · 없음) · `projectPoint` · `moveAlongSurface`, `nav.info`(최신 여부 · 에이전트 상태) · `nav.debug`(디버그 선).
  - `NavAgentComponent`: 속도 · 회전 속도 · 도착 반경, 물리가 있으면 CharacterVirtual 로 이동(아니면 메시 위 미끄러짐), 에이전트끼리 분리 + 내다보기 회피,
    경로 이탈 · 쫓는 액터 이동 때 다시 찾기, 막힘 2 초면 실패, 이벤트 버스 `NavMoveFinished`.
  - `AIBrainComponent`: 대기 · 순찰(`patrolTag` 액터를 이름순) · 추적(`targetTag`, 감지 · 놓침 반경), 타이머 · 이벤트 버스, `AIStateChanged`.

### 4-8. 오디오 (miniaudio)

- 48kHz 스테레오, WAV · FLAC · MP3(miniaudio 내장) · OGG Vorbis.
- 출력 장치(2026-10-16): 에디터 Edit > Audio Output 에서 시스템 기본 또는 특정 장치를 고르고(기계별 `Config/editor_state.txt`),
  재생 중에 바꿔도 소리가 이어집니다. XR Play 동안에는 런타임이 알려 주는 헤드셋 장치로 자동 전환합니다(끌 수 있음).
- 3D 공간화: 청취자 = 활성 카메라의 위치 · 방향(XR 세션 중에는 헤드셋 머리 자세, 2026-10-15), 감쇠 모델 Inverse · Linear · Exponential, 최소/최대 거리 · 롤오프.
- 에디터 Pause 는 월드 소리만 멈추고, 컴포넌트를 끄면 멈추고 되감습니다. 장치 없이 출력 샘플을 만들어 검산하는 헤드리스 경로가 있습니다.
- 들리지 않는 소리의 가상화(2026-11-17): 예상 게인이 −60 dB 아래면 믹싱을 멈추고 위치만 세다가 −54 dB 위로 오면 이어 냅니다. 최대 거리 밖
  무음(`silentBeyondMax`)은 곡선을 (g − g_max) / (1 − g_max) 로 맞춰 최대 거리에서 0 입니다.
- 버스 그래프 · 믹스(2026-11-21, [Audio_Design.md](Audio_Design.md) 12절): 버스(sfx · music · voice · ambient · ui + 게임 코드가 더한 버스)마다
  노드가 있고 부모 버스 · 볼륨 · 음소거 · 출력 피크 미터가 있습니다. 스냅숏(이름 있는 버스 볼륨 묶음, 우선순위 · 페이드 인 · 아웃), 신호 기준
  더킹(원천 버스 피크 → 대상 버스를 공격 · 유지 · 해제 곡선으로), 마스터 피크 리미터(기본 끔, 한도는 최종 출력 기준). 사운드 큐 `.hcue`:
  변형 가중치 랜덤(시드 고정, 기본은 경로 해시) · 피치 · 볼륨 범위 · 동시 재생 상한과 넘을 때 규칙(stopOldest · rejectNew) · 재시작 최소 간격,
  AudioComponent 의 파일이 `.hcue` 면 큐. 게임 코드는 `world.service<AudioSystem>()`, 개발 중에는 CVar `audio.bus.<이름>.volume` · `.mute` ·
  `audio.limiter` 와 콘솔 `audio.snapshot` · `audio.cue` · `audio.buses`.

### 4-9. 입력

- 키보드 · 마우스(창), XR 컨트롤러. 키 · 버튼은 직전 폴링 이후 눌린 적이 있으면 눌림으로 봅니다(짧은 클릭 유실 방지). OS 자동 반복 횟수를 셉니다.
- `HInputSystem`: 액션 이름에 키 · XR 버튼(트리거 · 그립은 0.5 에서 끊음)을, 축 이름에 XR 축 · 키 쌍 · 마우스 축을 묶고 Pressed · Released · Held 를
  구독하거나 `held()` 로 묻습니다. 축 값 = 가장 큰 XR 축 + 키 축 + 마우스 축입니다. 텍스트 입력 중에는 게임 입력이 모든 키를 뗀 것으로 봅니다.
- 바인딩은 `assets/project.ini` 의 `input.action.*` · `input.axis.*` 줄로 정합니다(`key:W`, `keys:W/S`, `xr:left.grip`, `xr:left.stick.y`, `mouse:x*0.5`).
  틀린 줄은 경고하고 기존 바인딩을 바꾸지 않습니다. 줄이 없으면 엔진 기본값(MoveX · MoveY · MoveUp · TurnX · LookX · LookY · GrabLeft · GrabRight).
- 마우스 축은 Play 중 뷰포트 오른쪽 버튼 끌기 양입니다. 햅틱(`haptic(손, 세기, 초)`)은 XR 진동으로 갑니다(2026-10-15).
- **입력 확장(2026-11-21, [Input_Extensions.md](Input_Extensions.md)).** 데스크톱 게임패드는 창(GLFW 표준 매핑)이 읽어 `pad:a` · `pad:lt` ·
  `pad:left.x` 같은 소스로 묶습니다(쓰던 패드 유지, 없으면 번호가 가장 작은 패드). 축마다 `input.mod.<축> = deadzone:0.2, axial, curve:2,
  sens:1.5, invert` 수정자(패드 기본 원형 데드존 0.15). `input.context.<이름>.*` 줄과 `pushContext` · `popContext` 로 입력 컨텍스트를
  쌓고, 위 컨텍스트가 매핑한 소스는 아래로 가지 않습니다. `rebind` · `resetBinding` 은 사용자 설정에 바뀐 줄만 저장하고 충돌을 알립니다.

### 4-10. UI (HSlate)

- UE Slate 를 본뜬 리테인드 모드 위젯 시스템. 외부 의존은 stb_truetype 하나입니다.
- 위젯 14종: SBox · SBorder · SButton · SCollapseSection · SHorizontalBox · SVerticalBox · SImage · SNumberField · SOverlay ·
  SScrollBox · SSearchBox · STabBar · STextBlock · SToggleButton.
- 2패스 배치(desired size → arrange), `Attr<T>` 지연 바인딩, 클립 스택(사각형 자르기), 컨테이너의 휠 전달, 프레스 추적, 마우스 캡처.
- 폰트 아틀라스 2048×512(ASCII 32 ~ 127), 글자 폭 실측. 전용 `VkRenderPass`, 프레임 슬롯별 정점 버퍼, 프레임당 정점 65,536개 상한.
- **UI 문맥과 GPU 출력의 분리(3D UI 패널 P0, 2026-11-15).** CPU 쪽 상태(루트 위젯 · 배치 · 포인터 캡처 · 그리기 목록 만들기)는
  `HSlateContext`(`src/ui/slate/HSlateContext.hpp`, 결과는 `HSlateDrawList`)가, 렌더 패스 · 파이프라인 · 버퍼 · 폰트 업로드는
  `HSlateRenderer` 가 맡습니다. `HSlateRenderer` 의 공개 API 는 그대로이고, 기록은 렌더 패스를 인자로 받는 `recordDrawList` 로 합니다.
  한 프레임에 여러 출력(에디터 + 월드 패널)이 정점 버퍼를 나눠 쓰도록 슬롯마다 오프셋을 두며, 렌더러가 프레임 시작에서 `beginFrame(slot)` 으로
  0 으로 돌립니다(P1). `HFontAtlas::initCpuOnly()` 로 GPU 없이 그리기 목록을 시험합니다.
- **월드 UI 패널(3D UI 패널 P1, 2026-11-16).** `UIPanelComponent`(`src/components/UIPanelComponent.hpp`): 액터 로컬 XY 평면의
  width × height(m) 판(가운데 원점, 앞면 +Z, 한 면 또는 양면), 해상도 `pixelsPerMeter`(기본 1,250 px/m, 텍스처 한 변 4096 상한, 크기는 편집을
  확정할 때만 다시 정함), 내용은 이름으로 고름(`content`, 엔진 기본 `Demo`, 게임 모듈은 `UIPanelComponent::registerContent`). 바뀐 프레임에만
  다시 그립니다(그리기 목록 해시 비교, `markDirty` 는 강제용). 선택 판정은 두께 1 cm 의 얇은 판(`localBounds`)이고 XR 리그의 바닥 후보가 아닙니다(`blocksFloor`). GPU 쪽은 `HSlateRenderer` 의 패널 렌더 패스 · 파이프라인
  (`recordPanel`, 투명으로 지운 뒤 알파를 덧쌓아 알파를 곱한 색을 남김)과 렌더러의 `UIPanelPass`(패널 텍스처 · 밉 블릿 · 세대 붙은 핸들)가
  맡습니다. 에디터가 있으면 에디터의 `HSlateRenderer` 를 같이 쓰고(글꼴 아틀라스 · 정점 버퍼 공유), 게임 실행 파일은 패널이 처음 생길 때
  렌더러가 하나 만듭니다(글꼴이 없으면 오류 한 번 뒤 패널을 그리지 않음).
- **월드 UI 패널 P2(2026-11-16).** 내용 팩토리는 `(UIPanelComponent&, float scale)` 를 받아 1,250 px/m 기준 픽셀 × 배율로 트리를 만듭니다(해상도를
  바꿔도 배치는 미터 기준). 패널별 값 `param.이름=값`(`setParam` · `paramAttr`), 동작(`actionCallback` → `setActionHandler`), 밝기(`brightness`),
  배치(`placement` = World · FollowHead, 머리 앞으로 천천히 따라옴). 입력은 `UIPanelPointer`(`src/components/UIPanelPointer.hpp`): 광선 → 패널 로컬
  평면 → UV → 텍스처 픽셀 → `HPointerEvent`, 가장 가까운 패널 · 물리 레이캐스트 가림(플레이어 계층 제외) · 누르는 동안 캡처. 엔진이 Play 중
  마우스(에디터 뷰포트 · 게임 창, XR 세션이 없을 때)와 XR 두 손(조준 자세, 트리거 = 클릭, 스틱 = 휠, 레이저 · 커서 · 누름 진동)을 갱신합니다.
  헤드셋 확인(P3) · quad 레이어(P4) · SDF 글꼴 · 손 찌르기 · 가상 키보드는 남았습니다.

- **접근성(2026-11-21, [Accessibility.md](Accessibility.md)).** 자막 큐 `SubtitleQueue`(월드 서비스, 소리 · `AudioComponent` 의 `subtitle` 칸 ·
  게임 코드 · 콘솔 `subtitle.say` 가 `HText` 줄을 넣음, 최대 2 줄 · 우선순위 · 글자 수 기반 표시 시간)를 `SubtitlePanel` 이 3D UI 패널 경로로
  그립니다(XR · 합성 눈은 머리 앞 1.2 m 시선 아래 15°, 데스크톱은 화면 아래). 사용자 설정 `access.subtitles` · `subtitleSize` ·
  `subtitleBackground` · `textScale`(3D UI 패널 팩토리가 `panel.textScale()` 을 곱함) · `hand`(left 면 XR · 패드 스틱을 맞바꿈) · `colorFilter`
  (Machado 보정 행렬, 톤매핑 뒤), `comfort.vignette`(부드러운 이동 · 회전 속도에 따른 가장자리 어둡게) · `comfort.seated` ·
  `comfort.standingHeight`(바닥 기준 공간에서 선 눈높이 − 앉은 머리 높이를 원점에 더함, `comfort.recenterSeated` 로 다시 잼).
  모두 기본값이면 예전과 같은 화소입니다.

### 4-11. 에디터

| 영역 | 기능 |
|---|---|
| 레이아웃 | 도크 트리(분할 + 탭), 스플리터, 탭 끌어 다시 도킹, 탭 닫기와 Window 메뉴로 다시 열기, 레이아웃 저장 · 복원(`Config/editor_layout.txt`), Reset Layout, 창 높이에 따른 UI 배율(1 ~ 2.5배) |
| 메뉴 · 툴바 | File(New · Open · Save · Save As Scene), Edit(Undo · Redo · Copy · Paste · Duplicate · Delete · Frame Selected · Audio Output), Actor(Add Empty · 직전 선택에 붙이기 · 떼기), View, Window. Play · Pause · Stop · Save · Open · Refresh |
| 뷰포트 | 기즈모(Q 선택 · W 이동 · E 회전 · R 스케일. 이동 · 회전은 World/Local, 스케일은 언제나 로컬, 스냅. 여러 액터를 고르면 주 선택 피벗 기준으로 함께 변환), 액터 픽(아이콘 → 로컬 바운드 광선-OBB, Ctrl+클릭은 선택에 넣고 빼기), 우클릭 비행 카메라(WASD · Q/E, 비행 중 휠은 속도 단계) · 좌클릭 끌기 이동 · 휠 전후 이동, F 화면 맞추기, 라이트 · 카메라 · 오디오 아이콘, 선택한 액터들의 바운드 상자 |
| 아웃라이너 | 계층 트리(들여쓰기 · 펼침), 검색(Ctrl+F), 가시성 토글, F2 리네임, 삭제, 타입 열, 액터 수, 다중 선택(Ctrl 넣고 빼기 · Shift 범위), 끌어 놓아 부모 바꾸기(빈 곳은 루트, 월드 자세 유지) |
| 디테일 | 트랜스폼, 컴포넌트별 속성(UE 식 숫자 칸: 드래그 · Shift 10배 · Ctrl 0.1배 · 직접 입력), 켜기/끄기 · 제거, 컴포넌트 추가(레지스트리 기반, 폭에 맞춰 줄 나눔), Save as Prefab(서브트리). 여러 액터를 고르면 주 선택만 보여 주고 "N actors selected" 배지 |
| 하단 | Content Browser(씬 · 모델 · 오디오 · 프리팹, 지금 열린 씬 강조), Output Log(카테고리 · 수준 필터, 검색, 꼬리 추종), GPU Stats |
| 상태바 | 최근 로그 한 줄(수준별 색), 지금 키보드 입력이 가는 곳(Rename · Search · Value · Log Search · Dialog), 열린 씬 이름과 변경 표시 |
| 씬 파일 | 에디터 안의 대화상자: Open Scene(씬 목록), Save Scene As(`assets/scenes/` 아래 이름, 덮어쓰기 확인), Unsaved Changes(새 씬 · 다른 씬 · 창 닫기 전 Save · Don't Save · Cancel). 변경 여부는 저장한 순간의 Undo 상태 표지와 비교. Play 중에는 저장 · 열기를 막음 |
| 편집 | Undo/Redo 64단계, 모든 커맨드가 액터 ID 기반(2026-10-15): 트랜스폼, 리네임, 활성, 부모 바꾸기, 컴포넌트 켜기 · 속성 · 추가 · 제거, 액터 생성 · 삭제(서브트리를 원래 ID · 부모로 되살림), 여러 액터 편집은 한 번에. 삭제를 되돌리면 그 전 편집 이력도 이어집니다. 복사(OS 클립보드) · 붙여넣기 · 복제는 같은 부모 · 같은 트랜스폼, 이름은 "Name (n)". 기즈모를 끄는 동안에는 되돌리지 않습니다. Stop · New Scene · Open 은 Undo 기록을 지웁니다 |
| 단축키 | Ctrl+Z 되돌리기, Ctrl+Y · Ctrl+Shift+Z 다시 하기, Ctrl+N · O · S · Shift+S 씬, Ctrl+C · V · D 복사 · 붙여넣기 · 복제, F 화면 맞추기, Ctrl+F 아웃라이너 검색, F2 리네임, Delete 액터 삭제, Esc 글자 편집 끝내기(숫자 칸은 값 확정), 글자 · 숫자 칸의 Ctrl+A · C · V |
| Play | Play · Pause · Stop. Play 를 누르면 씬을 글로 저장해 두고 Stop 하면 다시 읽어 복원합니다(2026-10-14 일괄 점검에서 세 번 왕복한 뒤 저장 결과가 글자까지 같음을 확인). Play 중 화면 카메라는 켜진 첫 primary 씬 카메라(없으면 에디터 카메라)이고, 그때 뷰포트 오른쪽 끌기는 플레이어 둘러보기로 갑니다 |

### 4-12. 품질 관리

- **단위 테스트 44개, 검사 3,577개(2026-11-21).** 외부 프레임워크 없이 자체 `check()` 로 만든 콘솔 실행 파일이며 그래픽 장치 없이 돕니다.

| 테스트 | 검사 | 대상 |
|---|---|---|
| `geometry_conventions` | 66 | 절차 도형 규약 |
| `actor_lifecycle` | 54 | 활성 · 컴포넌트 생명주기 · 카메라 자세 |
| `command_stack_purge` | 8 | 커맨드 스택의 액터 참조 제거 |
| `actor_undo` | 27 | 액터 생성 · 삭제 Undo(원래 ID · 부모 · 순서로 되살림, 삭제 전 이력이 이어짐) |
| `log_category` | 61 | 로그 카테고리 |
| `text_system` | 121 | 글자 체계([Text_System.md](Text_System.md)): UTF-8 읽기 · 고치기, 글자(grapheme) · 단어 경계, 한글 NFC, ASCII 대소문자 무시, 편집 상태, 입력 처리기(가짜 창 · 클립보드 · 자동 반복 · IME 조합 · 후보 창 자리), HText(번역 · 낡은 번역 · 층 · 서식 · 숫자), .po 읽기, HTEXT 모으기 · 한국어 표가 소스 키를 모두 덮음, 씬 이름 |
| `object_refs` | 98 | 참조 체계([Object_References.md](Object_References.md)): 액터 GUID(형식 · 겹침 · World 색인), 액터 참조(지우면 끊기고 되살리면 이어짐), 씬 v6 GUID 왕복 · 옛 파일 · 겹친 GUID, Fresh 정책(프리팹 두 번 꺼내기 · 복제한 쌍 · 밖 참조 · 등록되지 않은 컴포넌트 · CRLF), Undo · Play 뒤 참조, 디테일 액터 참조 칸 · 고르기 목록, 에셋 참조 색인(옛 assets/ 형식 · ini · glTF uri) · 끊긴 참조 · 옮기기(파일 · 폴더 · 거절 · 바이트 보존 · 되돌리기 · 코드 경고), 샘플 프로젝트에 끊긴 참조 없음, 샘플 LookAtComponent |
| `prefab_links` | 72 | 링크된 프리팹([Linked_Prefabs.md](Linked_Prefabs.md)): 결정적 GUID 파생, 인스턴스 블록 펼치기(계층 · 짝 · 안 참조), 바꾸지 않은 인스턴스 = 블록만, 비교로 얻는 덮어쓰기 왕복, 원본이 바뀌면 덮어쓰지 않은 값만 따라옴(ID 유지), 추가 · 지운 컴포넌트 · 지운 액터 · 더한 액터 · 밖으로 옮김 · `@PARENT`, 중첩 2단 왕복 · 안쪽 원본 갱신 · 원본에 적용(중첩 · 보통 블록), 순환 거부 · 펼치지 못한 블록 보존 · 저장 거부, 되돌리기 · 링크 끊기 Undo · 삭제 되살리기 · 붙여넣기 · `MEMBER_GUID`, 옛 프리팹(GUID 없음) · v5 · v6 머리 줄, 에셋 참조(원본 경로 · 덮어쓰기 값) |
| `cpu_profiling` | 85 | CPU 구간 계측([CPU_Profiling.md](CPU_Profiling.md)): 중첩 스코프(순서 · 포함), 스레드 구분(끝난 스레드 수집 · 이름), 시작 · 멈춤 · 상한(버린 수), Chrome trace JSON(작은 파서로 다시 읽기 · 이스케이프 · 마이크로초 표기 · 정렬), 끈 번역 단위(`constexpr` · 이벤트 0), FrameTimings 공유 측정 지점, 추적 경로 규칙 · 기록기 · 콘솔 명령 · `--trace-frames`, 스코프 비용 |
| `asset_hot_reload` | 58 | 에셋 핫리로드 · 가져오기 설정([Asset_Hot_Reload.md](Asset_Hot_Reload.md)): import.ini 패턴(`*` · `**` · `?` · 대소문자) · 잘못된 줄 알림 · 뒤 절이 이김 · 설정 해시, 바뀐 파일 모으기(300 ms · 합치기 · 넘침) · 분류 · 프로젝트 상대 경로 변환, 바깥 파일 기록 · 의존 텍스처가 바뀌면 캐시 무효(DEF-174) · `/Game/` 경로의 `../` 바깥 파일(DEF-175) · 마운트 넘는 uri 거부, 배율 · 자동 LOD · sRGB 적용, 정적 · 스킨 메시 다시 묶기(meshIndex 줄임), 은퇴 목록(중복 · 새 핸들 · null 제외), 임시 폴더 감시, 명령줄 |
| `texture_compression` | 72 | 텍스처 압축([Texture_Compression.md](Texture_Compression.md)): KTX2 쓰기 · 읽기(단계 순서 · 정렬 · DFD) · 손상 14가지 거부, 형식 표 · 블록 크기 · 열쇠(화소 · 용도 · 크기), 장치 지원 판단(기능 꺼짐 · 선형 필터 없음 → RGBA8), 밉(Vulkan 크기 · sRGB 평균 · 노멀 정규화), 압축 → 풀기 PSNR 하한(BC7 · BC5 · ASTC 6x6 · 5x5, 두 채널 노멀 B = 0), 쿠킹 캐시(다시 압축 안 함 · 손상 다시 만듦), TexProbe 쿠킹 → 런타임 찾기 다섯 칸(다른 대상 · 손상 · 화소 바뀜이면 붙이지 않음) |
| `particles_decals` | 90 | 파티클 · 데칼([Particles_And_Decals.md](Particles_And_Decals.md)): 결정적 시드(같은 시드 = 같은 비트 · reset · 구), 수명 제거 · 상한 · 비율 · 반복 안 함 · 반복 터뜨리기 · dt 상한, 수명 비율 곡선(보간 · 범위 · 글자 왕복 · 거절), 중력 · 감쇠 · 로컬/월드 · 회전한 원뿔, 월드 시간 배율 · stop · play · autoPlay, 정렬(먼 것부터 · 같은 거리 순서 · 이미터끼리 · 상한에서 먼 것부터 잘림 · 드로 합치기), 모양 아틀라스, 필드 왕복, 데칼 상자 · 모양 · 각도 · 추출, 확인 장면 `ParticleProbe.hscene` 두 번 같음 |
| `physics_joints` | 55 | 물리 조인트 · 충돌 이벤트 · 레이어([Physics_Joints_And_Events.md](Physics_Joints_And_Events.md)): 레이어 표 해석 · `project.ini` 줄, 레이어로 무시한 쌍(통과 · 알림 0 · 질의 마스크), 접촉 시작 · 유지 · 끝(잠듦은 끝 아님), 트리거 들어감 · 나감(지나감 · 안에서 잠듦 · 잠든 몸 둘레에 생김 · 지우면 나감), 질의는 트리거를 맞추지 않음, 알림 안에서 삭제, 경첩 문 한계 · 상대 삭제, 슬라이더 · 거리 · 고정, 셰이프 캐스트(구 · 상자 · 캡슐 · 회전 · 마스크 · 무시 · 시작 겹침), 필드 왕복 · 디테일 칸, 확인 장면 `PhysicsJointProbe.hscene` |
| `navigation` | 64 | 내비게이션 · AI([Navigation_AI.md](Navigation_AI.md)): 시험 지형(바닥 · 벽 · 18도 · 60도 경사 · 섬) 굽기 · 경로 길이 · 벽 돌기 · 부분 경로 · 없음 · 투영 · 표면 미끄러짐, 반경 · 경사 · 턱 설정 효과, 생성 시간, 결정성, `.hnav` 왕복 · 손상 거부, Static 충돌체만 입력, 에이전트(표면 · 캐릭터) 도착 · 이벤트 · 벽 통과 없음 · 경로 이탈 다시 찾기 · 도달 불가, 마주 오는 두 에이전트 회피, AI 순찰 · 대기 · 추적 · 놓침, 확인 장면(저장된 `.hnav` = 다시 구운 바이트, 600 프레임 뒤 Walker 가 목표 곁) |
| `gameplay_tools` | 84 | 게임플레이 기본 도구([Gameplay_Tools.md](Gameplay_Tools.md)): 시간 배율 · 게임 시간, 타이머(정확한 틱 · 배율 · 반복 따라잡기 · 16 번 상한 · 주인 삭제 취소 · 콜백 안 취소 · 추가 · clear), 물리 뒤 틱, 태그(규칙 · 색인 캐시 · 씬 파일 왕복), 이벤트 버스(형별 · 순서 · 발행 중 해제 · 재발행 · 이동), CVar(등록 · 범위 · 보관 · 콜백 · 출처), 콘솔 명령(값 · help · list · reset · 따옴표 인자) |
| `accessibility` | 111 | 접근성([Accessibility.md](Accessibility.md)): 자막 큐(자동 길이 · 같은 id 바꾸기 · 최대 줄 수 · 우선순위 밀어내기 · 기다림 버리기), 사운드 컴포넌트 자막 칸, 줄바꿈(폭 · … · 한글), 자막 판 크기 · 머리 고정 · 화면 아래 자리 · 켜기 · 다시 만들기, 3D UI 패널 글자 배율 레이아웃, 비네트 속도 → 세기 · 필터 · 모양 · 이동 컴포넌트 알림, 앉은 높이 · 다시 맞추기, 손 바꾸기(XR · 패드 스틱), 색 보정 행렬, 설정 왕복 |
| `save_settings` | 89 | 저장 게임 · 사용자 설정([Save_And_Settings.md](Save_And_Settings.md)): 저장 파일 왕복(int64 · float 비트 · 줄바꿈 든 문자열 · vec3 · GUID), CRC 로 잘림 · 손상 거절, 형식 · 데이터 버전, 슬롯 이름 규칙, 다시 시작 왕복, `.bak`(잘린 본 파일 · 본 파일 없음 · 둘 다 손상), 남은 `.tmp`, 목록 · 지우기, 다시 읽은 씬의 GUID 참조, 설정 형 · 범위 · 바꾼 값만 쓰기 · 모르는 이름 보존 · BOM · UTF-16, 편안함 서비스 |
| `async_loading` | 30 | 비동기 로딩([Async_Loading.md](Async_Loading.md)): 메가 버퍼 구간 할당자(첫 맞춤 · 이웃 합치기 · 겹침 거절 · 무작위 1만 번), 백그라운드 모델 읽기(워커 결과 = 동기 결과, 중복 요청 · 실패 · 동시 디코드 · waitFor · 멈춤), 씬의 모델 참조 모으기 |
| `animation` | 103 | 애니메이션([Animation_System.md](Animation_System.md)): STEP · LINEAR(회전 slerp) · CUBICSPLINE 샘플링, 커서 탐색, 자세 섞기 · 가산, TRS 전역 행렬, TestArm.glb 클립 · 영향 8개 → 4개, 캐시 v10 왕복, 재생기(반복 · 한 번 · 거꾸로 · 크로스페이드 · 가산 · 이벤트 · 루트 모션 · 손 자세 충돌 · 저장 왕복 · 미리보기), 상태 기계, 모프 타깃(스킨 · 스킨 없는 메시), 트윈 |
| `crash_reports` | 83 | 크래시 보고([Crash_Reporting.md](Crash_Reporting.md)): 자식 프로세스로 잘못된 메모리 쓰기 · 스택 넘침 · 순수 가상 호출 · abort · terminate · 잡히지 않은 예외 · HCHECK · 잡은 예외를 일으켜 보고서(이유 · 기호가 붙은 콜 스택 · 최근 로그 · 미니덤프 · 절대 경로 없음), 다음 실행 알림 한 번, HENSURE 한 번만 보고 · 계속, 보관 10개, 파일 이름 줄이기 |
| `audio_mix` | 86 | 오디오 버스 그래프 · 믹스([Audio_Design.md](Audio_Design.md) 12절): 더킹 포락선과 렌더 RMS(공격 · 유지 · 해제 곡선), 스냅숏 섞기 · 페이드 · 우선순위, 리미터 피크 ≤ 한도(마스터 볼륨 포함) · 한도 아래 그대로, 버스 더하기 · 부모 · 음소거 · 미터 · 플레이 중 버스 바꾸기, 사운드 큐 글 형식 · 가중치 빈도 · 같은 시드 = 같은 순서 · stopOldest · rejectNew · 재시작 간격 · 큐 볼륨 · 버스 · 다시 재생, CVar · 콘솔 명령 · 월드 서비스 |
| `audio_system` | 262 | 오디오(헤드리스 출력 검산, XR 머리 자세 청취자, 스테레오 패닝 값(DEF-166), 출력 장치 설정 · 장치 ID 변환 · 열 장치 후보 순서(DEF-146 · 147), 묶음 안 음원 디코딩, 음원별 노드 사슬, 차폐 · 회절 근사, 잔향 · 잔향 구역, HRTF(가짜 구현으로 FIFO · 꼬리 · 블록 경계, 실제 Steam Audio 로 방향 단서 · 크기 맞춤), 들리지 않는 소리의 가상화 · 최대 거리 밖 무음(2026-11-17)) |
| `slate_input` | 319 | UI 입력 · 숫자 칸, 편집 칸(커서 ↔ x · 선택 · 조합 밑줄 · 누르기 · 끌기) · 표시 글 위젯 · 대체 글꼴 사슬 · 아틀라스 페이지 · 한글 패널(글자 체계, 2026-11-19), 그리기 목록 고정(GPU 없이 만든 정점 · 배치의 해시) · 층 정렬 안정성 · 문맥별 포인터 캡처 · 슬롯 정점 오프셋, 글꼴 아틀라스 넘침(오류 한 번), 월드 UI 패널 컴포넌트(텍스처 크기 · 직렬화 왕복 · 확정 때만 크기 반영 · 선택 판정 상자 · 내용 만들기 · 배치 배율 · 패널별 값 · 동작 · 밝기), 자동 다시 그리기(그리기 목록 해시), 패널 포인터(광선 판정 · 호버 · 클릭 · 캡처 · 가림 · XR 조준 · 트리거 · 스틱 휠 · 레이저), 따라오는 패널 |
| `physics_lifecycle` | 197 | 바디 생명주기 · 물리 API · 얇은 콜라이더, 레이캐스트에서 액터 빼기(소리 차폐용, 출발점을 감싼 충돌체 포함), 잡기 · 던지기(던지지 않고 놓기, DEF-143) · 플레이어 리그 · 추적 자세 · XR 원점(리그가 아닌 부모의 머리 추적, DEF-144), 카메라 아래 바닥 찾기(밟는 면이 아닌 바운드 제외) · 보이지 않는 리그 · 리그 높이 값, 던지기 속도 ω × r · 프레임 평균(DEF-168), 연속 충돌, 볼록 껍질 · 메시 충돌체(2026-11-17), 물리의 월드 단위 서비스(S3) · 잡기 대상과 손 추적 수의 월드 단위(S7) |
| `stereo_views` | 45 | 눈별 투영 · 두 눈 컬링 · 합성 눈, 화면 크기 · LOD 이력 띠 · 작은 물체 컬링(2026-11-17) |
| `xr_input` | 109 | VR 입력, 키 · 마우스 축, 설정 줄 바인딩, 햅틱, 입력 확장([Input_Extensions.md](Input_Extensions.md)): 가짜 게임패드 버튼 · 축 · 해제 · 패드 선택 규칙, 데드존(원형 · 축별) · 곡선 · 감도 · 반전, 컨텍스트 소비 · `blockLower` · 우선순위, 재지정 · 충돌 · 저장 → 다시 읽기 왕복 · 기본값 되돌리기 |
| `draw_batching` | 34 | 자동 인스턴싱 묶기 · 행 배정 |
| `render_extensions` | 43 | 패스 레지스트리 · 확장 지점, 멀티뷰를 막는 확장 판정(2026-11-18), 텍스처 밉 사슬(DEF-165), 지연 삭제 대기열 |
| `scene_components` | 74 | 컴포넌트 등록 · 씬 v4 값 · 회전 왕복, 라이트 발광 방향(액터 회전 -Z) · 옛 `direction` 접기, 씬 v7 · 프리팹 v5 머리 줄 |
| `light_selection` | 45 | 라이트 선택 · 상한 설정 파일 |
| `asset_cache` | 28 | glTF 캐시 유효성(옛 형식 v1 ~ 3 다시 만듦), 논리 경로 키의 안정성, 묶음 안 원본 판정, 에셋 논리 경로 해석 |
| `scene_document` | 63 | 씬 변경 여부(Undo 상태 표지 · Play 고정), 원자적 저장(바이너리판 포함), 프로젝트 설정 읽기(XR 리그 높이 · 게임 VR 시작 · 품질 단계 · 플랫폼 접미사 포함), 손으로 만든 설정 파일의 인코딩(BOM · UTF-16 · UTF-32 거부, DEF-152) |
| `editor_commands` | 80 | ID 기반 Undo 커맨드, 칸이 나타나고 사라지는 속성 Undo(DEF-131), 부모 바꾸기의 월드 자세 유지, 그룹 변환, 클립보드 형식 · 세션 표지(DEF-136), 복사 이름, 화면 맞추기 거리, 프리팹 이름 · 머리 줄 · 실패한 블록 정리(DEF-140 ~ 142) |
| `pak_format` | 83 | 묶음 파일 왕복 · 블록 경계 범위 읽기 · 손상 감지 · 조작한 목차 · 정렬 · 결정적 출력 · 경로 정규화 · UTF-8 경로 · 버전 규칙 · 여러 묶음과 낱개 폴더의 우선순위(`--loose`, 에디터 배치) · 이름 규칙 · 동시 읽기 · 실제 목록 파일의 제외 패턴 · 셰이더 목록으로 묶음과 빌드의 짝 확인 · 묶음 폴더 확인 · 전체 경로 마운트 이름(DEF-149) |
| `gltf_external_files` | 17 | glTF uri 의 `../` · `./` 외부 버퍼 · 텍스처를 낱개 폴더와 묶음에서 찾음, 논리 경로 맨 위보다 위로 가는 uri · 드라이브 문자 uri 거부, 절대 경로로 연 glTF 는 운영체제 경로 |
| `vulkan_baseline` | 88 | 장치 계획(1.1 · push descriptor · 멀티뷰 필수 · sync2 선택 · 창 없음 · 텍스처 대상이 쓰는 압축 기능만), 기능 체인(멀티뷰 · sync2 · 압축 기능), KHR 이름만 묻는 함수 로드, 큐 계열, XR 런타임 범위 |
| `barrier_compat` | 63 | sync2 → 1.0 배리어 변환표 · 엔진 값 전수 · 단계 합치기 · 거부 · KHR/1.0 기록 분기 |
| `memory_types` | 25 | 핵심 힙 타입 선택(Quest Pro · ReBAR 유무 · 작은 UMA), 지연 할당 타입, 실패 문구 |
| `headless_render` | 140 | 빈 스왑체인, 이미지별 세마포어(DEF-118), 표면 없는 창(Headless · Android), 명령줄(시작 씬 `--scene` · `--gpu` · `--settings-platform` · `--no-multiview` · `--capture-eye` · `--user-setting` · `--cvar` · `--console-at` · `--ui-console` 포함), 프레임 시간 분포 · 예산(`FramePacing.hpp`), 종료 코드 · 빈 화면, 파이프라인 캐시 파일의 잘림 · 손상 · 다른 장치 거르기(DEF-153), XR 세션 다시 열기 시각(DEF-155, `runtime/XrRecovery.hpp`) |
| `hand_skinning` | 108 | glTF 스킨 읽기 · CPU 스키닝 · OpenXR 손 뼈대 규약 · 손 자세 컴포넌트 · 컨트롤러 → 손가락 굽힘 |
| `math_common` | 84 | `src/math` 기본 도형 · 판정 · 허용 오차, 감쇠 · 스프링 · 이징, 곡선, PCG32 · Perlin · 블루 노이즈, One Euro · 속도 추정, 두 뼈 IK · 스윙-트위스트, 아핀 역행렬(2026-11-17) |
| `architecture_deps` | 30 | 모듈 include 방향(`tests/ArchitectureRules.txt`): 규칙 파일 · 모든 모듈에 규칙 · 표에 순환 없음 · 모듈마다 허용 방향 안 · 허용 목록이 줄기만 함([Engine_Structure_Design.md](Engine_Structure_Design.md) 4절), 정적 시스템 포인터 0 · 전역 싱글턴 목록(S3, 8절) |
| `vulkan_imports` | 11 | 실행 파일이 vulkan-1.dll 에서 1.0 · 1.1 코어와 표면 · 스왑체인 함수만 가져옴(PE 가져오기 표, Windows) |

- 2026-10-14 이후 새로 만든 검사는 수정을 되돌리면 실패하는지 **변이로 확인**했습니다.
- 화면 회귀는 에디터를 띄워 `PrintWindow` 로 찍은 뒤 뷰포트를 픽셀 단위로 비교하고, Vulkan 검증 레이어 메시지 0 과 `Clean exit` 를
  확인합니다. 절차와 기록은 [Editor_QA_Checklist.md](Editor_QA_Checklist.md).
- 결함 정본은 [Core_Stabilization_Spec.md](Core_Stabilization_Spec.md) 와 [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) 두
  문서입니다. DEF-01 ~ 48 은 두 문서에 함께 실리고(진단 맵과 요약 표는 2026-10-15 에 결함 절의 상태와 맞춤), DEF-49 이후는
  Core_Defects_Task_Spec.md 뒤쪽 절에만 있습니다. 상태는 각 결함 절과 Core_Defects_Task_Spec.md 끝의 결과 절을 기준으로 봅니다
  (6장의 표도 그 기준으로 셌습니다).

### 4-13. 수치 한도 요약

| 항목 | 값 | 바꾸는 곳 |
|---|---|---|
| 창 기본 크기 | 1600x900 | 코드 |
| 프레임 인 플라이트 | 2 | 코드(`MAX_FRAMES`) |
| 한 프레임에 그릴 수 있는 메시(transform 행) | 16384 | `Config/engine.ini` |
| GPU 로 올리는 라이트 | 256 | `Config/engine.ini` |
| 물리 바디 | 4096 | `Config/engine.ini` |
| 눈(뷰) | 2 | 코드(`kMaxViews`) |
| 그림자 캐스터 | Directional 1개, 2048², 40m 상자 | 코드 |
| 장치 메모리 힙 | 1GB(64 / 896 / 64MB) | 코드 |
| UI 정점 | 65,536 / 프레임 | 코드 |
| Undo 단계 | 64 | 코드 |
| 잡기 반지름 · 이동 속도 · 스냅 각 | 0.12m · 2m/s · 45° | 컴포넌트 값(Grab Hand · Locomotion) |
| XR 리그 눈높이(고정 모드 · 바닥 공간이 없을 때) | 1.7m(0.5 ~ 2.5m) | XR Rig 컴포넌트, 리그 없는 씬은 `assets/project.ini` |

---

## 5. 미구현 기능

### 5-1. 플랫폼 · 런타임

| 항목 | 현재 상태 | 관련 |
|---|---|---|
| Quest 단독 실행(Android) | APK 까지 만듦(2026-10-31): NDK 빌드(Gradle 없이) · `android_main` · 표면 없는 AndroidWindow · 공유 OpenXR 로더. 헤드셋 첫 실행이 남았고, 첫 실행은 묶음 파일을 `adb push` 로 넣고 소리를 끕니다([Packaging_Design.md](Packaging_Design.md) 7절) | DEF-08 |
| 게임 배포 형태 | Windows 는 2026-10-19 ~ 24 에 생겼습니다(게임 실행 파일 · 묶음 파일 · 배포 ZIP, DEF-12 해결). Android APK 는 개발용(디버그 서명)만 있고 묶음 파일을 아직 APK 안에서 읽지 않습니다. 게임 설정 화면(창 크기 · 전체 화면 · 소리 장치)도 없습니다 | [Packaging_Design.md](Packaging_Design.md) |
| Linux · macOS | 빌드된 적 없음 | |
| 게임플레이 기본 도구 · CVar | 2026-11-20 에 들어옴: 시간 배율 · 게임 시간(`World::setTimeScale` · `gameTime`), 타이머(`World::timers`), 태그(`Actor::addTag` · `World::actorsWithTag`), 이벤트 버스(`World::events`), 물리 뒤 틱(`ETickGroup::PostPhysics`), CVar · 콘솔 명령(`EngineContext::cvars`, ini · `--cvar` · 에디터 Output Log 콘솔 줄). 없는 것: 헤드셋 안 콘솔, 스크립트 계층 | [Gameplay_Tools.md](Gameplay_Tools.md) |
| 내비게이션 · AI | 2026-11-21 에 들어옴: 미리 구운 내비 메시(`.hnav`) · 경로 질의 · 이동 컴포넌트 · 작은 AI 상태 기계([Navigation_AI.md](Navigation_AI.md)). 실행 중 타일 다시 만들기 · 동적 장애물 구멍 · 행동 트리 · 큰 군중은 없음 | [Navigation_AI.md](Navigation_AI.md) |
| 저장 게임 · 사용자 설정 | 2026-11-20 에 들어옴: 저장 슬롯(`EngineContext::saves`, CRC · 이전 저장본 · 데이터 버전), 사용자 설정 층(`EngineContext::settings`, 소리 버스 · AO · 블룸 · 그림자 품질 · 키 재지정 · 편안함 · `game.*`). 없는 것: 게임 설정 화면, 해상도 배율 · vsync | [Save_And_Settings.md](Save_And_Settings.md) |
| 비동기 에셋 로딩 | 2026-11-20 에 들어옴: 워커 스레드 읽기 · 프레임당 2 ms 업로드 · 대기 없는 텍스처 업로드 · 지연 해제 · 메가 버퍼 재사용 · 게임 씬 전환(`EngineContext::requestScene`). 없는 것: 큰 텍스처 나눠 올리기, XR 로딩 레이어, 에디터 씬 열기의 비동기화 | [Async_Loading.md](Async_Loading.md) · [Threading.md](Threading.md) |
| Zero-Copy 로딩 | 이름만 남음. 캐시를 호스트 메모리로 읽은 뒤 업로드합니다 | |

### 5-2. 렌더링

| 항목 | 현재 상태 |
|---|---|
| 클러스터드 라이팅 | 보류(DEF-34). 픽셀마다 올린 라이트를 모두 순회합니다. PC 에서 라이트 하나에 약 4µs(상한 256 에서 약 1ms), Quest 는 실측 필요 |
| 여러 그림자 캐스터 | Directional 하나. 캐스케이드는 2단(가까운 단 6 m · 먼 단, 가로 두 칸 아틀라스, 2026-11-17), 품질 단계로 1단(2026-11-18). 1단일 때도 아틀라스는 두 칸이라 Quest 섀도 메모리 16 MB 를 덜 쓰는 일은 남았습니다 |
| 점광원 · 스팟 그림자 | 없음 |
| TAA · 모션 벡터 | 없음(가짜 TAA 를 지웠음). 안티에일리어싱은 MSAA 만 |
| 고정 포비티드 렌더링(FFR) | 없음(필요하면 Quest 에서) |
| 외부 HDRI 환경 맵 · 여러 반사 캡처 | 없음. 반사 캡처는 한 장면에 하나 · 정적 · 구 프록시(ReflectionCaptureComponent), 높이 안개(HeightFogComponent)는 있음(2026-11-17, 배경에는 걸리지 않음) |
| 접촉 그림자의 정확도 | 화면 공간 12걸음을 AO 항에 곱하는 근사(2026-11-17). Light Channels(3채널) · 카메라 노출(EV)은 있음 |
| LOD | 화면 크기(투영 지름 ÷ 화면 높이) · 이력 띠로 고르는 코드는 있으나(2026-11-17) LOD 메시를 채우는 경로가 없습니다 |
| 스키닝 · 애니메이션 · 모프 | 2026-11-20 에 들어옴([Animation_System.md](Animation_System.md)): CPU 스키닝(손 쥐기 때부터) · glTF 클립 · 재생기 · 크로스페이드 · 가산 · 상태 기계 · 루트 모션 · 모프 타깃 · 트윈. 없는 것: GPU 스키닝, 스킨 없는 노드 애니메이션, 블렌드 공간 · 리타기팅 · 애니메이션 그래프 편집기. 다른 기반 공백은 [Engine_Foundation_Gaps.md](Engine_Foundation_Gaps.md) |
| glTF `alphaMode = MASK` | 없음 |
| GPU 컬링 · 간접 드로우 | 없음(순수 오버헤드여서 지웠음) |
| 파티클 · 데칼 | 2026-11-20 에 들어옴([Particles_And_Decals.md](Particles_And_Decals.md)): CPU 이미터 · 빌보드 · 정렬 · 포워드 상자 데칼. 파티클 텍스처 파일 · Sub UV · 메시 파티클 · 조명 · 안개 · 부드러운 파티클 · GPU 시뮬레이션 · 충돌 · 데칼 텍스처 · 에디터 미리보기는 없음 |
| 3D UI 패널 헤드셋 확인 | 패널 보이기(P1) · 마우스 · 컨트롤러 레이 입력(P2)까지 있음. 헤드셋 확인(P3) · OpenXR quad 레이어(P4, 선택) · SDF 글꼴 · 손 찌르기는 없음([Spatial_Representation_Design.md](Spatial_Representation_Design.md) 10절) |
| Floating Origin | 없음(Layer 1 옵션 후보). Reverse-Z + 무한 원평면 · QTangent 정점(28 B) · 3 행 트랜스폼은 있음(2026-11-17) |
| `FrameUBO.invViewProj` | 보류(DEF-13, 첫 사용처가 생길 때) |

### 5-3. 물리 · 오디오

| 항목 | 현재 상태 |
|---|---|
| 조인트 · 컨스트레인트 | 2026-11-20 에 들어옴: `JointComponent`(fixed · hinge · slider · distance, 한계), 충돌 레이어 표, 트리거 · 접촉 알림, 셰이프 캐스트. 없는 것: 조인트 모터 · 스프링 · 끊어짐 힘, 6자유도 조인트, 레이어 표 편집 화면 |
| VR 상호작용 | 그립 잡기 · 던지기 · 속도 추적 잡기 · 자동 맞춤 쥐기 · 텔레포트(오른 스틱) · 벽에 막히는 이동(캐릭터 컨트롤러, `RigLocomotion.collide`)이 있음(2026-11-17). 레이 포인터 잡기 · 두 손 잡기는 없음 |
| 버스 볼륨 저장 · 한 번 재생의 차폐 · HRTF | 없음. 스트리밍 · 보이스 예산 · 버스와 마스터 볼륨(API) · 도플러 · 공기 흡수 · 지향성 콘은 있음(2026-11-17) |
| 오디오 입력(마이크) · 패키징된 빌드의 장치 선택 화면 | 없음. 출력 장치 API 는 있음(2026-10-16) |

### 5-4. 에디터

| 항목 | 현재 상태 |
|---|---|
| 부동 창 · 스크롤 막대 끌기 | 없음. 여러 액터 속성 동시 편집은 있음(2026-11-17, 주 선택의 값을 보이고 같은 타입 · 순번 컴포넌트에 같은 값을 넣음, 한 번의 Undo) |
| 뷰 모드(Lit · Unlit · Wireframe) | 없음 |
| 아이콘 텍스처 | 없음(아이콘은 모양별 빌보드). 라이트 범위 · 카메라 절두체 · 오디오 반경 · 충돌체 시각화는 있음(2026-11-17, 선택한 액터) |
| 파일 메시 머티리얼 편집 | 없음(머티리얼이 텍스처 슬롯만 있어 팩터가 없음). 절차 도형은 Checker · 색 · 금속도 · 거칠기를 편집함(2026-11-17) |
| 한글 · 유니코드 표시 | 2026-11-19 에 들어옴([Text_System.md](Text_System.md)): 유니코드 입력 · IME · 번역(영어 · 한국어) · 글꼴 대체 사슬. 번역하지 않은 에디터 글이 일부 남음 |
| 에디터 Phase 4 | VR 뒤로 미룸 |

### 5-5. Layer 2 (우주 · 무중력 도메인)

전부 미착수입니다. 무중력 비행 역학, 파편장(`DebrisFieldComponent`, 명시적 인스턴싱과 함께), 생존 규칙, 우주 비주얼이 계획에 있고
정본은 [Deep_Space_VR_Specification.md](Deep_Space_VR_Specification.md) 입니다. 정적 기지 건설은 2026-10-15 에 구현하기로 정했고(시점 미정),
지침과 선결 코어 작업은 [Static_Base_Building_Design.md](Static_Base_Building_Design.md) 에 있습니다. 코어 쪽 선결 조건 중 물리 API · VR 입력 · 6DOF 카메라 ·
인스턴싱 · 렌더 확장 지점 · 컴포넌트 등록은 갖췄습니다. [Layer_Architecture.md](Layer_Architecture.md) §6 기준으로 2단계의
`FrameUBO.invViewProj`(DEF-13 보류)와 3단계의 감쇠 운동 적분기 · 정점 패킹 · 보이스 예산 · 깊이 정책(Reverse-Z)이 남았고,
Floating Origin 은 Layer 1 옵션 후보입니다.

---

## 6. 열린 결함과 보류 항목

| ID | 심각도 | 내용 | 처리 시점 |
|---|---|---|---|
| DEF-08 | HIGH · 빌드 해결 | Android 진입점 · 빌드는 2026-10-31 에 생겼습니다(위 5-1). 헤드셋 실행 확인이 남았습니다 | Quest 첫 실행 |
| DEF-02 | MEDIUM · 잠재 | Zone C ring 할당에 펜스 추적이 없습니다. 지금은 시작할 때만 할당하므로 드러나지 않습니다 | 매 프레임 할당이 생길 때 |
| DEF-91 | LOW · 잠재 | 에디터 일부가 창 좌표, 일부가 프레임버퍼 좌표를 씁니다. Windows 에서는 둘이 같아 드러나지 않습니다 | 다른 플랫폼 |
| DEF-119 | LOW | 엔진에 넣은 글꼴이 없어 Windows 의 Segoe UI 를 씁니다. Android 에는 쓸 글꼴이 없습니다 | 게임 UI 를 만들 때 |
| DEF-129 | LOW · 잠재 | 헤드셋 재연결 때 새 시스템 ID 로 그래픽 요구사항을 다시 묻지 않습니다 | `--xr` 게임 실행 확인(헤드셋) |
| DEF-13 · 32 · 34 | 보류 | `invViewProj` · 재투영 여유분 · 클러스터드 라이팅 | 첫 사용처 · 늦은 자세 갱신 · Quest 측정 |

---

## 7. 우려되는 점

1. **VR 은 PC(Quest Link)에서만 확인했습니다.** 2026-10-15 첫 헤드셋 확인에서 OpenXR 세션 · 두 눈 렌더 · 컨트롤러 입력 · 헤드셋
   주사율 물리가 통과했습니다. 2026-10-16 2차 확인에서 DEF-110(검증 메시지 0), 최소화 중 XR 그리기(DEF-106), 플레이어 리그 · 잡기,
   보이지 않는 리그를 보았고 사용자가 충분하다고 판단했습니다. 소리 방향은 사용자가 따로 보고하지 않았습니다. Quest 단독 실행은 아래
   2번 때문에 아직 실행해 보지 못했습니다.
2. **Quest 단독 실행은 헤드셋 확인만 남았지만, PC 흉내로만 다져졌습니다.** 2026-10-31 에 Vulkan 1.1 기준선 · 배리어 분기 · 창 없는
   렌더 · 힙 · 지연 할당(DEF-04 · 06 · 42 · 97)을 고치고 APK 까지 만들었습니다. 확인은 창 없는 PC 실행과 Quest Pro 형 Vulkan 프로필
   레이어뿐입니다. 프로필 레이어는 메모리 표와 런타임이 덧붙이는 확장을 흉내 내지 못하고, OpenXR 런타임 · Android 수명주기 · 실제
   드라이버는 헤드셋에서 처음 탑니다. 멀티뷰(2026-11-18)도 PC 와 프로필 레이어에서만 확인했습니다([Packaging_Design.md](Packaging_Design.md) 6 · 7절).
3. **성능 근거가 PC 측정 몇 건뿐입니다.** Quest 실측이 한 번도 없습니다. 2026-11-18 에 프레임 시간 분포 · 끊김 로그 · 시작 시간을 남기게
   해 두어, 첫 Quest 실행의 로그에서 바로 수치를 읽을 수 있습니다(4-4 "프레임 시간 기록"). 같은 날 PC(RTX 3080, 두 눈 1024²) 측정은 기본 장면
   GPU 0.44 ms(Quest 품질 단계 0.28 ms), CPU 프레임 0.45 ms 였습니다. 라이트 비용(DEF-34)은 PC 측정에서 어림한 값입니다.
   문서에 남은 측정은 2026-09 의 데스크톱(RTX 3080) 측정(GPU 1.52ms 중 AO + Bloom 0.84ms, 55%)과 2026-10-04 의 기본 씬 GPU 합계
   0.32 ~ 0.37ms(다른 프로그램이 GPU 를 쓰는 중이라 참고값)뿐이며, 화면 크기에 비례하는 후처리가 큰 몫이었습니다.
   Quest 의 90Hz 예산(11.1ms)에 들어가는지는 아직 모릅니다.
4. **배포 형태는 Windows ZIP 과 개발용 APK 뿐입니다.** 2026-10-19 ~ 24 에 게임 실행 파일과 묶음 파일로 Windows 배포 형태가 생겼지만,
   다른 PC(ReBAR 가 없는 GPU, VC++ 런타임 설치 여부가 다른 PC)에서 실행해 본 적이 없고 게임 설정 화면도 없습니다. Quest 는 디버그
   서명 APK 와 `adb push` 한 묶음 파일로 첫 실행을 준비했습니다(APK 안 묶음 읽기 · 라이선스 고지는 다음 단계).
5. **자동 검증의 공백이 있습니다.** 렌더러의 GPU 경로 · 메모리 할당자 · 에디터 UI 조작에는 자동 테스트가 없고, 화면 회귀는 사람이
   에디터를 띄워 픽셀을 비교해야 확인됩니다. CI 가 없어 빌드 · 테스트가 개발 PC 한 대에서만 돌았습니다.
6. **문서가 코드보다 쉽게 낡습니다.** 이번 작성 중에도 루트 README 의 "알려진 한계"(드로우 상한 1024, 스크롤 · 클리핑 없음 등)와 문서
   색인의 "VR 진행하지 않음"이 낡아 있었습니다. 결함 정본 두 문서의 동기화와 줄 번호 갱신도 손으로 해야 합니다. 그래서 이 저장소에서는
   문서의 체크박스보다 코드 확인이 우선입니다.
7. **원시 포인터를 붙잡는 구조가 남아 있습니다.** Undo 커맨드는 2026-10-15 에 ID 기반으로 바뀌어 포인터를 붙잡지 않지만, 디테일 패널의
   값 접근자와 리플렉션 속성(`Reflect.hpp`)은 여전히 컴포넌트 주소를 캡처합니다. 지금은 액터 파괴 이벤트와 컴포넌트를 제거할 때 패널을 다시
   만드는 것으로 막고 있지만, 컴포넌트를 해제하거나 옮기는 새 경로가 생기면 DEF-55 같은 use-after-free 가 다시 나올 수 있습니다. 잡기
   컴포넌트의 정적 목록 · 손과 물체의 연결도 포인터이며, 끄기와 소멸자 양쪽에서 정리합니다.
8. **메인 스레드 하나에 모든 일이 몰려 있습니다.** 에셋 로딩이 동기라 큰 모델을 읽는 동안 프레임이 멈추고, VR 에서는 곧 멀미로
   이어집니다. 비동기 로딩이 첫 멀티스레딩 도입 지점으로 예정되어 있습니다.
9. **직접 만든 부분이 많아 유지 비용이 큽니다.** 할당자 · UI 시스템 · 에디터를 외부 라이브러리 없이 1인이 유지합니다. 종합 오류 검출과
   일괄 점검에서 매번 새 결함이 나온 것(DEF-76 ~ 94, 105 ~ 109, 2026-10-31 오류 검토의 120 ~ 147)은 아직 덜 다져진 영역이 있다는 뜻입니다.
10. **저장소 관리.** 라이선스 파일이 없고(2026-10-15 사용자 결정으로 당분간 둠), `stb` 는 버전이 고정되지 않아 구성
    시점에 따라 다른 소스를 받을 수 있습니다(영향 범위는 stb_truetype). 초기 커밋의 날짜는 문서에 적힌 작업 날짜와 맞지 않습니다
    (예: 2026-07-03 커밋의 도킹 Phase 0 은 문서상 2026-08-27 작업, 2-1).
11. **사용성 제약.** UI 가 ASCII 만 표시합니다. 씬 파일 선택 · 되돌릴 수 없던 편집 · 다중 선택은 2026-10-15 에 해결했지만, 여러 액터의
    속성을 한 번에 바꾸지는 못하고 Play 중 둘러보기는 오른쪽 버튼 끌기만 됩니다(마우스 고정 없음).

---

## 8. 앞으로의 순서

정본은 [Roadmap.md](Roadmap.md) 입니다. 2026-10-15 기준으로 합의한 순서는 다음과 같습니다.

1. ~~**헤드셋 확인**~~: 2026-10-15 기본 확인, 2026-10-16 2차 확인(DEF-110 · DEF-106 · 플레이어 리그 · 보이지 않는 리그)을 마쳤습니다
   ([Editor_QA_Checklist.md](Editor_QA_Checklist.md) 5절). 같은 날 넣은 XR Play 동안의 헤드셋 소리 자동 전환만 헤드셋에서 확인하지 못했습니다.
2. **패키징(Windows · Android XR, 2026-10-17 목표)**: 1단계(Windows 첫 패키징)는 2026-10-24 에 끝났고 Quest Link 실행 확인만
   남았습니다. 2단계(PC 에서 확인하는 Android 준비)는 2026-10-31 에 코드를 끝냈고(창 · 헤드셋 확인 남음), 3단계(Android 빌드)는 APK 까지
   만들어 헤드셋 첫 실행이 남았습니다. 사용자 목표는 "가능한 빨리 스탠드얼론"(2026-10-31)이라 검토 결함 묶음(DEF-130 ~ 147)보다 먼저
   하려 했으나, 헤드셋 개발자 모드가 꺼져 있어 Quest 단독 확인은 보류 중이고 검토 결함 묶음을 먼저 마쳤습니다(2026-11-06). 그 뒤 Quest 에서 DEF-34(라이트 비용)를 실측하고, 멀티뷰는 그 뒤입니다. 정본
   [Packaging_Design.md](Packaging_Design.md).
3. **에디터 Phase 4 · 렌더링 기능**: Light Channels, CSM, 노출 배선, 반사 프로브, 에디터 시각화 등.
4. **Layer 2**: 우주 · 무중력 도메인과 파편장, 정적 기지 건설([Static_Base_Building_Design.md](Static_Base_Building_Design.md)).

남은 비VR 결함은 잠재 항목 DEF-02 · DEF-91 과 글꼴(DEF-119)입니다. 앞의 둘은 지금 드러나는 경로가 없습니다. 2026-10-31 오류 검토에서
나온 에디터 · 프리팹 · XR · 소리 결함(DEF-130 ~ 147)은 2026-11-06 에 모두 고쳤고, 창 · 헤드셋 · 장치로만 볼 수 있는 동작은
[Editor_QA_Checklist.md](Editor_QA_Checklist.md) 끝 "일괄 점검 대기" 표에 모았습니다.

2026-10-15 에 고른 엔진 기본 기능 다섯 가지 중 씬 관리 · 편집 기본기 · 플레이어와 입력은 같은 날 구현했고
([Engine_Basics_Design.md](Engine_Basics_Design.md)), 독립 실행 빌드는 패키징 1단계로 2026-10-24 에 Windows 형태가 생겼습니다
([Packaging_Design.md](Packaging_Design.md)). 머티리얼 편집은 위 순서 안의 자리를 아직 정하지 않았습니다.
