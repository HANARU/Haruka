# Haruka Engine 계층 아키텍처 — 코어와 도메인 레이어의 경계

> **문서 지위**: 이 문서는 Haruka Engine의 정체성과 계층 경계를 정의하는 **기준 문서**입니다.
> 다른 문서가 이 문서와 충돌하면 이 문서를 따릅니다.
> 작성 2026-08-24 · 근거는 `src/` 코드 실측과 [Carbon_Source_Audit.md](archive/Carbon_Source_Audit.md)입니다.
>
> **[갱신 · 2026-10-04]** §3 판정표와 §4 · §6 의 상태를 2026-10-04 의 두 작업(남은 결함 정리, VR 첫걸음)에 맞췄습니다. 바뀐 항목은 OpenXR 연결 · 두 눈 렌더 · 두 눈 컬링 · 카메라 자세 · VR 입력 · 물리 API · 숫자 칸 단위 · 셰이더 빌드입니다.
>
> **[갱신 · 2026-10-14]** §4 의 지점 표기를 줄 번호 대신 파일 · 함수 · 기호 이름으로 바꾸고, 각 줄을 지금 코드와 다시 대조했습니다. §6 0단계의 라이팅 확인 씬을 완료로 고치고, §3 의 Android 행을 `DEF-103` 에 맞췄습니다.

---

## 1. 엔진의 목적

**Haruka는 Vulkan 기반 VR 엔진입니다.** 이것이 이 프로젝트의 정체성이고, 완성해야 할 산출물입니다.

우주 및 무중력 기능은 그 코어 위에 얹는 **도메인 레이어**입니다. 첫 번째 응용이자 코어의 성능과 완성도를 실증하는 수단이지, 엔진의 정의는 아닙니다.

이 구분이 실무에서 갖는 의미는 다음과 같습니다.

- 코어(Layer 1)는 우주 프로젝트를 걷어내도 **그 자체로 쓸 수 있는 범용 Vulkan VR 엔진**이어야 합니다.
- 어떤 기능이 "우주에서만 쓸모 있는가"를 물어서 계층을 정합니다. 답이 "아니오"라면 그것은 코어입니다.
- 우주 도메인이 코어를 고쳐야만 동작한다면, 그것은 코어의 확장 지점이 부족하다는 신호입니다. 도메인 쪽에서 우회할 문제가 아닙니다.

```
+---------------------------------------------------------------+
|  [Layer 2] 우주 / 무중력 도메인 프레임워크                     |
|  ZeroGController · DebrisField · 생존 규칙 · 우주 비주얼        |
+---------------------------------------------------------------+
                          | 단방향 의존
                          v
+---------------------------------------------------------------+
|  [Layer 1] Haruka Core — 범용 Vulkan VR 엔진                   |
|  Vulkan 1.4 렌더러 · OpenXR/Multiview · 1GB Core Heap ·         |
|  World-Actor-Component · 물리 · 오디오 · HSlate · 에디터        |
+---------------------------------------------------------------+
```

---

## 2. 계층 판정 기준

어떤 기능을 어느 계층에 둘지는 아래 세 질문으로 정합니다.

1. **우주가 아닌 VR 프로젝트에서도 쓰이는가.** 쓰인다면 Layer 1입니다.
2. **엔진의 자료 구조나 셰이더 계약을 바꾸는가.** 바꾼다면 이름이 무엇이든 Layer 1입니다.
3. **정책인가 구현인가.** 정책(깊이 규약, 좌표 정밀도, 보이스 예산)은 Layer 1이 소유하고, 그 정책을 소비하는 구체적 연출과 규칙은 Layer 2입니다.

두 번째 질문이 특히 중요합니다. Reverse-Z, Floating Origin, 정점 패킹은 우주 프로젝트가 필요로 해서 논의가 시작되었지만, 셋 다 코어의 계약을 바꾸므로 **Layer 1의 범용 기능으로 흡수하는 것이 맞습니다.** 우주 전용 기능으로 취급하면 코어가 우주 전용으로 오염되어 재사용 목표가 무너집니다.

### 계층과 무관한 전역 규약: 1 unit = 1 meter

**Haruka의 길이 단위는 1 unit = 1 meter이고, 질량 단위는 1 unit = 1 kilogram입니다.** 이것은 계층 판정의 대상이 아니라 `Threading.md`의 스레드 소유권 규칙과 마찬가지로 **계층과 무관하게 전역 적용되는 규약입니다.** Layer 2가 이 규약을 다시 정하거나 국소적으로 뒤집을 수 없습니다.

사용자가 2026-08-25에 1 cm 단위 안을 검토한 뒤 **미터 유지로 확정했습니다.** 근거는 OpenXR이 정의상 미터라는 점, Jolt의 허용 오차가 전부 미터 기준 절대값이라는 점, VR의 크기 항상성이 헤드셋이 보고하는 실제 IPD에 묶여 있다는 점, 그리고 부동소수점 정밀도가 스케일 불변이라 단위를 바꿔도 얻을 이득이 없다는 점입니다. 상세한 근거와 기각 논증은 [Primitive_And_Lighting_Design.md](Primitive_And_Lighting_Design.md) 3-5절에 있습니다.

