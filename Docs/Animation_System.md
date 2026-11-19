# 애니메이션 체계 — glTF 클립 · 재생 · 블렌딩 · 상태 기계 · 모프 · 트윈 (2026-11-20)

[Engine_Foundation_Gaps.md](Engine_Foundation_Gaps.md) 의 1위 후보입니다. 손 쥐기 작업([Hand_Grasp_Design.md](Hand_Grasp_Design.md))이 만든
스킨 경로(스킨 자료형 · 로더 · 캐시 · CPU 스키닝 · 렌더러의 `skinArena`) 위에, glTF 애니메이션을 읽어 재생하는 부분을 더합니다.
이 문서는 작업 지시서가 아니라 **완료 조건 체크리스트**이고, 항목을 모두 만족하면 끝입니다.

## 1. 조사 요약

### 1-1. 지금 코드 (2026-11-20 감사)

| 있음 | 없음 |
|---|---|
| `SkinSourceData` · `hskin::`(계층 순서 · 전역 행렬 · 팔레트 · 선형 블렌드 스키닝) | glTF `animations` 읽기(채널 · 샘플러) |
| `GltfLoader::readSkin`(관절 · 쉬는 자세 TRS · 역바인드 · JOINTS_0/WEIGHTS_0), 캐시 v9 의 스킨 | 클립 · 샘플링 · 보간(STEP · LINEAR · CUBICSPLINE) |
| `SkinnedMeshComponent`(관절 **회전**만 설정 · `skinInto`) | 관절 이동 · 배율 자세 |
| 렌더러의 CPU 스키닝 → 프레임 슬롯 구간(32,768 정점), 스킨 정점 AABB 컬링 | 재생기 · 크로스페이드 · 가산 · 상태 기계 · 이벤트 · 루트 모션 |
| `math/Damping.hpp`(ease · 스프링), `math/Curves.hpp`(hermite), `math/Ik.hpp` | 모프 타깃, 트윈, 에디터 미리보기 · 뼈대 표시 |

JOINTS_1 은 경고만 내고 버립니다. 손 쥐기 문서는 "큰 4개만 남기고 다시 정규화"라고 적었으므로 코드를 문서에 맞춥니다.

### 1-2. 다른 엔진

- **Unreal:** `UAnimSequence`(트랙 = 뼈마다 TRS 키) → `UAnimInstance` · AnimGraph(블렌드 노드 · 상태 기계 · 전환 규칙 · 블렌드 시간), AnimNotify(시간 표시
  → 사건), Root Motion(뿌리 뼈 이동을 캡슐로 옮김), Morph Target(곡선으로 무게), Additive(기준 자세와의 차이). 평가는 로컬 TRS 를 섞은 뒤
  컴포넌트 공간으로 바꿉니다.
- **Unity:** `AnimationClip` · Animator Controller(상태 · 전환 · 파라미터 bool/float/int/trigger · Exit Time), Animation Events, Avatar,
  Blend Shape. 런타임 API `Animator.Play` · `CrossFade` · `SetFloat` · `SetTrigger`.
- **Godot:** `Animation`(트랙: 위치 · 회전 · 배율 · 블렌드 셰이프 · 메서드 호출) · `AnimationPlayer`(재생 · 큐 · 블렌드 시간) · `AnimationTree`
  (상태 기계 · 블렌드 공간), `Tween`(속성을 시간에 따라 바꿈, 이징 · 연쇄).
- **glTF 2.0:** 채널 = (샘플러, 대상 노드, 경로 translation · rotation · scale · weights). 샘플러 = 입력 시간(초, 오름차순) · 출력 값 · 보간 STEP ·
  LINEAR · CUBICSPLINE(키마다 in 접선 · 값 · out 접선, 접선에 키 간격을 곱함). rotation 의 LINEAR 는 slerp(구면 선형)이고, CUBICSPLINE 회전은
  계산 뒤 정규화합니다.

### 1-3. 공통 구조

세 엔진 모두 "클립을 로컬 TRS 자세로 샘플링 → 자세끼리 섞기 → 전역 행렬 → 스키닝"의 순서이고, 재생 상태(시간 · 속도 · 반복)는 클립 밖의 재생기가
가집니다. 상태 기계는 재생기 위에 얹는 선택 층입니다. 이 구조를 그대로 따릅니다.

## 2. 결정

