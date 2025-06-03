# Haruka Engine 하위 코어 결함 전수 감사 및 작업 사양서 (Core Defects Task Spec)

> **[검증 상태 · 2026-08-24]** 이 문서는 github.com/carbonengine 저장소 원본 및
> `src/` 코드와 대조하여 정정되었습니다. 정정 근거와 전체 내역은
> [Carbon_Source_Audit.md](archive/Carbon_Source_Audit.md)를 참조하십시오.
> 계층 구분은 [Layer_Architecture.md](Layer_Architecture.md)를 따릅니다.

> **수행 주체**: 하위 코어 수정 전담 엔지니어 / 서브 에이전트  
> **문서 목적**: Haruka Engine 하위 코어(렌더러 SSBO, 메모리 할당기, 모바일 Vulkan 호환성, 스왑체인 동기화, 액터 수명주기)에 존재하는 **모든 결함과 잠재적 버그를 코드 레벨에서 완벽하게 파악하고 즉시 수정할 수 있도록 작성된 정밀 작업 지시서**

---

## 📋 결함 요약 매트릭스

**[정정]** 이전 판의 요약 매트릭스는 DEF-02를 CRITICAL로, DEF-05를 미해결 HIGH로 분류했고 DEF-06의 범위를 실제보다 넓게 잡았습니다. 아래 표는 코드 실측에 맞추어 심각도와 대상 라인을 다시 잡은 것이며, 이전 판에 없던 DEF-08부터 DEF-15까지를 추가했습니다.

**[갱신 · 2026-08-25]** DEF-01을 **해결됨**으로 전환했고, 같은 날의 수정 작업에서 발견한 DEF-23부터 DEF-30까지를 표에 추가했습니다. DEF-23도 같은 날 해결했습니다.

**[갱신 · 2026-08-25 · 2차]** DEF-25를 **해결됨**으로 전환했고, 그 수정 과정에서 도출된 VR 스테레오 컬링 요구사항을 DEF-31과 DEF-32로 신규 등재했습니다. 두 항목은 **착수 시점이 OpenXR 세션 도입 시**입니다.

**[갱신 · 2026-08-25 · 3차]** 설계 검토에서 도출된 DEF-36부터 DEF-46까지를 표에 추가했습니다. **등재 당시에는 전부 미해결이었으며 전부 Layer 1(코어)에 속합니다.** 작업 지시서는 11절에 있습니다. 아래 두 항목은 등재 당시의 기준이므로, **현재 상태는 바로 아래 4차 갱신을 보십시오.**

* **DEF-36은 CRITICAL이었으며 직접광이 통째로 0이 되는 결함이었습니다.** DEF-37과 함께 처리해야 라이팅이 확인 가능한 상태가 되므로 두 항목을 한 작업 단위로 묶으라고 지시했고, **실제로 그렇게 처리하여 4차에서 둘 다 해결되었습니다.**
* **DEF-40, DEF-43, DEF-44는 미검증 항목입니다 (DEF-39는 2026-08-25 실측으로 성립 확인 후 해결되었습니다).** 심각도 칸에 `미검증`을 함께 표기했습니다. **착수 전에 실측하여 주장이 성립하는지 먼저 확인하십시오.**

**[갱신 · 2026-08-25 · 4차]** **DEF-36과 DEF-37을 해결됨으로 전환했습니다.** 두 항목은 한 작업 단위로 함께 처리했으며, DEF-36은 코드를 고치기 전에 지문 테스트로 확정했습니다. 그 직후 머티리얼 경로에서 발견한 **DEF-47과 DEF-48을 표에 추가하고 동시에 해결됨으로 표기했습니다.** 작업 지시서는 11-12절과 11-13절에 있습니다.

* **DEF-47과 DEF-48은 한 묶음이었습니다.** DEF-48의 sRGB 이중 디코드는 **흰색에서는 `pow(1, 2.2) = 1` 이라 증상이 드러나지 않으므로**, DEF-47을 고쳐 머티리얼에 색이 들어가는 순간 드러날 문제였습니다. 하나만 고쳤다면 잘못된 인과로 이어졌을 것이므로 함께 처리했습니다.
* **머티리얼이 비어 있을 때 엔진이 값을 대체하지 않는다는 정책 결정**은 결함이 아니므로 DEF 번호를 부여하지 않고 11-14절에 두었습니다. **엔진 내 머티리얼 편집 요구**도 결함이 아니므로 11-15절에 등재만 했습니다. 둘 다 위 요약 매트릭스에는 넣지 않았습니다.

| ID | 결함명 | 심각도 | 대상 파일 | 관련 라인 |
|---|---|---|---|---|
| **DEF-01** | Transform SSBO 인덱스 동기화 붕괴 | **해결됨** (2026-08-25, 원래 CRITICAL) | `src/renderer/Renderer.hpp`, `src/renderer/Renderer_render.cpp` | L215, L296, L313~326 |
| **DEF-02** | Zone C Ring Buffer 펜스 추적 부재 | **MEDIUM** (잠재) | `src/memory/HarukaAllocator.cpp` | L177~199 |
| **DEF-03** | `drawIndirectCount` 무조건 요구 및 폴백 부재 | **HIGH** | `src/renderer/passes/GeometryPass.cpp`, `src/renderer/Renderer.cpp` | L507, L586 |
| **DEF-04** | `vkCmdPipelineBarrier2` (Synch2) 무조건 호출 | **HIGH** | `src/renderer/RenderContext.cpp`, `src/renderer/passes/CullPass.cpp`, `src/renderer/Renderer.cpp` | L167, L166·L194, L586 |
| **DEF-05** | 스왑체인 리사이즈 시 GPU In-Flight 자원 파괴 | **해결됨** | `src/renderer/Renderer_swap.cpp` | L59~60 |
| **DEF-06** | MSAA `LAZILY_ALLOCATED` 메모리 속성 누락 | **HIGH** | `src/renderer/passes/GeometryPass.cpp` | L239 |
| **DEF-07** | `onActorDestroyed` 구독자 부재로 인한 댕글링 포인터 | **MEDIUM** | `src/core/World.hpp`, `src/core/World.cpp` | L27, L27 |
| **DEF-08** | 안드로이드 빌드 불가 (`GlfwWindow` 무조건 생성) | **HIGH** | `src/main.cpp`, `CMakeLists.txt` | L38, L186~191 |
| **DEF-09** | 멀티뷰 절반 구현 (피처만 켜지고 경로 없음) | **HIGH** | `src/renderer/Renderer.cpp`, `Renderer_render.cpp`, `Renderer.hpp` | L577~583, L169, L167~170 |
| **DEF-10** | `kMaxDraws` / `kMaxBodies` 이중 상한 | **MEDIUM** | `src/renderer/RenderTypes.hpp`, `src/physics/PhysicsSystem.cpp` | L12, L81 |
| **DEF-11** | 셰이더 빌드 파이프라인 결함 (DEPENDS 누락·하드코딩·고아·죽은 파일) | **MEDIUM** | `CMakeLists.txt`, `shaders/include/` | L94~108 |
| **DEF-12** | `World::tick`이 에디터 Play 상태에서만 실행 | **MEDIUM** | `src/main.cpp` | L115~119 |
| **DEF-13** | `FrameUBO`에 `invViewProj` 부재 | **LOW** | `src/renderer/RenderTypes.hpp` | L16~24 |
| **DEF-14** | `GpuTransform` 셰이더 4곳 중복 선언, 공유 include 부재 | **LOW** | `shaders/pbr.vert`, `depth.vert`, `shadow.vert`, `triangle.vert` | L25, L18, L18, L19 |
| **DEF-15** | `prevViewProj` 선언만 존재, TAA 입력 미비 | **LOW** | `shaders/post.frag` 외 4개 셰이더 | L48~51 |
| **DEF-23** | 스피어 인덱스 도메인 불일치 (DEF-01의 직계 형제) | **해결됨** (2026-08-25, 원래 CRITICAL) | `shaders/cull.comp`, `src/renderer/passes/CullPass.cpp`, `src/renderer/Renderer.hpp`, `src/renderer/Renderer_render.cpp` | L54·L57 |
| **DEF-24** | 라이트 개수 이중 계산 | **해결됨 · 2026-08-25** (원래 HIGH) | `src/renderer/Renderer_render.cpp`, `src/renderer/Renderer.hpp`, `src/renderer/RenderTypes.hpp`, `shaders/pbr.frag` | 8-2절 |
| **DEF-33** | 라이트 선택 정책 부재 (생성 순서로 절단) | MEDIUM | `src/renderer/Renderer_render.cpp` (`updateLightSSBO`) | 8-2-a절 |
| **DEF-34** | 타일드/클러스터드 라이팅 부재 (픽셀당 전체 순회) | MEDIUM | `shaders/pbr.frag` | 8-2-b절 |
| **DEF-35** | `evalLight`의 0 나눗셈 가능성 | LOW | `shaders/pbr.frag` | 8-2-c절 |
| **DEF-25** | 깊이 프리패스와 컬러 서브패스의 가시 집합 불일치 (CPU는 AABB, GPU는 구) | **해결됨** (2026-08-25, 원래 HIGH) | `src/renderer/Renderer_render.cpp`, `src/renderer/Renderer.hpp` | `buildDrawCommands`의 `sphereInFrustum`, 증상은 `GeometryPass.cpp` L330·L88 |
| **DEF-26** | 섀도 캐스터 라이트 선택 불일치 | **해결됨 · 2026-08-27** (원래 MEDIUM) | `src/renderer/Renderer_render.cpp`, `shaders/pbr.frag` | 8-4절 |
| **DEF-27** | 섀도 패스가 카메라 프러스텀으로 걸러진 목록 사용 | **해결됨 · 2026-08-27** (원래 MEDIUM) | `src/renderer/Renderer_render.cpp` | 8-5절 |
| **DEF-28** | 머티리얼 null 항목의 드로우 강행 | **MEDIUM** | `src/renderer/passes/GeometryPass.cpp` | L526, L552 |
| **DEF-29** | `VkDeviceCreateInfo`의 검증 레이어 설정 | **LOW** | `src/renderer/Renderer.cpp` | L603~604 |
| **DEF-30** | 투명 정렬 comparator의 매핑 메모리 읽기 | **LOW** | `src/renderer/Renderer_render.cpp` | 투명 정렬 comparator |
| **DEF-31** | 두 눈 합집합 절두체 부재 (단안 절두체로 스테레오를 컬링) | **HIGH** (착수: OpenXR 세션 도입 시) | `src/renderer/Renderer_render.cpp`, `src/renderer/passes/CullPass.cpp` | `extractRenderView`의 `cullViewProj`, L179~181 |
| **DEF-32** | 재투영 여유분만큼의 절두체 부풀리기 부재 | **MEDIUM** (착수: OpenXR 세션 도입 시) | `src/renderer/Renderer_render.cpp` | `extractFrustumPlanes` |
| **DEF-36** | 섀도 샘플러 비교 규약 반전 (모든 라이트 기여가 0이 됨) | **해결됨 · 2026-08-25** (원래 CRITICAL) | `src/renderer/passes/ShadowPass.cpp` | L37 (증상은 `shaders/pbr.frag` L165), 11-1절 |
| **DEF-37** | IBL 조도 추정기의 코사인 중복 적용 (저장값 약 2.09배 과다) | **해결됨 · 2026-08-25** (원래 HIGH) | `src/renderer/Renderer.cpp` | IBL 조도 큐브맵 생성 루프, 11-2절 |
| **DEF-38** | 라이트 종류와 방향이 직렬화되지 않음 | **HIGH** | `src/assets/SceneSerializer.cpp` | `LIGHT:` 기록 및 복원 경로 |
| **DEF-39** | `defaultCube` 탄젠트 handedness 반전 (여섯 면 전부) | **해결됨 · 2026-08-25** (원래 MEDIUM · 미검증 → 실측으로 성립 확인) | `src/assets/GltfLoader.cpp` | `defaultCube`, 11-4절 |
| **DEF-40** | 라인 오버레이 정점 버퍼의 GPU 인플라이트 레이스 | **MEDIUM · 미검증** (착수 전 실측 필요) | `src/renderer/passes/GeometryPass.*` | 라인 오버레이 정점 버퍼 |
| **DEF-41** | 콜라이더가 `Transform.scale`을 반영하지 않음 | **해결됨 · 2026-08-25** (원래 MEDIUM · 절차 도형 선결 조건) | `src/physics/PhysicsSystem.cpp` | 바디 생성 지점, 11-6절 |
| **DEF-42** | 힙이 1GB 미만일 때 Zone 오프셋이 실제 할당 범위를 넘음 | **MEDIUM** (Quest 포팅 전 필수) | `src/memory/HarukaAllocator.cpp`, `HarukaAllocator.hpp` | init 경로, Zone 상수 |
| **DEF-43** | 세터 안의 `resetIfLive`로 인한 매 프레임 바디 재생성 | **LOW · 미검증** (착수 전 실측 필요) | `src/components/RigidBodyComponent.cpp` | `resetIfLive` |
| **DEF-44** | `RIGID:`의 enum 서수 직렬화 | **LOW · 미검증** (착수 전 실측 필요) | `src/assets/SceneSerializer.cpp` | `RIGID:` 기록 경로 |
| **DEF-45** | `uploadMesh`의 핸들 누수 | **해결됨 · 2026-08-25** (원래 LOW) | `src/renderer/GpuScene.cpp` | `uploadMesh`, 11-10절 |
| **DEF-46** | 에디터 float 스텝 0.01 고정으로 1cm 미만 편집 불가 | **MEDIUM** (절차 도형 작업 선결 조건) | `src/core/Reflect.hpp` | `PropDesc::step` |
| **DEF-47** | glTF 스칼라 팩터를 한 번도 읽지 않음 (텍스처 없는 머티리얼이 전부 순백) | **해결됨 · 2026-08-25** (원래 HIGH) | `src/assets/GltfLoader.cpp` | 머티리얼 추출 경로, 11-12절 |
| **DEF-48** | sRGB 이중 디코드 (SRGB 포맷인데 셰이더가 `pow(2.2)`를 한 번 더 적용) | **해결됨 · 2026-08-25** (원래 MEDIUM) | `shaders/pbr.frag` | albedo 및 emissive 샘플링, 11-13절 |

> **상태 주의**: 이 표의 라인 번호는 2026-08-24 시점의 `E:\Haruka\src` 실측값이며, DEF-23부터 DEF-30까지는 2026-08-25 시점의 실측값입니다. 이전 판이 제시하던 `Renderer_render.cpp:L355~364`(DEF-05)와 `RenderContext.cpp:L154~168`(DEF-04)은 실제 위치와 다릅니다.

> **결함 ID 규약**: 위 표의 `DEF-01`부터 `DEF-15`까지, `DEF-23`부터 `DEF-30`까지, `DEF-31`과 `DEF-32`, `DEF-36`부터 `DEF-46`까지, 그리고 `DEF-47`과 `DEF-48`은 [Core_Stabilization_Spec.md](Core_Stabilization_Spec.md)와 **ID, 심각도, 해결 여부 표기가 동일합니다.** `DEF-39`, `DEF-40`, `DEF-43`, `DEF-44`의 **미검증 표기도 두 문서가 동일합니다.** `DEF-47`과 `DEF-48`의 심층 분석은 그 문서의 **2-F절**에 있습니다. 두 문서가 어긋나면 안 되므로, 한쪽을 고칠 때는 반드시 다른 쪽도 함께 고치십시오.