내부 저장은 예외 없이 미터이며, 단위 접미사는 에디터의 표시와 입력에만 붙습니다. 그 에디터 쪽 작업이 아래 §3 판정표의 "에디터 단위 표시와 적응형 스텝"입니다.

---

## 3. 계층별 소속 판정표

### Layer 1 — 코어 (범용 Vulkan VR 엔진)

| 항목 | 현재 상태 | 비고 |
|---|---|---|
| Vulkan 1.4 렌더러, Forward 파이프라인, 패스 구조 | 구현됨 | 리팩토링 7단계 완료 |
| 1GB Core Heap 할당기 (Zone A/B/C) | 구현됨 | Zone C 펜스 추적 미비 |
| World / Actor / Component | 구현됨 | |
| HSlate UI + 에디터 | 구현됨 | |
| **OpenXR 세션 + 스테레오 스왑체인** | **구현 · 2026-10-04**(PC, 헤드셋 확인 전) | VR 엔진의 핵심입니다. `src/xr/XrSystem` 이 렌더러보다 먼저 OpenXR 인스턴스를 만들고, 런타임이 고른 GPU 로 Vulkan 장치를 만듭니다(XR_KHR_vulkan_enable2). 세션, LOCAL 원점 공간(원점 = 활성 카메라의 위치 + 요), 두 층 배열 스왑체인 하나를 두고, 렌더러에는 `Renderer::attachXrSwapchain` 으로 이미지를 붙입니다. `--xr` 로 켜며 런타임이 없으면 데스크톱으로 계속합니다. Quest 단독 실행은 Android 진입점과 함께 합니다 |
| **Multiview 스테레오 렌더링** | **첫 단계 구현 · 2026-10-04** (`DEF-09`) | 지금은 눈마다 두 번 그립니다. 셰이더는 `FrameUBO` 의 눈별 `viewProj[2]` · `eyePos[2]` 를 눈 번호(`HARUKA_VIEW_INDEX`, 푸시 상수)로 고르므로, 멀티뷰로 갈 때는 그 번호를 `gl_ViewIndex` 로 바꾸고 `viewMask` · 배열 층을 켭니다(Quest 이식 때). 쓰지 않던 멀티뷰 기능 요청은 지웠습니다 |
| **스테레오 대응 컬링 (두 눈 절두체)** | **해결 · 2026-10-04** (`DEF-31`, 원래 HIGH) | 눈마다 절두체를 판정해 하나라도 통과하면 그립니다(`FrustumCull.hpp` 의 `sphereInAnyView`). 한쪽 눈에서만 물체가 사라지면 양안 경합이 되므로 VR 엔진의 필수 요소이며 도메인과 무관합니다. Quest 2·Pro·3 의 두 눈 시야는 평행하고 비대칭입니다(기울어진 canted 화면이 아님, 2026-10-04 정정). 상세는 [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) 9-1절 |
| **재투영 여유분만큼의 절두체 부풀리기** | 보류 · 2026-10-04 (`DEF-32`, 원래 MEDIUM) | 한 프레임에 눈 자세를 한 번 받아 컬링 · 렌더 · 제출에 함께 쓰는 동안에는, 런타임이 재투영해도 렌더한 자세가 기준이라 여유분이 필요 없습니다. 늦은 자세 갱신이나 과주사를 넣을 때 다시 엽니다. 상세는 [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) 9-2절 |
| **Foveated Rendering (FFR)** | **미구현** | 선언만 있던 `Renderer.hpp` 의 멤버는 2026-10-04 에 지웠습니다. OpenXR 에서는 런타임이 밀도 맵 이미지를 주는 구조라 그 멤버와 모양이 맞지 않았습니다. Quest 이식 때 새로 둡니다 |
| **VR 입력 (아날로그 축 + 6DOF 포즈)** | **구현 · 2026-10-04**(헤드셋 확인 전) | XR 모듈(`src/xr`)이 OpenXR 액션으로 만들고 `HInputSystem` 이 아날로그 축 · 버튼 · 자세로 내줌(DEF-20). 창(`IWindow`)은 키보드 · 마우스만 |
| **6DOF 카메라 자세 (쿼터니언)** | **구현 · 2026-10-04** (`DEF-21`) | `CameraComponent::viewMatrix` 가 액터의 월드 회전(쿼터니언)과 월드 위치로 뷰를 만듭니다. yaw · pitch 는 에디터 카메라만 갖습니다. 눈별 투영은 OpenXR 시야각으로 만들고(`projFromFov`), 헤드셋 자세는 카메라의 XR 원점(`xrOrigin`: 위치 + 요) 위에 올립니다 |
| **물리 힘·임펄스·속도·레이캐스트 API** | **구현 · 2026-10-04** (`DEF-19`) | 중력 세터, 힘 · 토크 · 충격량 · 선속도 · 각속도, 레이캐스트, 구 겹침을 `PhysicsSystem` 과 `RigidBodyComponent` 에 두었습니다. 규칙은 UE 식입니다(힘은 프레임 충격량을 그 프레임의 스텝들에 나눠 넣고, 게임 코드가 Dynamic 액터를 옮기면 순간 이동하며 속도를 유지) |
| **감쇠 운동 적분기 (dt 독립)** | 미구현 | Carbon `destiny`에서 도입. 아래 §5 참조 |
| **깊이 규약 정책 (Reverse-Z 포함)** | 미구현 | 전역 정책으로. 아래 §5 참조. 깊이 범위(0 ~ 1) 매크로는 2026-10-04 에 CMake 한 곳으로 모았습니다(`DEF-95`) |
| **정점 패킹 (QTangent)** | 미구현 | 48B → 24B. Carbon `mesh`에서 도입 |
| **인스턴싱 (instanceCount)** | **미구현** | 여섯 곳에 `1`이 리터럴로 박혀 있음(2026-10-14 확인, §4 참조) |
| **패스 확장 지점** | **미구현** | 패스 레지스트리 없음 |
| **에디터 단위 표시와 적응형 스텝** | **해결 · 2026-10-04** (`DEF-46`, 원래 MEDIUM) | 숫자 칸이 단위 종류(`PropUnit` · `ENumberUnit`)에 따라 끌기 감도를 정합니다(거리 1cm/px, 크기는 시작값의 1%/px, 각도 0.5°/px). UE 식으로 Shift 는 10배, Ctrl 은 0.1배이고, 표시는 끝의 0 을 지운 고정 소수점이라 1 mm 미만도 보입니다. 예전에는 `PropDesc::step` 이 `0.01f` 로 고정되어 1 cm 미만을 편집할 수 없었습니다. 범용 에디터 기능이며 도메인과 무관합니다. 단위 규약 자체는 §2의 전역 규약이고 여기는 표시와 입력 계층입니다. 상세는 [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md)와 [Primitive_And_Lighting_Design.md](Primitive_And_Lighting_Design.md) 7-7절 |
| **머티리얼 편집 (엔진 내 머티리얼 속성 조정)** | **미구현** | 사용자 요청 항목입니다. `MaterialHandle`이 `TexSlot` 다섯 개만 가지고 **스칼라 팩터를 보관하지 않으며**, 팩터는 `GltfLoader`가 로드 시점에 픽셀로 굽기 때문에 이후 조정할 수 없습니다. `StaticMeshComponent`도 `MaterialHandle*`만 들고 있어 에디터 노출이 없고 `SceneSerializer`에도 저장되지 않습니다. **`DEF-47`의 구현 방식(팩터 굽기) 재검토가 선행됩니다.** 범용 에디터 기능이며 도메인과 무관합니다. 상세는 [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) 11-15절, [Core_Stabilization_Spec.md](Core_Stabilization_Spec.md) 2-F절, [Primitive_And_Lighting_Design.md](Primitive_And_Lighting_Design.md) 8-11절 |
| **배정밀도 월드 좌표 옵션** | 미구현 | 대규모 씬 일반의 문제이지 우주 전용이 아님 |
| **오디오 보이스 예산** | 미구현 | Carbon `audio`에서 도입 |
| **프레임 예산 지연 작업 큐** | 미구현 | 비동기 에셋 로딩의 첫 단계 |
| **Android/Quest 런타임 진입점** | **미구현** | `AndroidWindow` 는 CMake 의 `if(ANDROID)` 분기에 들어 있지만 인스턴스화되지 않습니다. `Engine` 이 `GlfwWindow` 를 조건 없이 만들고(`DEF-08`), `AndroidWindow` 는 `IWindow` 의 순수 가상 클립보드 함수 두 개를 구현하지 않아 추상 클래스입니다(`DEF-103`) |
| 계측 (귀속 할당 추적, Tracy) | 미구현 | |