- **D1 자료 위치:** 클립은 스킨에 묶어 `SkinSourceData::clips` 에 둡니다. 트랙은 glTF 노드가 아니라 **관절 번호**를 가리킵니다. 그래서 로더 · 캐시 ·
  `AssetPair` · 컴포넌트의 기존 경로를 그대로 쓰고, 같은 모델을 여러 액터가 공유합니다. 스킨이 없는 노드 애니메이션(강체 계층)은 이번 범위 밖입니다(5절).
- **D2 자세 형:** 자세는 관절마다 로컬 TRS(`hanim::JointPose{t, r, s}`)입니다. 섞기는 이동 · 배율은 선형, 회전은 같은 반구로 맞춘 nlerp 후 정규화입니다
  (UE · Unity 의 자세 섞기와 같음). 클립 안의 키 사이 회전 보간은 glTF 규약대로 slerp 입니다.
- **D3 샘플링:** 키 찾기는 이분 탐색이고, 재생기가 앞 키 번호를 기억해 다음 프레임에 그 자리부터 찾습니다(긴 클립 대비). 시간이 범위 밖이면 끝 키 값을
  씁니다. 트랙이 없는 관절은 쉬는 자세입니다.
- **D4 재생기:** `AnimationComponent` 가 같은 액터의 `SkinnedMeshComponent` 를 움직입니다. 바닥 층은 클립 하나 + 크로스페이드 중인 이전 클립,
  그 위에 가산 층 하나(기준 = 그 클립의 첫 프레임)를 둡니다. 컴포넌트 틱은 Play 동안만 돌고, 에디터에서는 미리보기 시간으로 자세를 정합니다.
- **D5 이벤트 · 루트 모션:** 이벤트는 (클립, 시간, 이름) 표이고 재생 구간을 지날 때 한 번씩 알립니다(반복으로 되감기면 끝까지 · 처음부터 두 구간).
  루트 모션을 켜면 뿌리 관절의 이동 변화량을 액터 트랜스폼으로 옮기고 그 관절의 이동은 첫 키 값으로 고정합니다(반복 때 되감기 보정).
- **D6 상태 기계:** 자료 파일 형식 없이 C++ API 로 만듭니다(`hanim::StateMachine`: 상태 = 클립 · 반복 · 속도, 전환 = 조건(bool · float 비교 ·
  트리거 · 클립 끝) · 블렌드 시간). 파라미터는 게임 코드가 넣습니다. 그래프 편집기는 범위 밖입니다.
- **D7 모프:** glTF `targets` 의 POSITION · NORMAL 차이를 읽어 스키닝 전에 무게만큼 더합니다(CPU). 무게는 메시 기본값 `weights` 와 클립의 weights
  채널에서 옵니다. 스킨이 없는 모프 메시는 관절 하나(단위 행렬)짜리 스킨으로 만들어 같은 경로로 그립니다(`SkinnedMeshComponent`).
- **D8 트윈:** 게임 코드용 `World` 서비스 `TweenSystem` 입니다. 액터 위치 · 회전 · 배율과 임의 float 를 이징으로 바꾸고, 액터가 지워지면 함께
  취소합니다. 시간은 월드 틱 dt(일시정지 · Play 를 따름)입니다.
- **D9 결정적 시간:** 창 없는 확인 PNG 가 실행마다 같도록 `--fixed-dt=<초>` 를 더합니다(지금 dt 는 실제 경과 시간). 기준 PNG 7장의 명령은 바꾸지
  않습니다.

## 3. 체크리스트

### A1 클립 · 샘플링 · 자세 (순수 계산)
- [x] A1-1 `hanim::Clip`(이름 · 길이 · 트랙), 트랙 = 관절 번호 · 경로(T · R · S · weights) · 보간 · 시간 · 값. 시간이 오름차순이 아니면 거절.
- [x] A1-2 STEP · LINEAR(회전은 slerp, 짧은 쪽) · CUBICSPLINE(접선 × 키 간격, 회전 정규화)이 손으로 계산한 값과 1e-5 안에서 같음.
- [x] A1-3 범위 밖 시간은 끝 키, 키 하나짜리 트랙, 빈 클립.
- [x] A1-4 `sample(clip, t, rest, pose, cursor)` 의 커서 탐색이 처음부터 찾은 값과 같음(앞으로 · 되감기).
- [x] A1-5 자세 섞기(w = 0 · 1 · 0.5, 반대 반구 회전), 가산(기준과 같으면 바뀌지 않음).
- [x] A1-6 `hskin::computeGlobals` 의 TRS 판이 회전만 판과 같은 결과(이동 · 배율이 쉬는 값일 때).

