# Haruka Engine 렌더링 API 아키텍처 의사결정 보고서: 순수 Vulkan 단일화의 당위성

> **[검증 상태 · 2026-08-24]** 이 문서는 github.com/carbonengine 저장소 원본 및
> `src/` 코드와 대조하여 정정되었습니다. 정정 근거와 전체 내역은
> [Carbon_Source_Audit.md](archive/Carbon_Source_Audit.md)를 참조하십시오.
> 계층 구분은 [Layer_Architecture.md](Layer_Architecture.md)를 따릅니다.
>
> 최종 결론(순수 Vulkan 단일화)은 검증 후에도 유지됩니다. 정정된 것은 결론이 아니라 그 근거와 표현이며, 특히 §2-2의 "Multi-RHI는 불가능하다"는 논거, §2-1의 "100% 동일 구동" 주장, 그리고 §3 결론의 "완벽한 통일" 및 "우주 VR 타깃" 표현입니다.

> **핵심 질문**: *"현재 순수 Vulkan 기반으로 구성되어 있는데, DirectX 12 등을 추가로 포함하지 않아도 괜찮은가?"*  
> **최종 결론**: **"DirectX를 포함하지 않고 순수 Vulkan 단일 API를 고수하는 것이 옳습니다. 다만 그 실체는 '완벽한 단일화'가 아니라 '단일 API, 두 피처 계층'입니다."** (§3 참조)

---

## 1. 종합 평가 요약

```
[접근법 A: Multi-RHI (Vulkan + DirectX 12 + Metal)]
  • 렌더링 추상화 계층(RHI) 도입으로 코드베이스 3배 비대화
  • 셰이더 크로스 컴파일(HLSL ↔ GLSL/SPIR-V) 툴체인 복잡도 폭증
  • Quest Pro 모바일 타일 기반 최적화와 Windows PC 코드 분기로 Lean 철학 파괴

[접근법 B: Pure Vulkan 1.4 단일화 (Haruka Engine 채택 - 강력 권장)]
  • Windows PC 개발 환경과 Quest Pro (Snapdragon XR2+, Android) 타깃이 단일 API, 두 피처 계층
  • 단일 SPIR-V 셰이더 바이너리 및 1GB Core Heap 직접 제어
  • OpenXR (XR_KHR_vulkan_enable2) 네이티브 무결점 연동
```

---

## 2. 순수 Vulkan 단일화가 압도적인 4가지 공학적 이유

### 1) Quest Pro (모바일 XR) 타깃과의 절대적 호환성
* **DirectX의 한계**: DirectX 11/12는 Microsoft Windows 및 Xbox 전용 API로, **Quest Pro(Android/Linux 커널, Snapdragon XR2+)에서는 1%도 동작하지 않습니다.**
* **Vulkan의 우위**: Vulkan은 모바일 XR(Meta Quest, Pico, Vision Pro 파이프라인)의 **글로벌 표준이자 유일한 고성능 저수준 그래픽스 API**입니다.
* **일원화 효과**: Vulkan을 단일 백엔드로 유지하면 **API가 하나로 유지되고, 셰이더 바이너리와 렌더 패스 구조가 그대로 넘어갑니다.** 정확히 말하면 **"단일 API, 두 피처 계층"**입니다.

  **[정정]** 이전 판은 "Quest Pro 기기에서 1바이트의 변환도 없이 100% 동일하게 구동"이라고 서술했으나 이는 사실이 아닙니다. 현재 코드는 데스크톱 기준 Vulkan 1.4 피처를 조건 없이 요구합니다. `Renderer.cpp:502`가 `VK_API_VERSION_1_4`를 지정하고, 피처 체인이 `VkPhysicalDeviceVulkan14Features{pushDescriptor}` → `Vulkan13Features{synchronization2}` → `Vulkan12Features{drawIndirectCount}` 순으로 연결되어 있으며, `Renderer.cpp:586`이 synchronization2를 분기 없이 요구합니다. 실제 호출부도 폴백이 없습니다. `vkCmdPipelineBarrier2`가 `RenderContext.cpp:167`과 `CullPass.cpp:166`, `:194`에서 무조건 호출되고, `vkCmdDrawIndexedIndirectCount`가 `GeometryPass.cpp:507`에서 분기 없이 호출됩니다. [Roadmap.md](Roadmap.md)의 Phase 4-1 "모바일 Vulkan API 호환성 다운그레이드(Fallback)"가 이미 이 두 기능의 Vulkan 1.1/1.2 호환 처리를 과제로 잡고 있습니다. 따라서 이식 비용은 0이 아니라 "피처 계층 하나를 더 유지하는 비용"이며, 그래도 API 자체를 하나 더 유지하는 Multi-RHI 비용보다 훨씬 작다는 것이 이 절의 논지입니다.

