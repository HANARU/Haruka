# Renderer 리팩토링 설계 v2

> 작성: 2026-06-11 · 대상: `src/renderer/` (4,083줄, Renderer 단일 클래스 ~150 멤버)

## 1. 현황 진단

현재 Renderer는 물리적으로만 3개 파일(`Renderer.cpp` 초기화 / `Renderer_render.cpp` 프레임 / `Renderer_swap.cpp` 스왑체인·패스·PSO)로 나뉘어 있고, 논리적으로는 하나의 갓 클래스다.

**한 클래스에 섞여 있는 책임:**

| 책임 | 규모 | 비고 |
|---|---|---|
| Vulkan 부트스트랩 (인스턴스/디바이스/큐/디버그) | ~400줄 | 한 번 실행 |
| 저수준 헬퍼 (이미지/뷰/배리어/셰이더 모듈) | ~300줄 | 모든 곳에서 사용 |
| 자산 업로드 (메가버퍼/텍스처 슬롯/머티리얼) | ~350줄 | AssetManager 전용 경로 |
| 디스크립터 인프라 (set 0~3 레이아웃 + 프레임별 세트) | ~300줄 | 패스 간 공유 계약 |
| 렌더 패스 8종 생성/기록 | ~1,800줄 | 섀도/메인/linDepth/SSAO/블룸/포스트/라인/컬링 |
| IBL 베이킹 (1회성) | ~300줄 | 초기화 시에만 |
| 프레임 오케스트레이션 + 동기화 + 계측 | ~500줄 | drawFrame |

**고정 프레임 흐름** (Renderer_render.cpp 검증 결과):

```
updateUBO → updateLightSSBO → buildDrawCommands(CPU)
  ↓
[TS0] Shadow (CPU 드로우 리스트, 2048² depth)
[TS1] Cull (compute, opaque indirect 버퍼 생성)
      Main 3-subpass: DepthPre(indirect) → Opaque(indirect) → Transparent(CPU 정렬)
[TS2] LinDepth(half-res) → SSAO(half-res)
[TS3] Bloom (threshold + 4×down + 4×up = 9 드로우)
[TS4] Post (ACES + TAA resolve + bloom/SSAO 합성) → TAA 히스토리 blit → UI(HSlate)
[TS5] present
```

## 2. 설계 원칙

1. **범용 렌더그래프를 만들지 않는다.** 패스 그래프는 고정이고 Quest Pro 성능 예산에서 추상화 비용을 치를 이유가 없다. 배리어 자동화도 하지 않는다 — 현재 수동 배리어가 모두 검증된 상태다.
2. **패스는 구체 클래스.** 공통 수명주기(파괴/리사이즈/핫리로드)만 얇은 인터페이스로 묶고, 생성·기록은 패스별 시그니처를 그대로 둔다. 가상 디스패치는 프레임당 0회.
3. **모든 GPU 메모리는 HarukaAllocator 경유** — 현재 불변식 유지. 패스가 직접 vkAllocateMemory 금지.
4. **공개 API 불변.** EditorApp/AssetManager/main이 쓰는 19개 메서드 시그니처는 그대로.
5. **모든 마이그레이션 단계는 빌드+실행 가능.** 한 단계 = 한 모듈 추출.

## 3. 목표 모듈 구조

```
src/renderer/
├── RenderContext.hpp/.cpp     # ① 디바이스 핸들 번들 + 저수준 헬퍼
├── FrameResources.hpp/.cpp    # ② set0~3 레이아웃 + 프레임별 버퍼/디스크립터/동기화
├── GpuScene.hpp/.cpp          # ③ 메가버퍼 + 자산 업로드 + 텍스처 슬롯 + 컬링 입력
├── IBLBaker.hpp/.cpp          # ④ BRDF LUT/큐브맵 1회 베이킹 → SceneTextures 출력
├── passes/
│   ├── HPass.hpp              # 수명주기 인터페이스
│   ├── ShadowPass.hpp/.cpp
│   ├── CullPass.hpp/.cpp
│   ├── GeometryPass.hpp/.cpp  # main 3-subpass + 라인 오버레이
│   ├── AOPass.hpp/.cpp        # linDepth + SSAO 2-스테이지 한 모듈
│   ├── BloomPass.hpp/.cpp
│   └── PostPass.hpp/.cpp      # ACES/TAA + TAA 히스토리 + UI 훅
└── Renderer.hpp/.cpp          # ⑤ 오케스트레이터 (~400줄 목표)
```

### ① RenderContext — 디바이스 번들 + 헬퍼

