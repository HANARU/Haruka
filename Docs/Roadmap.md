# 진행 예정 정리 (2026-08-26 기준)

이 문서는 세션에서 도출된 다음 작업들을 우선순위와 함께 모읍니다. 각 항목의 상세 근거는
연결된 설계·결함 문서에 있습니다.

**관통하는 원칙**: 안전을 **규율이 아니라 구조로** 만듭니다. "각 호출부가 잊지 않는 것"에
걸린 안전을, 타입·이벤트·메커니즘으로 강제되게 바꿉니다. 이번 세션에서 이미 그렇게 한 것들:
월드 행렬 캐시 제거(stale 불가), `WorldViewport` 단일 진입점(좌표 혼용 불가),
`Transform` 필드 private 캡슐화(우회 불가).

---

## A. GameObject 구조 견고화 (정본: 이 세션의 감사)

유니티 GameObject 모델을 C++로 차용 중입니다. 골격은 건전하고 **오늘 도달 가능한 크래시는
0건**입니다. 다만 안전이 호출부 규율에 걸려 있어 초반 취약함이 느껴집니다. 규율을 구조로.

### A-1. 값싼 방어 가드 — ✅ 완료 (2026-08-26)

빌드·적대적 검증(6에이전트, 관점 5 + 종합 1) 통과. 검증 중 회귀 1건을 잡아 수정함.

1. ✅ **파괴 멱등 가드**: `World::destroyImmediate`(World.cpp) 진입부에서 `actors` 를 포인터
   값으로 스캔, 미소유면 `broadcast`·역참조 없이 조기 반환. 순차 이중 파괴·외부 포인터 차단.
   **한계(주석에 명시, 현재 무해)**: 파괴 핸들러 재진입 파괴와 주소 재사용(ABA)은 이 스캔으로
   못 막음. 지금은 그런 핸들러가 없어 도달 불가.
2. ✅ **커맨드 스택 무효화 메커니즘화**: `ICommand::refersTo(const Actor*)` 계약 추가,
   `HCommandStack::purge(actor)`가 undo/redo 양쪽에서 그 액터 참조 커맨드 제거. `EditorApp::init`
   에서 `onActorDestroyed` 구독(커맨드 purge + 선택 포인터 무효화), `cleanup`에서 해제. 개별
   삭제 2경로의 무딘 `cmdStack_.clear()` 제거(무관 undo 이력 보존 = UX 개선). 씬 리셋
   경로(stopPlay/loadScene)의 clear 는 씬 경계 의미로 유지.
   - **아키텍처 계약**: 액터/컴포넌트 상태를 캡처하는 **새 `ICommand` 파생은 반드시 `refersTo`
     재정의**. 안 하면 파괴 후 undo/redo 에서 UAF. 구현체는 `SetTransformCmd`(EditorCommands.hpp),
     `PropsSnapshotCmd`(EditorApp.cpp) 둘. **후자는 `.cpp` 안에 정의돼 있어 처음 놓쳤다가 검증이
     회귀로 잡음** — 소유 액터를 저장하고 refersTo 를 추가해 수정.
   - **예외(2026-10-15)**: 액터 생성 · 삭제 Undo 의 `ActorPresenceCmd`(EditorCommands.hpp)는 액터를 포인터가 아니라
     ID 로 기억하고, 지울 때 서브트리를 글로 떠 두었다가 원래 ID · 부모로 되살립니다(`ActorSnapshot`, `World::reassignId`).
     붙잡는 포인터가 없으므로 `refersTo` 를 재정의하지 않으며, 그래야 자기가 지운 액터 때문에 걷히지 않습니다.
   - **이후(2026-10-15, 엔진 기본 기능 보강)**: 모든 에디터 커맨드가 액터를 ID 로, 컴포넌트를 타입 + 순번으로 찾게 바뀌어
     `refersTo` 를 재정의하는 커맨드가 없습니다(`PropsSnapshotCmd` 는 지웠음). 메커니즘은 포인터를 붙잡는 커맨드가 다시 생길 때를
     위해 남겨 두었고, 그때는 컴포넌트 제거 경로에 `purgeComponent` 호출도 다시 넣어야 합니다. 정본은 Engine_Basics_Design.md 4-1.