### Layer 2 — 우주 / 무중력 도메인

| 항목 | 성격 |
|---|---|
| `ZeroGControllerComponent` (RCS 추진, 관성 제동, 표류, 부스트) | Layer 1의 감쇠 적분기에 우주복 파라미터를 채워 넣는 컴포넌트 |
| 산소 · 방사선 · 체온 생존 규칙 | 순수 게임플레이 규칙 |
| `DebrisFieldComponent` (파편장 스폰과 스트리밍) | Layer 1의 인스턴싱과 공간 분할을 소비 |
| 무중력 그랩 및 견인, 뉴턴 제3법칙 반작용 | Layer 1의 물리 힘 API를 소비 |
| 우주 배경 (성운 큐브맵, 스타필드) | Layer 1의 패스 확장 지점을 소비 |
| SDF 위험 구역, 추진기 플룸, 쉴드 연출 | 도메인 셰이더 |
| 파편장 특화 오디오 군집화 튜닝 | Layer 1의 보이스 예산 위에 얹는 정책 |

### 판정이 갈렸던 항목과 그 근거

| 항목 | 판정 | 근거 |
|---|---|---|
| **Reverse-Z** | **Layer 1** | 깊이 규약은 엔진 전역 정책입니다. 우주가 아니어도 대규모 실내나 원거리 지형에서 필요합니다. 게다가 셰이더와 파이프라인 열여덟 곳을 관통하므로 Layer 2에서 켤 수 없습니다(전체 목록은 [Deep_Space_VR_Specification.md](Deep_Space_VR_Specification.md) 4-4) |
| **Floating Origin / 배정밀도 좌표** | **Layer 1 (옵션)** | `Transform`과 `World`의 자료형을 바꿉니다. 대규모 씬은 우주 전용 문제가 아닙니다. 다만 기본값은 float 유지, 옵트인 |
| **정점 패킹 (QTangent)** | **Layer 1** | `Vertex.hpp`와 모든 정점 셰이더의 계약입니다. 대역폭 이득은 장르와 무관합니다 |
| **감쇠 운동 적분기** | **Layer 1 유틸 + Layer 2 파라미터** | dt 독립 적분은 부드러운 감속이 필요한 모든 제어에 유용합니다. 우주복 질량과 추력 값만 Layer 2입니다 |
| **성운 큐브맵 베이커** | **Layer 1 도구 + Layer 2 데이터** | 절차적 텍스처를 로드 시점에 굽는 기구는 범용입니다. 어떤 노이즈로 무엇을 굽는지가 Layer 2입니다 |
| **Dual Quaternion 트랜스폼** | **보류 (도입하지 않음)** | 원본이 Carbon에 존재하지 않고, 스케일을 표현하지 못하며, 대상 버퍼가 프레임당 64KB에 불과합니다. 스키닝을 도입할 때 재검토합니다 |
| **네트워크 계층 (HNet)** | **범위 밖** | 현재 로드맵에 없고 `Threading.md`의 단일 스레드 전제와 충돌합니다. 멀티플레이어를 결정하기 전까지 착수하지 않습니다 |

