# 손 쥐기: 메시 모양에 맞춰 쥐는 손

> **문서 지위**: 2026-11-07 에 사용자가 요청한 "메시 모양에 따라 자연스럽게 쥐는 손"의 **조사 요약 · 결정 · 설계 정본**입니다.
> 다른 엔진 · SDK 를 조사해 보고했고(2절), 사용자가 권장안을 골랐습니다("권장안으로", 3절).
>
> **진행(2026-11-07)**: A 단계(스키닝 · 손 뼈대 · 시험 장갑)와 B 단계(컨트롤러 손가락)를 구현했습니다(9 · 10절).
> 둘 다 헤드셋 확인이 남았고(Editor_QA_Checklist.md 끝), C ~ E 는 아직입니다.
>
> **기준 시점**: 커밋 `2dcb424`(2026-11-06). 작업 트리에 있는 사운드 공간 음향 작업(미커밋)은 이 문서의 범위가 아닙니다.
> 지금의 잡기 · 던지기는 [Engine_Basics_Design.md](Engine_Basics_Design.md) 5-4절, 엔진 전체의 현재 상태는
> [Engine_Specification.md](Engine_Specification.md) 를 보십시오.

---

## 1. 배경

쇼케이스에는 손에 쥐는 작은 물체(광물 같은 19 ~ 26 cm 크기의 수집품)가 핵심 상호작용으로 들어갑니다. 지금 엔진의 잡기는 다음과 같습니다.

- 그립을 쥐는 순간 손(grip 자세)에서 반지름 0.12 m 안의 가장 가까운 Grabbable 을 고르고, **그 순간의 상대 위치 · 회전을 그대로
  유지**해 따라가게 합니다(`GrabInteractorComponent::grab`). 물체는 손바닥에 닿지 않고 잡은 자리에 떠 있습니다.
- 손은 디버그 축과 가리키는 선으로만 그립니다. **손 메시 · 뼈대 · 스키닝이 없습니다.**
- XR 입력은 트리거 · 그립 값(0 ~ 1)과 버튼 클릭뿐입니다. 손가락이 버튼에 닿았는지 알려 주는 정전식 터치 입력은 쓰지 않습니다.
  손 추적(`XR_EXT_hand_tracking`)도 켜지 않습니다.
- 물리는 Jolt 이고 강체 모양은 상자 · 구 · 캡슐뿐입니다(`EPhysicsShape`). 불규칙한 광물을 이 모양으로 감싸면 손가락이 실제 표면이
  아닌 충돌체 표면에 멈춰, 떠 있거나 파고들게 됩니다. 질의는 광선(`raycast`)과 구 겹침(`overlapSphere`)만 있습니다.

정점은 메가 버퍼 하나(`GpuScene::megaVertBuf_`, 정점 48바이트 = 위치 · 노멀 · UV · 탄젠트)에 모여 있고, 물체마다 변환 행렬만
`TransformBuffer`(set 2)로 넘깁니다. 깊이 프리패스(`depth.vert`) · 섀도(`shadow.vert`) · 색(`pbr.vert`)이 같은 정점을 읽으며,
`depth.vert` 와 `pbr.vert` 는 `invariant gl_Position` 으로 같은 깊이를 보장합니다.

---

## 2. 조사 요약 (2026-11-07)

| 방식 | 대표 사례 | 동작 | 판단 |
|---|---|---|---|
| ① 미리 만든 자세 + 붙는 면 | Meta Interaction SDK(HandGrabPose), SteamVR Skeleton Poser, VR Expansion Plugin(Hand Socket) | 물체마다 쥐는 자세를 저장합니다. 구 · 원통 · 상자 · 베지어 면을 지정하면 그 면 위 어디서든 자세가 맞춰집니다. | 품질이 가장 좋지만 물체마다 작업이 듭니다. 특수 물체용으로 받습니다. |
| ② 절차적 손가락 굽힘 | Auto Hand(Unity), UE 상용 플러그인 | 손바닥 기준점을 표면에 맞추고, 손가락 끝 반지름(범퍼)으로 굽히다가 닿으면 멈춥니다. | 아무 물체에나 바로 쓸 수 있습니다. 기본 방식으로 받습니다. |
| ③ 물리 손 | Boneworks · Bonelab(Marrow) | 손 · 손가락이 강체와 관절로 움직입니다. | 공개 문서가 없고, 불안정하며 Quest CPU 비용이 큽니다. 보류합니다. |
| ④ 실제 손 추적 | OpenXR 손 추적(26관절), Meta Capsense | 실제 손이나 컨트롤러 센서로 관절 자세를 받습니다. | 쥐는 모양은 여전히 ① · ② 가 정합니다. 뼈대 규약만 맞추고 나중에 붙입니다. |

참고한 세부 사항입니다.

- **Meta Interaction SDK**: 손가락마다 자세를 고정(Locked) · 범위 안에서만 움직임(Constrained) · 자유(Free)로 정합니다. 잡기 조건도
  손가락마다 필수 · 선택 · 무시로 정합니다. 손 크기가 다르면 크기별(예: 0.8 · 1.0 · 1.2배)로 만든 자세 사이를 보간합니다.
  잡을 때 물체가 기본 0.1초 동안 손으로 끌려옵니다(ease-in-out). 손이 다가오는 동안 손가락을 미리 오므리는 모드도 있습니다.