3. ✅ **렌더 조회 `isEnabled()` 게이트**: `getVisibleMeshes`/`getLights`/`getActiveCamera`
   (World.cpp)에 컴포넌트 enabled 조건 추가(로드맵은 앞 둘만 지정했으나 카메라도 같은 누락이라
   함께). 디테일 패널에서 라이트를 끄면 이제 씬이 어두워짐.

### A-2. SetActive·계층 — ✅ 완료 (2026-08-26, "계층 직렬화까지" 범위)

빌드·유닛 테스트(Actor/Component 28개, 사이클 포함)·적대적 검증(6에이전트) 통과. 검증이 찾은
minor 3건(사이클 크래시·직렬화 인덱스 어긋남·물리/오디오 게이트 불일치)을 모두 수정.

4. ✅ **활성 모델 + 컴포넌트 연동**: `Actor::active`(public) 제거 → private `activeSelf_` +
   `activeInHierarchy()`(부모 체인). World 의 4개 게이트를 실효 상태로. 컴포넌트는 `live_`
   (=enabled_ && 액터 activeInHierarchy)로 정의하고 `syncLive()`가 전이 시에만 onEnable/onDisable
   발화(Component.cpp). setActive/attachTo/detach 가 `refreshSubtreeActive`로 서브트리 전파. active
   직렬화(ACTIVE 라인). 유닛 테스트가 전파·멱등·tick 게이트를 런타임 검증.
5. ✅ **계층 배선(직렬화까지)**: `World::destroyImmediate` 재귀 파괴. SceneSerializer 에 PARENT
   인덱스 직렬화, deserialize 가 블록당 슬롯(실패 시 nullptr 자리표시)으로 정합 유지 후 attachTo
   배선. `attachTo`에 사이클 가드(무한 재귀 차단, worldMatrix 의 기존 무가드 재귀도 보호).
   **미룸**: 부모 있는 Dynamic 바디의 물리 월드→로컬(수기 편집 씬에서만 도달, 렌더 드리프트),
   재부모화 UI(에디터, 사용자가 천천히. **2026-10-15 아웃라이너 끌어 놓기로 완료, 월드 자세 유지**), 플레이 중 setActive 로 이미 생성된 바디 해제.
   (사운드 쪽은 2026-10-02 에 해결: 끄면 멈추고 되감음. → G 절)

### A-3. 나중에 (편의)

6. `getComponents<T>()` 복수 조회, `getComponentInChildren`. 이때 `removeComponent`의
   `componentCache_` 재바인딩(제거 후 남은 첫 T 로 갱신) 한 줄도 함께 고쳐 중복 컴포넌트 대비.

---

## B. 엔티티 공간 표현 (정본: Spatial_Representation_Design.md)

1~3단계(트랜스폼 캡슐화, 픽 일반화, 아이콘) 완료. 남은 단계는 실제 그 엔티티를 다룰 때.

- **4단계 `drawEditorVisual`**: 라이트 range 구/spot 원뿔/방향, 카메라 절두체, 오디오 반경을
  와이어로. `drawEditorVisual` 훅은 아직 없고(4단계 때 추가), 그릴 곳인 에디터 라인 · 삼각형 오버레이 목록만 있음(2026-10-15 정정).
- ✅ **5단계 프레이밍(F 키)**: 2026-10-15 완료. 선택한 액터들의 월드 바운드 구에 카메라를 맞춤(Engine_Basics_Design.md 4-5).
- **6단계 3D UI 패널**: 얇은 판 바운드 + 패널 컴포넌트. **VR 핵심.** 레이아웃·입력·렌더가 얽혀
  별도 설계 필요. 방향은 정함(2026-08-26 결정, 2026-10-15 재확인): UE 식 render-to-texture, `UIPanelComponent`, `HWidget` 트리 재사용(상세는 Spatial_Representation_Design.md 10절 "남긴 것").
- 아이콘 텍스처(지금 단색 사각형), 기즈모 크기 설정 UI(`setSizeFactor` 준비됨).

---

## C. 트랜스폼·기즈모 잔여 (정본: Transform_Design.md)