---

## 4. 현재 경계가 새는 지점

아래는 코드 실측 결과이며, **Layer 2를 작성할 수 없게 만드는 구조적 장애**입니다. 우주 기능을 얹기 전에 이 중 상당수를 코어에서 먼저 해소해야 합니다.

> **[2026-08-25]** 이 절은 계층 경계가 막히는 지점만 다루며 결함 대장이 아닙니다. 코어 내부 결함의 등재와 추적은 [Core_Stabilization_Spec.md](Core_Stabilization_Spec.md)와 [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md)가 담당하므로, 개별 결함의 상세는 그쪽을 보십시오. 같은 날 해결된 Transform SSBO 인덱스 동기화와 스피어 인덱스 도메인 불일치, 그리고 깊이 프리패스와 컬러 서브패스의 가시 집합 불일치는 §6의 0단계에 완료로 표기했습니다. 스테레오 컬링은 경계 누출이 아니라 VR 코어의 미비 항목이므로 §3의 판정표에 두었습니다.

### 치명적 (Layer 2에서 우회 불가)

| 지점 | 문제 |
|---|---|
| `Renderer::buildDrawCommands`, `GeometryPass::record`, `ShadowPass::record` | `instanceCount = 1`이 리터럴로 박혀 있고 `GeometryPass::DrawItem`에 해당 멤버가 없습니다. 2026-10-14 에 확인한 곳은 여섯입니다. `buildDrawCommands` 가 `GpuDrawCmd::instanceCount` 를 두 곳에서 1 로 채우지만 읽는 곳이 없고, 실제 드로우는 `GeometryPass::record` 의 세 서브패스와 `ShadowPass::record` 가 `vkCmdDrawIndexed` 에 1 을 직접 넘깁니다. 파편장 인스턴싱이 원리상 불가능합니다 |
| `HPass`, `Renderer` | `HPass`가 의도적으로 `create()`와 `record()`를 제외한 수명주기 전용 인터페이스입니다. 패스 레지스트리가 없고, 다섯 패스(`GeometryPass` · `ShadowPass` · `AOPass` · `BloomPass` · `PostPass`)가 `Renderer`의 구체 멤버로서 생성 · 파괴 · 핫 리로드 · 크기 변경 경로마다 손으로 열거됩니다(2026-10-04 에 `CullPass` 를 지워 여섯에서 다섯이 됨, `DEF-03`). **새 렌더 패스를 Layer 2에서 추가할 수 없습니다** |
| `EngineContext` (`IGameModule.hpp`) | `EngineContext`에 렌더러 참조가 없습니다(월드 · 에셋 · 창 · 모듈만 있음). 게임 모듈이 렌더러에 닿을 수 없습니다 |
| `CMakeLists.txt` 의 `GLSL_SHADERS` | 셰이더 목록이 파일명으로 하드코딩되어 있습니다. 도메인 셰이더를 드롭인할 수 없습니다. 2026-10-04 사용자 결정으로 목록은 글롭 대신 명시로 두고, 목록 밖 셰이더 파일은 구성 단계에서 경고합니다(`DEF-11`). 그래서 Layer 2 셰이더도 이 목록에 넣어야 합니다 |
| `kMaxDraws` (`RenderTypes.hpp`), `kMaxBodies` (`PhysicsSystem.cpp`) | `kMaxDraws = 1024`, `kMaxBodies = 4096` 이중 상한. "수천 개 파편"이 양쪽에 걸립니다. 2026-10-04 에 상한을 넘긴 메시를 Dropped 통계와 경고로 알리는 진단만 넣었고(`DEF-10`), 상한 설정화는 남았습니다 |