- **Auto Hand**: 원시 도형과 볼록 충돌체에서는 잘 되고, 복잡한 모양에는 자세를 따로 지정하라고 안내합니다. 굽힘 · 정지의 세부
  알고리즘은 공개 문서에 나오지 않습니다.
- **SteamVR Skeleton Poser**: 주 자세 하나에 보조 자세들을 가중치로 섞고, 손가락별 마스크로 일부 손가락에만 적용합니다.
- **Half-Life: Alyx**: 내부 방식은 공개되지 않았습니다. 인터뷰에 따르면 손바닥을 먼저 맞추고, 필요하면 화면 속 손을 당기거나
  늘려 잡기를 완성합니다.
- **OpenXR**: 관절은 26개입니다(`XrHandJointEXT`: 손바닥 0, 손목 1, 엄지 2 ~ 5, 검지 6 ~ 10, 중지 11 ~ 15, 약지 16 ~ 20, 소지 21 ~ 25).
  `XR_EXT_hand_tracking_data_source` 는 컨트롤러 센서로 합성한 관절을 줄 수 있고, `XR_FB_hand_tracking_mesh` 는 런타임이 스키닝된
  손 메시를 줍니다. Touch Pro 컨트롤러는 트리거 굽힘 · 밀기 · 근접(`/input/trigger/curl_fb` · `slide_fb` · `proximity_fb`) 입력이
  따로 있습니다.