> **이 문서가 다루는 범위**: 아래 1절부터 7절까지는 `DEF-01`부터 `DEF-07`까지의 **정밀 작업 지시서**이고, 8절은 2026-08-25에 등재된 `DEF-23`부터 `DEF-30`까지의 작업 지시서이며, 9절은 같은 날 2차 작업에서 등재된 `DEF-31`과 `DEF-32`의 작업 지시서이며, 11절은 같은 날 설계 검토에서 등재된 `DEF-36`부터 `DEF-46`까지와 머티리얼 작업에서 등재된 `DEF-47`, `DEF-48`의 작업 지시서입니다. 11절 끝의 11-14절과 11-15절은 **결함이 아니라 정책 결정과 기능 요구**이므로 결함 ID를 부여하지 않았습니다. 10절의 **VR 컬링 설계 지침**은 결함이 아니라 판단 기준이므로 결함 ID를 부여하지 않았습니다. 위 표에 함께 실린 `DEF-08`부터 `DEF-15`까지의 심층 분석과 해결 사양은 [Core_Stabilization_Spec.md](Core_Stabilization_Spec.md) 2절에 있습니다. 그 문서에는 [Layer_Architecture.md](Layer_Architecture.md) 4절의 경계 누출 지점을 결함으로 등재한 `DEF-16`부터 `DEF-22`까지도 있으며, 이들은 아직 위 요약 매트릭스에 반영되어 있지 않습니다. 착수 순서를 판단할 때는 [Core_Stabilization_Spec.md](Core_Stabilization_Spec.md) 3절의 마스터 체크리스트를 따르십시오.

> **계층 지위**: 이 문서가 다루는 결함은 전부 **Layer 1(코어)**의 문제입니다. 우주 도메인 고유의 결함은 하나도 없습니다. 계층 판정 기준은 [Layer_Architecture.md](Layer_Architecture.md) 2절을 따릅니다.

---

## 1. [DEF-01] [해결됨 · 2026-08-25] Transform SSBO 인덱스 동기화 붕괴