### A2 glTF 가져오기 · 캐시
- [x] A2-1 스킨 관절을 가리키는 채널을 그 스킨의 클립으로 읽음. 관절이 아닌 노드를 가리키는 채널은 건너뛰고 한 번 알림.
- [x] A2-2 접근자 형식(float, 회전 정규화 정수 i8 · u8 · i16 · u16)과 CUBICSPLINE 의 3배 출력 개수 확인. 깨진 클립은 그 클립만 버림.
- [x] A2-3 `.gasset` 캐시 버전을 올리고 클립을 왕복(옛 캐시는 다시 만듦).
- [x] A2-4 JOINTS_1 · WEIGHTS_1 이 있으면 8개 중 큰 4개를 남기고 다시 정규화(문서와 코드 일치).
- [x] A2-5 시험 모델 `make_test_anim.py` → `Projects/Samples/Content/models/anim/TestArm.glb`(관절 3개, 클립 둘 이상, 세 보간 모두). 로더로 읽은
  값이 만든 값과 같음.

### A3 재생기
- [x] A3-1 `AnimationComponent`: 클립 이름 · 자동 재생 · 반복 · 속도 필드 저장 왕복, `play(name, fade)` · `stop` · `setTime` · `isPlaying` · `time`.
- [x] A3-2 반복 · 한 번 재생(끝에서 멈춤 · 끝 알림), 음수 속도.
- [x] A3-3 크로스페이드: 페이드 동안 자세가 두 클립의 섞음, 끝나면 새 클립만.
- [x] A3-4 가산 층: 무게 0 이면 바닥 자세 그대로.
- [x] A3-5 이벤트: 지나간 표시마다 한 번, 반복 경계, 큰 dt 에서도 빠짐없음.
- [x] A3-6 루트 모션: 한 바퀴 반복한 뒤 액터 이동 = 클립의 뿌리 이동 합, 뿌리 관절은 제자리.
- [x] A3-7 손 자세(`HandPoseComponent`)와 같은 액터에 없을 때만 자세를 씀(둘이 함께면 경고 한 번).

### A4 상태 기계
- [x] A4-1 상태 · 전환 · 파라미터(bool · float · 트리거) API, 조건이 맞으면 블렌드 시간으로 전환.
- [x] A4-2 "클립 끝" 전환, 트리거는 한 번 쓰면 지워짐, 같은 프레임 여러 전환 중 먼저 등록한 것.
- [x] A4-3 `AnimationComponent` 에 상태 기계를 붙이면 그것이 재생을 정함.

### A5 모프 타깃
- [x] A5-1 POSITION · NORMAL 차이와 기본 무게를 읽음(스킨 있는 · 없는 메시). 캐시 왕복.
- [x] A5-2 무게 0 이면 정점이 바인드와 같음, 1 이면 바인드 + 차이.
- [x] A5-3 클립의 weights 채널로 무게가 바뀜.

### A6 에디터 · 결정적 확인
- [x] A6-1 디테일: 클립(목록) · 반복 · 속도 · 자동 재생 · 미리보기 시간(에디터에서 그 시간의 자세를 보임).
- [x] A6-2 선택한 스킨 메시의 뼈대 선 표시.
- [x] A6-3 `SkinnedMeshComponent::localBounds` 가 마지막으로 스키닝한 경계를 따름(선택 상자 · 초점).
- [x] A6-4 `--fixed-dt` 와 샘플 씬 `AnimProbe.hscene` 으로 창 없는 PNG 두 번이 같음, 다른 시간의 PNG 는 다름.
- [x] A6-5 UI 덤프에 애니메이션 컴포넌트 디테일 장면.

### A7 트윈
- [x] A7-1 `TweenSystem`: 위치 · 회전 · 배율 · float, 이징, 지연, 반복 · 왕복, 끝 콜백.
- [x] A7-2 액터가 지워지면 취소, 같은 대상의 새 트윈이 앞 트윈을 대신함(선택).
- [x] A7-3 일시정지 동안 멈춤(월드 틱을 따름).

### G 공통
- [x] G-1 Release · Debug 경고 0, ctest 두 구성 통과(검사 수 기록), `HarukaArchTest` PASS(새 파일이 층 규칙을 지킴: `hanim` 은 assets 아래 순수 계산).
- [x] G-2 기준 PNG 7장 그대로, UI 덤프 기존 장면 그대로(새 등록 컴포넌트의 Add Component 단추 차이만).
- [x] G-3 Android 빌드 통과, 바꾼 파일의 경고 0.
- [ ] G-4 (헤드셋) Quest 에서 AnimProbe 재생과 프레임 시간.

