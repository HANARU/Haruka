# Haruka Engine 아키텍처 개괄 문서 (Architecture Overview)

> **[검증 상태 · 2026-08-24]** 이 문서는 github.com/carbonengine 저장소 원본 및
> `src/` 코드와 대조하여 정정되었습니다. 정정 근거와 전체 내역은
> [Carbon_Source_Audit.md](archive/Carbon_Source_Audit.md)를 참조하십시오.
> 계층 구분은 [Layer_Architecture.md](Layer_Architecture.md)를 따릅니다.

본 문서는 하이엔드 모바일 XR(Quest Pro) 타깃의 고성능 Vulkan 엔진 개발을 위해 수석 연구원님과 논의된 핵심 설계 철학과 아키텍처 방향성을 종합한 문서입니다.

**[정정]** 이전 판(2026.05.12)은 전부 미래형 목표 서술이었습니다. 그 사이에 상당 부분이 구현되었으므로, 이번 판부터는 각 절에 **목표**와 **2026-08-24 기준 구현 상태**를 구분해서 적습니다.

## 1. 개발 철학 및 타깃 플랫폼

### 1.0 엔진의 목적과 계층 구조

**Haruka는 Vulkan 기반 VR 엔진입니다.** 이것이 이 프로젝트의 정체성이며 완성해야 할 산출물입니다. 우주 및 무중력 기능은 그 코어 위에 얹는 **도메인 레이어(Layer 2)**이고, 코어의 성능과 완성도를 실증하는 첫 번째 응용이지 엔진의 정의가 아닙니다.

**[정정]** 이전 판은 계층 구조를 전혀 서술하지 않았고, 엔진의 목적도 "고성능 Vulkan 엔진"이라고만 적어 VR과 우주 도메인의 관계를 규정하지 않았습니다.

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

이 구분이 본 문서의 나머지 절에 대해 갖는 의미는 다음과 같습니다.

*   코어(Layer 1)는 우주 프로젝트를 걷어내도 **그 자체로 쓸 수 있는 범용 Vulkan VR 엔진**이어야 합니다. 본 문서 2절부터 5절까지가 서술하는 것은 전부 Layer 1입니다.
*   Reverse-Z, Floating Origin, 정점 패킹처럼 코어의 계약(깊이 규약, 좌표 자료형, 정점 레이아웃)을 바꾸는 항목은 우주 프로젝트가 요구해서 논의가 시작되었더라도 **Layer 1의 범용 옵션으로 흡수합니다.** 이 논점은 결론이 났으므로 미결 사항으로 다루지 않습니다.
*   계층 판정 기준과 항목별 소속, 그리고 작업 순서는 기준 문서인 [Layer_Architecture.md](Layer_Architecture.md)의 3절과 6절을 따릅니다. 본 문서와 그 문서가 어긋나면 그 문서를 따릅니다.

### 1.1 타깃과 설계 원칙

*   **핵심 타깃**: Quest Pro (Snapdragon XR2+ Gen 1), 스탠드얼론 VR 환경.
*   **Vulkan 버전**: 코드가 요구하는 것은 `VK_API_VERSION_1_4`입니다(`Renderer.cpp:502`). 디바이스 피처 체인이 `VkPhysicalDeviceVulkan14Features{pushDescriptor}`, `Vulkan13Features{synchronization2}`, `Vulkan12Features{drawIndirectCount}` 순서로 이어집니다.
    - **[정정]** 이전 판은 Vulkan 버전을 명시하지 않았습니다. 이 값은 단순한 정보가 아니라 이식의 최대 제약입니다. Quest Pro는 1.4를 제공하지 않으므로, Phase 4-1의 다운그레이드는 확장 하나를 바꾸는 문제가 아니라 세 층의 피처 체인을 함께 푸는 문제입니다.
*   **개발 환경**: 로컬 Windows PC 기반 네이티브 개발 및 테스트. CMake를 통한 Android NDK 크로스 컴파일(이원화 빌드 체계).
    - **구현 상태**: 이원화는 아직 성립하지 않았습니다. `AndroidWindow.cpp`가 `CMakeLists.txt:186-191`에서 컴파일되기는 하지만 어디에서도 인스턴스화되지 않으며, `main.cpp:38`이 `GlfwWindow`를 조건 없이 생성합니다. `IWindow` 팩토리와 별도의 Android 진입점이 없는 상태입니다(Roadmap 4-0 참조).