### 심각 (배관이 없어 기능이 성립하지 않음)

| 지점 | 문제 |
|---|---|
| `PhysicsSystem`, `RigidBodyComponent` | **해결 · 2026-10-04 (`DEF-19`).** 중력 세터와 힘 · 토크 · 충격량 · 속도 · 레이캐스트 · 구 겹침 API 를 두었습니다. 동기화 루프가 매 프레임 Dynamic 바디의 트랜스폼을 덮어써 게임 코드의 이동이 사라지던 문제는, 물리가 쓴 값을 기억해 게임 코드가 옮긴 경우를 순간 이동(속도 유지)으로 반영하는 방식으로 정리했습니다(UE 식). 예전에는 중력이 하드코딩되어 세터가 없고 힘 · 임펄스 · 속도 · 레이캐스트 API 가 하나도 없어, Layer 2 가 바디를 밀 방법이 없었습니다 |
| `HInputSystem`, `src/xr` | **해결 · 2026-10-04 (`DEF-20`, 헤드셋 확인 전).** XR 모듈이 OpenXR 액션으로 입력을 만들고, `HInputSystem` 이 아날로그 축(`bindAxis` · `axis`) · 컨트롤러 버튼(`bindXrButton`) · 머리와 손 자세(`xr()`)를 내줍니다. 창(`IWindow`)은 키보드 · 마우스만 맡습니다. 예전에는 디지털 입력 전용이라 아날로그 축도 VR 포즈도 없어 RCS 추력 입력을 받을 수 없었습니다 |
| `CameraComponent` | **해결 · 2026-10-04 (`DEF-21`).** 뷰를 액터의 월드 회전(쿼터니언)과 월드 위치로 만들고, 헤드셋 자세는 카메라의 XR 원점(위치 + 요) 위에 올립니다. 예전에는 yaw/pitch 오일러각으로 뷰를 만들고 `owner->transform.rotation` 을 무시해, 6DOF 자유 자세도 롤도 머리와 몸체의 분리도 불가능했습니다 |
| `Engine::Engine`, `Engine::run` (`src/core/Engine.cpp`) | `GlfwWindow`를 조건 없이 생성하며 안드로이드에서는 컴파일되지 않습니다(헤더 포함만 `#ifndef ANDROID` 로 막혀 있음). `World::tick`은 에디터 Play 상태에서만 돌아 런타임 빌드에 게임 루프가 없습니다. 예전에는 이 코드가 `main.cpp` 에 있었고, 그 뒤 `Engine` 클래스로 옮겨졌습니다 |
| `SceneSerializer::ActorData` | 컴포넌트 5종(메시 · 라이트 · 카메라 · 오디오 · 강체)이 구조체 필드로 하드코딩되어 새 컴포넌트가 `.hscene` 왕복을 하지 못합니다 |

### 보통 (기능은 되나 확장이 막힘)

| 지점 | 문제 |
|---|---|
| `FrameUBO` (`shaders/include/haruka_shared.h`) | `FrameUBO`에 `invViewProj`가 없습니다. 레이마칭 배경이나 화면 공간 굴절에 필요합니다. `FrameUBO` 는 2026-10-04 에 `RenderTypes.hpp` 에서 C++ 와 GLSL 이 함께 쓰는 이 헤더로 옮겼습니다(`DEF-14`) |
| `shaders/include/noise.glsl`, `sdf.glsl` | **해결 · 2026-10-04 (`DEF-11`).** 쓰지 않던 두 파일은 지웠고, 셰이더 include 는 `glslc -MD` 의 depfile 로 추적해 include 파일이 바뀌면 다시 컴파일합니다. 예전에는 두 파일을 어떤 셰이더도 `#include` 하지 않았고, CMake `DEPENDS` 에 include 파일이 없어 쓰기 시작하면 낡은 SPIR-V 를 배포하게 되어 있었습니다 |
| `PostPass::recordTonemap` | 포스트 푸시 상수(`PostPC`)가 이 함수 안에서 리터럴 값(AO · Bloom 강도, 노출)으로 채우는 고정 구조체입니다. 멀미 억제용 비네팅 반경을 게임 상태에서 넘길 수 없습니다 |
| `Renderer::init` | `GlfwSwapchain` 을 직접 생성합니다. XR 스왑체인은 2026-10-04 에 `ISwapchain` 구현체 대신 `Renderer::attachXrSwapchain` 으로 이미지를 붙이는 방식으로 넣었습니다(PC OpenXR). 창이 없는 Quest 단독 실행에서 창 스왑체인을 어떻게 뺄지는 Android 이식(`DEF-08`) 때 정합니다 |
| `Renderer::updateLightSSBO` | 라이트 개수 불일치 자체는 해결되었고(`DEF-24`, 2026-08-25) 상한도 256으로 올렸습니다. 다만 상한에 걸릴 때 **어느 라이트를 살릴지 정하는 정책이 없습니다.** 액터 생성 순서로 앞에서부터 자르므로, Layer 2가 광원을 많이 두면 가까운 조명이 꺼지고 먼 조명이 켜질 수 있습니다(`DEF-33`). 상세는 결함 문서를 참조하십시오 |