```cpp
// 모든 패스/모듈이 받는 단일 컨텍스트. Renderer가 소유.
class RenderContext {
public:
    VkDevice         device   = VK_NULL_HANDLE;
    VkPhysicalDevice physDev  = VK_NULL_HANDLE;
    VkQueue          gfxQueue = VK_NULL_HANDLE;
    HarukaAllocator* allocator = nullptr;
    VkPipelineCache  pipelineCache = VK_NULL_HANDLE;
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_4_BIT;
    VkFormat         swapFormat = VK_FORMAT_UNDEFINED;
    VkFormat         depthFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D       extent{};            // 리사이즈 시 갱신

    // Renderer에서 이동하는 헬퍼 (구현 동일, 소속만 변경)
    VkImage        createImage(...);
    VkImageView    createImageView(...);
    void           transitionImage(...);
    VkShaderModule createShaderModule(const std::vector<char>&);
    VkPipeline     createFullscreenPipeline(...);
    uint32_t       findMemType(...);
    VkCommandBuffer beginOneTimeCmd();
    void            endOneTimeCmd(VkCommandBuffer);
};
```

추가로 현재 7회 반복되는 단순 렌더패스 생성을 흡수한다:

```cpp
    VkRenderPass  createColorPass(VkFormat fmt, VkImageLayout finalLayout);
    VkFramebuffer createFramebuffer(VkRenderPass rp, std::span<VkImageView>, VkExtent2D);
    // 10회+ 반복되는 디스크립터 레이아웃 보일러플레이트 흡수
    VkDescriptorSetLayout makeSetLayout(std::initializer_list<BindingDesc>, bool pushDescriptor);
```

### ② FrameResources — 프레임별 상태

현재 `frameIdx`로 인덱싱되는 배열 ~20개를 구조체로 응집:

```cpp
struct FrameSlot {
    VkCommandBuffer  cmd;
    VkSemaphore      semImageAvail, semRenderDone;
    VkFence          fence;
    VkBuffer         ubo;            HarukaAllocation uboAlloc;
    VkBuffer         transformSSBO;  HarukaAllocation transformAlloc;
    VkBuffer         lightSSBO;      HarukaAllocation lightAlloc;
    VkDescriptorSet  frameSet, sceneSet, transformSet;
};

class FrameResources {
public:
    void create(RenderContext&);
    void destroy(RenderContext&);
    FrameSlot& slot(uint32_t i);

    // 패스 PSO 생성에 필요한 공유 계약
    VkDescriptorSetLayout frameLayout, sceneLayout, transformLayout, materialLayout;
    VkPipelineLayout      pbrPipelineLayout;   // set 0+1+2+3
};
```

set 0~3 레이아웃은 **패스 간 셰이더 계약**이므로 개별 패스가 아닌 여기서 소유한다.

### ③ GpuScene — 자산이 사는 곳

`uploadMesh / uploadMaterial / freeMesh / freeMaterial / uploadTexture / freeTexSlot` + 메가버퍼(128MB vert + 64MB idx) + 컬링 입력 버퍼(BoundSphere/DrawCmd). AssetManager가 만지는 모든 것. Renderer 공개 API는 GpuScene으로 위임.

### ④ 패스 공통 수명주기

```cpp
struct HPass {
    virtual ~HPass() = default;
    virtual void destroy(RenderContext&) = 0;
    virtual void onResize(RenderContext&) {}          // 크기 의존 자원 재생성
    virtual void rebuildPipelines(RenderContext&) {}  // 셰이더 핫리로드
};
```

`create(...)`와 `record(...)`는 패스마다 입력이 달라 **의도적으로 가상이 아니다**. Renderer가 명시적 시그니처로 호출한다:

```cpp
// 예: 패스 간 연결은 Renderer::wirePassInputs() 한 곳에 모은다 (리사이즈 시 재호출)
shadow_.create(rc_, frames_.pbrPipelineLayout);
ao_.create(rc_, depthView_);                            // 깊이 읽기
post_.create(rc_, swapchain_->views(),                  // 출력
             geometry_.hdrViews(), ao_.ssaoView(),      // 입력
             bloom_.resultView(), taaHistory_.view());
```

수명주기 일괄 처리는 포인터 배열로:

```cpp
std::array<HPass*, 6> passes_{ &cull_, &shadow_, &geometry_, &ao_, &bloom_, &post_ };
void Renderer::reloadPipelines() { for (auto* p : passes_) p->rebuildPipelines(rc_); }
```

### ⑤ Renderer — 남는 것

- init 순서 결정, RenderContext/FrameResources/GpuScene/패스 소유
- `drawFrame`: acquire → 프레임 데이터 갱신 → 각 패스 `record()` 순차 호출(타임스탬프 포함) → submit/present
- 리사이즈: `cleanupSwapChain` → extent 갱신 → 각 패스 `onResize` → `wirePassInputs`
- 공개 API 19개 유지 (내부는 위임)

### 패스별 소유 자원 매핑

| 패스 | 가져가는 멤버 (현재 Renderer.hpp 기준) |
|---|---|
| ShadowPass | shadowPass_/Framebuffer_/Image_/View_/Alloc_/Sampler_, shadowPipeline_ |
| CullPass | cullPipeline_/PipeLayout_/SetLayout_/DescPool_/DescSets_, opaqueDraw/Count 버퍼 |
| GeometryPass | mainPass_, mainFramebufs_, msaa·depth·hdr 이미지, depth/opaque/transparent/line PSO, 라인 버퍼 |
| AOPass | linDepth* 전부, ssao* 전부 (noise 텍스처 포함) |
| BloomPass | bloom* 전부 (이미지 9 + PSO 3 + extents) |
| PostPass | postPass_/Framebufs_/PSO, taaHist* + UI 훅(HSlateRenderer*) |
| IBLBaker→출력 | ibl*/brdfLut* 이미지 (소유는 Renderer의 SceneTextures 묶음, sceneSet에 바인딩) |