*   **설계 원칙**: 
    - **Lean 아키텍처**: 불필요한 추상화나 무거운 서드파티 라이브러리를 배제하고 극단적인 가벼움 추구.
    - **[정정] 에디터에 대한 원칙은 실제 개발 결과와 어긋났습니다.** 이전 판은 "에디터 UI 구축보다는 디버그 렌더링 중심의 빠른 프로토타이핑을 최우선으로 함"이라고 적었으나, 실제로 만들어진 것은 UE5 스타일의 에디터입니다: `src/editor/EditorApp.cpp`가 1,500줄이고, 그 아래에 자체 UI 프레임워크 HSlate(`src/ui/slate/`, 위젯 트리·폰트 아틀라스·전용 렌더러 포함)가 통째로 놓여 있습니다. 디테일 패널, 트랜스폼 기즈모, 커맨드 스택 기반 실행 취소, 씬 저장·불러오기가 모두 여기에 있습니다.
    - **이 괴리에 대한 판단**: 원칙을 지키지 못했다고 기록하는 대신, 원칙을 현재 상태에 맞게 갱신합니다. 갱신된 원칙은 다음과 같습니다. **에디터는 이미 구축되었고 앞으로도 유지합니다. 다만 에디터 계층은 런타임과 분리되어야 하며, Quest 런타임 빌드에는 들어가지 않아야 합니다.** 현재 이 분리는 지켜지지 않고 있습니다. `World::tick`과 `ModuleManager::tickAll`이 에디터의 Play 상태에 묶여 있어서(`main.cpp:115-119`) 에디터 없이는 게임 루프가 돌지 않습니다. 이 결합을 끊는 것이 Phase 4의 선행 조건입니다.

## 2. 메모리 및 에셋 관리 아키텍처

**[정정]** 이전 판은 이 절 전체를 미래형 목표로 적었습니다. 두 시스템 모두 구현되어 현재 운용 중이므로 아래는 목표가 아니라 **현재 상태**를 서술합니다.

*   **1GB Core Heap 시스템 (구현 완료, 운용 중)**:
    - Vulkan 메모리 할당의 파편화를 막기 위해 초기화 시점에 단일 메모리 블록을 할당하고 전체 범위를 지속 매핑합니다(`HarukaAllocator.cpp`의 `init`, `vkMapMemory`를 `VK_WHOLE_SIZE`로 호출).
    - 메모리 속성은 1순위가 `DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT`이며, Quest Pro 같은 UMA 기기를 이상적인 경우로 삼습니다. 해당 메모리 타입에서 1GB를 확보하지 못하는 PC 외장 GPU에서는 `HOST_VISIBLE | HOST_COHERENT`로 내려가고, 힙 크기도 가용량의 3/4까지 줄여 가며 재시도합니다. 이미지가 힙 메모리 타입과 호환되지 않으면 별도의 `DEVICE_LOCAL` 메모리를 할당하는 폴백 경로(`HarukaAllocation::auxMem`)가 있습니다.
    - 존 분할은 논리적 구획이 아니라 컴파일 타임 상수로 고정되어 있습니다(`HarukaAllocator.hpp:7-17`). Zone A 64MB(엔진 구조체, Bump 할당), Zone B 896MB(에셋 뱅크, FreeList 할당), Zone C 64MB(프레임 단위 동적 버퍼, 링 버퍼)이며 합이 1GB임을 `static_assert`로 강제합니다.[^zonec]
*   **Zero-Copy 로딩 (현재 이름: `.gasset` 바이너리 에셋 캐시, 구현 완료, 운용 중)**:
    - FlatBuffers 등의 직렬화 라이브러리를 거치지 않는다는 원칙은 그대로 지켜졌습니다. `GassetCache`는 `magic("GAST") · version · 원본 mtime · meshCount` 헤더 뒤에 정점과 인덱스와 텍스처를 그대로 이어 붙인 자체 포맷을 쓰고, 임시 파일에 쓴 뒤 rename 하는 방식으로 갱신합니다(`assets/GassetCache.cpp`).
    - `AssetManager`가 로드 시점에 캐시 유효성(원본 mtime 일치)을 확인해서 적중하면 캐시를 읽고, 실패하면 GLTF를 파싱한 뒤 캐시를 다시 굽습니다(`assets/AssetManager.cpp:15-26`). 시작할 때마다 GLTF를 다시 파싱하는 비용은 이렇게 제거되었습니다.
    - **[정정] 다만 엄밀한 의미의 Zero-Copy는 아직 아닙니다.** 이전 판은 "디스크 구조를 Vulkan 버퍼 레이아웃과 1:1로 일치시켜 메모리에 그대로 얹는다"고 적었으나, 현재 `GassetCache::load`는 `std::ifstream`으로 호스트측 `std::vector`에 읽어 들인 뒤 업로드합니다. 매핑된 Core Heap으로 직접 읽어 들이는 경로는 아직 없습니다. 목표는 유효하며 남은 작업으로 분류합니다.