---

## 5. Carbon에서 코어로 들여올 것

Carbon Engine 저장소를 원본으로 확인한 결과, **코어에 들어갈 가치가 있는 것과 도메인에 남을 것이 분명히 갈립니다.** 상세 근거는 [Carbon_Source_Audit.md](archive/Carbon_Source_Audit.md)에 있습니다.

### 코어(Layer 1)로 들여올 것

| 항목 | 출처 | 코어에 넣는 이유 | 우선순위 |
|---|---|---|---|
| **폐쇄형 감쇠 운동 적분기** | `destiny` Ballpark::Integrate | dt에 독립적이라 프레임 타임이 튀어도 결과가 동일합니다. Quest의 재투영 환경에서 조작감을 지켜 줍니다. 바디당 스칼라 20여 회와 `exp()` 한 번이라 GPU를 건드리지 않습니다 | 높음 |
| **QTangent 정점 패킹** | `mesh` tangents.h | TBN 전체를 `Int16Norm x 4 = 8바이트`에 담습니다. Haruka의 48바이트 정점이 24바이트가 됩니다. 정점 대역폭 절반은 장르와 무관한 이득입니다 | 높음 |
| **보이스 예산 우선순위** | `audio` SoundPrioritization | 약 150줄, MIT. 거리 제곱을 비용으로 삼고 특징을 감산 보너스로 처리해 예산까지만 살립니다. 모바일 VR의 고정 보이스 예산 문제를 직접 해결합니다 | 높음 |
| **전역 렌더 상태 오버라이드 방식의 깊이 정책** | `trinity` Tr2EffectStateManager | 깊이 극성을 파이프라인마다 손으로 뒤집는 대신 정책 한 곳에서 뒤집습니다. Reverse-Z 도입의 정답 구조입니다 | 높음 |
| **명시적 내로잉 규율** | `destiny` Vector3d | float에서 double로는 암시적, double에서 float로는 명시적 호출을 요구합니다. 정밀도 손실 경계를 전부 검색 가능하게 만듭니다 | 중간 |
| **저작 방향 역전** | `destiny` GotoThrust | 최고 속도와 둔중함을 저작하고 추력을 역산합니다. 뉴턴 값을 먼저 정하고 결과 속도를 발견하는 것보다 튜닝이 쉽습니다 | 중간 |
| **복합 충돌 프록시** | `destiny` MiniBall/MiniBox/MiniCapsule | 복잡한 형상을 소수의 구와 캡슐로 근사합니다. 충돌 메시를 상주시키지 않아 1GB 예산에 유리합니다 | 중간 |
| **프레임 예산 실행 패턴** | `scheduler` RunTaskletsForTime | 새 스레드 없이 메인 스레드의 지연 작업 큐를 데드라인까지만 소진합니다. `Threading.md`의 규칙을 어기지 않고 에셋 로드 프리즈를 줄입니다 | 중간 |
| **트랜지언트 리소스 풀** | `trinity` Tr2GpuResourcePool | 프레임 내에서만 사는 이미지의 수명 관리. Zone B 파편화 완화에 참고 가치가 있습니다 | 낮음 |
| **귀속 할당 추적, Tracy 계측** | `core` | 할당마다 소유자 이름을 붙이면 1GB 예산에서 어느 영역이 늘었는지 알 수 있습니다. Tracy는 Android/ARM64를 지원합니다 | 낮음 |

### 도메인(Layer 2)에 남을 것

- 점성 관성 비행의 **파라미터**(우주복 질량, RCS 추력, 시정수). 적분기 자체는 코어입니다.
- 파편장 공간 분할의 **셀 크기 튜닝**. `destiny`의 480m 셀은 성계 규모라 그대로 쓸 수 없습니다.
- 오디오 군집화 **정책**. 알고리즘 뼈대는 코어의 보이스 예산 위에 얹습니다.

### 들여오지 않을 것

| 항목 | 이유 |
|---|---|
| `blue`, `scheduler`, `io` 전부 | CPython 확장이거나 임베디드 Python 런타임입니다. Quest에서 인터프리터와 GIL을 안고 갈 이유가 없습니다 |
| `imageio` | BCn 전용이며 ASTC가 없습니다. Adreno가 지원하는 포맷이 하나도 없습니다 |
| `destiny`의 충돌 응답 | 접선 미끄러짐도 반발 계수도 없고 끼임이 문서화된 실패 모드입니다. VR 손 접촉에는 부적합합니다. Jolt를 유지합니다 |
| `destiny`의 1Hz 틱과 `Ball` 자료구조 | 서버 권위 MMO의 답이며, 바디마다 STL 컨테이너를 힙에 매답니다 |
| 셰이더 원천으로서의 `trinity` | 저장소에 셰이더 파일이 없습니다. 94개 이펙트가 전부 비공개 번들의 리소스 경로입니다 |
| Dual Quaternion 트랜스폼 | §3 판정표 참조 |