---

### 2) Haruka Engine의 핵심 설계 철학 *"Lean 아키텍처"* 수호

**[정정]** 이전 판은 이 절을 "Multi-RHI 추상화는 현실적으로 불가능하다"는 취지로 서술했습니다. 그러나 반례가 바로 옆에 실재합니다. **Carbon의 렌더러인 Trinity가 정확히 그 다중 RHI 추상화 계층(`trinityal/`)을 가지고 있으며, dx11 / dx12 / metal 세 백엔드를 실제로 운용합니다**(네 번째 디렉터리 `stub` 포함, CMake 옵션 `BUILD_DX11` / `BUILD_DX12` / `BUILD_METAL`). 따라서 정직한 논거는 "불가능해서"가 아니라 **"1인 개발 규모에서 비용 대비 이득이 없어서"**입니다. 아래와 같이 다시 씁니다.

* **비용은 실재하고 측정 가능합니다**: DirectX 12와 Vulkan을 동시에 지원하려면 가상 함수 인터페이스(`IRenderDevice`, `IBuffer`, `ICommandBuffer`) 형태의 RHI 래핑 계층과 그것을 지탱하는 백엔드별 리소스 수명 관리, 백엔드별 상태 테이블, 백엔드별 회귀 테스트가 함께 필요합니다. Trinity는 이 비용을 스튜디오 규모의 상근 렌더링 팀으로 감당하고 있습니다. Haruka에는 그 인력이 없습니다.
* **추상화가 강제하는 기술적 양보**:
  * 가상 함수 호출 오버헤드가 발생합니다.
  * Vulkan 특유의 강점인 **Subpass 타일 메모리 최적화(`VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT`)**나 **Timeline Semaphore** 같은 네이티브 기능을 공통 분모에 맞추느라 희생해야 합니다. 이는 Quest Pro의 타일 기반 GPU에서 바로 대역폭 손실로 나타납니다.
* **백엔드 추가는 팀이 있어도 쉽게 끝나지 않습니다**: CCP 상하이(Fenris Shanghai)가 Trinity에 Vulkan 백엔드를 붙이려 한 기록이 공개되어 있습니다. carbonengine/trinity PR #67 "M4 vulkan trinity al"(작성자 wuyuntao, 브랜치 `ccpshanghai/trinity:m4-vulkan-trinity-al`, 48커밋)은 `trinityal/vulkan/Tr2RenderContextVulkan.cpp/h`와 `TrinityAL_vulkan` CMake 타깃을 포함하지만 **병합되지 않고 닫혔습니다.** 이미 3-백엔드 추상화를 갖춘 팀조차 네 번째 백엔드를 본선에 올리지 못했다는 사실은, 추상화 계층이 존재한다고 해서 백엔드 추가 비용이 낮아지지는 않는다는 점을 보여 줍니다.
* **Vulkan 단일화의 이득**: Vulkan 1.4 API를 엔진 코어가 직접 호출함으로써 **중간 계층 0%, 1GB Core Heap 직접 바인딩, 극단적인 Zero-Overhead**를 달성할 수 있습니다. 결정의 근거는 상대 진영의 불가능성이 아니라 이쪽의 이득과 인력 제약입니다.