출처: [Meta Hand Grab Interaction](https://developers.meta.com/horizon/documentation/unity/unity-isdk-hand-grab-interaction/),
[Meta Hand Grab Pose 만들기](https://developers.meta.com/horizon/documentation/unity/unity-isdk-creating-handgrab-poses/),
[Meta Capsense](https://developers.meta.com/horizon/documentation/unity/unity-capsense/),
[Auto Hand: Hand](https://earnest-robot.gitbook.io/auto-hand-docs/auto-hand/hand),
[Auto Hand: Finger Component](https://earnest-robot.gitbook.io/auto-hand-docs/auto-hand/hand/finger-component),
[SteamVR Skeleton Poser](https://valvesoftware.github.io/steamvr_unity_plugin/articles/Skeleton-Poser.html),
[VR Expansion Plugin: Hand Socket](https://vreue4.com/GeneratedDocs/VRExpansionPlugin/HandSocketComponent/HandSocketComponent.html),
[Kotaku: Half-Life: Alyx 손 인터뷰](https://kotaku.com/half-life-alyx-almost-had-enemies-who-reacted-when-you-1846941509),
[Khronos: XrHandJointEXT](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrHandJointEXT.html),
[Khronos: XR_EXT_hand_tracking_data_source](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_EXT_hand_tracking_data_source.html),
[Khronos: XrHandTrackingMeshFB](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrHandTrackingMeshFB.html),
[Khronos: XR_FB_touch_controller_pro](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_FB_touch_controller_pro.html).

---

## 3. 결정 (2026-11-07, 사용자 "권장안으로")

### 3-1. 받은 것

| 항목 | 결정 |
|---|---|
| 방식 | **혼합형**: 자동 맞춤(② 방식, 4-4절)을 기본으로 하고, 특수 물체만 미리 만든 자세(① 방식, 4-5절)로 덮어씁니다. |
| 누가 움직이는지 | 작은 물체는 **물체가 손으로 끌려옵니다**(Meta 의 기본 동작과 같음). 손은 실제 컨트롤러 자리를 지킵니다. |
| 손 모양 | **직접 만든 우주복 장갑**(왼손 · 오른손 각각). 런타임이 주는 Meta 손 메시는 쓰지 않습니다. 장갑이 두꺼워 손가락이 살짝 파고들어도 덜 보입니다. |
| 뼈대 규약 | **OpenXR 26관절**을 엔진 손 뼈대의 규약으로 씁니다. 나중에 손 추적을 붙일 때 관절을 그대로 대응시킵니다. |
| 진행 순서 | A → B → C → D 순서로 진행하고, 단계마다 확인한 뒤 다음으로 넘어갑니다. E 는 필요한 물체가 생길 때 합니다(4절). |

### 3-2. 미룬 것

| 항목 | 이유 |
|---|---|
| 물리 손(③) | 안정성과 Quest CPU 비용 부담이 큽니다. 무중력 게임과 어울리는 면이 있어 나중에 다시 봅니다. |
| 손 추적(④) | 뼈대 규약만 맞춰 둡니다. 컨트롤러로 먼저 완성합니다. |
| 손이 물체 쪽으로 붙는 잡기 | 사다리 · 손잡이처럼 고정된 물체에 필요합니다. 그런 물체가 생길 때 정합니다. |
| 두 손으로 잡기 · 손 크기별 자세 | 지금 쇼케이스 물체에는 필요하지 않습니다. |
| 다가오는 동안 손가락 미리 오므리기 | D 가 자리 잡은 뒤 헤드셋에서 필요하면 더합니다. |
| 오목한 물체용 볼록 분해(V-HACD · CoACD 같은 방식) | 광물은 대체로 볼록해서 볼록 껍질 하나로 충분합니다. |

---

## 4. 단계별 설계

### 4-1. A. 손 기반: 스키닝 · 손 뼈대 · 손 메시

**손 뼈대(`src/xr/HandSkeleton.hpp`, 새로)**

- 관절 번호는 `XrHandJointEXT` 와 같은 26개 열거형입니다. 부모 관계는 손목 → 각 손가락 metacarpal → proximal → (intermediate) →
  distal → tip 이고, 손바닥은 손목의 자식입니다. 엄지에는 intermediate 가 없습니다.
- 손 모델(glb)의 뼈 이름은 이 관절 이름과 같아야 합니다(예: `Wrist`, `IndexProximal`). 로더가 이름으로 대응시키고, 빠진 관절이
  있으면 경고합니다.
- 관절의 로컬 축 규약(2026-11-07 사양 확인): +Z 는 뼈를 따라 손가락 끝의 반대쪽, +Y 는 손등 바깥(손톱에 수직), X 는 오른손
  법칙(Y x Z)입니다. 손목은 +Z 가 손목에서 중지 metacarpal 의 반대쪽이고, 손바닥 관절은 중지 metacarpal 뼈의 가운데에 놓입니다.
  두 손에 같은 규칙이라 손가락을 손바닥 쪽으로 굽히는 것은 양손 모두 로컬 X 둘레의 음의 회전입니다. 손 모델은 쉬는 자세에서
  관절 노드의 로컬 축이 이 규약을 따라야 하고, `HandPoseComponent` 가 자식 관절이 로컬 -Z 쪽에 없는 관절을 찾아 경고합니다.

**로더 · 캐시**

- `GltfLoader` 가 glTF `skins`(관절 노드 · inverseBindMatrices), 정점의 `JOINTS_0` · `WEIGHTS_0`(영향 4개까지), 관절 노드의 쉬는
  자세(rest pose)를 읽어 `MeshSourceData` 에 담습니다. 영향 5개 이상(`JOINTS_1`)은 무게가 큰 4개만 남기고 다시 정규화(2026-11-20 부터 코드도 이대로, [Animation_System.md](Animation_System.md) A2-4)합니다.
- `GassetCache` 버전을 6에서 7로 올립니다(스킨 데이터 · 4-3절의 볼록 껍질 점을 함께 저장).
- `AssetManager` 는 지금 업로드 뒤 CPU 정점을 버립니다. 스킨이 있는 메시만 CPU 에 바인드 자세 정점 · 무게를 남깁니다(손 두 개라
  메모리는 1 MB 아래입니다).

**스키닝 위치: CPU 스키닝(권장, 4-1 끝의 표 참고)**

- 매 프레임 CPU 가 관절 행렬(관절 월드 행렬 × inverseBind)로 위치 · 노멀 · 탄젠트를 변환합니다.
- 메가 버퍼는 Zone B 에 있어 늘 호스트에 매핑되어 있고 HOST_COHERENT 입니다(`uploadMesh` 가 이미 `memcpy` 로 씁니다). 그래서
  **렌더러가 프레임 슬롯마다 스킨 정점 구간 하나**(`GpuScene::skinArena`, 슬롯마다 32,768 정점, 처음 쓸 때 한 번 예약)를 두고, 매 프레임
  그 슬롯의 구간에 보이는 스킨 메시를 차례로 CPU 스키닝해 직접 씁니다. 쓰는 자리는 `Renderer::buildDrawCommands` 로, 슬롯 펜스를
  기다린 뒤의 extract 구간입니다(코드에서 확인). 그래서 GPU 가 읽는 중인 구간을 덮지 않고, 제출 시점에 쓰기가 보이므로 복사 · 장벽 ·
  flush 가 필요 없습니다.
- 처음 설계는 스킨 메시 인스턴스마다 구간을 예약하는 것이었습니다. 메가 버퍼는 반납이 없는 범프 할당이라 Play 를 할 때마다 월드를
  다시 만들면 구간이 계속 새어 나가므로, 구현에서 렌더러 소유의 프레임별 구간 하나로 바꿨습니다. 넘치면 넘친 메시만 그리지 않고 한 번
  경고합니다.
- 그리기 목록은 그 구간 안의 자리를 정점 오프셋으로 그립니다. 그래서 `depth.vert` · `shadow.vert` · `pbr.vert` 를 바꾸지 않습니다.
  셰이더 변형이 늘지 않고 `invariant` 보장도 그대로입니다. 인덱스는 쉬는 자세로 올린 메시(`AssetPair::mesh`)의 것을 씁니다.
- 컬링용 경계 구는 스키닝한 정점의 AABB 로 매 프레임 다시 계산합니다.
- CPU 스키닝(`hskin::skinVertices`)은 `MeshSourceVertex` 로 쓰고, 렌더러는 같은 바이트를 `Vertex` 로 읽습니다. 두 구조체의 배치가
  같다는 것을 `GpuScene.cpp` 의 `static_assert` 가 지킵니다. 덕분에 스키닝 코드가 Vulkan 헤더 없이 시험됩니다.

| 선택지 | 장점 | 단점 | 판단 |
|---|---|---|---|
| CPU 스키닝 + 매핑된 메가 버퍼에 프레임별 구간 직접 쓰기 | 셰이더 · 패스 무변경, 복사 · 장벽 없음, 구현이 작음 | 스키닝 메시가 많아지면 CPU 비용이 늘고 예약 구간이 프레임 수 배로 듦 | **채택**: 손 두 개(합계 1만 정점 안팎)면 Quest 에서도 0.1 ms 아래로 예상 |
| 컴퓨트 스키닝(UE 의 스킨 캐시 · Godot 4 방식) | 캐릭터가 많아도 확장됨 | 하루카에 컴퓨트 파이프라인 기반이 없음(2026-10-04 GPU 컬링 삭제 뒤) | 캐릭터가 생기면 옮김. 메가 버퍼에 이미 STORAGE 용도가 있어 출력 자리는 그대로 씀 |
| 정점 셰이더 스키닝 | 전송이 없음 | 세 정점 셰이더의 스킨 변형 · 두 번째 정점 바인딩 필요, `invariant` 를 변형마다 지켜야 함 | 받지 않음 |

**컴포넌트**

- `SkinnedMeshComponent`(Layer 1, 새로): 모델 경로 · 메시 번호 · 재질과 관절 자세(관절별 로컬 회전)를 가집니다. 관절 행렬 계산과
  CPU 스키닝(`skinInto`)을 맡고, 렌더러가 매 프레임 부릅니다. 손이 아닌 스킨 모델에도 그대로 씁니다. `hasSkin()` 은 자세 계산이
  되는지, `loaded()` 는 GPU 메시까지 있어 그릴 수 있는지입니다. `StaticMeshComponent` 를 상속하지 않습니다(`getComponent` 는 정확한
  타입으로 찾고, 직렬화 · LOD 가 다름).
- `HandPoseComponent`(새로): 같은 액터의 `SkinnedMeshComponent` 관절을 OpenXR 이름으로 찾아, 손가락별 굽힘 값(0 ~ 1)을 관절 회전으로
  바꿉니다. 지금은 굽힘 값이 속성이고, 입력(4-2절) · 잡기 결과(4-4절)가 이 값을 정하게 됩니다. Play 중에는 매 tick, 편집 중에는 값을
  확정할 때와 씬을 읽을 때 적용합니다(같은 액터에서 `SkinnedMeshComponent` 뒤에 둠).
- `assets/prefabs/XrPlayer.hprefab`: 손 액터의 6 cm 구 메시와 0.06 배율을 빼고, 손마다 자식 액터 `LeftGlove` · `RightGlove`(두 컴포넌트)를
  달았습니다. 장갑을 자식 액터로 둔 것은 에디터 기즈모로 grip 과 손목 사이의 오프셋을 맞출 수 있게 하려는 것입니다.

**손 메시**

- 우주복 장갑을 Blender 에서 만들어 `assets/models/hands/` 에 왼손 · 오른손 glb 를 따로 둡니다. 왼손을 음수 배율로 뒤집지 않습니다
  (하루카는 음수 배율의 감김 순서 뒤집기를 지원하지 않습니다).
- 첫 시험용 장갑은 Blender 대신 생성 스크립트(`src/tools/make_test_gloves.py`)로 만들었습니다(`TestGlove_L.glb` · `TestGlove_R.glb`,
  손 하나에 정점 1,567 · 삼각형 2,880). 관절 축 · 무게를 계산으로 정확히 맞출 수 있고, 사용자의 Blender 장면을 건드리지 않으며, 다시
  만들 수 있기 때문입니다. 최종 아트는 쇼케이스 아트 작업에서 바꿉니다.
- 예산: 손 하나에 1만 삼각형 이하(Quest Pro 권장 75만 ~ 100만의 2% 이하), 텍스처 1024² 이하.
- grip 자세 원점과 장갑 손목 사이의 오프셋은 장갑 자식 액터의 로컬 트랜스폼입니다. 기본값은 OpenXR grip 규약에서 구했습니다.
  grip 의 +X 는 손바닥에 수직이고(오른손은 손바닥으로 들어가는 쪽, 왼손은 손바닥에서 나오는 쪽이라, 악수하듯 두 손을 들면 둘 다
  몸의 오른쪽을 향함),
  -Z 는 쥔 손가락이 만든 관을 지나 새끼손가락에서 엄지 쪽, +Y = Z x X 입니다. 그래서 장갑 축은 grip 기준으로 오른손 X_hand = +Z,
  Y_hand = +X, Z_hand = +Y, 왼손 X_hand = -Z, Y_hand = -X, Z_hand = +Y 이고(손가락은 grip -Y), 손목은 grip 에서 손목 쪽 6 cm ·
  손등 쪽 3.5 cm 입니다(오른손 위치 (0.035, 0.06, 0) · `ROT:-180 -90 90`, 왼손 (-0.035, 0.06, 0) · `ROT:-180 90 -90`). 이 회전은
  glm 오일러로 짐벌 잠금 각도라 `QUAT` 와 비트까지 같은 `ROT` 를 쓰기 어려워, glm 식으로 같은 회전이 나오는 `ROT` 를 적었습니다.
  실제 손과 화면 속 손이 어긋나 보이는 것이 이 기능에서 가장 눈에 띄는 결함이 되기 쉬우므로 헤드셋에서 맞춥니다.

### 4-2. B. 컨트롤러로 손가락 움직이기

- 손가락마다 굽힘 값 0(편 손) ~ 1(쥔 손)을 둡니다. 관절마다 최대 굽힘 각을 정하고(검지 · 중지 · 약지 · 소지는 MCP · PIP · DIP 세
  관절, 엄지는 굽힘과 맞섬 두 축), 굽힘 값만큼 각 관절을 굽힘 축으로 돌립니다. 최대 각은 손 모델 설정 값으로 두고 헤드셋에서 맞춥니다.
- 입력과 손가락의 대응입니다.

| 손가락 | 입력 | 규칙 |
|---|---|---|
| 검지 | 트리거 값 · 트리거 터치 | 닿지 않으면 0(가리키기), 닿으면 0.3 + 0.7 × 트리거 값 |
| 중지 · 약지 · 소지 | 그립 값 | 그립 값 그대로 |
| 엄지 | 엄지 터치(스틱 · A · B · X · Y · 엄지 받침) | 닿으면 내림(0.6), 아니면 들기(0) |

- 값은 50 ms 안팎으로 부드럽게 따라가게 합니다(갑자기 튀지 않게).
- `HHandInput` 에 `triggerTouch` · `thumbTouch` 를 더하고, `XrSystem` 이 Touch 컨트롤러 프로필에 `/input/trigger/touch` ·
  `/input/thumbstick/touch` · `/input/a/touch` · `/input/b/touch` · `/input/x/touch` · `/input/y/touch` · `/input/thumbrest/touch`
  를 묶습니다. 간단 컨트롤러 프로필(`khr/simple_controller`)에는 터치가 없으므로 기본값(검지는 트리거 값, 엄지는 들기)을 씁니다.
- Touch Pro 의 트리거 굽힘 · 밀기 · 근접 입력은 B 가 자리 잡은 뒤 검지 표현을 다듬을 때 씁니다.

### 4-3. C. 볼록 껍질 충돌체

- `EPhysicsShape::ConvexHull`(= 3)을 더합니다. 모양의 출처는 같은 액터의 `StaticMeshComponent` 모델(그 컴포넌트가 그리는 메시
  번호)입니다.
- 모델을 읽을 때 정점 위치로 볼록 껍질 점을 미리 구해(최대 256점) `AssetPair` 에 두고 캐시에도 저장합니다(4-1절의 버전 7).
- 바디를 만들 때 트랜스폼 배율을 점에 미리 곱해 Jolt `ConvexHullShapeSettings` 로 만듭니다. 그래서 상자와 달리 비균등 배율도 됩니다.
  배율이 바뀌면 지금의 상자 · 구처럼 다시 만듭니다.
- 쥐기 질의(4-4절)를 위해 `PhysicsSystem` 에 **바디 하나만 대상으로 하는 질의**를 더합니다. 하나는 광선 판정이고, 다른 하나는 캡슐이
  겹치는지와 침투 깊이를 돌려주는 판정입니다. 둘 다 바디의 셰이프를 잠가 Jolt 의 셰이프 대 셰이프 판정을 직접 부릅니다.
- 덤으로 광물이 탁자 위를 상자처럼 미끄러지지 않고 모양대로 구르게 됩니다.

### 4-4. D. 자동 맞춤

잡는 순간 한 번 계산합니다. 계산은 GPU 없이 시험할 수 있게 순수 함수(`src/xr/GraspSolver.hpp/.cpp`, 새로)로 둡니다.

1. **대상 고르기**: 지금의 `pick` 을 쓰되, 기준점을 grip 원점이 아닌 손바닥 중심(손 모델의 Palm 관절)으로 바꿉니다.
2. **손바닥 맞추기**: 손바닥 중심에서 손바닥 노멀 방향과 물체 중심 방향으로 광선을 쏴, 손바닥에 가장 가까운 표면점과 그 노멀을
   얻습니다. 물체를 손바닥 노멀 방향으로 옮겨 그 표면점이 손바닥 두께만큼 떨어진 자리에 오게 합니다. 첫 구현은 **이동만** 하고
   회전은 잡은 순간의 상대 회전을 지킵니다. 표면 노멀에 맞춰 돌리는 정렬은 헤드셋에서 필요하면 옵션으로 더합니다.
3. **손가락 감싸기**: 손가락마다 굽힘 값을 0에서 1까지 16단계로 늘리며, 마디마다 캡슐(관절 사이, 반지름은 장갑 두께 포함)이 물체와
   겹치는지 봅니다. 처음 닿은 단계에서 이분 탐색 3번으로 다듬습니다. 닿은 마디보다 손바닥 쪽 관절은 그 자리에 멈추고, 끝 쪽 관절만
   계속 굽혀 닿거나 최대 각에 이를 때까지 감쌉니다. 허용 침투는 2 mm 입니다.
4. **잡기 조건**: 엄지와 다른 손가락 하나가 닿거나 손바닥이 닿으면 맞춤 잡기로 봅니다. 아무것도 닿지 않으면(멀거나 너무 큰 물체)
   지금처럼 상대 위치를 유지하는 잡기로 돌아갑니다. 잡기가 실패하는 일은 없게 합니다.
5. **전환**: 물체의 이동과 손가락 자세를 0.1초 동안 ease-in-out 으로 함께 옮깁니다(Meta 기본값과 같음). 잡은 동안 손가락은 맞춘
   자세를 지키고, 그립을 놓으면 입력 자세(4-2절)로 0.1초 동안 돌아갑니다. 던지기는 지금 그대로입니다.
6. **비용**: 손가락 5개 × 마디 3개 × (16 + 3)단계 = 잡을 때 약 300번의 셰이프 판정이며, 잡는 순간에만 합니다.

### 4-5. E. 지정 자세 (필요한 물체가 생길 때)

- `GrabPoseComponent`(새로)를 잡을 수 있는 액터나 그 자식에 붙여 손목 자세(물체 기준), 관절별 회전, 손가락별 자유도(고정 · 자동
  감싸기), 붙는 면(점 · 구 · 원통 · 상자)을 저장합니다.
- 지정 자세가 있으면 D 대신 쓰고, 자유도가 "자동 감싸기"인 손가락만 D 의 3단계를 그 자세에서 이어 합니다. 자세가 여럿이면 손과의
  거리 · 회전 차이로 고릅니다(Meta 의 점수 방식).
- 만들기: Play 중 손 자세를 저장하는 에디터 기능과 숫자 편집. 왼손 자세는 관절 회전을 거울 변환한 계산으로 만들고, 음수 배율은 쓰지
  않습니다.

---

## 5. 수정 범위

| 영역 | 파일 | 변경 |
|---|---|---|
| 에셋 | `src/assets/MeshSource.hpp`, `GltfLoader.cpp`, `GassetCache.cpp`, `AssetManager.cpp` | 스킨 데이터 · 볼록 껍질 점, 캐시 버전 7, 스킨 메시의 CPU 정점 보관 |
| 렌더러 | `src/renderer/GpuScene.*`, `Renderer_render.cpp`, `MeshHandle.hpp` | 스킨 메시의 프레임별 메가 버퍼 구간 예약, 그리기 목록의 프레임 구간 선택, 프레임마다 경계 구 갱신 |
| 컴포넌트 | `SkinnedMeshComponent`(새로), `HandPoseComponent`(새로), `GrabPoseComponent`(E, 새로), `XrComponents.*`, `RigidBodyComponent.*` | 손 자세 · 잡기 결과 연결, 볼록 껍질 모양 |
| 물리 | `src/physics/PhysicsSystem.*` | 볼록 껍질 셰이프, 바디 하나 대상 광선 · 캡슐 판정 |
| 입력 | `src/input/HXrInput.hpp`, `src/xr/XrSystem.cpp` | 트리거 · 엄지 터치 |
| 새 모듈 | `src/xr/HandSkeleton.hpp`, `src/xr/GraspSolver.*` | 26관절 규약 · 굽힘 계산 · 자동 맞춤 |
| 에셋 파일 | `assets/models/hands/`, `assets/prefabs/XrPlayer.hprefab` | 장갑 glb 두 개, 리그 손에 컴포넌트 추가 |

`PhysicsSystem.*` · `CoreComponents.cpp` · `CMakeLists.txt` 는 작업 트리의 사운드 작업(미커밋)도 고친 파일입니다. 두 작업이 한 커밋에
섞이지 않도록, 그 작업이 커밋된 뒤에 C 단계를 시작합니다.

---

## 6. 검증 계획

| 종류 | 내용 |
|---|---|
| 단위 테스트(새 `HarukaHandTest`) | 합성 glb 의 스킨 읽기, 관절 이름 대응 · 빠진 관절 경고, 바인드 자세 = 원래 정점, 관절 하나를 돌린 스키닝 값, 입력 → 굽힘 값 대응(터치 유무), 굽힘 0 · 1 의 끝 관절 위치 |
| 단위 테스트(물리) | 볼록 껍질 생성 · 비균등 배율, 바디 하나 대상 광선 · 캡슐 판정 |
| 단위 테스트(자동 맞춤) | 구 · 상자 · 캡슐 · 볼록 껍질에서 닿은 손가락 수와 침투 2 mm 이하, 너무 먼 물체 → 상대 위치 잡기로 돌아감, 얇은 판, 같은 입력에 같은 결과 |
| 창 없는 실행 | 손이 보이는 시험 씬을 `--synthetic-eyes` 로 찍어 손 메시가 그려지는지 확인. 기준 PNG 씬에는 손이 없으므로 기준 PNG 는 바뀌지 않아야 합니다. |
| 헤드셋(Quest Link) | 화면 속 손과 실제 손의 어긋남, 손가락 반응(트리거 · 그립 · 엄지), 광물 3종 쥐기 · 놓기 · 던지기, 프레임 시간 |
| Quest APK | 손 두 개 스키닝 · 잡기 계산의 프레임 시간 |

---

## 7. 비용과 위험

- **비용(예상)**: 손 2 × 1만 삼각형, CPU 스키닝 0.1 ms 아래, 매핑된 메모리 쓰기 프레임당 약 0.5 MB, 자동 맞춤은 잡는 순간에만 계산합니다.
  측정값은 구현 뒤 이 문서에 채웁니다.
- **어긋남**: grip 자세와 장갑 손목의 오프셋이 맞지 않으면 손이 실제와 다른 자리에 보입니다. 헤드셋에서 손마다 맞추는 단계를 따로 둡니다.
- **엄지**: 굽힘 축 하나로는 맞섬 동작이 어색합니다. 엄지에만 축 두 개를 두고, 그래도 어색하면 E 의 지정 자세로 덮습니다.
- **오목한 물체**: 볼록 껍질이 오목한 곳을 메워 손가락이 떠 보일 수 있습니다. 쇼케이스 광물을 만들 때 볼록에 가깝게 만들거나,
  필요하면 볼록 분해를 다시 검토합니다.
- **벽 통과**: 잡은 물체는 지금처럼 Kinematic 이라 벽을 뚫습니다(Engine_Basics_Design 2-3 의 "속도 추적 잡기" 미룸과 같은 문제).

---

## 8. 다음 단계

1. ~~A: 손 뼈대 규약 확인(관절 축) → 로더 스킨 읽기 · 캐시 7 → CPU 스키닝 · 프레임별 구간 쓰기 → 시험 장갑 → 창 없는 실행 확인~~
   (2026-11-07 구현, 9절. 헤드셋 확인은 B 와 함께)
2. ~~B: 터치 입력 · 손가락 굽힘~~(2026-11-07 구현, 10절) → 헤드셋에서 오프셋 · 최대 각 맞추기(대기)
3. C: 볼록 껍질 · 바디 하나 대상 질의(사운드 작업 커밋 뒤)
4. D: 자동 맞춤 → 헤드셋에서 광물 쥐기 확인
5. E: 필요한 물체가 생길 때

---

## 9. A 단계 구현 결과 (2026-11-07)

### 9-1. 바뀐 파일

| 영역 | 파일 | 내용 |
|---|---|---|
| 손 뼈대 | `src/xr/HandSkeleton.hpp`(새로) | 26관절 열거형 · 이름 · 엔진이 정한 부모 · 손가락별 굽힘 관절 · 기본 최대 각 |
| 스킨 | `src/assets/Skin.hpp/.cpp`(새로) | `SkinSourceData`(관절 · 정점별 관절 번호 · 무게 · 평가 순서), 평가 순서 · 전역 행렬 · 스키닝 행렬 · CPU 스키닝 |
| 로더 · 캐시 | `GltfLoader.cpp`, `MeshSource.hpp`, `GassetCache.cpp` | glTF skin · JOINTS_0 · WEIGHTS_0(정수 · 정규화 정수 · 실수) 읽기, 스킨 메시는 노드 트랜스폼을 굽지 않음, 캐시 버전 7 |
| 에셋 | `AssetManager.*` | 스킨 메시는 쉬는 자세로 스키닝해 올리고 바인드 정점 · 스킨을 `AssetPair` 에 남김 |
| 렌더러 | `GpuScene.*`, `Renderer_render.cpp`, `Renderer.hpp` | 프레임 슬롯별 스킨 정점 구간, `buildDrawCommands` 에서 스키닝 · 후보 추가(스태틱 · 스킨 메시가 같은 후보 함수를 씀) |
| 컴포넌트 | `SkinnedMeshComponent.*` · `HandPoseComponent.*`(새로), `CoreComponents.cpp` | 두 컴포넌트와 등록(에디터 이름 "Skinned Mesh" · "Hand Pose") |
| 에셋 파일 | `assets/models/hands/TestGlove_L.glb` · `_R.glb`, `src/tools/make_test_gloves.py` | 시험 장갑과 생성 스크립트 |
| 씬 · 프리팹 | `assets/scenes/HandProbe.hscene`(새로), `assets/prefabs/XrPlayer.hprefab` | 손 확인 씬(편 손 · 주먹 · 가리키기를 옆에서 봄), 플레이어 손을 장갑으로 |
| 시험 · 빌드 | `tests/HandTest.cpp`(새로), `CMakeLists.txt` | `HarukaHandTest`(ctest `hand_skinning`), 캐시 · glTF 시험에 `Skin.cpp` 추가 |

`XrDemo.hscene` · `LightingProbe.hscene` 은 리그를 씬 안에 복사해 두고 있어 프리팹을 바꿔도 따라 바뀌지 않습니다. `XrDemo` 는
B 단계에서 헤드셋 확인용으로 장갑으로 바꿨고(10절), 기준 PNG 를 찍는 `LightingProbe` 는 그대로 구 손이라 기준 PNG 는 바뀌지 않습니다.

### 9-2. 확인

| 종류 | 결과 |
|---|---|
| 장갑 파일 검산(스크립트) | 두 손 모두 관절 전역 행렬 x inverseBind 오차 3 x 10⁻⁸, 축 규약 위반 없음, 무게 합 1, 검지 뿌리 마디 -90° 에서 끝이 손바닥 쪽으로 감 |
| `HarukaHandTest` | 73개 통과: 뼈대 표 7, 스키닝 수식 22(관절 순서를 바꾼 경우 포함), 장갑 읽기 18(두 손), 캐시 7 왕복 4, 손 자세 22(두 손, 축 위반 감지 포함) |
| 전체 시험(ctest) | 25개 모두 통과(Release) |
| 창 없는 실행 | `HandProbe.hscene`: 120/120 프레임, 검증 오류 0, 편 손 · 주먹 · 가리키기 모양과 그림자가 의도대로. 프리팹 리그를 담은 임시 씬: 장갑이 손 액터에 붙고 grip 기본 자세(악수 자세)대로 보임 |

### 9-3. 남은 것

- 헤드셋에서 장갑의 자리 · 방향(grip 오프셋) 확인은 B 와 함께 합니다.
- 캐시 버전이 7 로 올라 모든 모델의 캐시가 첫 로드 때 한 번 다시 만들어집니다.
- `Roadmap.md` · `Engine_Specification.md` 의 반영은 사운드 작업이 커밋된 뒤에 합니다(같은 파일을 그 작업이 고치는 중).

---

## 10. B 단계 구현 결과 (2026-11-07)

### 10-1. 바뀐 것

| 영역 | 파일 | 내용 |
|---|---|---|
| 입력 상태 | `src/input/HXrInput.hpp` | `HHandInput` 에 `hasTouch` · `triggerTouch` · `thumbTouch` |
| OpenXR | `src/xr/XrSystem.cpp` | 불리언 액션 `trigger_touch` · `thumb_touch`. Touch 컨트롤러 프로필에 트리거 터치와 엄지 터치(스틱 · X/Y 또는 A/B · 엄지 받침, 한 액션에 여러 경로라 하나라도 닿으면 참)를 묶음. 이 제안이 거부되면 터치 없이 다시 제안(경고 "Retrying Touch controller bindings ...") |
| 대응 규칙 | `src/xr/HandInputMap.hpp`(새로) | `curlTargets`(4-2절 표) · `smoothCurl`(지수 감쇠, 시간 상수 30 ms = 약 50 ms 에 80%). 트리거를 당기는 중에는 터치 신호가 빠져도 닿은 것으로 봄 |
| 컴포넌트 | `HandPoseComponent.*` | 속성 `Hand`(None · Left · Right, 저장 키 `hand`). Left · Right 이면 Play 중 그 손의 입력으로 굽힘 값을 정하고, 입력이 없으면(데스크톱 · 세션이 입력을 받지 않음) 값을 지킴 |
| 프리팹 · 씬 | `XrPlayer.hprefab`, `XrDemo.hscene` | 장갑에 `hand=left` · `hand=right`. `XrDemo` 의 손을 프리팹과 같은 장갑으로 바꿈(헤드셋 확인용, 머리말에 적음) |
| 시험 | `tests/HandTest.cpp`, `CMakeLists.txt` | 6절 입력 대응 11개 · 7절 컴포넌트 입력 5개, 시험에 `HInputSystem.cpp` 추가 |

어느 손인지는 부모의 `TrackedPoseComponent` 를 찾지 않고 속성으로 명시했습니다. 찾게 하면 손 컴포넌트가 XR 컴포넌트 전체(물리 포함)에
의존해 시험 대상이 커지고, 장갑을 리그 밖에 둔 씬(손 확인 씬)에서는 어차피 지정해야 하기 때문입니다.

### 10-2. 확인

| 종류 | 결과 |
|---|---|
| `HarukaHandTest` | 89개 통과(A 73 + B 16) |
| 전체 시험(ctest) | 25개 모두 통과(Release) |
| 창 없는 실행 | `XrDemo.hscene`: 13 액터, 장갑 두 개를 스키닝해 그림, 검증 오류 0(데스크톱에서는 손이 화면 밖) |
| 헤드셋 | 통과(2026-11-16, Quest Pro · Air Link, 8항목 모두 정상, Touch 바인딩 재시도 경고 0 · 검증 0). 항목은 Editor_QA_Checklist.md "헤드셋 확인 (2026-11-07 손 쥐기 A · B 단계)" |

Touch Pro 의 트리거 굽힘 · 밀기 · 근접(`curl_fb` · `slide_fb` · `proximity_fb`)은 아직 쓰지 않습니다. 헤드셋에서 검지 표현이 부족하면 더합니다.

## 11. C · D 단계 구현 결과 (2026-11-17)

- **C 볼록 껍질 질의**: `PhysicsSystem::raycastBody`(바디 하나의 셰이프에만 광선) · `capsulePenetration`(손가락 마디 캡슐과 바디의
  최대 침투 깊이). 다른 바디는 보지 않으므로 잡은 물체에만 닿는지 봅니다.
- **D 자동 맞춤**: `xr/GraspSolver`(순수 함수 `hgrasp::solve`, 손가락마다 굽힘 16단계 + 이분 3번, 손바닥이 닿고 엄지와 다른 손가락
  하나 이상이 닿아야 성공), `HandPoseComponent::setGrasp` · `clearGrasp`(0.1 초 inOutCubic 섞기), `GrabInteractorComponent.autoFit`
  (기본 켬): 잡는 순간 손바닥 노멀 방향으로 표면을 찾아(12 cm) 물체를 손바닥에 맞추고(최대 8 cm) 손가락을 물체 모양대로 감쌉니다.
  맞추지 못하면 예전처럼 상대 자세를 지키는 잡기입니다.
- **확인**: `hand_skinning` 89 → 108(장갑 + Jolt: 공을 2 cm 당김, 다섯 손가락 닿음, 침투 1.6 mm). 헤드셋 확인은 남았습니다.