---

## 6. 작업 순서

계층 관계를 기준으로 정리하면 순서가 명확해집니다.

**0단계: 코어 무결성.** 다른 모든 것이 이 위에 올라갑니다.
- Transform SSBO 인덱스 동기화 수정 (**완료 2026-08-25**). `src/renderer/Renderer.hpp`와 `src/renderer/Renderer_render.cpp`에서 인덱스 배정 지점을 `buildDrawCommands` 한 곳으로 모았습니다. 동일한 패턴이 남아 있던 스피어 인덱스 도메인 불일치(`shaders/cull.comp:54`, `:57`)도 같은 날 함께 해소되었습니다.
- 깊이 프리패스와 컬러 서브패스의 가시 집합 일치 (`DEF-25`, **완료 2026-08-25**). `src/renderer/Renderer_render.cpp`와 `src/renderer/Renderer.hpp`에서 CPU의 `aabbInFrustum`을 제거하고 `shaders/cull.comp`와 동일한 `sphereInFrustum` 판정으로 통일했으며, 월드 공간 바운딩 구를 `buildDrawCommands` 한 곳에서만 계산해 컬링 판정과 업로드에 함께 씁니다.
- 라이트 개수의 단일 진리 원천화 (`DEF-24`, **완료 2026-08-25**). `updateLightSSBO`가 실제 기록 개수를 반환하고 `updateUBO`가 인자로 받도록 바꾸었으며, 상한 `kMaxLights`는 16에서 256으로 올렸습니다. 상세는 [Core_Stabilization_Spec.md](Core_Stabilization_Spec.md)에 있습니다.
- 섀도 샘플러 비교 규약 반전 (`DEF-36`, **완료 2026-08-25**). `src/renderer/passes/ShadowPass.cpp`의 `compareOp`를 엔진의 정방향 Z 규약에 맞춰 `LESS_OR_EQUAL`로 되돌렸습니다. 수정 전에 셰이더로 `shadow` 값을 직접 출력하는 지문 테스트를 수행해 결함을 확정했습니다.
- IBL 조도 추정기의 코사인 중복 (`DEF-37`, **완료 2026-08-25**). `src/renderer/Renderer.cpp`의 몬테카를로 루프를 코사인 가중 샘플링에 맞게 정규화했습니다. `DEF-36`과 같은 검증 사이클에서 함께 고쳐야 효과가 눈에 보입니다.
- 머티리얼 팩터와 색 공간 (`DEF-47` HIGH, `DEF-48` MEDIUM, **둘 다 완료 2026-08-25**). `src/assets/GltfLoader.cpp`가 glTF 스칼라 팩터를 읽지 않아 팩터만 지정한 머티리얼이 전부 순백이 되던 문제와, `shaders/pbr.frag`가 이미 선형인 sRGB 샘플을 한 번 더 디코드하던 문제입니다. **다만 팩터를 로드 시점에 픽셀로 굽는 방식이라 §3의 "머티리얼 편집" 항목이 이 결정에 묶여 있습니다.**
- 밝기 밸런스 (**적용됨 2026-08-25 · 결함 아님**). 방향광 기본 강도 `0.85 → 3.0`, 하늘 복사휘도에 `kSkyRadianceScale = 0.30`, 톤매핑 노출 `1.0 → 0.4`를 한 세트로 움직였습니다. `src/components/LightComponent.hpp`, `src/renderer/Renderer.cpp`, `src/renderer/passes/PostPass.cpp`입니다. **문서 기록 없이 코드에만 들어가 있던 것을 검수에서 확인해 등재했으며, 실측 기록은 확인되지 않았습니다.** 상세는 [Primitive_And_Lighting_Design.md](Primitive_And_Lighting_Design.md) 8-12절입니다.
- 모바일 Vulkan 폴백. `drawIndirectCount` 요구는 GPU 컬링과 함께 지웠습니다(**2026-10-04**, `DEF-03`). 남은 것은 Vulkan 1.4 장치 전제(`DEF-97`: Quest 는 1.1 · 1.3 이고 `synchronization2` · push descriptor 는 확장으로 있음)이며, Quest 이식 때 `DEF-04` 와 함께 처리합니다.
- 라이팅 확인 씬 (**완료 2026-08-25**). 두께 0.2 m 바닥(`Ground`) 위에 확인용 도형을 둔 `Lighting_Probe` 배치를 만들어, 그림자가 떨어질 표면이 생겼습니다. 처음에는 `DefaultGameModule::onStart` 가 코드로 만들었고, 2026-10-02 에 `PRIM:` 직렬화가 붙어 `assets/scenes/LightingProbe.hscene` 파일로 옮겼습니다(`onStart` 는 이 파일을 읽기만 함). 예전에는 바닥이 없어 섀도를 검증할 수 없었고, 이 씬이 선결 조건이던 `DEF-26` 과 `DEF-27` 은 2026-08-27 에 해결했습니다. 상세는 [Primitive_And_Lighting_Design.md](Primitive_And_Lighting_Design.md) 9절과 부록 C