---

### 3) 셰이더 파이프라인의 단일화 (Pure SPIR-V)
* DirectX 12를 지원하려면 HLSL(DXIL)과 GLSL(SPIR-V)을 모두 관리하거나, SPIRV-Cross 같은 트랜스파일러를 빌드 체인에 추가해야 합니다.
* Vulkan 단일 파이프라인에서는 `glslc`를 통해 **단 하나의 SPIR-V 바이너리(`*.spv`) 포맷**만 생성하여 Windows와 Quest Pro 모두에 그대로 배포할 수 있습니다.

> **각주 (정확성 보완)**: HLSL이 SPIR-V로 컴파일되지 않는다는 뜻은 아닙니다. 실제로 CCP 상하이의 Vulkan 백엔드 작업은 Trinity의 HLSL 셰이더를 `dxc -spirv -fvk-b-shift ... -fvk-t-shift ...`로 SPIR-V로 컴파일합니다. 즉 "DirectX를 지원하면 반드시 SPIRV-Cross 같은 트랜스파일러가 필요하다"는 서술은 지나치게 강합니다. 다만 이 사실이 위 논거를 약화시키지는 않습니다. 문제는 소스 언어가 아니라 **관리해야 할 산출물과 툴체인의 개수**이기 때문입니다. 참고로 Trinity 본체의 `shadercompiler/`는 CCP 자체 HLSL 프론트엔드(`HLSLParser.y` + `HLSLScanner.re`, lemon + re2c)이며 **HLSL과 MSL만 방출하고 SPIR-V·GLSL 경로가 없습니다.** 따라서 Trinity에서 Vulkan을 쓰려면 별도의 dxc 경로를 추가로 유지해야 하며, 이는 셰이더 툴체인이 하나 더 늘어난다는 뜻입니다. Haruka는 `glslc` 하나만 유지합니다.

---

### 4) OpenXR 표준과의 네이티브 결합
* Meta Quest 및 차세대 VR HMD의 표준 런타임인 **OpenXR**은 `XR_KHR_vulkan_enable2` 확장을 기본 제공합니다.
* Vulkan 스왑체인 이미지를 OpenXR 렌더 타깃으로 무복사(Zero-Copy) 직접 바인딩하고, **Multiview (`VK_KHR_multiview`)**를 단일 렌더패스로 결합하여 **좌우 두 눈(Left/Right Eye) 렌더링을 1회 드로우콜로 90fps 방어**할 수 있습니다.

---

## 3. 결론

Haruka Engine에 DirectX 12를 추가하는 것은 **1인 개발 규모에서 치르는 비용에 비해 돌아오는 것이 없는 선택**입니다. RHI 추상화 계층과 그것을 지탱하는 백엔드별 리소스 수명 관리, 상태 테이블, 회귀 테스트를 영구히 유지해야 하는 대신, 얻는 것은 Haruka의 주 타깃 기기인 Quest Pro가 애초에 실행할 수 없는 API 하나입니다. §2-2에서 정리했듯 이 판단의 근거는 Multi-RHI가 불가능하다는 데 있지 않고(Trinity가 반례입니다), 이 프로젝트의 인력 규모에서 비용 대비 이득이 성립하지 않는다는 데 있습니다.

**[정정]** 이전 판의 결론 문장은 "Vulkan 1.4 단일 API로 Windows 개발 환경과 Quest Pro 스탠드얼론 우주 VR 타깃을 완벽하게 통일하는 현재의 아키텍처가 가장 정확하고 이상적인 방향"이었습니다. 두 가지를 고칩니다. 첫째, "완벽하게 통일"은 사실이 아니며 정확한 표현은 **"단일 API, 두 피처 계층"**입니다. 둘째, 이 결정은 엔진 코어(Layer 1)의 결정이므로 "우주 VR 타깃"이 아니라 **"VR 타깃"**이라고 적어야 합니다. 우주 도메인은 이 결정 위에 얹히는 Layer 2이며, 우주 프로젝트를 걷어내도 결론은 그대로 성립합니다. 계층 구분은 [Layer_Architecture.md](Layer_Architecture.md)를 따릅니다.