### 1-1. 대상 파일 및 위치
* **파일**: [`src/renderer/Renderer_render.cpp`](file:///e:/Haruka/src/renderer/Renderer_render.cpp), [`src/renderer/Renderer.hpp`](file:///e:/Haruka/src/renderer/Renderer.hpp)
* **라인 (수정 전 기준)**:
  * `buildDrawCommands`: [L215](file:///e:/Haruka/src/renderer/Renderer_render.cpp#L215)
  * `extractRenderView`의 `opaqueItems` 루프: [L296](file:///e:/Haruka/src/renderer/Renderer_render.cpp#L296)
  * 투명 머티리얼 역참조 루프: [L313-L326](file:///e:/Haruka/src/renderer/Renderer_render.cpp#L313-L326)

**상태**: **해결되었습니다 (2026-08-25).** 원래 심각도는 CRITICAL이었습니다. `Core_Stabilization_Spec.md`가 한때 이 항목을 근거 없이 완료로 표기한 적이 있으나, 이번 완료 표기는 아래 1-4절의 검증 결과에 근거합니다.

### 1-2. 결함 메커니즘 (Root Cause)

**[정정]** 이전 판은 이 결함을 "컬링 여부 하나가 다른 두 카운터"의 문제로 서술했으나, 실제로는 증가 조건이 서로 다른 **세 개의 경로**가 존재하며 opaque와 transparent의 카운터 공유까지 얽혀 있습니다.

1. `buildDrawCommands`(L215)는 visible, mesh 유무, LOD, 프러스텀 판정을 **모두** 통과한 메시에 대해서만 `ssboIdx`를 증가시키고 `tfData[idx]`에 트랜스폼 행렬을 채웁니다. 이때 **opaque와 transparent가 카운터 하나를 공유**하므로, 두 목록의 인덱스가 하나의 증가 수열 위에 섞여서 배치됩니다.
2. `extractRenderView`의 `opaqueItems` 빌드 루프(L296)는 프러스텀 컬링을 전혀 수행하지 않으면서 별도 카운터를 증가시킵니다.
3. 투명 머티리얼 역참조 루프(L313-L326)도 같은 구조의 세 번째 카운터를 사용하며, opaque 쪽이 카운터를 공유한다는 사실을 반영하지 않습니다.
4. **영향**: 세 경로의 증가 조건이 일치하지 않으므로 인덱스가 어긋납니다. `firstInstance`가 가리키는 SSBO 행과 실제 트랜스폼 데이터가 어긋나 오브젝트 위치가 뒤섞이고, 어긋난 폭이 커지면 SSBO 범위를 넘어서는 인덱싱이 발생합니다.

**[정정 · 2026-08-25]** 이전 판의 2번 항목은 "**머티리얼이 없거나 LOD가 없는 메시에 대해서도** 카운터를 증가시킨다"를 세 번째 근거로 들었습니다. **이 근거는 사실이 아니므로 삭제했습니다.** [`World.cpp:L72`](file:///e:/Haruka/src/core/World.cpp#L72)의 `World::getVisibleMeshes()`가 `m->visible && m->mesh && m->material`을 이미 걸러내므로, 머티리얼이 없는 메시는 애초에 순회 목록에 들어오지 않습니다. LOD 실패 경로도 같은 이유로 도달하지 않습니다. 실제로 성립하던 불일치 근거는 프러스텀 컬링 유무와 opaque·transparent 카운터 공유 두 가지뿐입니다. 이 오류는 `getVisibleMeshes()`의 필터 조건을 코드로 확인하지 않고 호출부의 루프 형태만 보고 추정하여 쓴 데서 비롯되었습니다. 이에 따라 1-3절의 "머티리얼이나 LOD가 없는 메시를 어떻게 처리할지 먼저 결정하라"는 지시도 전제가 사라졌으므로 함께 정리했습니다.

### 1-3. 적용된 수정 내용 (2026-08-25)

* `RenderView`에 `std::vector<const MaterialHandle*> materialRows`를 추가했습니다. 이 벡터의 인덱스가 곧 `GpuDrawCmd::firstInstance`이자 transform SSBO의 행 번호입니다.
* `buildDrawCommands`가 네 번째 출력 인자 `materialRowsOut`을 받고, `uint32_t idx = ssboIdx++;` 직후에 `materialRowsOut.push_back(mc->material)`을 수행합니다. 행 배정과 push 사이에 분기가 없으므로 `materialRows.size() == ssboIdx`가 항상 유지됩니다.
* `extractRenderView`의 World 재순회 두 루프를 **완전히 제거**했습니다. `toItems` 람다가 `opaqueDraws`와 `transpDraws`를 순회하면서 `materialRows[dc.firstInstance]`로 O(1) 역참조하여 `opaqueItems`와 `transparentItems`를 만듭니다. 항목이 드로우 커맨드에서 필드 단위로 파생되므로 둘이 어긋나는 것이 구조적으로 불가능합니다.
* `buildDrawCommands` 진입 시 `opaqueOut`과 `transpOut`도 clear하도록 했고, `materialForRow`에 assert를 추가했습니다.
* **부수 해소 (별도 ID 없음)**: 수정 전 `extractRenderView`는 `getMeshForDistanceSq(0.f)`를 호출하여 **항상 최고 LOD**를 골랐습니다. 즉 깊이 프리패스와 컬러 패스가 같은 오브젝트에 서로 다른 LOD 메시를 그릴 수 있었습니다. 재순회 루프를 제거하면서 이 호출이 사라졌으므로 LOD 선택 지점도 `buildDrawCommands` 하나로 통일되었습니다.

### 1-4. 검증 결과 (2026-08-25)

* Release 빌드를 통과했고 신규 경고는 0건입니다. 기존 `uploadCubemapMip` 미참조 경고 1건만 남아 있습니다.
* Debug 빌드를 통과했고 신규 경고는 0건입니다.
* 실행 스모크 테스트가 정상 기동했고 런타임 에러는 0건입니다.
* **검증 레이어 위반은 0건입니다.** 수정 전후의 stderr가 동일하며, 남은 것은 DEF-29로 등재한 기존 문제인 `VUID-VkDeviceCreateInfo-enabledLayerCount-12384` 1건뿐입니다.
* assert는 발동하지 않았습니다.

### 1-5. 남은 관련 작업

* 동일한 패턴이 GPU 컬링 경로에 하나 더 있었으며 **DEF-23**으로 등재하여 같은 날 해결했습니다. 8-1절을 보십시오.
* 인스턴싱(DEF-16)을 도입할 때 `materialRows`가 드로우 커맨드당 한 행을 전제한다는 점을 다시 검토해야 합니다.

---

## 2. [DEF-02] [MEDIUM · 잠재] HarukaAllocator Zone C Ring Buffer 펜스 추적 부재

### 2-1. 대상 파일 및 위치
* **파일**: [`src/memory/HarukaAllocator.cpp`](file:///e:/Haruka/src/memory/HarukaAllocator.cpp)
* **라인**: [L177-L199](file:///e:/Haruka/src/memory/HarukaAllocator.cpp#L177-L199) (`allocC`)

### 2-2. 결함 메커니즘 (Root Cause)

**[정정]** 이전 판은 이 항목을 CRITICAL로 분류하고 즉시 발생하는 데이터 레이스로 서술했으나, 실사용량을 고려하면 현재 조건에서 실제 충돌이 일어날 가능성은 사실상 없습니다. 구조적 결함은 실재하므로 항목은 유지하되, 프레임당 사용량이 커질 때의 잠재 리스크로 재분류합니다.

1. `allocC`(L177-L199)는 Zone C 링버퍼 공간을 선형 할당하며, 끝에 도달하면 시작 오프셋으로 Wrap-around 합니다. **펜스 추적이 없다는 것은 사실입니다.**
2. 그러나 `HarukaAllocator.hpp:9`의 주석에 따르면 Zone C 실사용량은 64MB 중 약 0.5MB이며, 이는 약 128배의 여유입니다.
3. 따라서 랩어라운드는 약 128프레임마다 한 번 일어나고, 그때 덮어쓰는 영역은 128프레임 전에 기록된 데이터입니다. 프레임 인플라이트 깊이가 2인 상황에서 GPU가 128프레임 전 데이터를 아직 읽고 있을 수는 없습니다.
4. **실제 리스크가 성립하는 조건**: 프레임당 Zone C 사용량이 32MB에 근접하면 랩어라운드 주기가 2프레임 수준으로 줄어들어 인플라이트 영역과 겹치기 시작합니다. 대량 인스턴싱, 동적 정점 스트리밍, 프레임당 SSBO 업로드량 증가 같은 변경이 이 문턱을 넘길 수 있습니다.

### 2-3. 수정 가이드라인 (담당 에이전트 지시사항)
* 우선순위는 DEF-03, DEF-04, DEF-08, DEF-09보다 낮습니다. (DEF-01, DEF-24, DEF-25는 2026-08-25에 해결되어 이 목록에서 제외했습니다.) 다만 사용량이 늘어난 뒤에 고치면 원인 추적이 어려운 종류의 결함이므로, 그 전에 처리하는 것이 좋습니다.
* **감시 장치 우선**: 프레임당 Zone C 소비량과 랩어라운드 주기를 계측하여, 주기가 `MAX_FRAMES`의 몇 배 이내로 떨어지면 경고를 남기도록 하십시오. 이것만으로도 잠재 리스크가 실제 리스크로 바뀌는 시점을 잡을 수 있습니다.
* **옵션 A**: Zone C를 `MAX_FRAMES`개의 독립 슬롯으로 파티셔닝하고, 각 프레임 인덱스 전용 오프셋에서만 할당한 뒤 해당 프레임의 펜스가 완료될 때 리셋합니다.
* **옵션 B**: 프레임 펜스로 확인된 안전 오프셋까지만 링 헤드가 전진할 수 있도록 테일 추적 가드를 도입합니다.

---

## 3. [DEF-03] [HIGH] `drawIndirectCount` 무조건 요구 및 폴백 부재

### 3-1. 대상 파일 및 위치
* **파일**: [`src/renderer/passes/GeometryPass.cpp`](file:///e:/Haruka/src/renderer/passes/GeometryPass.cpp), [`src/renderer/Renderer.cpp`](file:///e:/Haruka/src/renderer/Renderer.cpp)
* **라인**: `vkCmdDrawIndexedIndirectCount` 호출은 [L507](file:///e:/Haruka/src/renderer/passes/GeometryPass.cpp#L507), 디바이스 피처 요구는 [`Renderer.cpp:L599-L613`](file:///e:/Haruka/src/renderer/Renderer.cpp#L599-L613)

### 3-2. 결함 메커니즘 (Root Cause)

**[정정]** 이전 판은 이 결함이 드로우 시점에 함수 포인터 null 참조로 나타난다고 서술했으나, 실제로는 그보다 훨씬 앞선 **디바이스 생성 단계에서 실패**합니다.

* `Renderer.cpp:524`가 `VK_API_VERSION_1_4`를 요구하고, 디바이스 피처 체인은 `VkPhysicalDeviceVulkan14Features{pushDescriptor}` 다음 `Vulkan13Features{synchronization2}` 다음 `Vulkan12Features{drawIndirectCount}`로 이어집니다.
* `Renderer.cpp:599-613`이 이 체인을 **조건 없이** 요구합니다. 따라서 `drawIndirectCount`나 `synchronization2`를 제공하지 않는 디바이스에서는 드로우까지 가지 못하고 `vkCreateDevice` 단계에서 실패합니다. 즉 현재 코드는 폴백이 없는 정도가 아니라, 해당 기기에서 아예 초기화되지 않습니다.
* `GeometryPass.cpp:507`의 `vkCmdDrawIndexedIndirectCount`는 분기 없이 호출됩니다. 디바이스 생성 조건을 완화하는 순간 이 호출이 두 번째 실패 지점이 됩니다.

### 3-3. 수정 가이드라인 (담당 에이전트 지시사항)
* 수정 순서가 중요합니다. 먼저 `Renderer.cpp:599-613`의 피처 요구를 **필수 항목과 선택 항목으로 분리**하고, 선택 항목은 지원 여부를 질의한 결과를 런타임 플래그로 보관하십시오.
* 그다음 `GeometryPass.cpp:507`에 그 플래그로 분기하는 폴백 경로를 두고, 미지원 시 `vkCmdDrawIndexedIndirect`를 드로우 개수만큼 반복 호출하거나 CPU가 개수를 확정해 기록하는 경로를 쓰십시오.
* 피처 분리 없이 드로우 분기만 추가하면 디바이스 생성 단계에서 여전히 실패하므로 효과가 없습니다.

> **주의**: Quest Pro의 Adreno 드라이버가 `drawIndirectCount`를 지원하는지 여부는 이 문서 작성 시점에 **실기로 확인되지 않았습니다**. 이전 판의 "일부 드라이버에서 미지원"이라는 서술은 근거가 확인되지 않은 추정입니다. 폴백 경로 자체는 실기 확인과 무관하게 필요하지만, 지원 여부를 단정한 서술은 실측 전까지 사용하지 마십시오.

---

## 4. [DEF-04] [HIGH] `vkCmdPipelineBarrier2` (Synchronization2) 무조건 호출

### 4-1. 대상 파일 및 위치
* **파일**: [`src/renderer/RenderContext.cpp`](file:///e:/Haruka/src/renderer/RenderContext.cpp), [`src/renderer/passes/CullPass.cpp`](file:///e:/Haruka/src/renderer/passes/CullPass.cpp), [`src/renderer/Renderer.cpp`](file:///e:/Haruka/src/renderer/Renderer.cpp)
* **라인**: [`RenderContext.cpp:L167`](file:///e:/Haruka/src/renderer/RenderContext.cpp#L167), [`CullPass.cpp:L166`](file:///e:/Haruka/src/renderer/passes/CullPass.cpp#L166), [`CullPass.cpp:L194`](file:///e:/Haruka/src/renderer/passes/CullPass.cpp#L194), 디바이스 피처 요구는 [`Renderer.cpp:L599-L613`](file:///e:/Haruka/src/renderer/Renderer.cpp#L599-L613)

### 4-2. 결함 메커니즘 (Root Cause)

**[정정]** 이전 판은 호출 지점을 `RenderContext.cpp`의 `transitionImage` 한 곳(L154-L168)으로만 적었으나, 실제 호출 지점은 세 곳이며 정확한 위치도 다릅니다. 그리고 DEF-03과 마찬가지로 실패는 디바이스 생성 단계에서 먼저 일어납니다.

* `vkCmdPipelineBarrier2`가 `RenderContext.cpp:167`, `CullPass.cpp:166`, `CullPass.cpp:194` 세 곳에서 **무조건** 호출됩니다.
* `Renderer.cpp:599-613`이 `Vulkan13Features{synchronization2}`를 조건 없이 요구하므로, 이 피처가 없는 디바이스에서는 `vkCreateDevice`부터 실패합니다.

### 4-3. 수정 가이드라인 (담당 에이전트 지시사항)
* DEF-03과 동일한 순서를 따르십시오. `Renderer.cpp:599-613`의 피처 요구를 선택 항목으로 내리고 런타임 플래그를 보관한 뒤, 호출 지점에 분기를 넣습니다.
* 호출 지점이 세 곳이므로 배리어 발행을 얇은 래퍼 함수 하나로 모은 뒤 그 안에서만 분기하는 편이 안전합니다. 세 곳에 각각 분기를 복사하면 이후 호출 지점이 늘어날 때 누락이 생깁니다.
* 래퍼는 `VkImageMemoryBarrier2`를 받아 미지원 시 `VkImageMemoryBarrier`와 `vkCmdPipelineBarrier`로 변환하는 형태가 됩니다. `VkPipelineStageFlags2`의 상위 비트 스테이지에는 1.0 대응이 없으므로, 변환 표를 만들 때 어느 스테이지를 어떻게 근사할지 명시적으로 결정하십시오.

---

## 5. [DEF-05] [해결됨] 스왑체인 리사이즈 시 GPU In-Flight 자원 파괴 위험

### 5-1. 대상 파일 및 위치
* **파일**: [`src/renderer/Renderer_swap.cpp`](file:///e:/Haruka/src/renderer/Renderer_swap.cpp)
* **라인**: [L59-L60](file:///e:/Haruka/src/renderer/Renderer_swap.cpp#L59-L60)

### 5-2. 상태: 이미 해결되어 있습니다

**[정정]** 이전 판은 이 항목을 미해결 HIGH 결함으로 분류하고 대상 파일을 `Renderer_render.cpp`로 적었으나, 두 가지 모두 사실과 다릅니다. `recreateSwapChain`은 `Renderer_swap.cpp`에 있으며, 요구하던 수정은 이미 코드에 들어 있습니다.

* **근거**: `Renderer_swap.cpp:59-60`의 `recreateSwapChain`이 함수 진입 직후 `vkDeviceWaitIdle(device)`를 호출한 뒤에 `cleanupSwapChain()`을 실행합니다. 즉 크기 의존 자원을 파괴하기 전에 GPU가 모든 작업을 마치도록 보장하고 있습니다.

### 5-3. 후속 조치
* 이 항목에 대해 새로 작성할 코드는 없습니다. 체크리스트에서 완료로 표기하십시오.
* 남는 작업은 검증뿐입니다. 크기 의존 자원을 해제하는 새로운 경로를 추가할 때 그 경로가 `recreateSwapChain`을 거치는지 확인하십시오. `vkDeviceWaitIdle`은 `recreateSwapChain` 진입부에만 있으므로, 이 함수를 우회하여 자원을 해제하면 같은 위험이 되살아납니다.

---

## 6. [DEF-06] [HIGH] MSAA `LAZILY_ALLOCATED` 메모리 속성 누락

### 6-1. 대상 파일 및 위치
* **파일**: [`src/renderer/passes/GeometryPass.cpp`](file:///e:/Haruka/src/renderer/passes/GeometryPass.cpp)
* **라인**: [L239](file:///e:/Haruka/src/renderer/passes/GeometryPass.cpp#L239)

### 6-2. 결함 메커니즘 (Root Cause)

**[정정]** 이전 판은 사용 플래그와 메모리 속성이 **둘 다** 빠져 있다고 서술했으나, 절반은 이미 되어 있습니다. `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT`는 `GeometryPass.cpp:239`에 이미 들어 있습니다.

* 실제로 빠진 것은 메모리 속성 한 가지뿐입니다. `VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT`는 코드베이스 전체에서 검색 결과가 0건입니다.
* MSAA 컬러 버퍼는 서브패스 진행 중 타일 내부에서만 쓰이고 Resolve 후 버려지므로, 사용 플래그만으로는 실제 물리 메모리 커밋을 피할 수 없습니다. 지연 할당 메모리 타입에 바인딩해야 타일 기반 GPU가 백업 저장소를 생략할 수 있습니다.

### 6-3. 수정 가이드라인 (담당 에이전트 지시사항)
* 작업 범위는 **메모리 타입 선택 한 곳**입니다. 사용 플래그는 건드릴 필요가 없습니다.
* MSAA 이미지의 메모리 타입을 고를 때 `VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT`를 가진 타입을 먼저 탐색하고, 없으면 기존 경로로 내려가도록 하십시오. 데스크톱 GPU에는 이 메모리 타입이 대체로 존재하지 않으므로 폴백이 반드시 필요합니다.
* 현재 MSAA 이미지가 Zone B에 할당되고 있으므로, 지연 할당 메모리는 `HarukaAllocator`의 Zone 경로를 우회하는 별도 할당이 됩니다. 해제 경로가 어느 쪽으로 가는지 함께 정리하십시오.
* 깊이 이미지도 서브패스 밖으로 나가지 않는다면 같은 처리 대상입니다. 다만 현재 깊이 이미지가 서브패스 밖에서 읽히는지는 확인되지 않았으므로, 확인 후에 적용하십시오.

---

## 7. [DEF-07] [MEDIUM] `onActorDestroyed` 구독자 부재로 인한 댕글링 포인터

### 7-1. 대상 파일 및 위치
* **파일**: [`src/core/World.hpp`](file:///e:/Haruka/src/core/World.hpp) ([L27](file:///e:/Haruka/src/core/World.hpp#L27) 선언), [`src/core/World.cpp`](file:///e:/Haruka/src/core/World.cpp) ([L27](file:///e:/Haruka/src/core/World.cpp#L27) 브로드캐스트)

### 7-2. 결함 메커니즘 (Root Cause)

**[정정]** 이전 판은 이 결함을 통지 구조 자체가 없는 문제로 서술했으나, 인프라는 절반이 지어져 있습니다. 빠진 것은 구조가 아니라 연결입니다.

* **이미 있는 것 (1)**: `onActorDestroyed` 이벤트가 `World.hpp:27`에 선언되어 있고 `World.cpp:27`에서 실제로 브로드캐스트됩니다.
* **이미 있는 것 (2)**: 틱 도중에 들어온 파괴 요청을 지연시키는 큐가 이미 구현되어 있습니다. 즉 틱 중간에 컨테이너가 변형되는 종류의 문제는 이미 막혀 있습니다. (해당 코드의 정확한 위치는 이 문서에서 확인하지 않았습니다.)
* **빠진 것**: `onActorDestroyed`의 **구독자가 한 곳도 없습니다.** 이벤트는 매번 발행되지만 아무도 듣지 않으므로, `PhysicsSystem`의 바디 핸들, 오디오 쪽 핸들, 에디터의 선택 포인터와 언두 스택에 남은 `Actor*`가 그대로 남습니다.
* 따라서 크래시 경로는 파괴 시점의 컨테이너 변형이 아니라, 파괴 이후에 무효화되지 않은 참조를 역참조하는 시점입니다.

### 7-3. 수정 가이드라인 (담당 에이전트 지시사항)
* 새 수명주기 구조를 설계하기 전에, **먼저 기존 이벤트에 구독자를 붙이십시오.** 작업량이 훨씬 적고 즉시 효과가 있습니다.
* 최소 구독자 목록은 다음과 같습니다.
  * `PhysicsSystem`: 해당 액터의 바디를 제거하고 동기화 목록에서 뺍니다.
  * 에디터: 선택 포인터가 파괴된 액터를 가리키면 비웁니다.
  * 에디터 언두/리두 스택: 파괴된 액터를 참조하는 항목을 무효화하거나 스택을 잘라냅니다.
* `Actor*` 생포인터를 `uint64_t` 핸들로 전면 전환하는 방안은 여전히 유효하지만, 규모가 큰 변경이므로 위 연결 작업 이후의 별도 과제로 분리하십시오.

---

## 8. 2026-08-25 신규 등재 결함 작업 지시서 (DEF-23 ~ DEF-30)

아래 항목은 DEF-01을 수정하면서 같은 코드 경로를 실측하는 과정에서 발견한 것입니다. **전부 Layer 1(코어)에 속합니다.** DEF-23, DEF-24, DEF-25는 같은 날 해결했고, **DEF-26·DEF-27은 2026-08-27에 해결**했습니다(섀도 정확성 + per-light Cast Shadows). DEF-28~DEF-30은 미해결이므로 담당 에이전트의 작업 대상입니다. DEF-33부터 DEF-35까지는 DEF-24 수정에서 도출된 라이팅 후속 항목이며, 전부 실측 후 판단할 대상입니다.

---

### 8-1. [DEF-23] [해결됨 · 2026-08-25] 스피어 인덱스 도메인 불일치

#### 대상 파일 및 위치
* **파일**: [`shaders/cull.comp`](file:///e:/Haruka/shaders/cull.comp), [`src/renderer/passes/CullPass.cpp`](file:///e:/Haruka/src/renderer/passes/CullPass.cpp), [`src/renderer/Renderer.hpp`](file:///e:/Haruka/src/renderer/Renderer.hpp), [`src/renderer/Renderer_render.cpp`](file:///e:/Haruka/src/renderer/Renderer_render.cpp)
* **라인**: [`cull.comp:L54`](file:///e:/Haruka/shaders/cull.comp#L54), [`cull.comp:L57`](file:///e:/Haruka/shaders/cull.comp#L57), 증상 관련은 [`GeometryPass.cpp:L88`](file:///e:/Haruka/src/renderer/passes/GeometryPass.cpp#L88)

**상태**: **해결되었습니다 (2026-08-25).** 원래 심각도는 CRITICAL이었습니다.

#### 결함 메커니즘 (Root Cause)

이 결함은 **DEF-01과 동일한 패턴**이 네 번째 경로에 남아 있던 것입니다. 하나의 인덱스가 서로 다른 두 도메인에서 발급되어 소비 측에서 같은 값으로 취급되는 구조입니다.

1. `cull.comp:54`와 `cull.comp:57`이 `spheres[id]`와 `inDraws[id]`를 **같은 `id`**로 읽습니다. `id`의 범위는 `inputCount`, 즉 `opaqueDraws.size()`입니다.
2. `inDraws`는 `CullPass::uploadInput`이 `opaque.data()`를 오프셋 0부터 memcpy하므로 **opaque 목록 내 위치**로 인덱싱됩니다.
3. 그런데 스피어는 `sphData[idx]`로, 즉 **전역 transform 행 번호**로 기록되고 있었습니다. 두 도메인은 투명 메시가 하나도 없을 때만 우연히 일치하며, 투명 메시가 한 개라도 앞선 행을 차지하면 즉시 어긋납니다.
4. **증상**: 잘못 컬링된 불투명 메시가 깊이 프리패스에서 빠집니다. 그런데 컬러 서브패스는 `GeometryPass.cpp:88`에서 `depthWriteEnable = VK_FALSE`이므로 그 메시의 깊이가 **어디에도 기록되지 않습니다.** 결과적으로 더 먼 오브젝트가 앞의 것을 덮어씁니다.

#### DEF-01 수정만으로는 부족했던 이유

DEF-01의 수정은 CPU 측 세 경로(드로우 커맨드, opaque 항목, transparent 항목)를 `materialRows`라는 단일 인덱스 도메인으로 통일했습니다. 그러나 GPU 컬링에 넘기는 바운딩 스피어는 그 세 경로 어디에도 속하지 않는 **별도의 업로드 경로**를 타므로 통일 대상에서 빠져 있었습니다. CPU 항목들이 전부 정합해진 뒤에도 GPU만 다른 행을 읽는 상태가 남았습니다. 따라서 DEF-01을 닫을 때 이 경로를 함께 점검하지 않았다면 증상이 그대로 남았을 것입니다.

#### 적용된 수정 내용
* `RenderView`에 `std::vector<GpuBoundSphere> opaqueSpheres`를 추가했습니다.
* `buildDrawCommands`가 `opaqueOut.push_back(cmd)`과 **같은 자리에서** `opaqueSpheresOut.push_back(...)`을 수행합니다. 두 벡터가 같은 문장 흐름에서 자라므로 도메인이 구조적으로 일치합니다.
* `CullPass::uploadInput(fi, opaque, spheres)`가 둘을 함께 업로드하고 크기 일치를 assert합니다.
* `CullPass::sphereData()` 접근자는 제거했습니다. 외부에서 스피어 버퍼에 직접 쓰는 통로를 남겨 두면 같은 결함이 재발하기 때문입니다.
* 검증 결과는 1-4절과 동일한 실행에서 확인했습니다.

> **주의**: 스피어 인덱스를 고쳐도 프리패스와 컬러 서브패스의 가시 집합 불일치 자체는 남았습니다. 판정 기준이 CPU와 GPU에서 서로 달랐기 때문이며, 이는 DEF-25로 별도 등재하여 **같은 날 해결했습니다.** 8-3절을 보십시오.

---

### 8-2. [DEF-24] [해결됨 · 2026-08-25] 라이트 개수 이중 계산 (원래 HIGH)

#### 대상 파일 및 위치
* [`Renderer_render.cpp`](file:///e:/Haruka/src/renderer/Renderer_render.cpp) (`updateUBO` / `updateLightSSBO`), [`Renderer.hpp`](file:///e:/Haruka/src/renderer/Renderer.hpp), [`RenderTypes.hpp`](file:///e:/Haruka/src/renderer/RenderTypes.hpp), [`pbr.frag`](file:///e:/Haruka/shaders/pbr.frag)

#### 결함 메커니즘 (Root Cause)
1. `updateUBO`가 `lights.size()`를 그대로 UBO의 `miscParams.x`에 넣었습니다.
2. 그러나 `updateLightSSBO`는 `kMaxLights`에서 break합니다. 두 값이 어긋나면 `pbr.frag`가 `miscParams.x`까지 순회하면서 SSBO 범위 밖을 읽습니다.
3. **구조적 원인**: `updateUBO`가 `updateLightSSBO`보다 먼저 호출되어 UBO 측이 개수를 독자적으로 재계산할 수밖에 없었습니다. 하나의 값이 두 곳에서 따로 계산되는, DEF-01과 같은 형태의 문제입니다.

> **[정정 · 2026-08-25]** 이전 판은 `kMaxLights = 16`과 "라이트가 17개 이상이면 범위 밖 읽기"를 근거로 삼았습니다. **그 전제는 무효입니다.** 상한은 현재 256이고, 16이라는 값 자체에 근거가 없었습니다. `pbr.frag`의 `lights[]`는 크기 없는 SSBO이며 디스크립터도 `VK_WHOLE_SIZE`로 바인딩하므로 셰이더 쪽에는 상한이 없습니다.

#### 수정 내용
1. **상한 상향**: `kMaxLights` 16 → **256**. 하드웨어 제약이 아니라 메모리 예산 상한임을 근거와 함께 주석으로 남겼습니다. 256개에 32KB이며 Zone C 기준으로 무시할 수준입니다.
2. **단일 계산 지점화**: `updateLightSSBO`가 `uint32_t`로 실제 기록 개수를 반환하고, `updateUBO(World&, uint32_t lightCount)`가 인자로 받습니다. `extractRenderView`에서 호출 순서를 뒤집었습니다.
3. **셰이더 얼리아웃**: `evalLight`에서 `atten <= 0.0` 또는 `NdotL <= 0.0`이면 즉시 반환합니다. 최종식이 `... * NdotL * atten`이므로 출력은 비트 단위로 동일하고 낭비 계산만 사라집니다.

#### 설계 방침
이 단계에서는 인위적인 성능 상한을 두지 않습니다. 최대 성능을 먼저 확인한 뒤 기기에 맞추는 방향이며 PC 구동 가능성도 고려한 결정입니다. 기기별 상한은 실기 측정 후 **런타임 설정으로 낮추는 것이지 `kMaxLights`를 조이는 것이 아닙니다.**

#### 검증
Release/Debug 빌드 통과, 신규 경고 0, 실행 정상, 검증 레이어 위반 0건, assert 미발동.

---

### 8-2-a. [DEF-33] [MEDIUM] 라이트 선택 정책 부재

* **착수 조건**: 라이트 수가 상한에 실제로 걸릴 때입니다. 실측 없이 지금 만들 항목이 아닙니다.
* **메커니즘**: `updateLightSSBO`는 `World::getLights()`의 순서, 즉 액터 생성 순서대로 채우고 상한에서 자릅니다. 거리나 화면 기여도를 보지 않으므로, 상한에 걸리면 가까운 조명이 꺼지고 먼 조명이 켜집니다.
* **수정 가이드라인**: 카메라 거리와 화면 기여도로 비용을 매겨 상위 N개를 고르십시오. Carbon Engine `audio` 저장소의 `SoundPrioritization`이 오디오에서 같은 문제를 푸는 구조입니다.

---

### 8-2-b. [DEF-34] [MEDIUM] 타일드 또는 클러스터드 라이팅 부재

* **착수 조건**: `pbr.frag`의 픽셀당 루프가 병목으로 측정될 때입니다.
* **메커니즘**: 픽셀마다 전체 라이트 배열을 순회합니다. 거리 컬링도 타일 분할도 없으므로 화면 구석의 점광원 하나가 모든 픽셀에서 평가됩니다.
* **수정 가이드라인**: 화면을 타일로 나누고 타일별 라이트 목록만 순회하면 개수 상한이 사실상 의미를 잃습니다. Trinity의 `Tr2LightManager`(`LightBuffer` + `LightIndexBuffer`)가 그 방식입니다. 렌더러 구조 변경이므로 VR 코어 완성보다 뒤입니다.

---

### 8-2-c. [DEF-35] [LOW] `evalLight`의 0 나눗셈 가능성

* **메커니즘**: `lightDir = toLight / dist`에서 라이트와 표면점이 정확히 일치하면 `dist`가 0이 되어 NaN이 됩니다.
* **판단**: 극단적인 경우이므로 DEF-24 수정 시 함께 손대지 않았습니다. 광원이 표면과 겹칠 수 있는 연출을 넣을 때 재검토하십시오.

---

### 8-3. [DEF-25] [해결됨 · 2026-08-25] 깊이 프리패스와 컬러 서브패스의 가시 집합 불일치

#### 대상 파일 및 위치
* **수정 파일**: [`src/renderer/Renderer_render.cpp`](file:///e:/Haruka/src/renderer/Renderer_render.cpp), [`src/renderer/Renderer.hpp`](file:///e:/Haruka/src/renderer/Renderer.hpp)
* **증상이 나타나던 위치**: [`GeometryPass.cpp:L330`](file:///e:/Haruka/src/renderer/passes/GeometryPass.cpp#L330) (subpass 0의 `depthWriteEnable = TRUE`), [`GeometryPass.cpp:L88`](file:///e:/Haruka/src/renderer/passes/GeometryPass.cpp#L88) (subpass 1의 `depthWriteEnable = FALSE`)

**상태**: **해결되었습니다 (2026-08-25).** 원래 심각도는 HIGH였습니다.

#### 결함 메커니즘 (Root Cause)

**진짜 원인은 인덱스가 아니라 바운딩 도형의 불일치였습니다.** CPU와 GPU가 **서로 다른 도형**으로 컬링하고 있었습니다.

1. **CPU**: `aabbInFrustum`이 로컬 AABB의 여덟 꼭짓점을 월드로 변환한 뒤 그것을 다시 축 정렬 상자로 감싸고, 그 상자에 positive-vertex 테스트를 적용했습니다.
2. **GPU**: [`shaders/cull.comp`](file:///e:/Haruka/shaders/cull.comp)의 `sphereInFrustum`이 바운딩 구로 판정했습니다.
3. 구는 로컬 AABB를 감싸도록 만들어집니다([`GpuScene.cpp:L78-L79`](file:///e:/Haruka/src/renderer/GpuScene.cpp#L78-L79)의 `boundCenter = (aabbMin + aabbMax) * 0.5`, `boundRadius = length(aabbMax - boundCenter)`).
4. **회전이 두 판정을 갈라놓습니다.** 회전이 없으면 구가 더 관대하지만, 물체가 회전하면 **CPU가 재계산한 월드 AABB가 구보다 커질 수 있습니다.** 축 정렬 상자는 회전한 형상을 다시 감싸면서 부피가 늘어나는 반면, 구는 회전에 불변이기 때문입니다.
5. **수치 예시**: 10x1x1 메시를 Z축으로 45도 회전시키면 구의 반지름은 약 **5.05**인데 재계산된 월드 AABB의 대각 반경은 약 **5.52**입니다. 그 차이만큼의 띠에서 **CPU는 살리고 GPU는 버립니다.**
6. **그 결과가 심각했던 이유**: GPU 컬링 결과는 subpass 0(깊이 프리패스, `depthWriteEnable = TRUE`, `GeometryPass.cpp:330`)이 그리고, CPU 결과는 subpass 1(컬러, `depthWriteEnable = FALSE`, `GeometryPass.cpp:88`)이 그립니다. GPU가 버린 메시는 **깊이를 어디에도 기록하지 못한 채** 컬러 패스에서만 그려지므로, 나중에 그려지는 더 먼 오브젝트가 그 위를 덮어씁니다. 그리기 순서가 깊이 순서를 이깁니다.

#### 적용된 수정 내용 (2026-08-25)

* `aabbInFrustum`을 제거하고 `sphereInFrustum`으로 교체했습니다. [`shaders/cull.comp`](file:///e:/Haruka/shaders/cull.comp)의 `sphereInFrustum`과 **동일한 판정식**입니다(`dot(plane.normal, center) + plane.d < -radius`이면 컬링합니다). CPU의 `extractFrustumPlanes`가 이미 평면을 정규화하고 있으므로 구 판정에 그대로 쓸 수 있습니다.
* 월드 공간 바운딩 구를 `buildDrawCommands` 안에서 **한 번만** 계산하여 컬링 판정과 `opaqueSpheres` 업로드에 함께 씁니다. 두 곳에서 따로 계산하면 그것이 곧 DEF-01, DEF-23과 같은 불일치의 씨앗이 되기 때문입니다.
* 구는 회전에 불변이므로, 물체가 회전할 때 프레임마다 컬링 판정이 흔들리던 현상도 함께 사라졌습니다.
* **부수 효과**: 투명 메시는 GPU 2차 컬링을 받지 않으므로 **컬링이 소폭 헐거워집니다.** 불투명은 CPU 판정 뒤에 GPU가 한 번 더 거르므로 실질 손해가 거의 없습니다.

#### 검증 결과 (2026-08-25)

* Release 빌드와 Debug 빌드를 모두 통과했고 신규 경고는 0건입니다.
* 실행이 정상이며 검증 레이어 위반은 0건입니다. 남은 것은 DEF-29로 등재한 기존 문제인 `VUID-VkDeviceCreateInfo-enabledLayerCount-12384` 1건뿐입니다.

#### 재발 방지 계약 (반드시 지켜야 합니다)

> [`src/renderer/Renderer_render.cpp`](file:///e:/Haruka/src/renderer/Renderer_render.cpp)의 `sphereInFrustum`과 [`shaders/cull.comp`](file:///e:/Haruka/shaders/cull.comp)의 `sphereInFrustum`은 **항상 같은 입력에 대해 같은 판정을 내려야 합니다. 한쪽을 고치면 반드시 다른 쪽도 같이 고쳐야 합니다.**

이 계약이 필요한 이유는 두 판정 결과가 **서로 다른 서브패스의 그리기 집합이 되기 때문입니다.** CPU 판정은 컬러 서브패스(subpass 1)의 집합을 정하고, GPU 판정은 깊이 프리패스(subpass 0)의 집합을 정합니다. subpass 1이 `depthWriteEnable = FALSE`로 도는 것은 "프리패스가 모든 가시 메시의 깊이를 이미 기록했다"는 계약에 기대는 것이므로, 두 판정이 한 메시에 대해 갈리는 순간 그 메시의 깊이가 통째로 사라집니다. **판정식이 어긋나면 성능이 조금 나빠지는 것이 아니라 화면이 깨집니다.**

프러스텀 평면 추출 방식(`extractFrustumPlanes`의 정규화 여부), 바운딩 구의 정의([`GpuScene.cpp:L78-L79`](file:///e:/Haruka/src/renderer/GpuScene.cpp#L78-L79)), 부호 규약 가운데 어느 하나를 바꿀 때도 같은 규칙이 적용됩니다. DEF-31과 DEF-32처럼 평면 집합 자체를 바꾸는 작업에도 그대로 적용됩니다.

---

### 8-4. [DEF-26] [해결됨 · 2026-08-27] 섀도 캐스터 라이트 선택 불일치

> **해결**: `updateLightSSBO` 가 캐스터(첫 Directional + `castShadows`) 슬롯을 채우기와 **같은 루프**에서
> 기록(`shadowCasterSlot_`) → `updateUBO` 가 `miscParams.w` 로 슬롯 전달 → `pbr.frag` 가 그 슬롯 라이트의
> 기여에만 그림자 곱(`if(i==shadowIdx) c*=shadow`). 슬롯 0 가정·`Lo*=shadow` 전역 곱 모두 해소. 정본은
> `Core_Stabilization_Spec.md` DEF-26. 아래 원본 지시사항은 이력으로 남깁니다.

#### 대상 파일 및 위치
* [`Renderer_render.cpp:L181`](file:///e:/Haruka/src/renderer/Renderer_render.cpp#L181), [`pbr.frag:L163`](file:///e:/Haruka/shaders/pbr.frag#L163), [`pbr.frag:L165`](file:///e:/Haruka/shaders/pbr.frag#L165), `updateLightSSBO`

#### 결함 메커니즘 (Root Cause)
1. `Renderer_render.cpp:181`은 월드 순서상 **첫 Directional** 라이트로 섀도 카메라를 만듭니다.
2. 그런데 `pbr.frag:163`은 **슬롯 0이 Directional**이기를 요구합니다.
3. `updateLightSSBO`는 타입과 무관하게 월드 순서로 슬롯을 채웁니다. 따라서 포인트 라이트가 먼저 등록되면 섀도 맵 자체는 정상적으로 렌더링되는데 셰이더 가드가 실패하여 **그림자가 통째로 사라집니다.**
4. **별개 결함 (같은 셰이더의 인접 지점)**: `pbr.frag:165`의 `Lo *= shadow`가 디렉셔널 그림자를 **모든 라이트의 기여**에 곱합니다. 포인트 라이트가 비추는 영역까지 디렉셔널 그림자로 어두워집니다. 원인이 다르므로 위 슬롯 문제와 구분하여 다루십시오.

#### 수정 가이드라인 (담당 에이전트 지시사항)
* 섀도 캐스터로 선택한 라이트의 **슬롯 번호를 UBO로 셰이더에 전달**하여 셰이더가 슬롯 0을 가정하지 않도록 하십시오. 이것이 두 지점의 규칙을 하나로 묶는 방법입니다.
* 대안으로 `updateLightSSBO`가 Directional을 슬롯 0에 오도록 정렬할 수도 있습니다. 이 경우 선택 지점과 채우기 지점이 같은 규칙을 쓰는지 assert로 고정하십시오.
* `Lo *= shadow`는 해당 라이트의 기여에만 그림자가 곱해지도록 라이트 루프 안으로 옮기십시오.

---

### 8-5. [DEF-27] [해결됨 · 2026-08-27] 섀도 패스가 카메라 프러스텀으로 걸러진 목록 사용

> **해결**: `buildDrawCommands` 를 프러스텀별로 일반화(inCam/inLight 분리), 라이트 프러스텀 섀도 목록
> `RenderView::shadowDraws` 를 별도 생성해 `drawFrame` 이 섀도 패스에 전달. 행 배정은 **카메라 우선 2단계**
> (라이트 전용 행이 컬러 패스 카메라 드로우를 못 밀어냄 — 적대적 검증이 kMaxDraws 오버플로 회귀를 잡아
> 수정). 정본은 `Core_Stabilization_Spec.md` DEF-27. 아래 원본 지시사항은 이력으로 남깁니다.

#### 대상 파일 및 위치
* [`src/renderer/Renderer_render.cpp`](file:///e:/Haruka/src/renderer/Renderer_render.cpp): `drawFrame`이 `view.opaqueDraws`를 `shadow_.record`에 전달하는 지점과 `buildDrawCommands`

#### 결함 메커니즘 (Root Cause)
* `drawFrame`이 `view.opaqueDraws`를 그대로 `shadow_.record`에 넘기는데, 그 목록은 `buildDrawCommands`에서 **카메라** 프러스텀으로 걸러진 것입니다.
* 라이트 프러스텀 안에 있지만 카메라 프러스텀 밖에 있는 캐스터는 목록에 없으므로 그림자를 던지지 못합니다.
* **증상**: 카메라를 돌리면 그림자가 나타났다 사라집니다. 오브젝트 자체는 화면 밖이고 그림자만 화면 안에 들어와야 하는 구도에서 특히 두드러집니다.

#### 수정 가이드라인 (담당 에이전트 지시사항)
* 섀도 패스에는 **라이트 프러스텀으로 걸러진 별도의 드로우 목록**을 만들어 넘기십시오.
* `buildDrawCommands`를 프러스텀을 인자로 받도록 일반화하여 카메라용과 라이트용을 각각 호출하는 방식이 자연스럽습니다.
* **주의**: 두 호출이 transform SSBO 행을 공유해야 합니다. 행 배정 지점이 하나로 유지되는지(DEF-01의 원칙) 반드시 확인하십시오. 라이트용 호출이 별도의 `ssboIdx`를 새로 세기 시작하면 DEF-01과 DEF-23이 그대로 재발합니다.

---

### 8-6. [DEF-28] [MEDIUM] 머티리얼 null 항목의 드로우 강행

#### 대상 파일 및 위치
* [`GeometryPass.cpp:L526`](file:///e:/Haruka/src/renderer/passes/GeometryPass.cpp#L526), [`GeometryPass.cpp:L552`](file:///e:/Haruka/src/renderer/passes/GeometryPass.cpp#L552)

#### 결함 메커니즘 (Root Cause)
* 두 지점의 `if (item.material)` 가드가 **푸시 디스크립터 바인딩만 건너뛰고** `vkCmdDrawIndexed`는 그대로 실행합니다.
* 직전 항목의 set 3이 남아 있으면 엉뚱한 텍스처로 그려지고, 해당 항목이 첫 항목이면 미바인딩 상태에서 드로우가 발행됩니다.
* **현재 도달 가능성**: `World::getVisibleMeshes()`가 [`World.cpp:L72`](file:///e:/Haruka/src/core/World.cpp#L72)에서 머티리얼 없는 메시를 걸러내므로 **현재는 도달 불가입니다.** 그러나 `DrawItem`의 계약이 `material == nullptr`을 허용하는 형태로 열려 있으므로, 다른 경로로 항목을 만들면 즉시 발현합니다.

#### 수정 가이드라인 (담당 에이전트 지시사항)
* 두 지점 모두 `if (!item.material) continue;`로 바꾸십시오. 한 줄이면 끝나고 위험도가 없습니다.
* 나아가 `DrawItem`이 애초에 null 머티리얼을 담을 수 없도록 계약을 좁히거나, 기본 머티리얼로 대체하는 규칙을 명시하십시오. 현재는 계약이 열려 있고 구현이 그 계약을 지키지 않는 상태입니다.

---

### 8-7. [DEF-29] [LOW] `VkDeviceCreateInfo`의 검증 레이어 설정

#### 대상 파일 및 위치
* [`Renderer.cpp:L625-L626`](file:///e:/Haruka/src/renderer/Renderer.cpp#L625-L626)

#### 결함 메커니즘 (Root Cause)
* `Renderer.cpp:625-626`이 `VkDeviceCreateInfo`에 검증 레이어를 넣습니다. 이 필드는 Vulkan 1.0부터 무시되며, 검증 레이어가 `VUID-VkDeviceCreateInfo-enabledLayerCount-12384`로 경고합니다.
* 2026-08-25 시점에 stderr에 남아 있는 **유일한 검증 레이어 위반**입니다.

#### 수정 가이드라인 (담당 에이전트 지시사항)
* 두 줄을 삭제하면 끝납니다.
* 인스턴스 쪽(`Renderer.cpp:533-534`)의 레이어 설정은 올바르므로 건드리지 마십시오. 디바이스 레이어를 지운다고 검증이 꺼지지는 않습니다.

---

### 8-8. [DEF-30] [LOW] 투명 정렬 comparator의 매핑 메모리 읽기

#### 대상 파일 및 위치
* [`src/renderer/Renderer_render.cpp`](file:///e:/Haruka/src/renderer/Renderer_render.cpp): 투명 정렬 comparator

#### 결함 메커니즘 (Root Cause)
* comparator가 `tfData[...].model[3]`을 직접 읽어 깊이를 비교합니다.
* `tfData`는 HOST_VISIBLE 매핑 메모리이며 uncached이므로, 정렬 과정에서 **N log N 번의 uncached read**가 발생합니다. 정렬 대상이 늘어날수록 CPU 프레임 시간에 비선형으로 반영됩니다.

#### 수정 가이드라인 (담당 에이전트 지시사항)
* 정렬 전에 각 항목의 위치를 로컬 `std::vector`에 한 번씩만 읽어 캐시하고, comparator는 그 로컬 벡터만 읽도록 하십시오.
* 기능 변화가 없는 순수 성능 수정이므로 위험도가 낮습니다. 정렬 결과가 수정 전과 동일한지만 확인하면 충분합니다.

---

## 9. VR 스테레오 컬링 작업 지시서 (DEF-31 ~ DEF-32)

DEF-25를 수정하면서 CPU와 GPU의 컬링 판정을 하나로 통일했습니다. 그런데 통일된 그 판정은 **단안 절두체 하나**를 전제합니다. 이 엔진의 목적은 Vulkan 기반 VR이므로, 컬링 코드는 스테레오에서 반드시 다르게 동작해야 합니다. 아래 두 항목은 그 요구사항을 결함으로 등재한 것입니다. **둘 다 Layer 1(코어)에 속하며 미해결입니다.**

두 항목의 **착수 시점은 OpenXR 세션 도입 시**입니다. 그 전에는 검증할 대상 자체가 없으므로, 지금 코드를 미리 고치지 마십시오.

---

### 9-1. [DEF-31] [HIGH] 두 눈 합집합 절두체 부재

#### 대상 파일 및 위치
* [`src/renderer/Renderer_render.cpp`](file:///e:/Haruka/src/renderer/Renderer_render.cpp): `extractRenderView`가 `RenderView::cullViewProj`를 채우는 지점
* [`CullPass.cpp:L179-L181`](file:///e:/Haruka/src/renderer/passes/CullPass.cpp#L179-L181): `CullPass::record`가 받는 `CullPush`

**착수 시점**: **OpenXR 세션 도입 시 필수입니다.**

#### 결함 메커니즘 (Root Cause)
1. `RenderView::cullViewProj`가 현재 `viewProj[0]` 하나를 그대로 씁니다. 즉 컬링 절두체가 **한쪽 눈의 것**입니다.
2. `CullPass::record`도 push constant로 `mat4` 하나만 받습니다(`CullPass.cpp:179-181`의 `CullPush`). 자료 구조 자체가 절두체 하나를 전제합니다.
3. 스테레오에서 한쪽 눈 절두체로 컬링하면, 그 절두체 밖이지만 반대쪽 눈 절두체 안에 있는 물체가 버려집니다. 결과적으로 **한쪽 눈에만 물체가 사라져 양안 경합이 발생합니다.**
4. **평면 화면과 다른 점**: 평면 게임에서 같은 오류는 화면 가장자리의 팝인으로 끝납니다. 그러나 스테레오에서는 두 눈이 서로 다른 장면을 보게 되므로 **즉각적인 불쾌감으로 이어집니다.** 심각도를 HIGH로 잡은 이유가 이것입니다.

#### 수정 가이드라인 (담당 에이전트 지시사항)
* 좌우 두 절두체를 **모두 감싸는 합집합 절두체**에서 평면을 뽑아야 합니다.
* **Quest Pro는 디스플레이가 안쪽으로 기울어져 있어(canted) 두 절두체가 평행하지 않습니다. 따라서 한쪽 절두체를 좌우로 넓히는 확장 근사는 쓸 수 없습니다.** 그런 근사는 두 절두체가 평행할 때만 성립합니다. 실제 합집합을 보수적으로 감싸는 방식이어야 합니다.
* `cullViewProj` 하나를 넘기는 구조 대신 **컬링 평면 집합 자체를 넘기는 구조**로 바꾸는 편이 자연스럽습니다. 그러면 `CullPush`의 `mat4` 전제도 함께 정리됩니다.
* **DEF-25의 재발 방지 계약이 그대로 적용됩니다.** 합집합 절두체를 도입하면 CPU와 GPU 양쪽의 평면 입력이 동시에 바뀌므로, `Renderer_render.cpp`와 `shaders/cull.comp`를 반드시 같이 고쳐야 합니다.
* 멀티뷰 완성(DEF-09)과 같은 작업 단위로 묶는 것이 합리적입니다.

---

### 9-2. [DEF-32] [MEDIUM] 재투영 여유분만큼의 절두체 부풀리기 부재

#### 대상 파일 및 위치
* [`src/renderer/Renderer_render.cpp`](file:///e:/Haruka/src/renderer/Renderer_render.cpp): `extractFrustumPlanes`가 만드는 평면 집합

**착수 시점**: **OpenXR 세션 도입 시**입니다. 다만 값을 확정하려면 실기 측정이 선행되어야 합니다.

#### 결함 메커니즘 (Root Cause)
1. VR은 `xrLocateViews`가 준 **예측 포즈**로 렌더하고, 런타임이 스캔아웃 직전에 실제 포즈로 재투영합니다.
2. 예측과 실제가 다르면, 렌더 시점에 화면 밖이던 영역이 재투영 후 화면 안으로 들어옵니다.
3. 그 영역을 컬링해 버렸다면 **주변부에 빈 곳이 생깁니다.** 머리가 빠르게 돌수록 그 폭이 커집니다.

#### 수정 가이드라인 (담당 에이전트 지시사항)
* 판정이 평면 기반이므로, 각 평면의 `d` 값만 조정하면 절두체를 바깥으로 부풀릴 수 있습니다. 구조 변경 없이 값 하나로 처리됩니다.
* **여유분의 크기는 실기에서 실제 재투영 폭을 측정한 뒤에만 정할 수 있는 값이며, 미리 정할 수 없습니다.** 문서나 코드에 임의의 숫자를 먼저 박아 두지 마십시오. 측정 없이 넣은 값은 너무 작으면 빈 곳을 못 막고, 너무 크면 컬링 이득을 통째로 없앱니다.
* 측정 방법을 먼저 마련하십시오. 예측 포즈와 재투영 시점의 실제 포즈 차이를 프레임마다 기록하고, 그 차이가 만드는 화면 공간 이동량의 상위 분위수를 보는 방식이 적절합니다.
* **DEF-25의 재발 방지 계약이 여기에도 적용됩니다.** 평면의 `d`를 부풀리는 조정은 CPU와 GPU가 동일하게 적용해야 하며, 한쪽만 부풀리면 두 판정이 다시 갈라집니다.

---

## 10. VR 컬링 설계 지침 (결함이 아니라 판단 기준입니다)

아래 세 항목은 고칠 대상이 있는 결함이 아니라, **앞으로 컬링과 LOD를 설계할 때 따라야 할 판단 기준**입니다. 그러므로 결함 ID를 부여하지 않았고 요약 매트릭스에도 넣지 않았습니다. 대신 DEF-31, DEF-32를 포함한 모든 컬링 관련 작업이 이 지침 위에서 결정되어야 합니다.

### 10-1. 시야각이 넓어 컬링 효율이 애초에 낮습니다

평면 게임은 수평 FOV가 보통 60도에서 90도 사이인데, Quest Pro는 눈당 100도를 넘습니다. 절두체가 넓을수록 밖으로 나가는 물체가 적으므로, **프러스텀 컬링이 걸러 주는 양 자체가 평면보다 적습니다.** 렌더 부담은 스테레오라 두 배인데 컬링 이득은 오히려 줄어드는 구조입니다.

따라서 **컬링에만 기대는 성능 계획을 세우면 안 됩니다.** 드로우 콜 병합, 인스턴싱(DEF-16), 멀티뷰(DEF-09) 같은 다른 축을 함께 확보해야 목표 프레임을 맞출 수 있습니다.

### 10-2. 시간적 재사용 기법은 별도 검토가 필요합니다

이전 프레임의 깊이 버퍼를 재사용하는 오클루전 컬링(계층적 Z 버퍼 등)은 프레임 간 일관성을 전제합니다. **머리가 빠르게 도는 VR에서는 그 전제가 약합니다.** 또한 좌안에서 가려진 물체가 우안에서는 보일 수도 있으므로, 한쪽 눈의 결과를 다른 쪽에 그대로 쓸 수 없습니다.

도입 자체를 금지하는 것은 아니지만, 착수 전에 프레임 간 일관성과 양안 차이를 어떻게 다룰지 먼저 결정하십시오.

### 10-3. 거리 컬링과 LOD를 구분해야 합니다

이 둘을 하나로 묶어 생각하면 안 됩니다. 방향이 서로 반대이기 때문입니다.

* **LOD 감소는 거리에 따라 공격적으로 해도 됩니다.** 양안 시차는 대략 10미터에서 20미터를 넘으면 실질적으로 무의미해지므로, 먼 물체의 형상 디테일을 줄여도 입체감 손실이 거의 없습니다.
* **그러나 거리 컬링(제거)은 평면 게임보다 오히려 보수적이어야 합니다.** 이유는 세 가지입니다. 첫째, VR에서는 플레이어가 멈춰 서서 먼 곳을 응시할 수 있으므로 팝인이 그대로 보입니다. 둘째, 우주 씬은 대기가 없어 거리 안개로 팝인을 가릴 수 없습니다. 셋째, 물체가 갑자기 생기면 뇌가 그것을 깊이 단서로 오인하므로 입체 환경에서 더 거슬립니다.
* 제거할 때는 딱 끊지 말고 **페이드나 디더 전환**을 두는 편이 낫습니다.

---

## 11. 2026-08-25 설계 검토 신규 등재 결함 작업 지시서 (DEF-36 ~ DEF-48)

아래 항목은 2026-08-25 설계 검토와 같은 날의 머티리얼 작업에서 도출되어 새로 등재한 것입니다. **전부 Layer 1(코어)에 속합니다.**

**[갱신 · 2026-08-25 · 4차]** **DEF-36, DEF-37, DEF-47, DEF-48은 해결되었습니다.** 해당 절은 작업 지시서가 아니라 **수정 기록**으로 다시 썼으므로, 담당 에이전트의 작업 대상이 아닙니다. 나머지 아홉 항목(DEF-38부터 DEF-46까지)은 미해결이며 여전히 작업 대상입니다.

착수 전에 다음 두 가지를 반드시 확인하십시오.

1. **DEF-36과 DEF-37은 하나의 작업 단위였습니다.** DEF-36만 고치면 앰비언트가 약 4.4배로 세서 되살아난 방향광이 묻히므로, 고쳤는지 여부조차 판단할 수 없었습니다. 두 항목을 함께 처리하여 **라이팅이 검증 가능한 상태가 되었습니다.** 같은 이유로 **DEF-47과 DEF-48도 한 묶음이었습니다.** 11-12절 서두에 이유가 있습니다.
2. **DEF-40, DEF-43, DEF-44는 미검증 항목입니다 (DEF-39는 2026-08-25 실측으로 성립 확인 후 해결되었습니다).** 검토자가 제시했으나 코드로 확인하지 않았습니다. **서술을 근거로 곧바로 코드를 고치지 마시고, 각 항목의 실측 절차를 먼저 수행하십시오.** 실측 결과 주장이 성립하지 않으면 수정하지 말고 **기각으로 표기하고 근거를 남기십시오.**

---

### 11-1. [DEF-36] [해결됨 · 2026-08-25] 섀도 샘플러 비교 규약 반전으로 직접광 전부 소실 (원래 CRITICAL)

**상태**: **해결되었습니다 (2026-08-25).** 원래 심각도는 CRITICAL이었으며 이 문서에 등재된 결함 가운데 가장 심각했습니다. **아래는 작업 지시서가 아니라 수정 기록입니다.**

#### 대상 파일 및 위치
* **파일**: [`src/renderer/passes/ShadowPass.cpp`](file:///e:/Haruka/src/renderer/passes/ShadowPass.cpp)
* **라인**: [L37](file:///e:/Haruka/src/renderer/passes/ShadowPass.cpp#L37)
* **증상이 나타나던 위치**: [`pbr.frag:L165`](file:///e:/Haruka/shaders/pbr.frag#L165)의 `Lo *= shadow`

결과가 "그림자가 이상하다"에 그치지 않고 **모든 라이트의 기여가 0이 되었습니다.** 화면에 남는 것은 앰비언트와 IBL뿐이었습니다.

#### 결함 메커니즘 (Root Cause)

문제였던 한 줄은 `si.compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;` 이며, 같은 줄의 주석은 `// 1.0=lit, 0.0=shadow` 였습니다. **주석과 코드가 정반대였습니다.**

근거는 다섯 갈래이며 전부 같은 방향을 가리킵니다.

1. **엔진 전체가 정방향 Z입니다.** [`GeometryPass.cpp`](file:///e:/Haruka/src/renderer/passes/GeometryPass.cpp)의 깊이 클리어가 `{1.0f, 0}` 이고, `:89`가 `LESS_OR_EQUAL`, `:331`이 `LESS` 입니다. [`ShadowPass.cpp:L132`](file:///e:/Haruka/src/renderer/passes/ShadowPass.cpp#L132)이 `LESS`, `:154`의 클리어가 `{1.f, 0}` 입니다. **리버스 Z를 쓰는 곳이 한 곳도 없습니다.**
2. **Vulkan 사양상 깊이 비교는 `D_ref <compareOp> D_texel` 로 평가됩니다.** 따라서 `GREATER_OR_EQUAL` 은 "레퍼런스가 저장값보다 멀거나 같을 때 1.0"을 뜻하며, 이는 **가려졌을 때 밝음**이라 주석이 선언한 규약과 정확히 반대입니다.
3. **바이어스의 방향이 반대 규약을 전제합니다.** [`pbr.frag:L74`](file:///e:/Haruka/shaders/pbr.frag#L74)의 `proj.z -= 0.003` 이 레퍼런스를 더 가깝게 밉니다. 이것은 `LESS_OR_EQUAL` 규약에서 자기 그림자를 피하는 방향입니다.
4. **앞면 컬링이 결정적 근거입니다.** [`ShadowPass.cpp:L115`](file:///e:/Haruka/src/renderer/passes/ShadowPass.cpp#L115)가 `rs.cullMode = VK_CULL_MODE_FRONT_BIT;` 이며 주석도 shadow acne 감소를 위한 front-face culling이라고 적혀 있습니다. 앞면을 컬링하므로 섀도맵에는 **뒷면의 깊이**, 즉 더 먼 값이 저장됩니다. 그 결과 라이트를 향한 표면은 `D_ref < D_texel` 이 되어 비교가 실패하고 `shadow = 0` 이 됩니다.
5. **가드 세 조건이 전부 열려 있습니다.** `miscParams.w` 는 `shadow_.valid()` 이고 `ShadowPass::valid()` 는 `image_ != VK_NULL_HANDLE` 이므로 `create()` 이후 항상 참입니다. `DefaultGameModule::onStart` 가 Sun을 첫 액터로 만들므로 슬롯 0이 Directional입니다. 따라서 이 경로는 실제로 도달합니다.

#### ★ 지문 테스트로 확정했습니다 (코드를 고치기 전에 실제로 수행)

**코드를 한 줄도 고치기 전에 실측으로 확정했습니다.** 근거 체인이 다섯 갈래로 강했으나, 서술만 믿고 고치지 않는다는 이 문서의 원칙에 따라 관찰을 선행했습니다.

수행 방법은 [`pbr.frag`](file:///e:/Haruka/shaders/pbr.frag) 에 `outColor = vec4(vec3(shadow), 1.0); return;` 를 임시로 넣어 **shadow 값을 그대로 화면에 출력한 것입니다.** 조명 계산을 거치지 않고 비교 결과 자체를 보므로 다른 요인이 끼어들 여지가 없습니다.

관찰 결과는 두 가지였고 **둘 다 결함 메커니즘이 예측한 그대로였습니다.**

1. **물체가 거의 전부 검게 나왔습니다.** `shadow = 0` 이 화면 전역에 적용되고 있었다는 직접 증거이며, 위 4번(앞면 컬링) 논증의 예측과 일치합니다.
2. **검은 물체의 실루엣 가장자리에만 얇은 흰 테두리가 나타났습니다.** 이것이 결정적 단서였습니다. `VK_CULL_MODE_FRONT_BIT` 때문에 섀도맵에 뒷면 깊이가 저장되므로 빛을 향한 면은 `D_ref < D_texel` 이라 비교가 실패하여 0이 되는데, **실루엣 가장자리에서는 앞면과 뒷면 깊이가 수렴하여 `D_ref ≈ D_texel` 이 되므로** 비교가 통과하여 1이 됩니다. 흰 테두리가 정확히 그 지점이었습니다. 다른 원인이었다면 이런 형태의 지문은 나올 수 없습니다.

**진단 코드는 확인 직후 즉시 되돌렸습니다.** 셰이더에 남기지 않았습니다.

> **참고 · 등재 당시 제안했던 20미터 지문**: 섀도맵은 원점을 중심으로 하는 40미터 상자이므로, 물체가 범위 밖으로 나가면 [`pbr.frag:L72`](file:///e:/Haruka/shaders/pbr.frag#L72)의 UV 범위 조기 반환이 걸려 `shadow = 1.0` 이 됩니다. 원점에서 20미터 이상 벗어날 때 물체가 갑자기 밝아지는 현상으로도 확정할 수 있었습니다. 실제로는 위의 직접 출력 방식이 더 짧고 해석의 여지가 없어 그쪽을 택했습니다. **두 지문은 같은 메커니즘의 서로 다른 관측면입니다.**

#### 적용한 수정 내용

* **고친 곳은 샘플러 한 줄뿐입니다.** `ShadowPass.cpp:37`의 `compareOp` 를 `VK_COMPARE_OP_GREATER_OR_EQUAL` 에서 **`VK_COMPARE_OP_LESS_OR_EQUAL`** 로 바꿨습니다.
* **셰이더와 바이어스는 건드리지 않았습니다.** 바이어스 방향과 `VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE` 가 둘 다 `LESS_OR_EQUAL` 규약을 전제하므로, 샘플러를 고치는 순간 나머지가 맞아 들어갔습니다. **셰이더까지 함께 뒤집었다면 반전이 두 번 일어나 원래대로 돌아갔을 것입니다.**
* 같은 줄의 주석 `// 1.0=lit, 0.0=shadow` 는 수정 후에야 사실이 되었으므로 그대로 두었습니다.
* **DEF-37을 함께 처리했습니다.** 11-2절의 첫 문단에 이유가 있습니다.

#### ★ 재발 방지 계약 (세 요소가 한 묶음입니다)

> 다음 셋은 **전부 같은 규약을 전제하므로 하나만 뒤집으면 다시 깨집니다.** 이 사실을 `ShadowPass.cpp` 의 주석에 남겨 두었습니다.
>
> 1. 샘플러의 비교 연산자 `VK_COMPARE_OP_LESS_OR_EQUAL`
> 2. [`pbr.frag`](file:///e:/Haruka/shaders/pbr.frag) 의 바이어스 `proj.z -= 0.003` (레퍼런스를 더 가깝게 미는 방향)
> 3. 샘플러의 `VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE` (가장 먼 값이므로 섀도맵 밖은 항상 밝음)

#### 검증

지문 테스트로 수정 전 상태를 확정했고 진단 코드는 확인 후 즉시 되돌렸습니다. 수정 후 사용자가 육안으로 확인했으며 **"이제 명암이 보이네"** 로 명암 복귀가 확인되었습니다.

---

### 11-2. [DEF-37] [해결됨 · 2026-08-25] IBL 조도 추정기의 코사인 중복 적용 (원래 HIGH)

**상태**: **해결되었습니다 (2026-08-25).** 원래 심각도는 HIGH입니다. **아래는 작업 지시서가 아니라 수정 기록입니다.**

**DEF-36과 같은 작업 단위였습니다.** DEF-36만 고치면 앰비언트 대 직접광이 약 4.4 대 1이 되어 되살아난 방향광이 앰비언트에 묻힙니다. (이 비율은 당시 조명 값 기준의 **추정**입니다. 11-16절의 밝기 밸런스 변경 이후에는 다릅니다.) 그 상태에서는 "고쳤는데 화면이 그대로다"라는 잘못된 결론에 이르기 쉽습니다. **두 항목을 모두 처리한 뒤에야 라이팅이 검증 가능한 상태가 되었습니다.**

#### 대상 파일 및 위치
* **파일**: [`src/renderer/Renderer.cpp`](file:///e:/Haruka/src/renderer/Renderer.cpp)의 IBL 조도 큐브맵 생성 루프

#### 결함 메커니즘 (Root Cause)

문제의 세 줄은 다음과 같습니다.

1. `float theta = std::acos(std::sqrt(1.f - r1));` 은 **코사인 가중 샘플링**이며 pdf는 `cosθ/π` 입니다.
2. `irr += skyColor(L) * glm::dot(N, L);` 이 **cos를 한 번 더** 곱합니다.
3. `irr = irr * (3.14159265f / kSamples);` 이 π까지 곱합니다.

코사인 가중 추정기의 올바른 식은 `(π/N) Σ L` 입니다. cosθ는 이미 pdf에 흡수되어 있으므로 다시 곱하면 안 됩니다. 현재 코드는 `(π/N) Σ L·cosθ` 를 계산하므로 실질적으로 `∫ L cos²θ dω` 를 구합니다.

균일 복사휘도에서 `∫ cos²θ dω = 2π/3 ≈ 2.094` 이므로 **저장값이 약 2.09배 과다합니다.** [`pbr.frag`](file:///e:/Haruka/shaders/pbr.frag)가 `kD * irradiance * albedo` 로 π 나눗셈 없이 사용하므로 과다가 그대로 화면에 나타납니다.

#### 적용한 수정 내용 (B-1 방안, 두 곳을 동시에 고쳤습니다)

권고안이었던 **B-1을 채택했습니다.** 고친 곳은 정확히 두 곳입니다.

1. `irr += skyColor(L) * glm::dot(N, L);` 에서 **`* glm::dot(N, L)` 를 제거했습니다.** 코사인 가중 샘플링이므로 pdf가 `cosθ/π` 이고 cos 항이 이미 흡수되어 있기 때문입니다.
2. `irr = irr * (3.14159265f / kSamples);` 를 **`irr = irr * (1.f / kSamples);` 로 바꿨습니다.**

이로써 저장값의 의미를 **"그 방향에서 오는 평균 복사휘도"** 로 정의하여 [`pbr.frag`](file:///e:/Haruka/shaders/pbr.frag)의 `kD * irradiance * albedo` 를 그대로 쓸 수 있게 했습니다. **셰이더를 건드리지 않으므로 프리필터 큐브맵과 스페큘러 IBL 경로에 영향이 없습니다.** 이것이 B-2 대신 B-1을 택한 이유입니다.

채택하지 않은 **B-2**는 `dot(N, L)` 만 제거하고 셰이더를 `kD * irr / PI * albedo` 로 바꾸는 방향이며, 셰이더까지 손대야 하므로 영향 범위가 넓어 택하지 않았습니다.

#### ★ 왜 두 곳을 반드시 함께 고쳐야 했는가

> **한쪽만 고치면 결함이 더 커집니다.** `dot(N, L)` 만 제거하고 계수를 `π/N` 으로 두면 이번에는 **π배 과다**가 되어 결함의 크기가 2.09배에서 3.14배로 오히려 커집니다. 반대로 계수만 `1/N` 으로 바꾸고 cos를 남기면 조도가 부족해집니다. **두 곳은 하나의 수식이며 항상 함께 맞춰야 합니다.** 이 경고를 코드 주석에 남겨 두었습니다.

#### 검증

DEF-36 수정과 함께 확인했습니다. 앰비언트가 정상 크기로 내려오면서 되살아난 방향광이 묻히지 않게 되었고, 사용자가 육안으로 명암을 확인했습니다.

---

### 11-3. [DEF-38] [HIGH] 라이트 종류와 방향이 직렬화되지 않음

#### 대상 파일 및 위치
* **파일**: [`src/assets/SceneSerializer.cpp`](file:///e:/Haruka/src/assets/SceneSerializer.cpp)
* **위치**: `LIGHT:` 기록 경로 및 복원 경로

#### 결함 메커니즘 (Root Cause)

1. 기록하는 것은 `color.r`, `color.g`, `color.b`, `intensity`, `ambient`, `enabled` 입니다.
2. **빠진 것은 `lightType` 과 `direction` 입니다.** 복원 경로도 같은 여섯 값만 되돌립니다.
3. **영향**: 방향광의 방향과 광원 종류가 `.hscene` 왕복에서 소실되어 전부 기본값인 Directional과 `normalize(1, 2, 1.5)` 로 되돌아갑니다. 스팟과 포인트는 종류 자체를 잃으므로, 씬을 저장하고 다시 여는 것만으로 조명 구성이 무너집니다.

#### 수정 가이드라인 (담당 에이전트 지시사항)

* `lightType` 과 `direction` 을 기록과 복원 양쪽에 추가하십시오.
* **`range`, `spotInnerDeg`, `spotOuterDeg` 도 함께 추가하십시오.** 이 세 필드는 `LightComponent.hpp` 에 존재하지만 `LIGHT:` 기록 경로와 복원 경로 어디에도 없다는 점을 검수에서 코드로 확인했습니다. 스팟 라이트는 이 세 값이 없으면 종류를 복원해도 형상이 달라집니다.
* 이 항목은 DEF-22(SceneSerializer 컴포넌트 하드코딩)와 같은 파일을 건드립니다. DEF-22의 타입 등록 기반 직렬화를 하면 결함이 구조적으로 사라지지만 규모가 큰 변경이므로, **필드 추가를 먼저 하여 데이터 소실을 막는 편이 낫습니다.**
* 기존 `.hscene` 파일과의 호환을 어떻게 다룰지 함께 결정하십시오. 필드가 없는 옛 파일을 읽을 때 기본값으로 채울지, 버전 표기를 도입할지 명시해야 합니다.
* 검증은 라이트 세 종류를 배치한 씬을 저장하고 다시 열어 값이 그대로인지 확인하는 방식으로 하십시오.

---

### 11-4. ✅ [DEF-39] [해결됨 · 2026-08-25] `defaultCube` 탄젠트 handedness 반전 (원래 MEDIUM · 미검증)

> **실측으로 주장이 성립함을 확인했고 수정했습니다.** 다만 범위가 검토자의 주장보다 넓었습니다. 검토자는 "옆 네 면"이라고 했으나 `generateTangents` 와 동일한 산식으로 여섯 면을 전부 검산한 결과 **여섯 면 모두** 반대였습니다. 즉 일부 면의 실수가 아니라 전체가 일관되게 뒤집힌 상태였습니다.

> 각 면의 UV 정의에서 유도하면 `cross(N,T)` 가 언제나 v 감소 방향이 되어 `w = -1` 이 나옵니다. 하드코딩된 값은 전부 `+1` 이었습니다. 24 개 정점의 `tangent[3]` 을 `-1` 로 고쳤습니다. `defaultCube` 에는 노멀맵이 없어(체커 albedo 만 생성) 지금 화면은 달라지지 않습니다. 고친 이유는 **절차 도형 생성기가 이 패턴을 베끼면 모든 절차 도형의 노멀맵 Y 가 뒤집히기 때문**입니다.

> 재발을 막기 위해 `src/geometry/PrimitiveBuilder.cpp` 에는 상수 `w` 가 한 곳도 없습니다. `handedness(N, T, B)` 가 항상 해석적으로 계산하며, `tests/GeometryTest.cpp` 가 그 결과를 검산합니다.


#### 대상 파일 및 위치 (주장)
* [`src/assets/GltfLoader.cpp`](file:///e:/Haruka/src/assets/GltfLoader.cpp)의 `defaultCube`

#### 주장 내용
* `defaultCube` 의 탄젠트 handedness가 **여섯 면 전부** 엔진 규약과 반대라는 것입니다.

#### 착수 전 실측 절차 (담당 에이전트 지시사항)
1. 엔진의 탄젠트 handedness 규약이 무엇인지 먼저 확정하십시오. 규약이 명시된 곳이 없다면 그것부터 정해야 합니다.
2. `defaultCube` 여섯 면의 탄젠트 부호를 실제로 계산하여 규약과 대조하십시오.
3. 노멀 맵을 입힌 상태에서 조명 방향에 따라 요철이 반대로 보이는지 관찰하십시오. 이것이 handedness 반전의 전형적인 증상입니다.

#### 수정 가이드라인 (확인된 뒤에)
* **확인되기 전에는 부호를 뒤집지 마십시오.** 규약 자체를 잘못 잡은 상태에서 뒤집으면 다른 모든 메시가 반대로 깨집니다.
* 확인되면 `defaultCube` 쪽을 규약에 맞추고, 규약을 코드 주석으로 못 박아 다음 절차 도형이 같은 실수를 반복하지 않도록 하십시오.

---

### 11-5. [DEF-40] [MEDIUM · 미검증 · 착수 전 실측 필요] 라인 오버레이 정점 버퍼의 GPU 인플라이트 레이스

> **미검증 항목입니다.** 설계 검토자가 제시했으나 코드로 확인하지 않았습니다. **아래 실측을 먼저 수행하십시오.**

#### 대상 파일 및 위치 (주장)
* [`src/renderer/passes/GeometryPass.cpp`](file:///e:/Haruka/src/renderer/passes/GeometryPass.cpp) 및 [`GeometryPass.hpp`](file:///e:/Haruka/src/renderer/passes/GeometryPass.hpp)의 라인 오버레이 정점 버퍼

#### 주장 내용
* 해당 정점 버퍼가 **단일 버퍼**인데 매 프레임 덮어쓰므로, GPU가 이전 프레임의 내용을 아직 읽는 중에 CPU가 새 내용을 쓰는 레이스가 발생한다는 것입니다. 성격은 **DEF-02와 같은 유형**입니다.

#### 착수 전 실측 절차 (담당 에이전트 지시사항)
1. 버퍼가 실제로 `MAX_FRAMES` 개로 나뉘어 있지 않고 하나인지 확인하십시오.
2. 기록 시점과 제출 시점 사이에 펜스 대기가 있는지 확인하십시오. **프레임 펜스로 이미 보호되고 있다면 레이스가 성립하지 않습니다.**
3. 증상은 라인 오버레이가 간헐적으로 깜빡이거나 이전 프레임의 선이 섞여 보이는 형태로 나타납니다.

#### 수정 가이드라인 (확인된 뒤에)
* 프레임 인덱스별 버퍼로 나누는 것이 가장 단순합니다. DEF-02의 옵션 A와 같은 구조입니다.
* 라인 오버레이는 에디터 전용이므로, 런타임 빌드에서 이 경로가 어떻게 도는지도 함께 확인하십시오.

---

### 11-6. ✅ [DEF-41] [해결됨 · 2026-08-25] 콜라이더가 `Transform.scale` 을 반영하지 않음 (원래 MEDIUM)

> **수정했습니다.** `PhysicsSystem::tick` 의 바디 생성 지점이 `halfExtent` 에 `a->transform.scale` 을 곱합니다. Box 는 성분별로 곱하고, Sphere 와 Capsule 은 해석적 대응이 없으므로 **최대 성분으로 보수 근사하고 경고를 로그합니다.** 조용히 틀리는 것보다 시끄럽게 근사하는 편이 낫다는 판단입니다.

> 부모 스케일은 합성하지 않습니다. 같은 지점의 회전이 `a->transform.rotation`(로컬)을 쓰는 것과 수준을 맞춘 것이며, 계층 변환 반영은 별도 항목입니다.

> **남은 한계**: 바디는 생성 시점의 스케일로 만들어집니다. 에디터에서 기즈모로 스케일을 바꿔도 이미 만들어진 바디는 따라오지 않습니다. `RigidBodyComponent` 의 `resetIfLive` 가 `halfExtent` 세터에만 걸려 있기 때문이며, 스케일 변경을 여기에 연결하는 것은 DEF-43(커밋 훅)과 같은 작업 묶음입니다.

#### 대상 파일 및 위치
* **파일**: [`src/physics/PhysicsSystem.cpp`](file:///e:/Haruka/src/physics/PhysicsSystem.cpp)
* **위치**: 바디 생성 지점

#### 결함 메커니즘 (Root Cause)

1. 셰이프를 `rb->halfExtent` 로만 만듭니다. 구체적으로 `SphereShapeSettings(r)`, `CapsuleShapeSettings(rb->halfExtent.y, r)`, `BoxShapeSettings(Vec3(halfExtent.x/y/z))` 입니다.
2. 액터의 `transform.rotation` 은 읽지만 **`transform.scale` 은 어디에도 쓰이지 않습니다.**
3. **영향**: 에디터에서 물체를 키우면 렌더 메시만 커지고 콜리전은 원래 크기로 남습니다.

#### ★ 절차 도형 설계와 정면으로 충돌합니다

이 항목을 단순한 편의 결함으로 보면 안 됩니다. **크기 변경을 `Transform.scale` 로 처리하기로 결정했기 때문입니다.** 그 결정 아래에서는 스케일이 곧 물체의 크기이므로, 콜라이더가 스케일을 무시한다는 것은 **절차 도형으로 만든 모든 물체의 충돌 크기가 틀린다는 뜻입니다.**

> **따라서 절차 도형 작업에 착수하기 전에 이 결함을 처리하십시오.** 순서를 뒤집으면 절차 도형으로 배치한 모든 물체가 잘못된 콜리전을 갖게 되고, 나중에 고칠 때 이미 저장된 씬의 크기 해석까지 함께 흔들립니다.

#### 수정 가이드라인 (담당 에이전트 지시사항)

* 셰이프 생성 시 `transform.scale` 을 반영하십시오. Jolt의 `ScaledShapeSettings` 로 감싸거나 셰이프 치수에 스케일을 직접 곱하는 두 방법이 있습니다.
* **비균일 스케일을 어떻게 다룰지 먼저 결정하십시오.** 구와 캡슐은 비균일 스케일을 그대로 표현할 수 없으므로, 금지할지 최대 성분으로 근사할지 규칙을 명시해야 합니다. 규칙 없이 구현하면 같은 값이 두 곳에서 다르게 해석되는 이 프로젝트의 반복 결함 유형이 그대로 재현됩니다.
* 스케일이 런타임에 바뀔 때 바디를 어떻게 갱신할지도 함께 정하십시오. 이 지점은 **DEF-43(미검증)의 `resetIfLive` 주장과 같은 코드를 건드리므로 함께 보십시오.**

---

### 11-7. [DEF-42] [MEDIUM] 힙이 1GB 미만일 때 Zone 오프셋이 실제 할당 범위를 넘음

#### 대상 파일 및 위치
* **파일**: [`src/memory/HarukaAllocator.cpp`](file:///e:/Haruka/src/memory/HarukaAllocator.cpp)의 init 경로, [`src/memory/HarukaAllocator.hpp`](file:///e:/Haruka/src/memory/HarukaAllocator.hpp)의 Zone 상수

#### 결함 메커니즘 (Root Cause)

1. init 경로가 `heapSize = std::min(kHeapTarget, avail * 3 / 4);` 로 시작합니다. 가용 메모리가 적으면 목표보다 작은 힙을 잡습니다.
2. 할당에 실패하면 `while (hHeap == VK_NULL_HANDLE && heapSize >= kHeapTarget / 4) { ... heapSize /= 2; }` 로 절반씩 줄이며 재시도합니다. **실제 힙이 1GB보다 작아질 수 있습니다.**
3. 그런데 `kZoneAOffset`, `kZoneBOffset`, `kZoneCOffset` 은 **컴파일 타임 상수**이며 `static_assert` 가 합이 `kHeapTarget` 과 같음을 강제합니다. 즉 오프셋은 항상 1GB 힙을 전제합니다.
4. **영향**: 힙이 축소되면 Zone B와 Zone C의 기점이 **실제 할당 범위 밖**을 가리킵니다. `heapSize < kHeapTarget` 일 때 경고 로그는 출력되지만 **Zone 오프셋은 조정되지 않습니다.**

#### 수정 가이드라인 (담당 에이전트 지시사항)

* **Quest 실기에서 1GB를 받지 못할 가능성이 있으므로 포팅 전에 처리해야 합니다.** 데스크톱에서는 축소 경로가 거의 타지 않으므로 결함이 드러나지 않다가 실기에서 처음 발현할 수 있는 종류입니다.
* Zone 오프셋을 실제 `heapSize` 에서 유도되는 런타임 값으로 바꾸는 것이 정공법입니다. 컴파일 타임 `static_assert` 는 비율 관계를 검사하는 형태로 바꾸십시오.
* 대안으로 축소를 허용하지 않고 `kHeapTarget` 확보 실패 시 초기화를 중단할 수도 있습니다. 다만 이 경우 실기에서 아예 기동하지 못할 수 있으므로, **어느 쪽을 택할지 실기 가용 메모리를 실측한 뒤에 결정하십시오.**
* 어느 쪽을 택하든 축소 경로에 assert를 넣어, 조정되지 않은 오프셋이 사용되는 상태가 조용히 지나가지 않도록 하십시오.

---

### 11-8. [DEF-43] [LOW · 미검증 · 착수 전 실측 필요] 세터 안의 `resetIfLive` 로 인한 매 프레임 바디 재생성

> **미검증 항목입니다.** 설계 검토자가 제시했으나 코드로 확인하지 않았습니다. **아래 실측을 먼저 수행하십시오.**

#### 대상 파일 및 위치 (주장)
* [`src/components/RigidBodyComponent.cpp`](file:///e:/Haruka/src/components/RigidBodyComponent.cpp)의 `resetIfLive`

#### 주장 내용
* `resetIfLive` 가 세터 안에 있어서, 에디터에서 슬라이더를 드래그하는 동안 **매 프레임 Jolt 바디를 파괴하고 재생성한다**는 것입니다.

#### 착수 전 실측 절차 (담당 에이전트 지시사항)
1. `resetIfLive` 가 실제로 세터에서 호출되는지, 값이 바뀌지 않아도 호출되는지 확인하십시오.
2. 슬라이더 드래그 중 바디 생성 횟수를 계측하십시오. 로그 한 줄이면 확인됩니다.
3. 증상은 드래그 중 물체가 튀거나 속도가 초기화되는 형태로 나타납니다.

#### 수정 가이드라인 (확인된 뒤에)
* 값이 실제로 바뀐 경우에만 리셋하도록 비교를 넣거나, 리셋을 프레임 끝으로 지연시키십시오.
* **DEF-41의 스케일 반영 작업과 같은 지점을 건드리므로 함께 처리하는 편이 효율적입니다.**

---

### 11-9. [DEF-44] [LOW · 미검증 · 착수 전 실측 필요] `RIGID:` 의 enum 서수 직렬화

> **미검증 항목입니다.** 설계 검토자가 제시했으나 코드로 확인하지 않았습니다. **아래 실측을 먼저 수행하십시오.**

#### 대상 파일 및 위치 (주장)
* [`src/assets/SceneSerializer.cpp`](file:///e:/Haruka/src/assets/SceneSerializer.cpp)의 `RIGID:` 기록 경로

#### 주장 내용
* `RIGID:` 가 enum 서수를 그대로 기록하므로, `EPhysicsShape` 에 값을 **중간 삽입**하면 기존 씬이 조용히 깨진다는 것입니다.

#### 착수 전 실측 절차 (담당 에이전트 지시사항)
1. `RIGID:` 가 실제로 정수 서수를 기록하는지 확인하십시오.
2. `EPhysicsShape` 의 값이 명시적으로 고정되어 있는지 확인하십시오. **명시 값이 박혀 있다면 중간 삽입이 서수를 밀지 않으므로 주장이 성립하지 않습니다.**

#### 수정 가이드라인 (확인된 뒤에)
* 문자열 태그로 기록하거나, enum 값을 명시적으로 고정하고 그 사실을 주석으로 못 박으십시오.
* **DEF-38과 같은 파일을 건드리므로 함께 처리하는 편이 효율적입니다.**

---

### 11-10. ✅ [DEF-45] [해결됨 · 2026-08-25] `uploadMesh` 의 핸들 누수 (원래 LOW)

> **수정했습니다.** 용량 검사를 `new MeshHandle()` 앞으로 옮겼습니다. 초과 시 throw 하는 경로에서 핸들이 이미 할당되어 있던 문제가 구조적으로 사라집니다.

> 같은 작업에서 두 가지를 함께 넣었습니다. 첫째, `MeshHandle::vertexCount` 를 추가했습니다. 기존 구조는 메가버퍼를 "반납하지 않는" 것이 아니라 **반납에 필요한 정보를 기록조차 하지 않는** 상태였고, 나중에 서브할당자를 넣더라도 이 필드가 없으면 소급 적용이 불가능합니다. 둘째, 사용률이 80 % 를 넘으면 경고를 출력합니다. 회수되지 않는 버퍼이므로 사후 throw 보다 사전 경고가 낫습니다.

#### 대상 파일 및 위치
* **파일**: [`src/renderer/GpuScene.cpp`](file:///e:/Haruka/src/renderer/GpuScene.cpp)
* **위치**: `uploadMesh`

#### 결함 메커니즘 (Root Cause)
* `auto* h = new MeshHandle();` 가 **용량 검사보다 앞**에 있습니다.
* 검사에 실패하면 `throw std::runtime_error("Mega buffer overflow! ...")` 로 예외를 던지므로 `h` 가 누수됩니다.

#### 수정 가이드라인 (담당 에이전트 지시사항)
* 용량 검사를 `new` 보다 앞으로 옮기거나, `h` 를 스마트 포인터로 잡고 성공 시에만 소유권을 넘기십시오.
* 한 곳의 수정으로 끝나며 기능 변화가 없으므로 위험도가 없습니다.

---

### 11-11. [DEF-46] [MEDIUM] 에디터 float 스텝이 0.01로 고정되어 1cm 미만을 편집할 수 없음

#### 대상 파일 및 위치
* **파일**: [`src/core/Reflect.hpp`](file:///e:/Haruka/src/core/Reflect.hpp)
* **위치**: `PropDesc` 의 필드 `float step {0.01f};`, 그리고 `XPROP_Float`, `XPROP_FloatMin`, `XPROP_FloatClamp` 매크로

#### ★ 배경: 이 항목이 단위 변경 논의를 촉발한 실제 원인입니다

사용자가 "1cm 단위로 갈 수 없나, 작은 것을 만들어야 할 수도 있다"고 물었고, 검토 끝에 **길이 단위는 미터로 유지하고 에디터를 개선하는 방향으로 확정되었습니다.** 근거는 다음과 같습니다.

1. **부동소수점 정밀도는 스케일 불변입니다.** 1cm 물체를 미터로 `0.01` 에 담으면 ULP가 약 0.93나노미터이고, cm로 `1.0` 에 담으면 약 1.19나노미터입니다(1.19e-7 cm). 물리량으로 환산하면 사실상 같습니다. **단위 변경으로 얻는 정밀도 이득이 없으므로 정밀도는 논거가 되지 못하며, 이득이 0인 변경에 비용을 치를 이유가 없다는 점만 남습니다.**
2. **OpenXR은 규약이 아니라 정의상 미터입니다.** cm 월드는 엔진 경계 전체에 단위 변환 계층을 요구하며, 이는 이 프로젝트에서 반복된 결함 유형인 "같은 값을 두 곳에서 다르게 해석"을 구조적으로 심는 것과 같습니다. DEF-01, DEF-23, DEF-25, DEF-36이 전부 그 유형이었습니다.
3. **Jolt의 내부 허용 오차가 전부 미터 기준 절대값입니다.** penetration slop, convex radius, speculative contact distance, collision tolerance가 모두 그러하므로 cm 월드에서는 허용 오차가 100배 빡빡해집니다.
4. **VR은 헤드셋이 실제 IPD를 미터로 보고합니다.** 월드 스케일이 어긋나면 크기 항상성이 깨집니다.

> **결론**: **문제는 단위가 아니라 UI였습니다.** 단위가 미터로 확정된 이상, 이제 해법은 이 항목을 고치는 것입니다.

#### 결함 메커니즘 (Root Cause)

1. `PropDesc` 의 `step` 이 `0.01f` 로 고정되어 있고, 세 매크로가 스텝을 **선언 시점에 고정**합니다.
2. 미터 단위에서 0.01은 1cm입니다. 따라서 **5mm짜리 물체를 슬라이더로 만들 수 없습니다.**
3. 반대로 100m짜리 구조물을 배치할 때는 같은 스텝이 지나치게 잘아서 쓸 수 없습니다. 한 값이 양쪽 끝을 모두 감당하지 못하는 구조입니다.

#### 수정 가이드라인 (담당 에이전트 지시사항)

**두 갈래를 함께 검토하십시오. 배타적이지 않습니다.**

* **(1) 표시 단위 분리**: 내부는 **항상 미터로 저장**하고, 에디터가 값의 크기에 따라 표시 단위를 고릅니다. `0.005` 는 `"5 mm"`, `0.35` 는 `"35 cm"`, `12.0` 은 `"12 m"` 로 표시하고, 입력도 `5mm` 형태로 받아 내부에서 변환합니다. **저장 형식과 엔진 경계는 미터 그대로이므로 단위 변환 계층이 생기지 않습니다.** 변환이 에디터 표시 계층 안에서만 일어난다는 점이 핵심입니다.
* **(2) 적응형 스텝**: 현재 값의 크기에 따라 스텝을 조정합니다. 값이 0.005면 0.0001, 값이 50이면 0.5 같은 식입니다. **`PropDesc::step` 이 선언 시점 고정이므로 지금 구조로는 불가능하며, 스텝을 상수 필드가 아니라 현재 값에서 유도되는 함수로 바꾸는 구조 변경이 선행되어야 합니다.**

(1)이 사용자 경험을 직접 개선하고 (2)가 그 아래의 구조 제약을 풉니다. 다만 (2)는 `Reflect.hpp` 의 매크로 계약을 바꾸므로 **영향 범위를 먼저 조사하십시오.**

**절차 도형 작업 선결 조건입니다.** 절차 도형으로 작은 물체를 만들려면 이 제약이 먼저 풀려야 하며, 같은 이유로 DEF-41과 함께 처리하는 것이 자연스럽습니다.

---

### 11-12. [DEF-47] [해결됨 · 2026-08-25] glTF 스칼라 팩터를 한 번도 읽지 않음 (원래 HIGH)

**상태**: **해결되었습니다 (2026-08-25).** 원래 심각도는 HIGH입니다. **아래는 작업 지시서가 아니라 수정 기록입니다.**

> **★ DEF-48과 한 묶음입니다.** DEF-48의 sRGB 이중 디코드는 **흰색에서는 `pow(1, 2.2) = 1` 이라 증상이 전혀 드러나지 않습니다.** 그런데 이 결함(DEF-47) 때문에 모든 머티리얼이 순백으로 렌더링되고 있었으므로 이중 디코드가 화면에 흔적을 남기지 못했습니다. **DEF-47을 고쳐 색이 들어가는 순간 드러날 문제였으므로 반드시 함께 처리해야 했습니다.** 하나만 고쳤다면 "팩터를 읽게 만들었더니 색이 이상해졌다"는 잘못된 인과로 이어졌을 것입니다.

#### 대상 파일 및 위치
* **파일**: [`src/assets/GltfLoader.cpp`](file:///e:/Haruka/src/assets/GltfLoader.cpp)
* **위치**: 머티리얼 추출 경로

#### 결함 메커니즘 (Root Cause)

1. 로더가 `baseColorTexture` 등 **텍스처만 읽고** `baseColorFactor`, `metallicFactor`, `roughnessFactor`, `emissiveFactor` 를 **한 번도 읽지 않았습니다.**
2. glTF에서는 **텍스처 없이 팩터만 지정한 머티리얼이 매우 흔합니다.** 그런 머티리얼은 값이 통째로 버려진 채 `GpuScene::uploadMaterial` 의 1x1 폴백으로 넘어갔습니다.
3. albedo 폴백이 흰색 `kWhite` 이므로 **전부 순백으로 렌더링되었습니다.**

#### 적용한 수정 내용: 로드 시점에 픽셀로 굽습니다

셰이더와 디스크립터를 바꾸지 않고 **로드 시점에 팩터를 픽셀에 구워 넣는 방식**을 택했습니다. 팩터는 머티리얼당 상수이므로 이 방식이 수학적으로 정확합니다.

* 텍스처가 있으면 팩터를 픽셀에 곱합니다.
* 텍스처가 없으면 팩터만으로 1x1 텍스처를 만듭니다.
* 항등 팩터면 픽셀 순회를 건너뜁니다.

추가한 헬퍼는 `srgbToLinear`, `linearToSrgb`, `toByte`, `applyColorFactor`, `applyMetalRoughFactor` 입니다.

#### ★ 색 공간 처리 규칙 (이 코드를 손대는 사람은 반드시 읽으십시오)

**팩터를 픽셀에 곱할 때 텍스처마다 처리 순서가 다릅니다. 이 규칙이 어긋나면 색이 조용히 틀어집니다.**

| 대상 | 업로드 포맷 | 저장 바이트의 상태 | 팩터 적용 방법 |
|---|---|---|---|
| albedo | `VK_FORMAT_R8G8B8A8_SRGB` | sRGB 인코딩 | **디코드 → 곱 → 인코드** |
| emissive | `VK_FORMAT_R8G8B8A8_SRGB` | sRGB 인코딩 | **디코드 → 곱 → 인코드** |
| albedo의 알파 | 위와 같음 | 어느 쪽에서도 sRGB가 아님 | **그대로 곱함** |
| metallicRoughness | UNORM | 선형 | **직접 곱함** (B=metallic, G=roughness) |

1. albedo와 emissive는 `VK_FORMAT_R8G8B8A8_SRGB` 로 업로드되므로 **저장 바이트가 sRGB 인코딩 상태여야 하는데 팩터는 선형입니다.** 따라서 sRGB 바이트를 선형으로 디코드하고, 선형 팩터를 곱한 뒤, 다시 sRGB로 인코드해야 합니다. **인코드를 빠뜨리면 화면이 어두워지고, 디코드를 빠뜨리면 색이 밝은 쪽으로 왜곡됩니다.**
2. **알파는 sRGB 전달 함수의 대상이 아닙니다.** glTF에서도 Vulkan의 sRGB 포맷에서도 알파 채널은 선형이므로 디코드와 인코드 없이 그대로 곱합니다.
3. metallicRoughness는 UNORM(선형)이며 glTF 규약상 **B 채널이 metallic, G 채널이 roughness** 입니다. 선형이므로 전달 함수 없이 직접 곱하고, 채널 배치를 뒤집지 않도록 주의하십시오.

#### 손대지 않은 것

`normalTexture.scale` 과 `occlusionTexture.strength` 는 처리하지 않았습니다. 둘 다 **셰이더 지원이 필요하여 범위가 커지기 때문입니다.** 픽셀로 굽는 방식으로는 해결되지 않으므로 11-15절의 머티리얼 편집 요구와 함께 다루는 것이 자연스럽습니다.

---

### 11-13. [DEF-48] [해결됨 · 2026-08-25] sRGB 이중 디코드 (원래 MEDIUM)

**상태**: **해결되었습니다 (2026-08-25).** 원래 심각도는 MEDIUM입니다. **DEF-47과 같은 작업 단위이며, 아래는 작업 지시서가 아니라 수정 기록입니다.**

#### 대상 파일 및 위치
* **파일**: [`shaders/pbr.frag`](file:///e:/Haruka/shaders/pbr.frag)
* **위치**: albedo 및 emissive 샘플링

#### 결함 메커니즘 (Root Cause)

1. albedo와 emissive 텍스처는 `VK_FORMAT_R8G8B8A8_SRGB` 로 업로드되므로 **샘플러가 하드웨어 단계에서 이미 선형 값을 돌려줍니다.**
2. 그런데 셰이더가 `pow(rgb, vec3(2.2))` 로 **한 번 더 디코드**하여 감마가 이중으로 적용되었습니다.
3. 그 결과 색이 들어간 머티리얼이 실제보다 어둡게 나옵니다.

#### 왜 오래 남아 있었는가

**흰색에서는 `pow(1, 2.2) = 1` 이라 증상이 전혀 드러나지 않습니다.** DEF-47 때문에 모든 머티리얼이 순백으로 렌더링되고 있었으므로 이 결함이 화면에 아무 흔적도 남기지 못했습니다. **DEF-47을 고쳐 색이 들어가는 순간 드러날 문제였고, 그래서 두 항목을 한 묶음으로 처리했습니다.**

#### 적용한 수정 내용

albedo와 emissive 양쪽에서 `pow(..., 2.2)` 를 제거했습니다.

#### 함께 확인한 것

`normalMap`, `metalRough`, `ao` 는 **전부 UNORM 포맷이며 셰이더에서 감마 처리를 하지 않으므로 정상입니다.** 이들에 `pow` 를 넣으면 반대 방향의 결함이 됩니다.

---

### 11-14. [정책 · 결함 아님] 머티리얼이 비어 있어도 엔진은 값을 대체하지 않습니다

**이것은 결함이 아니라 정책 결정이므로 DEF 번호를 부여하지 않았습니다.** 요약 매트릭스에도 넣지 않았습니다.

#### 관찰된 사실

DEF-47을 고치는 과정에서 로더에 진단 로그를 추가하여 테스트 모델을 실측했습니다.

```
[Asset]   mat "Material.001"  baseColor(1, 1, 1, 1)  metallic=1  roughness=1  tex[albedo=- normal=- mr=- ao=- emis=-]
```

`assets/models/model.glb` 의 머티리얼은 **텍스처가 하나도 없고 모든 팩터가 glTF 기본값입니다.** 제작 도구에서 머티리얼을 만들기만 하고 값을 지정하지 않은 상태입니다.

glTF 규약의 기본값이 `metallic = 1`, `roughness = 1` 이므로 완전 금속에 완전 거침이 되고, [`pbr.frag`](file:///e:/Haruka/shaders/pbr.frag)에서 `kD = (1-F)*(1-metallic)` 이 0이 되어 **확산광이 통째로 사라집니다.** IBL 반사도 `mipLevel = roughness * 4.0` 으로 가장 흐린 밉을 쓰므로 방향성이 거의 남지 않아 음영 없는 평평한 표면으로 보입니다. **즉 엔진이 파일 내용대로 정확히 그린 결과이며 결함이 아닙니다.**

#### 검토한 세 가지 안

| 안 | 내용 | 판단 |
|---|---|---|
| (가) | glTF 규약을 그대로 유지하고 아무것도 하지 않습니다 | 왜 평평한지 알 방법이 없습니다 |
| (나) | 팩터가 전부 기본값이고 텍스처도 없으면 엔진 기본값으로 대체합니다 | **기각** |
| (다) | 규약을 유지하되 경고 로그를 남깁니다 | **채택** |

#### 사용자 결정과 근거

**사용자가 (다)로 확정했습니다.** 근거는 다음과 같습니다.

**엔진이 파일 내용을 몰래 바꾸면, 제대로 만든 에셋을 넣었을 때 왜 다르게 보이는지 추적할 수 없게 됩니다.** 그리고 그것은 이 프로젝트에서 반복된 결함 유형, 즉 **값이 조용히 바뀌어 두 곳이 서로 다른 것을 믿게 되는 상황**과 정확히 같은 형태입니다. DEF-01, DEF-23, DEF-25, DEF-36이 전부 그 유형이었습니다. 눈에 보이는 증상을 없애기 위해 추적 불가능한 층을 하나 더 얹는 거래이므로 (나)를 기각했습니다.

#### 구현

[`GltfLoader.cpp`](file:///e:/Haruka/src/assets/GltfLoader.cpp) 에서 **텍스처가 하나도 없고 모든 팩터가 glTF 기본값이면 경고를 출력합니다.** 경고는 왜 평평하게 보이는지와 **엔진이 값을 대체하지 않는다는 사실**을 함께 설명합니다. 머티리얼 정보를 요약하는 진단 로그도 상시 출력하도록 남겼습니다.

---

### 11-15. [요구 등재 · 결함 아님] 엔진 내 머티리얼 편집

사용자가 **"언젠가는 엔진이 Material을 수정할 수 있으면 좋겠다"** 고 요청했습니다. **결함이 아니라 기능 요구이므로 DEF 번호를 부여하지 않고 등재만 합니다. 지금 착수할 항목이 아닙니다.**

현재 상태는 다음과 같습니다.

* `MaterialHandle` 은 `TexSlot` 다섯 개(albedo, normalMap, metalRough, ao, emissive)만 가지며 **스칼라 팩터를 보관하지 않습니다.** 팩터는 DEF-47의 해결 방식에 따라 **로드 시점에 픽셀로 구워지므로 이후 조정할 수 없습니다.**
* `StaticMeshComponent` 는 `MaterialHandle*` 를 들고 있을 뿐 편집 가능한 속성이 없습니다.
* 따라서 에디터 디테일 패널에 머티리얼 속성이 노출되지 않습니다.
* 머티리얼은 `SceneSerializer` 에도 저장되지 않습니다.

이것은 Layer 1(코어)의 범용 에디터 기능이며 **우주 도메인과 무관합니다.** 구현하려면 팩터를 픽셀에 굽는 현재 방식 대신 **머티리얼 상수를 런타임에 유지하고 셰이더에 전달하는 경로**가 필요하므로, **DEF-47의 구현 방식을 재검토해야 합니다.** DEF-47에서 손대지 않은 `normalTexture.scale` 과 `occlusionTexture.strength` 도 같은 경로가 생겨야 처리할 수 있습니다.

---

### 11-16. [기록 · 결함 아님] 2026-08-25 밝기 밸런스 변경 (문서 기록 없이 코드에만 있던 것)

> **⚠️ 이 변경은 어느 문서에도 기록되지 않은 채 코드에만 들어가 있었습니다.** 2026-08-25 검수에서 발견하여 등재합니다. 결함 수정이 아니라 **의도된 튜닝**이므로 DEF 번호를 부여하지 않았습니다. 아래 수치는 해당 코드와 코드 주석에서 그대로 옮긴 것이며, **별도의 실측 기록은 확인되지 않았습니다.**

DEF-36과 DEF-37을 고친 뒤에도 대비가 부족하여 **세 값을 한 세트로 움직였습니다.**

| 지점 | 이전 | 현재 | 코드 |
|---|---|---|---|
| 방향광 기본 강도 | `0.85f` | **`3.0f`** | [`LightComponent.hpp`](file:///e:/Haruka/src/components/LightComponent.hpp)의 `intensity` |
| 하늘 복사휘도 스케일 | 없음 | **`kSkyRadianceScale = 0.30f`** | [`Renderer.cpp:268`](file:///e:/Haruka/src/renderer/Renderer.cpp#L268), `skyColor` 반환값에 곱합니다 |
| 톤매핑 노출 | `1.0f` | **`0.4f`** | [`PostPass.cpp:158`](file:///e:/Haruka/src/renderer/passes/PostPass.cpp#L158)의 `PostPC` 세 번째 필드 |

* 프리필터 큐브맵의 평균 앰비언트 상수에도 같은 스케일을 적용했습니다([`Renderer.cpp:398`](file:///e:/Haruka/src/renderer/Renderer.cpp#L398)). 빠뜨리면 거칠기가 높은 밉에서 예전 하늘 밝기가 스페큘러 IBL로 되돌아옵니다.
* **★ 세 값은 한 세트입니다.** 강도만 올리고 노출을 그대로 두면 ACES 숄더에 올라타 밝기만 커지고 대비는 늘지 않습니다.
* **DEF-37 서술의 "약 2.09배"와 "약 4.4 대 1"은 이 변경 이전 기준의 값입니다.** 앞의 것은 추정기 수식에서 유도한 수치라 그대로 유효하고, 뒤의 것은 태양 강도 `0.85`와 하늘 스케일 없음을 전제로 어림한 **추정**이므로 현재 코드에 대입하면 안 됩니다.
* 상세와 근거는 [Primitive_And_Lighting_Design.md](Primitive_And_Lighting_Design.md) 8-12절에 있습니다. 노출이 여전히 하드코딩이고 `FrameUBO::timeParams.w`가 읽히지 않는다는 구조적 지적은 같은 문서 8-4절에 그대로 남아 있습니다.

---

## 트랜스폼 결함 (DEF-49 ~ DEF-54, 2026-08-26)

정본 설계·분석은 [Transform_Design.md](Transform_Design.md)입니다.

| ID | 심각도 | 내용 | 상태 |
|---|---|---|---|
| DEF-49 | CRITICAL(편집) | 월드 행렬 캐시 우회로 에디터 편집이 화면에 반영 안 됨 | **해결 2026-08-26** (캐시 제거) |
| DEF-50 | HIGH | `gizmoMode_`가 기즈모에 미전달. 회전·크기 기즈모 부재 | **해결 2026-08-26** (회전 원호·크기 핸들 구현) |
| DEF-51 | MEDIUM | `worldSpace_`가 기즈모에 미전달 | **해결 2026-08-26** (이동 기즈모 기준) |
| DEF-52 | MEDIUM | 기즈모가 월드 이동량을 로컬에 그대로 더함 | **해결 2026-08-26** (부모 공간 변환) |
| DEF-53 | MEDIUM | 물리가 로컬 회전 + 월드 위치를 섞음 | **해결 2026-08-26** (월드 TRS). 단 부모 있는 다이내믹 바디 되쓰기는 미해결 |
| DEF-54 | LOW | 기즈모 박스 고정 ±0.55 | **해결 2026-08-26** (메시 AABB×스케일) |
