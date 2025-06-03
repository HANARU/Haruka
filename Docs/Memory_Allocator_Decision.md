# 1GB Core Heap 메모리 할당기 아키텍처 결정 보류 문서

> **[검증 상태 · 2026-08-24]** 이 문서는 github.com/carbonengine 저장소 원본 및
> `src/` 코드와 대조하여 정정되었습니다. 정정 근거와 전체 내역은
> [Carbon_Source_Audit.md](archive/Carbon_Source_Audit.md)를 참조하십시오.

본 문서는 하이엔드 XR 타깃 (Quest Pro 등) Vulkan 엔진 구축 시, **1GB 단일 `DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT` 힙 영역**을 효율적으로 관리하기 위한 메모리 할당기(Allocator) 구조에 대한 설계 논의를 백업한 문서입니다.

이슈: 1GB 힙 영역 내에서 Zone A(엔진 구조체), Zone B(에셋), Zone C(동적 버퍼)를 관리하기 위해 VMA 라이브러리를 내장할 것인가, 아니면 자체 커스텀 할당기를 바닥부터(From Scratch) 구현할 것인가.

---

## 0. 현재 구현 상태 (2026-08 실측)

이 결정은 여전히 보류이지만, 그동안 **옵션 B에 해당하는 `HarukaAllocator`가 이미 구현되어 운용 중**입니다. 아래 값은 `HarukaAllocator.hpp`에서 직접 확인한 것입니다.

| Zone | 크기 | 오프셋 | 용도 | 할당 방식 |
|---|---|---|---|---|
| Zone A | 64MB (`kZoneASize`) | 0 | 엔진 구조체 | 범프(bump) |
| Zone B | 896MB (`kZoneBSize`) | 64MB | 에셋 뱅크 | `bufferImageGranularity` 고려한 블록 |
| Zone C | 64MB (`kZoneCSize`) | 960MB | 동적 버퍼 | 링(ring) |

세 값의 합이 `kHeapTarget`(1GB)과 같다는 것은 `static_assert`로 고정되어 있어, 한 Zone을 키우려면 다른 Zone을 반드시 줄여야 합니다. 즉 Zone 경계는 런타임 튜닝 대상이 아니라 컴파일 타임 계약입니다.

따라서 남은 질문은 "무엇을 처음부터 만들 것인가"가 아니라 **"이미 도는 것을 계속 유지할 것인가, 일부를 검증된 구현으로 교체할 것인가"**입니다.

> **각주 (알려진 문제)**: `HarukaAllocator.cpp:177-199`의 `allocC`에 펜스 추적이 없습니다. 링 버퍼가 랩어라운드할 때 아직 인플라이트인 영역을 덮어쓸 수 있는 구조입니다. 다만 심각도는 낮습니다. `HarukaAllocator.hpp:9` 주석에 따르면 Zone C 실사용은 64MB 중 약 0.5MB(약 128배 여유)이므로 랩어라운드는 약 128프레임마다 일어나고 덮어쓰는 대상은 128프레임 전 데이터입니다. 즉 CRITICAL이 아니라 **프레임당 Zone C 사용량이 32MB에 근접할 때 발현되는 잠재 리스크**이며, 인스턴싱이나 대규모 파편장 도입으로 동적 버퍼 사용량이 늘어나면 그 시점에 펜스 추적을 붙여야 합니다.

---

## 옵션 A: VMA (Vulkan Memory Allocator) 도입

AMD에서 오픈소스로 제공하는 C++ 단일 헤더 기반(`vk_mem_alloc.h`)의 Vulkan 공식 권장 메모리 할당기. 

### 장점 (Pros)
1. **압도적인 디버깅 및 프로파일링 능력**
   - 메모리 릭(Leak) 추적, 현재 힙의 단편화(Fragmentation) 상태를 시각화할 수 있는 JSON 덤프 기능 등 강력한 디버깅 툴 제공.
2. **복잡한 Vulkan 스펙 자동 준수**
   - Vulkan은 하드웨어 제조사마다 버퍼와 이미지 간 정렬 규칙(`bufferImageGranularity`) 및 메모리 타입 매칭이 매우 까다로움. VMA는 이를 알아서 완벽하게 처리함.
3. **Custom Pool 기능 지원**
   - VMA 전체를 쓰지 않고, 우리의 1GB 힙을 VMA의 'Custom Pool'로 등록하기만 하면, 그 안에서 Zone 분할 및 메모리 할당/해제 로직을 매우 빠르고 안전하게 구축 가능.

### 단점 (Cons)
1. **의존성 증가 및 블랙박스화**
   - 라이브러리 코드 크기가 방대하여 빌드 타임이 미세하게 증가할 수 있음.
   - 내부 블랙박스 오버헤드가 존재하여, "불필요한 기능이 단 하나도 없는 극한의 Lean 아키텍처"라는 엔진 철학에 부분적으로 위배됨.

---

## 옵션 B: 커스텀 할당기 자체 구현 (From Scratch)

엔진 코어에서 `vkAllocateMemory`로 1GB를 한 번만 직접 할당받은 뒤, 자체적인 포인터 수학 연산을 통해 메모리를 배분하는 방식.