[^zonec]: Zone C에는 펜스 추적이 없습니다(`HarukaAllocator.cpp:177-199`의 `allocC`). 링 버퍼가 한 바퀴 돌 때 인플라이트 프레임이 아직 읽고 있는 영역을 덮어쓸 수 있다는 뜻입니다. 다만 실사용량이 64MB 중 약 0.5MB(약 128배 여유)라 랩어라운드가 약 128프레임마다 일어나고 그때 덮이는 것은 128프레임 전 데이터이므로, 현재로서는 실제 충돌 가능성이 사실상 없습니다. 프레임당 Zone C 사용량이 32MB에 근접하면 그 시점부터 실재하는 위험이 되므로, 그 전에 펜스 추적을 넣어야 합니다.

## 3. 렌더링 파이프라인
*   **Forward Rendering 고정**:
    - 모바일 VR 기기의 대역폭 한계와 타일 기반(Tile-based) GPU 아키텍처 특성을 고려하여 Deferred를 배제하고 Forward 파이프라인으로 구축.
    - **구현 상태**: Forward 파이프라인과 패스 구조는 구현되어 있습니다. 다만 렌더 패스를 Layer 2에서 추가할 수 있는 확장 지점이 없습니다. `HPass`가 `create()`와 `record()`를 제외한 수명주기 전용 인터페이스이고(`HPass.hpp:8-15`), 여섯 패스가 `Renderer`의 구체 멤버로 여섯 곳에서 손으로 열거됩니다. 패스 레지스트리 도입은 [Layer_Architecture.md](Layer_Architecture.md) 6절의 2단계 과제입니다.
    - `VK_KHR_multiview` 확장을 적극 활용하여 좌/우안 렌더링 오버헤드 최소화. **현재는 절반만 되어 있습니다.** 자세한 것은 아래 4절을 보십시오.
*   **Math over Textures**:
    - 메모리 대역폭을 갉아먹는 고해상도 텍스처 의존도를 낮춤. (최대 해상도 2K 제한, ASTC 압축 필수)
    - 텍스처 대신 노이즈(Noise) 함수, SDF(Signed Distance Field) 등 GPU의 강력한 연산 능력을 활용한 절차적(Procedural) 셰이더 수학 연산 극대화.
    - **구현 상태**: `shaders/include/noise.glsl`과 `sdf.glsl`이 존재하지만 현재 어떤 셰이더도 이들을 `#include` 하지 않습니다. 즉 이 원칙은 아직 셰이더에 반영되지 않았습니다. 더구나 CMake의 셰이더 컴파일 규칙에 include 파일이 `DEPENDS`로 들어 있지 않아, 쓰기 시작하는 순간 include 수정이 재컴파일을 유발하지 않고 낡은 SPIR-V가 배포됩니다. 절차적 셰이더를 실제로 도입하기 전에 이 의존성부터 고쳐야 합니다.
*   **동적 오프셋 최적화**: 
    - 매 프레임 변하는 데이터는 SSBO(Shader Storage Buffer Object)에 담고, 동적 오프셋(Dynamic Offset)을 활용해 캐시 적중률과 바인딩 효율 최적화.

## 4. VR 런타임 계층 (OpenXR · 스테레오 · 입력 · 자세)

**[정정]** 이전 판은 VR 엔진을 표방하면서도 OpenXR, Multiview, Foveated Rendering, VR 입력, 6DOF 카메라 자세를 사실상 언급하지 않았습니다. 이것들은 부가 기능이 아니라 Layer 1의 정의에 해당하므로 절을 따로 둡니다.

아래 다섯 항목은 **모두 미구현이거나 절반만 되어 있습니다.** 이것이 현재 Haruka와 "VR 엔진"이라는 선언 사이의 실제 거리이며, [Layer_Architecture.md](Layer_Architecture.md) 6절이 이를 우주 도메인보다 앞선 1단계로 두는 이유입니다.