- Capsule/Torus 비균등 스케일을 기즈모가 막지 않음(규약상 균등만). 물리는 경고하나 기즈모는 방치.
- 회전 기즈모가 시선과 회전 평면이 거의 나란할 때 불안정(광선-평면 교차). 카메라 돌리면 안정.
- `getFramebufferSize`(에디터) == `swapchain extent`(렌더) 가정. 리사이즈 직후 한 프레임에서
  갈릴 수 있음. 근본 해결은 D의 뷰포트 렌더.

---

## D. 렌더링 결함 — ✅ 완료 (2026-08-27, 전부 적대적 검증 통과. 상세: rendering_pipeline_debt.md)

- ✅ **D-3** SSAO 밴딩: 원인은 half/full-res 아니라 CLAMP 노이즈 샘플러. REPEAT+NEAREST 전용 샘플러 +
  half-res noiseScale. (트레이드오프: 캐시 지역성↓로 SSAO 약간 느려짐, 탭 감축의 선결.)
- ✅ **D-4** 블룸: 알파가 아니라 업샘플 스킵-커넥션 부재. binding 1 추가 + `bloom_upsample.frag` 대응 down 밉 가산.
- ✅ **D-5** T-c 로 처리: 가짜 TAA(지터·모션벡터·클램프 전무) 비활성 + 히스토리 blit 제거. 진짜 TAA 는 OpenXR 이후.
- ✅ **A-4** GPU 컬 dispatch 제거, 깊이 프리패스도 CPU opaqueItems. CullPass 는 GPU 구동 트랙에서 되살림.
- ✅ **뷰포트 렌더** `renderExtent` 분리 + post 서브렉트 합성 + `WorldViewport` 좌표 뷰포트 rect 전환(근본 해결).
- 추가 완료: 반투명 A1(볼록 2패스) · 투명 그림자 · 투영 헬퍼 단일화.

## D'. 라이팅 (정본: Lighting_UE_Design.md, lighting.md)

UE식 용어 채택, **"명칭만" 결정**(내부 상대값, 물리 단위·노출계 안 함).

- ✅ **완료(2026-08-27)**: 방향 Transform 커플링, Phase 0 직렬화 복구.
- ✅ **Phase 1 완료(2026-08-27)**: 필드 개명(`range`→`attenuationRadius`, `spotInner/OuterDeg`→
  `inner/outerConeAngle`), 사문 `ambient` 제거(직렬화 예약 슬롯), 디테일 패널 Type 드롭다운 + 조건부 노출.
  적대적 검증(4렌즈) 통과. Mobility enum 은 저가치라 보류(유효값 하나뿐).
- ✅ **Phase 2 색온도 완료(2026-08-27)**: `useTemperature`(기본 꺼짐)/`temperatureK`, `effectiveColor()`
  =color×kelvinToRGB(Tanner-Helland). 기본 꺼짐이라 렌더 불변. 직렬화 trailing 확장.
- ✅ **Phase 2 per-light Cast Shadows 완료(2026-08-27)**: `LightComponent::castShadows`(기본 켜짐, Directional
  에서만 UI 노출), 직렬화 trailing 확장. **선결이던 그림자 정확성 DEF-26/27 도 이때 함께 해결**(캐스터
  슬롯 전달 + `Lo*=shadow` 범위 축소, 라이트 프러스텀 섀도 목록 + 카메라 우선 2단계 행 배정). 적대적 검증 통과.
- **다음 — Phase 2 잔여**: **Light Channels**(per-object 채널을 셰이더까지 배선 = 중간 규모: 파이프라인
  레이아웃·푸시 상수·프리미티브 필드·직렬화·UI). GpuLight 여유 슬롯(spotParams.zw)은 라이트 쪽만 있음.
- **라이트 방향 표현(2026-10-15 사용자 결정)**: `direction` 필드를 없애고 액터 회전의 -Z 를 빛의 방향으로 씁니다(카메라 ·
  OpenXR · Godot 과 같은 축). 옛 씬은 읽을 때 `direction` 을 회전에 접어 넣어 화면을 유지합니다. 다음 라이팅 작업 때 합니다.