## 4. RenderView — 시뮬레이션/렌더 분리 준비 (멀티스레딩 대비)

`drawFrame(World&)`이 렌더링 도중 World를 직접 읽는 결합을 끊는다. 한 프레임에
World에서 읽어야 하는 모든 것을 프레임 초입에 한 번 추출해 스냅샷으로 만들고,
이후 모든 패스 기록은 스냅샷만 소비한다:

```cpp
// 한 프레임의 렌더 입력 전체 — World 없이 기록 가능해야 한다
struct RenderView {
    FrameUBO                ubo;          // 카메라/시간/TAA/미스크
    std::vector<GpuLight>   lights;
    std::vector<GpuTransform> transforms; // firstInstance 인덱스 순
    std::vector<GpuDrawCmd> opaqueDraws;
    std::vector<GpuDrawCmd> transpDraws;  // CPU 정렬 완료 상태
    std::vector<Renderer::LineVertex> lines;
};

RenderView extractRenderView(World& world);   // World를 읽는 유일한 지점
void       recordFrame(const RenderView&);    // World 접근 금지
```

**규칙**: `extractRenderView` 이후의 코드 경로(패스 record 전부)에서 `World*`,
`Actor*`, `Component*` 참조 금지. 컴파일 수준에서 강제하기 위해 패스 record
시그니처에는 World를 아예 전달하지 않는다.

지금은 둘 다 메인 스레드에서 순차 실행되지만, 이 경계가 있으면 나중에
"추출은 게임 스레드, 기록은 렌더 스레드"로 바꿀 때 구조 변경이 필요 없다.
스레드 소유권 규칙은 [Threading.md](Threading.md) 참고.

## 5. VR(Quest Pro) 확장 포인트 — 지금은 시그니처만 예약

- `ISwapchain`이 이미 추상화돼 있으므로 XR 스왑체인은 새 구현체로 추가
- `FrameUBO.viewProj[2]`가 이미 스테레오 슬롯 — 셰이더 계약 변경 불필요
- GeometryPass 생성 시 `uint32_t viewCount = 1` 파라미터를 받아두고, 2가 되면 `VK_KHR_multiview`(1.1 코어) 마스크로 renderPass 분기 — 구현은 XR 작업 시
- FFR rate image는 GeometryPass 소유로 이동 (현재 placeholder 유지)

## 6. 마이그레이션 플랜 — 7단계, 각 단계 빌드 가능

| 단계 | 작업 | 상태 (2026-06-11) |
|---|---|---|
| 1 | RenderContext 도입 + 저수준 헬퍼 이동 | ✅ 완료 |
| 2 | GpuScene 추출 — 자산 경로 격리 | ✅ 완료 |
| 3 | FrameResources 추출 (set 레이아웃 + 프레임별 버퍼/디스크립터/동기화 그룹핑) | ✅ 완료 |
| 4 | 리프 패스 추출: Bloom → AO → Shadow | ✅ 완료 |
| 5 | PostPass + TAA 히스토리 | ✅ 완료 |
| 6 | GeometryPass — main pass/PSO 4종/라인 | ✅ 완료 |
| 7 | CullPass 추출 + **RenderView 도입(§4)** + 죽은 멤버 제거 | ✅ 완료 |

각 단계 종료 조건: Release 빌드 통과 + 실행 + 리사이즈(FHD↔창 최소화↔복원) + R 핫리로드 + Play/Stop 확인. Debug 빌드(validation layer)로 단계당 1회 스모크.

**구현 노트 (계획 대비 변경점)**:
- HPass 리사이즈는 단일 `onResize`가 아닌 2단계(`releaseSizeDependent`/`acquireSizeDependent`)로 구현 — Zone B 메모리 코얼레싱을 위해 스왑체인 파괴 전 해제가 필요하기 때문.
- 파이프라인 레이아웃을 각 패스 `create()`에서 1회 생성으로 변경 — 기존 핫리로드 경로가 레이아웃을 매번 재생성하며 누수하던 버그를 구조적으로 제거.
- `transpDrawBufs_`(프레임당 20KB×2, 생성·파괴만 되고 사용처 없음) 죽은 자원 제거.
- RenderView v1은 추출 단계에서 mapped 버퍼 업로드까지 수행 — 스레드 분리 시 업로드만 기록 측으로 이동하면 됨.

## 7. 비목표 (명시적으로 안 하는 것)

- 범용 렌더그래프 / 배리어 자동 추론
- 멀티스레드 커맨드 레코딩 (Quest 포팅 시 재검토)
- 디퍼드 렌더링 전환 (포워드 유지 — 아키텍처 결정 사항)
- VMA 전환 (보류 중인 결정과 독립적으로 진행 가능하도록 HarukaAllocator 인터페이스 유지)