## 4. 결과 (2026-11-20)

| 항목 | 결과 |
|---|---|
| 빌드 | Release · Debug 경고 0 |
| ctest | 두 구성 32/32 통과, 검사 2,581(새 `animation` 103) |
| 아키텍처 | `HarukaArchTest` PASS(검사 28). `hanim` 은 assets, `TweenSystem` 은 scene(헤더만), `AnimationComponent` 는 components |
| 기준 PNG 7장(`base7.sh an`) | 모두 그대로(ce6def24 · 73dd1f02 · c283f276 · b4bd1fee · 5d6e3e5f · 9cf28353 · 201fad0e), 검증 오류 0 |
| 결정적 확인(A6-4) | `AnimProbe.hscene` 을 `--fixed-dt=0.016667 --frames=30` 으로 두 번 그리면 같은 PNG(17ac81a4), 45프레임은 다름(aa0089aa), 검증 오류 0. 그림에서 Wave(팔 굽힘) · Bounce(뿌리 이동 · 손 배율) · Flex(팔꿈치 부풂)가 보임 |
| UI 덤프(`uidump.sh an cr`) | 54장면(52 + 애니메이션 2). 기존 52장면의 뷰포트는 `select_hand` 하나만 다름(선택한 장갑의 뼈대 선, A6-2 의도). UI 차이는 Add Component 의 "+ Animation" 단추(선택 장면들), 콘텐츠 목록의 TestArm · TestMorph · AnimProbe 뿐 |
| Android | `build/android` 의 `HarukaGame` 빌드 통과, 바꾼 파일의 경고 0. 처음에 clang 이 "클래스 안 구조체의 기본 멤버 초기화를 같은 클래스의 기본 인자로 쓰기"를 거절해 `TweenOptions` 를 클래스 밖으로 옮김 |

**새 파일:** `src/assets/AnimClip.hpp` · `Animation.hpp/.cpp` · `AnimStateMachine.hpp/.cpp`, `src/components/AnimationComponent.hpp/.cpp`,
`src/scene/TweenSystem.hpp`, `tests/AnimationTest.cpp`, `src/tools/make_test_anim.py` →
`Projects/Samples/Content/models/anim/TestArm.glb` · `TestMorph.glb`, `Projects/Samples/Content/scenes/AnimProbe.hscene`.

**바꾼 곳:** `Skin.hpp/.cpp`(JointPose · clips · 모프 자료 · TRS `computeGlobals` · `restPose` · `applyMorph`), `GltfLoader.cpp`(클립 · 모프 ·
영향 8개), `GassetCache.cpp`(v10), `SkinnedMeshComponent`(TRS 자세 · `setPose` · 모프 무게 · 스키닝 경계), `World`(트윈 진행 · 삭제 때 취소),
`ComponentVisualizers.cpp`(뼈대 선), `CoreComponents.cpp`(등록 "Animation"), `EngineOptions` · `Engine.cpp`(`--fixed-dt`), CMake, `uidump.sh`.

**쓰는 법:** glTF 에 애니메이션이 든 스킨 모델을 `SkinnedMeshComponent` 로 놓고 같은 액터에 `AnimationComponent` 를 더합니다(디테일에서 클립 ·
반복 · 속도 · 루트 모션, Preview Time 으로 에디터에서 자세 확인). 게임 코드는 `play(name, fade)` · `playAdditive` · `addNotify` + `onNotify`,
또는 `hanim::StateMachine` 을 만들어 `setStateMachine` 으로 붙이고 파라미터를 넣습니다. 단순한 움직임은 `world.tweens().moveTo(actor, to, 초, 옵션)`.

## 5. 범위 밖 · 다음 후보

- GPU(컴퓨트) 스키닝, 화면 밖 스킨 메시의 스키닝 건너뛰기(쉬는 자세 경계 + 여유로 미리 컬링).
- 스킨이 없는 노드 애니메이션(문 · 기계 같은 강체 계층). 지금 로더가 노드 트랜스폼을 메시에 구워 넣기 때문에, 노드마다 메시를 나누는 일이 함께 필요합니다.
- 블렌드 공간(1D · 2D), 자세 압축, 리타기팅, IK 층(`math/Ik.hpp` 의 두 뼈 IK 를 자세 뒤에 적용), 애니메이션 그래프 편집기 · 타임라인.
- 여러 프리미티브가 한 뼈대 · 자세를 공유하는 캐릭터(지금은 프리미티브마다 컴포넌트).