- soon: 노출 배선, CSM 2캐스케이드, Contact Shadows, Reflection Capture, Height Fog. skip(예약어): Static/
  Stationary·Lightmass·Lumen·VSM·Distance Field·Auto Exposure.

---

## E. Quest / VR (정본: Core_Stabilization_Spec.md 2-K)

"구조 불가피" 판정은 뒤집힘(튜닝/패스로 예산 안에 듦). 남은 것은 성능이 아니라 완성도.

- ✅ **PC 스테레오 + OpenXR(2026-10-04)**: 두 눈 렌더 · 두 눈 컬링 · VR 입력 · OpenXR 세션(`--xr`). **2026-10-15 Quest Link
  (Meta Quest Pro)에서 확인**, 같은 날 사용자 결정으로 헤드셋에는 Play 동안만 그림(DEF-111). 2026-10-16 2차 확인으로
  최소화(DEF-106) · 소리 · DEF-110(검증 메시지 0)까지 마침.
- ✅ **잡기 · 던지기와 플레이어 리그(2026-10-15 코드 완료)**: 리그 · 추적 자세 · 스틱 이동 · 스냅 회전 · 그립 잡기와 던지기 · 진동,
  바닥 기준 공간(LOCAL_FLOOR), 시연 씬 `XrDemo`. 2026-10-16 헤드셋 확인. 같은 날 사용자 결정으로 **XR 에는 언제나 리그**(리그 없는 씬은 카메라 아래 바닥에 보이지 않는 리그,
  리그 높이는 실제 키 또는 고정 눈높이). 정본은 Engine_Basics_Design.md 2-5 · 5절.
- **Quest 단독 실행**: `AndroidWindow::createSurface` 가 예외 스텁이라 Quest 에서 직접 실행할 수 없음. 멀티뷰 · Vulkan 1.1
  대체 경로와 함께 Quest 이식 때.
- FFR(선택): PBR 픽셀 비용이 크게 나오면. PC 화질과 분리 가능.

---

## F. 섀도 (정본: Core_Stabilization_Spec.md 2-H, 2-I)

- 바이어스 0.003→0.0004 수정의 **화면 관측 미확인**. 실행해 접촉 그림자 붙는지 확인 필요.
- ✅ **DEF-26/27 해결(2026-08-27)**: 캐스터 슬롯 전달 + `Lo*=shadow` 범위 축소, 라이트 프러스텀 섀도 목록
  + 카메라 우선 2단계 행 배정. 포인트 라이트 선등록·카메라 회전에서 그림자가 사라지던 문제 해소.
- PCF 실효 커널, 섀도 박스 과대(40×40m 고정) 등은 **미검증**(반증 실패). 착수 전 실측 필요.

---

## G. 사운드 (정본: Audio_Design.md)

- ✅ **기초 정리 완료(2026-10-02)**: 청취자 방향(예전엔 위치만 넘겨 늘 -Z), Pause 가 소리를 멈춤(월드
  그룹), 비활성화하면 멈추고 되감음, stop 은 되감기, 플레이 중 Loop·설정 반영, 3D 감쇠 설정 노출
  (Spatialize·Attenuation·Min/Max Distance·Rolloff, 씬 `AUDIO_3D`), OGG Vorbis(stb_vorbis), 종료 정리.
  시험 음원 3종 + AudioProbe 프리팹, `HarukaAudioTest`(장치 없는 엔진으로 출력 샘플 검산) 추가.
- ✅ **VR 청취자(2026-10-15)**: XR 세션 중에는 청취자가 헤드셋 머리 자세를 따릅니다(Audio_Design.md 3-1). 2026-10-16 헤드셋 장치로 재생해 확인.
- ✅ **출력 장치 선택(2026-10-16 사용자 결정 · 구현)**: 에디터 Edit > Audio Output 에서 고르고(기계별 설정), XR Play 동안 헤드셋 장치로 자동
  전환(기본 켜짐, 헤드셋 실기 확인 남음). 패키징된 빌드는 같은 API 를 게임 설정 화면이 씀(화면은 게임 UI 를 만들 때. 2026-10-19 부터 게임
  실행 파일은 시스템 기본 장치 + XR 중 헤드셋 자동 전환). 정본 Audio_Design.md 8절.