| 요소 | 상태 | 근거와 남은 일 |
|---|---|---|
| **OpenXR 세션 + 스테레오 스왑체인** | **미구현** | `Renderer.cpp:54-57`이 `GlfwSwapchain`을 직접 생성합니다. `ISwapchain` 추상화는 준비되어 있으나 `XrSwapchain` 구현체를 주입할 팩토리가 없습니다. 세션 수명주기, 프레임 페이싱, 예측 표시 시각 처리가 전부 남아 있습니다 |
| **Multiview 스테레오 렌더링** | **절반** | 디바이스 피처는 `Renderer.cpp:577-583`에서 켜집니다. 그러나 `Renderer_render.cpp:169`가 `viewProj[1] = viewProj[0]`으로 두어 두 눈이 같은 행렬을 쓰고, 어떤 렌더패스도 `viewMask`를 설정하지 않으며, 모든 셰이더가 `viewProj[0]`만 인덱싱합니다. `viewMask` 설정, 셰이더의 `gl_ViewIndex` 사용, 어태치먼트 이미지의 `arrayLayers` 확장이 함께 필요합니다 |
| **Foveated Rendering (FFR)** | **미구현** | `Renderer.hpp:167-170`에 멤버가 선언만 되어 있고 어디에서도 쓰이지 않습니다 |
| **VR 입력 (아날로그 축 + 6DOF 컨트롤러 포즈)** | **미구현** | `HInputSystem`은 디지털 전용이며 `win.isKeyDown`만 호출합니다(`HInputSystem.cpp:22-42`). `IWindow`(`IWindow.hpp:51-62`)에 아날로그 축도 포즈도 없습니다. 액션 바인딩 개념 자체가 없으므로 OpenXR 액션 세트를 받을 자리부터 만들어야 합니다 |
| **6DOF 카메라 자세** | **미구현** | `CameraComponent::viewMatrix`(`CameraComponent.cpp:6-14`)가 yaw/pitch 오일러각으로 뷰를 만들고 `owner->transform.rotation`을 무시하며, `worldPosition()`이 아니라 `owner->transform.position`을 씁니다. 롤이 표현되지 않고 머리와 몸체를 분리할 수 없어 HMD 자세를 그대로 받을 수 없습니다. 쿼터니언 기반 자세로 바꿔야 합니다 |

여기에 더해 **Quest 런타임 진입점 자체가 없습니다.** 위 1절에 적은 대로 `AndroidWindow`는 컴파일되기만 하고 인스턴스화되지 않으며, 게임 루프가 에디터의 Play 상태에 묶여 있습니다(`main.cpp:115-119`). VR 런타임 계층은 이 진입점 위에 올라가므로 순서상 진입점이 먼저입니다.

## 5. 물리 및 AI 연산 (NPU) — 미착수 목표

> **이 절은 전부 미착수 목표입니다.** 아래 두 항목 중 구현된 것이 하나도 없습니다. 설계 방향을 기록해 두는 자리이지 현재 아키텍처의 서술이 아닙니다.

*   **(미착수 목표)** 물리 시뮬레이션 및 추론 연산을 위해 모바일 칩셋의 NPU 텐서 코어를 적극 활용합니다.
*   **(미착수 목표)** NPU 활용이 불가능한 기기나 로컬 PC 환경을 고려하여, GPU 컴퓨트 셰이더 기반의 Fallback 연산을 수행할 수 있도록 추상화 계층을 설계합니다.
*   **현재 상태**: NPU 관련 코드도 추상화 계층도 존재하지 않습니다. 물리는 Jolt를 CPU에서 돌리고 있고, 컴퓨트 셰이더는 컬링 경로(`cull.comp`)에만 쓰입니다. 게다가 물리 쪽은 NPU 이전에 기본 배관이 먼저 필요합니다. 중력이 `PhysicsSystem.cpp:103`에 하드코딩되어 세터가 없고, 힘·임펄스·속도·레이캐스트 API가 하나도 없습니다.

---

## 6. 관련 문서

| 문서 | 관계 |
|---|---|
| [Layer_Architecture.md](Layer_Architecture.md) | 계층 경계의 기준 문서입니다. 본 문서와 충돌하면 그 문서를 따릅니다 |
| [Roadmap.md](Roadmap.md) | 마일스톤과 Phase 구분입니다 |
| [Carbon_Source_Audit.md](archive/Carbon_Source_Audit.md) | 본 문서 정정의 근거가 되는 1차 자료 검증 결과입니다 |
| [RendererRefactor.md](RendererRefactor.md) | 3절이 서술하는 현재 렌더러 구조의 상세입니다 |
| [Threading.md](Threading.md) | 스레드 소유권 규칙이며 계층과 무관하게 전역 적용됩니다 |

---
**문서 상태**: 개정판 (코드 실측 대조 반영, 계층 구조 및 VR 런타임 절 추가)
**수정일**: 2026.08.24