따라서 정확한 결론은 다음과 같습니다.

> **Haruka는 렌더링 API로 Vulkan 하나만을 채택하고, 그 위에서 두 개의 피처 계층을 유지합니다.**

### 두 피처 계층

| 계층 | 대상 | 성격 | 현재 상태 |
|---|---|---|---|
| **데스크톱 개발 계층** | Windows PC 개발 환경 | Vulkan 1.4를 조건 없이 요구합니다. 피처 체인은 `VkPhysicalDeviceVulkan14Features{pushDescriptor}` → `Vulkan13Features{synchronization2}` → `Vulkan12Features{drawIndirectCount}`이며, `Renderer.cpp:502`가 `VK_API_VERSION_1_4`를 지정합니다 | 구현되어 동작합니다 |
| **Quest 폴백 계층** | Quest Pro 스탠드얼론 (Snapdragon XR2+, Android) | 상위 피처를 Vulkan 1.1/1.2 범위의 등가 경로로 대체합니다. `vkCmdDrawIndexedIndirectCount`와 `vkCmdPipelineBarrier2`가 각각 폴백 대상입니다 | **미구현.** [Roadmap.md](Roadmap.md) Phase 4-1 "모바일 Vulkan API 호환성 다운그레이드(Fallback)"가 과제로 잡고 있으며, [Layer_Architecture.md](Layer_Architecture.md) §6의 0단계에 배치되어 있습니다 |

두 계층이 **공유하는 것**은 다음과 같습니다.

* 그래픽스 API 자체(Vulkan)와 그에 딸린 개념 모델(디스크립터, 렌더패스, 메모리 타입, 큐 패밀리)을 공유합니다.
* 셰이더 소스와 산출물을 공유합니다. `glslc`가 만든 동일한 `*.spv`를 양쪽에 그대로 배포합니다.
* 렌더 패스 구조, 리소스 레이아웃, 1GB Core Heap의 존 배치를 공유합니다.
* OpenXR 연동 경로(`XR_KHR_vulkan_enable2`, `VK_KHR_multiview`)를 공유합니다.

두 계층이 **갈라지는 것**은 위 표에 적은 소수의 명령과 그것을 요구하는 디바이스 피처뿐입니다. 갈라지는 지점은 디바이스 생성 시점의 피처 질의와 그 결과에 따른 호출부 분기로 국지화됩니다.

이 구분이 중요한 이유는, 그것이 이식 비용을 0으로 주장하지 않으면서도 결론을 지켜 주기 때문입니다. 실제 비용은 0이 아니라 **"피처 계층 하나를 더 유지하는 비용"**이며, 이는 **"API를 하나 더 유지하는 비용"**보다 한 자릿수 이상 작습니다. 전자는 호출부 몇 곳의 분기이고, 후자는 추상화 계층과 백엔드 전체입니다.

### 남은 과제

결론이 유지된다고 해서 현재 코드가 타깃 기기에서 도는 것은 아닙니다. Quest 폴백 계층은 아직 존재하지 않으며, 이것이 이 결정을 실현하기 위한 선결 과제입니다. 착수 순서는 [Layer_Architecture.md](Layer_Architecture.md) §6을 따릅니다.

> **참고**: 위 폴백 대상 두 가지는 Roadmap Phase 4-1이 명시한 항목입니다. 피처 체인 최상단의 `VkPhysicalDeviceVulkan14Features{pushDescriptor}` 역시 같은 조건 없는 요구에 해당하므로, 폴백 범위를 확정할 때 함께 검토해야 합니다. 이 항목의 폴백 처리 방침은 아직 문서화되어 있지 않습니다.