- 다음 후보: 긴 음악 스트리밍, 보이스 예산, HRTF, 버스·마스터 볼륨.

## I. 에셋 루트 (2026-10-15)

- ✅ **사용자 결정 · 완료**: 에디터 빌드는 프로젝트 폴더의 `assets/` 를 직접 읽고 씁니다(UE · Unity · Godot 방식,
  `src/core/ProjectPaths.hpp`). 예전에는 실행 폴더의 복사본을 써서, 에디터에서 저장한 프리팹이 소스 트리에 남지 않고
  다음 빌드의 에셋 복사에 덮어써졌습니다. 씬 · 프리팹에는 논리 경로(`assets/...`)가 그대로 남고, 캐시는 실행 폴더에 둡니다.
- ✅ 씬 파일 선택(2026-10-15): 씬은 `assets/scenes/` 아래 논리 경로로 열고 저장하고, 시작 씬은 `assets/project.ini` 의 `startScene` 입니다
  (Engine_Basics_Design.md 3절).

## H. 정적 기지 건설 (정본: Static_Base_Building_Design.md)

- **2026-10-15 사용자 결정: 구현합니다. 착수 시점은 정하지 않았습니다.** Layer 2 기능이며, 기지 모듈은 지은 뒤 움직이지 않습니다
  (이동형 기지는 범위 밖). 외부 가이드 초안을 코드와 대조해 바로잡은 지침입니다.
- 선결 Layer 1 작업(지침 4절): 품질 설정(SSAO · Bloom 끄기와 깊이 STORE 제거), MSAA 지연 할당(DEF-06) · 멀티뷰 · 힙 축소(DEF-42)는
  Quest 이식과 함께, 컬링 가시성 질의 지점, 복합 · 메시 콜라이더, 바디 일괄 추가 · 추가할 때 영역 깨우기 · 상자 겹침 질의.
- 구현 때 정할 것은 지침 6절(Quest SSAO 기본값, 가시성 질의 형태, 모듈 콜라이더 종류 등).

## J. 엔진 기본 기능 (정본: Engine_Basics_Design.md)

2026-10-15 사용자 요청으로 엔진에 부족한 기본 기능 다섯 가지를 골랐습니다: 씬 관리, 편집 기본기, 플레이어와 입력, 머티리얼 편집,
독립 실행 빌드.

- ✅ **1 ~ 3(2026-10-15)**: 설계 에이전트의 제안서(UE · Unity · Godot 조사)를 코드와 대조해 받고 · 고치고 · 미룰 것을 정한 뒤 구현했습니다.
  씬 파일 선택과 변경 표시, 프로젝트 설정 `assets/project.ini`, ID 기반 Undo(삭제 전 이력 보존으로 결정 뒤집음), 다중 선택 · 복사와
  붙여넣기 · 부모 바꾸기 · 화면 맞추기 · 계층 프리팹, 플레이어 리그 · 이동 · 잡기 · 키와 마우스 축 · 햅틱. 2026-10-16 헤드셋 확인을 마쳤고, 그 뒤 "XR 에는 언제나 리그"를 더했습니다.