**1단계: VR 엔진으로서의 코어 완성.** 이것이 엔진의 정의이므로 우주 기능보다 앞섭니다.
- Quest 런타임 진입점 (`AndroidWindow` 인스턴스화, `IWindow` 팩토리, 별도 `main`)
- OpenXR 세션과 XR 스왑체인, 스왑체인 주입 지점 (**PC OpenXR 완료 2026-10-04**, 헤드셋 확인 전). `src/xr/XrSystem` 이 세션 · LOCAL 원점 공간 · 두 층 배열 스왑체인을 만들고, 렌더러에는 `Renderer::attachXrSwapchain` 으로 이미지를 붙입니다. Quest 단독 실행은 위 진입점과 함께 합니다.
- Multiview 완성 (`viewMask`, 셰이더의 `gl_ViewIndex`, 이미지 `arrayLayers`). 첫 단계인 눈마다 두 번 그리기는 **완료 2026-10-04**(`DEF-09`)입니다. 셰이더 · UBO 가 두 눈 기준이라, 멀티뷰는 Quest 이식 때 뷰 번호만 바꿔 켭니다.
- 컬링의 스테레오 대응. `DEF-31` 은 **완료 2026-10-04**(두 눈 절두체를 각각 판정해 하나라도 통과하면 그림)이고, `DEF-32` 재투영 여유분은 한 프레임에 자세 하나로 컬링 · 렌더 · 제출하는 동안 필요 없어 **보류**했습니다. 상세는 [Core_Stabilization_Spec.md](Core_Stabilization_Spec.md)와 [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md)를 보십시오.
- VR 입력 (아날로그 축, 6DOF 포즈) (**완료 2026-10-04**, `DEF-20`, 헤드셋 확인 전)
- 6DOF 카메라 자세 (**완료 2026-10-04**, `DEF-21`)

**2단계: Layer 2를 가능하게 하는 확장 지점.**
- 패스 등록 구조와 `record(RenderView&)` 진입점
- 인스턴싱 (`instanceCount` 경로)
- 물리 힘·임펄스 API와 중력 설정 (**완료 2026-10-04**, `DEF-19`)
- `FrameUBO.invViewProj`. 셰이더 목록은 글롭으로 바꾸지 않고 명시 목록과 목록 밖 파일 경고로 정했습니다(사용자 결정 2026-10-04, `DEF-11`).

**3단계: Carbon 유래 코어 기능.**
- 감쇠 운동 적분기, QTangent 정점 패킹, 보이스 예산, 깊이 정책 구조

**4단계: 우주 도메인 레이어.**
- `ZeroGControllerComponent`, 파편장, 생존 규칙, 우주 비주얼

우주 기능을 먼저 얹고 싶은 유혹이 있겠지만, 2단계 없이는 Layer 2 코드를 작성해도 동작시킬 방법이 없습니다. `ZeroGControllerComponent` 에 필요한 입력 축과 물리 힘 API 는 2026-10-04 에 들어왔지만, `DebrisFieldComponent` 는 인스턴싱 경로가 없으면 수천 개 파편을 그릴 수 없고 우주 배경은 패스 확장 지점이 없으면 넣을 자리가 없습니다.

---

## 7. 관련 문서

| 문서 | 관계 |
|---|---|
| [Architecture_Overview.md](Architecture_Overview.md) | 설계 철학. 이 문서의 계층 구분을 전제로 읽습니다 |
| [Roadmap.md](Roadmap.md) | 마일스톤. §6의 순서를 반영합니다 |
| [Core_Stabilization_Spec.md](Core_Stabilization_Spec.md) · [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) | 코어 결함의 등재 대장과 작업 지시서입니다. §6 0단계 항목의 상세가 여기에 있습니다 |
| [Carbon_Source_Audit.md](archive/Carbon_Source_Audit.md) | §5 판정의 근거가 되는 1차 자료 검증 결과 |
| [Engine_Strategy_Evaluation.md](Engine_Strategy_Evaluation.md) | 범용 VR 코어 위에 얹을 첫 도메인으로 우주가 타당한지에 대한 평가. 계층 경계 자체는 이 문서가 확정합니다 |
| [Deep_Space_VR_Specification.md](Deep_Space_VR_Specification.md) | Layer 2 명세. 코어 선결 조건을 함께 읽어야 합니다 |
| [Threading.md](Threading.md) | 스레드 소유권 규칙. 계층과 무관하게 전역 적용됩니다 |
| [RendererRefactor.md](RendererRefactor.md) | 현재 렌더러 구조. 패스 확장 지점 논의의 출발점입니다 |