### 장점 (Pros)
1. **극강의 Lean 아키텍처 실현**
   - VMA의 방대한 기능 중 쓰지 않는 것들을 전부 덜어낸, 프로젝트에 딱 맞는 초경량/초고속 할당기 제작 가능.
2. **100% 완벽한 메모리 통제권**
   - FlatBuffers 같은 중간 직렬화 라이브러리를 배제하고 Raw 바이너리를 1:1로 직접 메모리에 매핑(Zero-Copy)하려는 수석 연구원님의 개발 철학에 가장 부합함. 엔진이 바이트 단위까지 하드웨어 메모리를 완벽하게 장악함.

### 단점 (Cons)
1. **높은 개발 난이도와 시간 비용**
   - 할당/해제에 따른 메모리 단편화(Fragmentation) 관리 전략(예: TLSF, Buddy System 등)과 블록 병합(Coalescing)을 직접 설계하고 검증해야 함.
   - Vulkan 하드웨어별 세부 정렬 규칙들을 모두 메뉴얼하게 대응해야 하므로 초기 버그 발생 확률이 높음.
2. **디버깅의 어려움**
   - 개발 중 메모리 누수나 오염 발생 시 시각적으로 상태를 추적할 도구를 처음부터 같이 만들어야 함.

---

## 옵션 C: VMA를 Vulkan 없이 오프셋 할당기로만 사용 (제3안)

이 선택지는 이전 판에 없었습니다. 실측 자료를 근거로 추가합니다.

**Trinity는 DirectX 엔진이면서도 AMD의 VMA를 vcpkg 의존성으로 가지고 있습니다.** `vcpkg.json`에 `vulkan-headers`와 `vulkan-memory-allocator`가 들어 있지만, Vulkan을 쓰기 위해서가 아닙니다. `trinity/Tr2VirtualAllocator.cpp`가 VMA의 **가상 블록 할당자만 CPU측 서브할당자로** 사용합니다.

* 매크로 설정: `VMA_STATIC_VULKAN_FUNCTIONS 0`, `VMA_DYNAMIC_VULKAN_FUNCTIONS 0`
* 호출하는 함수: `vmaCreateVirtualBlock`, `vmaVirtualAllocate`, `vmaVirtualFree`, `vmaDestroyVirtualBlock` **이 넷뿐입니다.**
* Vulkan 디바이스도 Vulkan 메모리도 사용하지 않습니다.

### 이것이 이 결정에 주는 의미

VMA의 가상 블록 API는 실제 메모리를 다루지 않고 **크기와 정렬만 받아 오프셋을 돌려주는 순수 할당 대수(algebra)**입니다. 즉 "VMA를 도입한다"와 "VMA를 도입하지 않는다"가 배타적 이분법이 아니었습니다. 다음과 같은 배분이 가능합니다.

* `vkAllocateMemory` 호출, 1GB 단일 블록 보유, Zone 경계 결정, 매핑 포인터 관리, 하드웨어 정렬 규칙 대응은 **Haruka가 그대로 소유**합니다. Zero-Copy 철학과 바이트 단위 통제권이 유지됩니다.
* 옵션 B의 가장 어려운 부분, 즉 Zone B 안에서의 자유 리스트 관리와 블록 병합(Coalescing), 단편화 통계는 **VMA의 가상 블록에 위임**합니다. TLSF나 Buddy System을 직접 설계하고 검증할 필요가 사라집니다.
* Zone A(범프)와 Zone C(링)는 애초에 자유 리스트가 필요 없으므로 지금 형태를 유지합니다. 즉 위임 대상은 Zone B 하나입니다.

### 비용과 유의점
* 헤더 하나가 빌드에 추가되므로 "의존성 0" 상태는 아니게 됩니다. 다만 Vulkan 함수 포인터를 전혀 로드하지 않으므로 런타임 표면적은 옵션 A보다 훨씬 작습니다.
* Trinity가 이 용법을 프로덕션에서 쓰고 있다는 점이 실현 가능성의 근거입니다. 다만 Trinity의 대상 플랫폼은 x64-windows / x64-osx / arm64-osx이며 **Android 프리셋이 없으므로**, Quest Pro(arm64-android)에서의 빌드 가능 여부는 이 사례가 보증해 주지 않습니다. 채택 전에 직접 확인해야 합니다.

---

## 결론 및 현재 상태
- 본 아키텍처 결정은 여전히 **보류 상태**입니다. 다만 보류의 성격이 바뀌었습니다. 옵션 B가 이미 구현되어 돌고 있으므로, 지금의 질문은 "무엇으로 시작할 것인가"가 아니라 "지금 것을 어디까지 유지할 것인가"입니다.
- 선택지는 세 개입니다. **(A)** VMA 전면 도입, **(B)** 현행 `HarukaAllocator` 유지, **(C)** Zone B의 자유 리스트만 VMA 가상 블록에 위임하는 혼합안입니다.
- 결정 트리거를 두 가지로 잡습니다. 첫째, **Zone B 단편화가 실제로 관측될 때** (A) 또는 (C)를 검토합니다. 둘째, **프레임당 Zone C 사용량이 커질 때** 옵션과 무관하게 링 버퍼 펜스 추적을 먼저 붙입니다. 그 전까지는 (B)를 유지합니다.