- 미룬 것: 속도 추적 잡기, 텔레포트, 캐릭터 컨트롤러, 여러 액터 속성 동시 편집(Engine_Basics_Design.md 2-3).
- **4(머티리얼 편집)**: 순서 미정.
- **5(독립 실행 빌드) → 패키징(정본 Packaging_Design.md)**: 2026-10-17 사용자가 패키징(Windows · Android XR)을 목표로 두고 열린 결함부터
  해결하기로 했습니다("standalone VR 이 사실상 Quest 단독 실행을 위한 패키징이다. 방향은 같다").
  - ✅ **1단계 Windows 첫 패키징(2026-10-19 ~ 24)**: 조사 6건 뒤 사용자 결정(결정 6 자체 묶음 파일, 나머지 권장안). 런타임 분리
    (`HarukaGame`, DEF-12 해결), 게임 실행 규칙, 실행 폴더 기준 경로 · 쓰기 루트, 배포 ZIP, `.hpak` v1 · 가상 파일 시스템 · `HarukaPak`,
    라이선스 고지. 이 과정에서 DEF-112 ~ 119 등재(6건 해결). 남은 것: `--xr` 로 Quest Link 실행 확인(DEF-129 와 함께).
  - ✅ **마무리 묶음(2026-10-31)**: 헤드셋을 다른 사람이 써서 2단계를 먼저 준비하며 미커밋 작업 전체를 오류 검토(DEF-120 ~ 147 등재).
    사용자 결정 "전부 권장대로, 패키징 마무리 묶음부터"로 1단계 코드의 DEF-121 ~ 128 을 고침(Packaging_Design.md 5-2). 나머지 검토
    결함(DEF-130 ~ 147)은 2단계 뒤 별도 묶음으로 중간 등급부터.
  - ✅ **2단계 Android 준비(2026-10-31, 코드 끝남)**: DEF-97 · 04(Vulkan 1.1 고정, 확장 함수, 배리어 래퍼), DEF-42(1GB 를 못 잡으면 멈춤),
    DEF-06(MSAA 컬러 지연 할당), 창 없는 렌더 경로(DEF-118 · 103), DEF-120 을 해결(Packaging_Design.md 6절). 창 없는 실행 · Quest Pro 형
    프로필 레이어로 확인. 남은 것: 창 · 헤드셋 확인(에디터 회귀 · Quest Link). 새 결함 DEF-148(서브패스 의존성).
  - **3단계 Android(2026-10-31 ~, 진행 중)**: 사용자 목표 "가능한 빨리 스탠드얼론"에 따라 검토 결함 묶음보다 먼저. Gradle 없이 NDK r30 ·
    aapt2 · apksigner, `android_main` · 공유 OpenXR 로더 · 표면 없는 AndroidWindow, APK 까지 만듦(Packaging_Design.md 7절). 남은 것:
    헤드셋 개발자 모드 → 첫 실행(묶음은 `adb push`) → 수명주기 → 소리 · APK 안 묶음 읽기, 그 뒤 Quest 에서 DEF-34 측정.
    2026-11-01 Android 오류 검출(패키징 관점)에서 나온 DEF-149 ~ 156 과 Windows 쪽 DEF-157 을 고침(Packaging_Design.md 7-1): 묶음과
    실행 파일의 짝 확인, Android 빌드가 묶음을 만듦, configChanges, APK 고지, 설정 파일 인코딩, 파이프라인 캐시, 종료 순서, XR 손실, SSAO.
  - ✅ **검토 결함 묶음 DEF-130 ~ 147(2026-11-06)**: 헤드셋 개발자 모드가 꺼져 Quest 단독 확인이 보류된 동안 18건을 모두 고침(중간 등급
    130 · 131 · 143 · 145 부터, 사용자 결정 "전부 권장대로"). 에디터(대화상자 뒤 디테일 · 끄는 동안 단축키 · 표시 · 붙여넣기 부모 · 창 닫기 ·
    Save As 이름 · 스플리터), 속성 Undo 칸 이름, 프리팹(이름 검사 · 고아 정리 · BOM), XR(놓기 속도 · 리그가 아닌 부모의 머리 추적),
    소리 장치(잃은 장치 다시 열기 · 후보 순서). 창 · 헤드셋 · 장치로만 볼 동작은 Editor_QA_Checklist.md 끝 "일괄 점검 대기".

---

## 권고 착수 순서

1. ✅ **A-1 방어 가드** · **A-2 SetActive·계층** — 완료(2026-08-26).
2. ✅ **D 렌더 결함 + 뷰포트 렌더 + 반투명** — 완료(2026-08-27).
3. ✅ **라이팅 방향 커플링 + UE 용어 설계 + Phase 0 직렬화** — 완료(2026-08-27).
4. ✅ **라이팅 Phase 1 + Phase 2 색온도** — 완료(2026-08-27). 리네이밍(렌더 불변) + Type 드롭다운/조건부
   노출 + 색온도. UI 로 라이트 타입·각도·색온도 조절 가능해짐. 적대적 검증 통과.
5. ✅ **그림자 정확성 DEF-26/27 + per-light Cast Shadows** — 완료(2026-08-27). 적대적 검증(회귀 1건 잡아 수정) 통과.
6. **다음 선택지**:
   - **라이팅 Phase 2 잔여**: Light Channels(중간 규모, per-object 채널 셰이더 배선)만 남음.
   - **F 섀도 후속**: 바이어스 화면 관측, CSM(2캐스케이드), Contact Shadows, 섀도 박스 프러스텀 피팅(미검증 항목 실측).
   - **E 스테레오**(VR 첫걸음) / **라이팅 soon**(노출 배선·반사 프로브) / **B 3D UI** / **A-3**(컴포넌트 편의).
   - 에디터 UI 비중 큰 B 는 "천천히" 방향이라 뒤로.
7. **합의한 순서(2026-10-01)**: ✅ 절차 도형 PRIM 직렬화 → ✅ 사운드 기초(G) → **종합 오류 검출**(진행 중:
   2026-10-02 검출과 수정 전 기준선 완료. UI 결함 DEF-56~69 남은 13건 + 신규 DEF-76~91, DEF-73~75 는 자동 조작
   환경의 산물로 닫음. 같은 날 1~3 묶음(크래시·값 손실, 입력 정리, 물리·렌더·정리)을 고치고 같은 체크리스트로
   재확인함(커밋 e88913b). 2026-10-03 기반 공사 묶음 DEF-56·57·58·60·69(클리핑 · 위젯 휠 경로와 스크롤 상자 · 키 반복 ·
   글자 폭 실측 · Window 메뉴와 탭 닫기)도 고치고 같은 체크리스트로 확인함(커밋 cbae320). 남은 것은 잠재 DEF-91. 절차와 결과는
   Editor_QA_Checklist.md) → **2026-10-04 남은 결함 정리**(타 엔진 해법 조사 뒤 권장 순서: ① 1단계 정리 묶음 DEF-03 · 11 ·
   14 · 15 · 28 · 29 · 30 · 35 와 DEF-10 진단, 신규 DEF-98 해결 ② 화면 기준 재정렬 DEF-95 · 96 과 점광원
   역제곱 감쇠 완료, 노출 1.0 ③ DEF-46 숫자 칸 완료(단위 종류 · UE 식 수정 키, 커서 고정 없음). ①~③ 은 기반 공사와 함께 커밋 cbae320 ④ VR 첫걸음과 함께 DEF-21 → 09 · 31 · 32 ·
   20 · 19 · 99 — **2026-10-04 코드 완료, 커밋 5d3a86b**: 카메라 자세 · 두 눈 렌더(눈마다 두 번) · 두 눈 컬링 · VR 입력 · 물리 API ·
   Kinematic 동기화 해결, DEF-32 보류, PC OpenXR 연결(`--xr`)과 스테레오 흉내 모드. 헤드셋 확인은 2026-10-15 Quest Link 로 완료(Play 동안만 VR). ⑤ Quest 이식 때 DEF-08 · 12 · 42 · 04 · 97 · 06 ⑥ 나머지 — **2026-10-14 DEF-16 자동 인스턴싱 완료**(행 예산 16384, 명시적 인스턴싱 컴포넌트는 파편장과 함께), **DEF-17 · 18 패스 확장 지점 완료**(고정 지점 넷 + `EngineContext::render`, 화면 확인은 사용자 지시로 일괄 점검 때), **DEF-22 직렬화 타입 등록 · DEF-101 완료**(씬 v4 · 프리팹 v2, `EngineContext::components`), **DEF-33 라이트 선택 · DEF-10 상한 설정 완료**(`Config/engine.ini`), DEF-34 · 13 보류(실측 뒤 · 첫 사용처), **같은 날 일괄 점검 완료**(미뤄 둔 화면 확인 모두 통과, 점검 중 찾은 DEF-106 최소화 멈춤 · DEF-107 얇은 상자 충돌 · DEF-108 회전 흔들림 · DEF-109 추가 버튼 잘림과 DEF-105 캐시 무효화 해결, DEF-34 는 PC 측정 뒤 Quest 측정까지 보류). 정본은 Core_Defects_Task_Spec.md 끝 절)
   → VR 첫걸음(PC OpenXR, Quest Link) → 에디터 Phase 4·렌더링 기능.
