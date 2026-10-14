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

**[갱신 · 2026-10-02 · 종합 오류 검출]** 코드와 다시 대조해 낡은 표기를 고쳤습니다. **DEF-07**(2026-08-26 A-1 에서
`onActorDestroyed` 구독으로 해결)과 **DEF-38**(2026-08-27 라이팅 Phase 0 에서 해결)을 해결됨으로 바꿨습니다.
**DEF-40 · DEF-43 · DEF-44** 는 "미검증" 이 아니라 코드로 성립을 확인했습니다. **DEF-03** 은 A-4(GPU 컬 제거,
2026-08-27) 뒤로 indirect-count 드로우가 하나도 남지 않아, 고칠 것이 `drawIndirectCount` 기능 요구를 지우는
일로 줄었습니다. **DEF-15** 는 TAA 판단(가짜 TAA 비활성, 진짜 TAA 는 OpenXR 뒤)이 이미 내려졌으므로 남은 TAA
코드 정리로 범위가 바뀌었습니다. 라인 번호는 2026-08-24 실측값 그대로이니 파일·함수 이름으로 찾으십시오.
신규 결함 DEF-76 ~ DEF-91 은 문서 끝의 "종합 오류 검출 신규 결함" 절에 있습니다.

**[갱신 · 2026-10-02 · 종합 오류 검출 수정]** 사용자 결정("권장대로 1번부터")에 따라 세 묶음을 고쳤습니다.
(1) 크래시·값 손실: DEF-76 · DEF-77, 그리고 고치는 중에 찾은 DEF-92. (2) 입력 정리: DEF-62 ~ DEF-67, DEF-78 · DEF-79,
DEF-82 ~ DEF-84, DEF-87 · DEF-88 · DEF-90. (3) 물리·렌더·정리: DEF-40 · DEF-43 · DEF-44, DEF-59 · DEF-68, DEF-80 · DEF-81, DEF-85 · DEF-86 ·
DEF-89. **DEF-64 는 사용자 결정으로 "메뉴가 열린 동안 바깥 첫 클릭은 닫기에만 쓴다" 를 의도된 동작으로 두고**, 메뉴보다
먼저 도는 판정이 클릭을 가져가던 우회 경로만 고쳤습니다. 남은 것은 기반 공사 묶음(DEF-56 · DEF-57 · DEF-58 · DEF-60 ·
DEF-69, 따로 진행)과 잠재 항목 DEF-91 입니다. 검증은 단위 테스트 2종 신설(`slate_input` 44개 · `physics_lifecycle` 23개
검사, 고치기 전 코드에서 실패함을 확인, 검토 반영 뒤 46 · 29개)과 실기 체크리스트([Editor_QA_Checklist.md](Editor_QA_Checklist.md) 5절 "수정 후")로
했습니다. 항목별 수정 내용은 문서 끝 "수정 결과" 절에 있습니다. 수정 뒤 독립 검토(코드 재검토)에서 나온 4건도
반영했습니다: 바디를 옮길 때 이웃 바디 깨우기, 값이 실제로 바뀌었을 때만 바디 재생성, 숫자 칸 확정 판정을 입력 여부로,
그리고 UI 패스 정점 버퍼의 같은 레이스(신규 DEF-93). 반영 뒤 테스트는 `slate_input` 46개 · `physics_lifecycle` 29개 검사입니다.

**[갱신 · 2026-10-03 · 기반 공사]** 남겨 두었던 기반 공사 묶음을 고쳤습니다. DEF-56(클리핑) · DEF-57(위젯 휠 경로) ·
DEF-58(키 반복) · DEF-60(글자 폭 실측) · DEF-69(Window 메뉴)가 해결이고, 고치는 중에 찾은 DEF-94(재도킹 뒤 활성 탭이
다른 패널로 바뀜)도 함께 고쳤습니다. 순서 제약은 모두 지켰습니다. DEF-60 은 뷰포트 툴바 폭 사다리와 함께 고쳤고(손으로
계산한 임계값 표를 없애고 단계별 툴바를 세워서 잼), DEF-69 의 Window 메뉴와 탭 닫기 x 는 같은 작업에서 넣었으며,
DEF-56 · DEF-57 뒤에 아웃라이너와 Output Log 의 휠 우회를 함께 위젯 경로로 옮겼습니다. 수정 뒤 독립 검토에서 나온 4건
(잘려 안 보이는 탭이 클릭을 받음, 스크롤로 가려진 숫자 칸이 포커스를 놓지 않음, 닫힌 Outliner 로 Ctrl+F · F2 포커스가 들어감,
백스페이스를 누른 채 숫자 칸에 들어가면 바로 지워짐)과 폰트 기준 포인터, 사소한 2건도 반영했습니다. 검증은 `slate_input` 79개 검사
(새 검사 33개, 휠 전달 · 클립 기록 · 글자 폭 실측을 하나씩 되돌리면 실패함을 확인)와 실기 체크리스트
([Editor_QA_Checklist.md](Editor_QA_Checklist.md) 5절 "2026-10-03 기반 공사 수정 후")로 했습니다. 남은 항목은 잠재 DEF-91
하나입니다. 항목별 내용은 문서 끝 "기반 공사 수정 결과" 절에 있습니다.

**[갱신 · 2026-10-04 · 1단계 정리 묶음]** 사용자 결정("추천대로 진행하되, 그 전에 다른 엔진들은 해당 문제를 어떻게
해결했는지 조사를 선행")에 따라 남은 결함마다 다른 엔진의 해법을 조사하고 적용성을 확인한 뒤, 1단계 정리 묶음을
고쳤습니다. DEF-03 · DEF-11 · DEF-14 · DEF-15 · DEF-28 · DEF-29 · DEF-30 · DEF-35 가 해결이고, DEF-10 은 진단만 넣었습니다
(상한 설정화는 남음). 쓰지 않던 GPU 컬링 코드도 지웠습니다. 조사에서 새로 찾은 DEF-95 ~ DEF-103 을 등재했고, 그중
DEF-98(셰이더 핫 리로드 종료)은 같은 묶음에서 고쳤습니다. Quest 화면이 기울어져(canted) 있다는 예전 주장 등 문서의
틀린 전제도 바로잡았습니다. 수정 전후 뷰포트가 픽셀까지 같음을 확인했습니다. 항목별 내용은 문서 끝 "1단계 정리 묶음
수정 결과와 신규 결함" 절에 있습니다.

**[갱신 · 2026-10-04 · 3단계 VR 첫걸음]** 권장 순서 ③ 을 진행했습니다. DEF-21(카메라 자세) · DEF-09(두 눈 렌더, 첫 단계는
눈마다 두 번 그리기) · DEF-31(두 눈 컬링) · DEF-20(VR 입력) · DEF-19(물리 API) · DEF-99(Kinematic 동기화)가 해결이고, DEF-102 ·
DEF-53 의 남은 부분 · DEF-100 도 같은 작업에서 고쳤습니다. DEF-32 는 "한 프레임에 자세 하나" 규칙을 지키는 동안 필요 없어 보류했고,
고치는 중에 찾은 DEF-104(프레임 사이 공유 중간 타깃의 동기화)도 고쳤습니다. PC OpenXR 연결(`--xr`, Quest Link 등)을 넣었지만 이
PC 에 런타임이 없어, 헤드셋 확인은 사용자가 Quest Link 를 준비한 뒤에 합니다(사용자 결정). 사용자 결정으로 물리 규칙은 UE 식,
물리 스텝은 XR 세션 동안 헤드셋 주사율에 맞춥니다. 항목별 내용은 문서 끝 "3단계 VR 첫걸음 수정 결과" 절에 있습니다.

**[갱신 · 2026-10-14 · DEF-16]** 권장 순서의 "나머지" 묶음을 시작했습니다. DEF-16(인스턴싱)을 자동 인스턴싱으로 해결했습니다.
같은 지오메트리 · 머티리얼의 불투명 메시가 연속된 transform 행을 받아 드로우 하나(`instanceCount = N`)로 그려지고, 셰이더는
바꾸지 않았습니다. 사용자 결정으로 명시적 인스턴싱 컴포넌트는 파편장 착수 때로 미루고, 행 예산 `kMaxDraws` 는 1024 에서
16384 로 올렸습니다(DEF-10 의 상한 설정화는 남음). 확인 중에 찾은 DEF-105(빌드마다 GLTF 캐시 무효화)를 등재했습니다.
항목별 내용은 문서 끝 "DEF-16 자동 인스턴싱 수정 결과" 절에 있습니다. 이어서 DEF-17 · DEF-18(패스 확장 지점)을 고정 지점 넷의
렌더 확장과 `EngineContext::render` 로 해결했습니다. 도중의 사용자 지시("에디터 실행은 잠시 미루자. 개선 점검은 추후 일괄로")에
따라 화면 확인은 일괄 점검으로 미뤘습니다. 문서 끝 "DEF-17 · 18 패스 확장 지점 수정 결과" 절을 보십시오. 이어서 DEF-22(직렬화 타입 등록)와 DEF-101 을 해결했습니다
(씬 v4 · 프리팹 v2, 문서 끝 "DEF-22 직렬화 타입 등록 수정 결과" 절). 이어서 DEF-33(라이트 선택)과 DEF-10(상한 설정)을 해결하고
DEF-34 · DEF-13 은 보류했습니다(문서 끝 "DEF-33 · 10 수정 결과" 절).

**[갱신 · 2026-10-14 · 일괄 점검]** 사용자 지시("실측을 위해 에디터 직접 열고 검증 진행하고 해당 과정에서 나온 문제점들 정확히
파악해서 내 판단 없이 스스로 작업 진행해")로 미뤄 둔 화면 점검을 한 번에 했습니다. DEF-16 · 17 · 18 · 22 · 33 · 10 · 101 의 확인
항목은 모두 통과했고, 점검 중에 찾은 DEF-106(최소화한 채 닫으면 멈춤) · DEF-107(얇은 상자 콜라이더로 Play 하면 충돌) · DEF-108
(Play → Stop 마다 회전 값이 흔들림) · DEF-109(컴포넌트 추가 버튼이 패널 밖으로 잘림)를 등재와 함께 고쳤습니다. 점검마다 다시 드러난
DEF-105(빌드마다 GLTF 캐시 무효화)도 고쳤습니다. DEF-34 는 PC 에서 측정해 보류를 유지했습니다(Quest 측정 뒤 결정). 문서 끝
"일괄 점검 결과와 신규 결함" 절을 보십시오.

| ID | 결함명 | 심각도 | 대상 파일 | 관련 라인 |
|---|---|---|---|---|
| **DEF-01** | Transform SSBO 인덱스 동기화 붕괴 | **해결됨** (2026-08-25, 원래 CRITICAL) | `src/renderer/Renderer.hpp`, `src/renderer/Renderer_render.cpp` | L215, L296, L313~326 |
| **DEF-02** | Zone C Ring Buffer 펜스 추적 부재 | **MEDIUM** (잠재) | `src/memory/HarukaAllocator.cpp` | L177~199 |
| **DEF-03** | `drawIndirectCount` 무조건 요구 및 폴백 부재 | **해결됨 · 2026-10-04** (원래 HIGH, 요구 제거. 장치 버전 전제는 DEF-97) | `src/renderer/passes/GeometryPass.cpp`, `src/renderer/Renderer.cpp` | L507, L586 |
| **DEF-04** | `vkCmdPipelineBarrier2` (Synch2) 무조건 호출 | **HIGH** | `src/renderer/RenderContext.cpp`, `src/renderer/passes/CullPass.cpp`, `src/renderer/Renderer.cpp` | L167, L166·L194, L586 |
| **DEF-05** | 스왑체인 리사이즈 시 GPU In-Flight 자원 파괴 | **해결됨** | `src/renderer/Renderer_swap.cpp` | L59~60 |
| **DEF-06** | MSAA `LAZILY_ALLOCATED` 메모리 속성 누락 | **HIGH** | `src/renderer/passes/GeometryPass.cpp` | L239 |
| **DEF-07** | `onActorDestroyed` 구독자 부재로 인한 댕글링 포인터 | **해결됨 · 2026-08-26** (원래 MEDIUM, A-1: EditorApp 구독 + 컴포넌트 `notifyDestroy`) | `src/core/World.hpp`, `src/core/World.cpp` | L27, L27 |
| **DEF-08** | 안드로이드 빌드 불가 (`GlfwWindow` 무조건 생성) | **HIGH** | `src/core/Engine.cpp`, `CMakeLists.txt` | `Engine::Engine`, CMake `if(ANDROID)` 분기 (예전 `main.cpp` L38) |
| **DEF-09** | 멀티뷰 절반 구현 (피처만 켜지고 경로 없음) | **해결됨 · 2026-10-04** (원래 HIGH, 첫 단계는 눈마다 두 번 그리기. 셰이더 · UBO 는 두 눈 기준이라 멀티뷰는 Quest 이식 때 뷰 번호만 바꿔 켬) | `src/renderer/Renderer_render.cpp`, `shaders/include/haruka_shared.h` | `drawFrame` 의 눈 루프 |
| **DEF-10** | `kMaxDraws` / `kMaxBodies` 이중 상한 | **해결됨 · 2026-10-14** (원래 MEDIUM, `Config/engine.ini` 의 render.maxDrawRows · render.maxLights · physics.maxBodies) | `src/core/EngineSettings.hpp`, `src/core/Engine.cpp` | `loadEngineLimits` |
| **DEF-11** | 셰이더 빌드 파이프라인 결함 (DEPENDS 누락·하드코딩·고아·죽은 파일) | **해결됨 · 2026-10-04** (원래 MEDIUM) | `CMakeLists.txt`, `shaders/include/` | L94~108 |
| **DEF-12** | `World::tick`이 에디터 Play 상태에서만 실행 | **MEDIUM** | `src/core/Engine.cpp` | `Engine::run` 의 Tick 블록 (예전 `main.cpp` L115~119) |
| **DEF-13** | `FrameUBO`에 `invViewProj` 부재 | **보류 · 2026-10-14** (LOW, 첫 사용처가 생길 때. 렌더 확장이 CPU 행렬을 받음) | `shaders/include/haruka_shared.h` | `FrameUBO` |
| **DEF-14** | `GpuTransform` 셰이더 4곳 중복 선언, 공유 include 부재 | **해결됨 · 2026-10-04** (원래 LOW, `haruka_shared.h`) | `shaders/pbr.vert`, `depth.vert`, `shadow.vert`, `triangle.vert` | L25, L18, L18, L19 |
| **DEF-15** | `prevViewProj` 선언만 존재, TAA 입력 미비 | **해결됨 · 2026-10-04** (원래 LOW, 가짜 TAA 잔재와 함께 제거) | `shaders/post.frag` 외 4개 셰이더 | L48~51 |
| **DEF-23** | 스피어 인덱스 도메인 불일치 (DEF-01의 직계 형제) | **해결됨** (2026-08-25, 원래 CRITICAL) | `shaders/cull.comp`, `src/renderer/passes/CullPass.cpp`, `src/renderer/Renderer.hpp`, `src/renderer/Renderer_render.cpp` | L54·L57 |
| **DEF-24** | 라이트 개수 이중 계산 | **해결됨 · 2026-08-25** (원래 HIGH) | `src/renderer/Renderer_render.cpp`, `src/renderer/Renderer.hpp`, `src/renderer/RenderTypes.hpp`, `shaders/pbr.frag` | 8-2절 |
| **DEF-33** | 라이트 선택 정책 부재 (생성 순서로 절단) | **해결됨 · 2026-10-14** (원래 MEDIUM, 화면 밖 거르기 + 상한 초과 시 방향광 우선 · 가까운 순) | `src/renderer/LightSelection.hpp`, `Renderer::updateLightSSBO` | 8-2-a절 |
| **DEF-34** | 타일드/클러스터드 라이팅 부재 (픽셀당 전체 순회) | **보류 · 2026-10-14** (MEDIUM, 실측 뒤. 하면 클러스터 방식) | `shaders/pbr.frag` | 8-2-b절 |
| **DEF-35** | `evalLight`의 0 나눗셈 가능성 | **해결됨 · 2026-10-04** (원래 LOW, 반경 최솟값 포함) | `shaders/pbr.frag` | 8-2-c절 |
| **DEF-25** | 깊이 프리패스와 컬러 서브패스의 가시 집합 불일치 (CPU는 AABB, GPU는 구) | **해결됨** (2026-08-25, 원래 HIGH) | `src/renderer/Renderer_render.cpp`, `src/renderer/Renderer.hpp` | `buildDrawCommands`의 `sphereInFrustum`, 증상은 `GeometryPass.cpp` L330·L88 |
| **DEF-26** | 섀도 캐스터 라이트 선택 불일치 | **해결됨 · 2026-08-27** (원래 MEDIUM) | `src/renderer/Renderer_render.cpp`, `shaders/pbr.frag` | 8-4절 |
| **DEF-27** | 섀도 패스가 카메라 프러스텀으로 걸러진 목록 사용 | **해결됨 · 2026-08-27** (원래 MEDIUM) | `src/renderer/Renderer_render.cpp` | 8-5절 |
| **DEF-28** | 머티리얼 null 항목의 드로우 강행 | **해결됨 · 2026-10-04** (원래 MEDIUM, 자홍색 오류 머티리얼) | `src/renderer/passes/GeometryPass.cpp` | L526, L552 |
| **DEF-29** | `VkDeviceCreateInfo`의 검증 레이어 설정 | **해결됨 · 2026-10-04** (원래 LOW) | `src/renderer/Renderer.cpp` | L603~604 |
| **DEF-30** | 투명 정렬 comparator의 매핑 메모리 읽기 | **해결됨 · 2026-10-04** (원래 LOW) | `src/renderer/Renderer_render.cpp` | 투명 정렬 comparator |
| **DEF-31** | 두 눈 합집합 절두체 부재 (단안 절두체로 스테레오를 컬링) | **해결됨 · 2026-10-04** (원래 HIGH, 눈마다 판정해 하나라도 통과하면 그림) | `src/renderer/FrustumCull.hpp`, `src/renderer/Renderer_render.cpp` | `sphereInAnyView`, `buildDrawCommands` |
| **DEF-32** | 재투영 여유분만큼의 절두체 부풀리기 부재 | **보류 · 2026-10-04** (원래 MEDIUM. 컬링 · 렌더 · 제출이 한 프레임의 같은 자세를 쓰는 동안 필요 없음. 늦은 자세 갱신 · 과주사를 넣을 때 재개) | `src/renderer/Renderer_render.cpp` | `buildDrawCommands` 주석 |
| **DEF-36** | 섀도 샘플러 비교 규약 반전 (모든 라이트 기여가 0이 됨) | **해결됨 · 2026-08-25** (원래 CRITICAL) | `src/renderer/passes/ShadowPass.cpp` | L37 (증상은 `shaders/pbr.frag` L165), 11-1절 |
| **DEF-37** | IBL 조도 추정기의 코사인 중복 적용 (저장값 약 2.09배 과다) | **해결됨 · 2026-08-25** (원래 HIGH) | `src/renderer/Renderer.cpp` | IBL 조도 큐브맵 생성 루프, 11-2절 |
| **DEF-38** | 라이트 종류와 방향이 직렬화되지 않음 | **해결됨 · 2026-08-27** (원래 HIGH, 라이팅 Phase 0) | `src/assets/SceneSerializer.cpp` | `LIGHT:` 기록 및 복원 경로 |
| **DEF-39** | `defaultCube` 탄젠트 handedness 반전 (여섯 면 전부) | **해결됨 · 2026-08-25** (원래 MEDIUM · 미검증 → 실측으로 성립 확인) | `src/assets/GltfLoader.cpp` | `defaultCube`, 11-4절 |
| **DEF-40** | 라인 오버레이 정점 버퍼의 GPU 인플라이트 레이스 | **해결됨 · 2026-10-02** (원래 MEDIUM, 라인·기즈모 삼각형 버퍼를 프레임 슬롯별로 분리) | `src/renderer/passes/GeometryPass.*` | 라인 오버레이 정점 버퍼 |
| **DEF-41** | 콜라이더가 `Transform.scale`을 반영하지 않음 | **해결됨 · 2026-08-25** (원래 MEDIUM · 절차 도형 선결 조건) | `src/physics/PhysicsSystem.cpp` | 바디 생성 지점, 11-6절 |
| **DEF-42** | 힙이 1GB 미만일 때 Zone 오프셋이 실제 할당 범위를 넘음 | **MEDIUM** (Quest 포팅 전 필수) | `src/memory/HarukaAllocator.cpp`, `HarukaAllocator.hpp` | init 경로, Zone 상수 |
| **DEF-43** | 세터 안의 `resetIfLive`로 인한 매 프레임 바디 재생성 | **해결됨 · 2026-10-02** (원래 LOW, 재생성을 편집 확정 훅으로 옮김) | `src/components/RigidBodyComponent.cpp` | `resetIfLive` |
| **DEF-44** | `RIGID:`의 enum 서수 직렬화 | **해결됨 · 2026-10-02** (원래 LOW, 이름으로 기록하고 옛 순번도 읽음) | `src/assets/SceneSerializer.cpp` | `RIGID:` 기록 경로 |
| **DEF-45** | `uploadMesh`의 핸들 누수 | **해결됨 · 2026-08-25** (원래 LOW) | `src/renderer/GpuScene.cpp` | `uploadMesh`, 11-10절 |
| **DEF-46** | 에디터 float 스텝 0.01 고정으로 1cm 미만 편집 불가 | **해결됨 · 2026-10-04** (원래 MEDIUM, 단위 종류 · UE 식 수정 키) | `src/core/Reflect.hpp` | `PropDesc::step` |
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
* 인스턴싱(DEF-16)을 도입할 때 `materialRows`가 드로우 커맨드당 한 행을 전제한다는 점을 다시 검토해야 합니다. **[2026-10-14 반영]** 행은 인스턴스마다 하나이고 묶음의 행은 모두 같은 머티리얼이라, 묶음 드로우는 첫 행(`firstInstance`)의 머티리얼을 씁니다. 행 배정은 `hbatch::build` 한 곳에 모았습니다.

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

## 3. [DEF-03] [해결됨 · 2026-10-04, 원래 HIGH] `drawIndirectCount` 무조건 요구 및 폴백 부재

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

## 7. [DEF-07] [해결됨 · 2026-08-26, 원래 MEDIUM] `onActorDestroyed` 구독자 부재로 인한 댕글링 포인터

> **해결(2026-08-26, Roadmap A-1):** `EditorApp::init` 이 `onActorDestroyed` 를 구독해 커맨드 스택을 걷어내고
> 선택 포인터를 비웁니다. 물리·오디오는 `notifyDestroy` → 컴포넌트 `onDestroy` 로 정리합니다. 아래 본문은
> 등재 당시의 분석입니다. 재진입 파괴와 주소 재사용(ABA) 한계는 `World::destroyImmediate` 주석에 남아 있습니다.

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

### 8-2-a. [DEF-33] [해결됨 · 2026-10-14, 원래 MEDIUM] 라이트 선택 정책 부재

> **해결**: 문서 끝 "DEF-33 · 10 수정 결과" 절. 착수 조건(상한에 걸릴 때)과 달리 지금 고친 이유는, 화면 밖 라이트를 거르는 일은
> 실측 없이도 맞고 틀림을 따질 수 있고 상한 아래에서도 픽셀 루프를 줄이기 때문입니다.

* **착수 조건**: 라이트 수가 상한에 실제로 걸릴 때입니다. 실측 없이 지금 만들 항목이 아닙니다.
* **메커니즘**: `updateLightSSBO`는 `World::getLights()`의 순서, 즉 액터 생성 순서대로 채우고 상한에서 자릅니다. 거리나 화면 기여도를 보지 않으므로, 상한에 걸리면 가까운 조명이 꺼지고 먼 조명이 켜집니다.
* **수정 가이드라인**: 카메라 거리와 화면 기여도로 비용을 매겨 상위 N개를 고르십시오. Carbon Engine `audio` 저장소의 `SoundPrioritization`이 오디오에서 같은 문제를 푸는 구조입니다.

---

### 8-2-b. [DEF-34] [보류 · 2026-10-14, MEDIUM] 타일드 또는 클러스터드 라이팅 부재

> **보류**: 착수 조건대로 실측 뒤(사용자 결정). 할 때는 클러스터 방식(CPU 배정)을 권합니다(물체별 목록은 인스턴싱과 맞지 않음).
>
> **PC 측정 · 2026-10-14**: 메인 패스 GPU 시간은 라이트 수에 비례해, 라이트 하나가 프레임마다 약 4µs 를 더합니다(RTX 3080,
> 뷰포트 1023x600 · 4x MSAA). 기본 상한 256 에서는 약 1ms 라 PC 에서는 병목이 아닙니다. Quest Pro 는 그리는 화소가 약 11배,
> GPU 가 20 ~ 25배 느려 같은 루프가 라이트 하나에 약 1ms 로 어림됩니다. 보이는 국소광이 열 개 안팎을 넘는 씬을 Quest 에 올릴 때
> 클러스터 배정이 필요해질 수 있어, Quest 이식 때 실측으로 정합니다. 수치는 문서 끝 "일괄 점검 결과와 신규 결함" 절.

* **착수 조건**: `pbr.frag`의 픽셀당 루프가 병목으로 측정될 때입니다.
* **메커니즘**: 픽셀마다 전체 라이트 배열을 순회합니다. 거리 컬링도 타일 분할도 없으므로 화면 구석의 점광원 하나가 모든 픽셀에서 평가됩니다.
* **수정 가이드라인**: 화면을 타일로 나누고 타일별 라이트 목록만 순회하면 개수 상한이 사실상 의미를 잃습니다. Trinity의 `Tr2LightManager`(`LightBuffer` + `LightIndexBuffer`)가 그 방식입니다. 렌더러 구조 변경이므로 VR 코어 완성보다 뒤입니다.

---

### 8-2-c. [DEF-35] [해결됨 · 2026-10-04, 원래 LOW] `evalLight`의 0 나눗셈 가능성

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

### 8-6. [DEF-28] [해결됨 · 2026-10-04, 원래 MEDIUM] 머티리얼 null 항목의 드로우 강행

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

### 8-7. [DEF-29] [해결됨 · 2026-10-04, 원래 LOW] `VkDeviceCreateInfo`의 검증 레이어 설정

#### 대상 파일 및 위치
* [`Renderer.cpp:L625-L626`](file:///e:/Haruka/src/renderer/Renderer.cpp#L625-L626)

#### 결함 메커니즘 (Root Cause)
* `Renderer.cpp:625-626`이 `VkDeviceCreateInfo`에 검증 레이어를 넣습니다. 이 필드는 Vulkan 1.0부터 무시되며, 검증 레이어가 `VUID-VkDeviceCreateInfo-enabledLayerCount-12384`로 경고합니다.
* 2026-08-25 시점에 stderr에 남아 있는 **유일한 검증 레이어 위반**입니다.

#### 수정 가이드라인 (담당 에이전트 지시사항)
* 두 줄을 삭제하면 끝납니다.
* 인스턴스 쪽(`Renderer.cpp:533-534`)의 레이어 설정은 올바르므로 건드리지 마십시오. 디바이스 레이어를 지운다고 검증이 꺼지지는 않습니다.

---

### 8-8. [DEF-30] [해결됨 · 2026-10-04, 원래 LOW] 투명 정렬 comparator의 매핑 메모리 읽기

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

> **해결 · 2026-10-04.** 눈마다 절두체를 뽑아 하나라도 통과하면 그립니다(`FrustumCull.hpp` 의 `sphereInAnyView`).
> GPU 컬링 코드는 이미 지워 `cull.comp` 를 함께 고칠 일이 없습니다. 검산은 `stereo_views` 테스트.

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
* (2026-10-04 정정) 예전 판은 Quest Pro 화면이 안쪽으로 기울어져(canted) 있다고 적었지만 사실이 아닙니다. HMD 기하 데이터베이스에서 Quest 2·Pro·3 의 두 눈 시야 회전은 0° 이고 시야가 비대칭일 뿐입니다(기울어진 화면은 Valve Index 등). 권장은 두 눈 절두체를 각각 판정해 하나라도 통과하면 그리는 방식입니다. 정확하고, 기울어진 기기에도 그대로 맞습니다.
* `cullViewProj` 하나를 넘기는 구조 대신 **컬링 평면 집합 자체를 넘기는 구조**로 바꾸는 편이 자연스럽습니다. 그러면 `CullPush`의 `mat4` 전제도 함께 정리됩니다.
* **DEF-25의 재발 방지 계약이 그대로 적용됩니다.** 합집합 절두체를 도입하면 CPU와 GPU 양쪽의 평면 입력이 동시에 바뀌므로, `Renderer_render.cpp`와 `shaders/cull.comp`를 반드시 같이 고쳐야 합니다.
* 멀티뷰 완성(DEF-09)과 같은 작업 단위로 묶는 것이 합리적입니다.

---

### 9-2. [DEF-32] [MEDIUM] 재투영 여유분만큼의 절두체 부풀리기 부재

> **보류 · 2026-10-04.** 엔진은 한 프레임에 눈 자세를 한 번만 받아(`xrLocateViews`) 컬링 · 렌더 · 제출에 함께 씁니다.
> 그동안은 런타임이 재투영해도 렌더한 자세 기준이라 여유분이 필요 없습니다(Godot 도 같음). 늦은 자세 갱신이나 과주사를
> 넣으면 그때 평면의 `d` 만이 아니라 법선도 바깥으로 돌려 부풀립니다.

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

### 11-3. [DEF-38] [해결됨 · 2026-08-27, 원래 HIGH] 라이트 종류와 방향이 직렬화되지 않음

> **해결(2026-08-27, 라이팅 Phase 0):** `LIGHT:` 줄이 타입·Attenuation Radius·Cone 각·방향을 기록하고 읽으며,
> 옛 파일은 기본값을 씁니다([Lighting_UE_Design.md](Lighting_UE_Design.md)). 아래 본문은 등재 당시의 분석입니다.

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

### 11-5. ✅ [DEF-40] [해결됨 · 2026-10-02] 라인 오버레이 정점 버퍼의 GPU 인플라이트 레이스 (원래 MEDIUM)

> **수정했습니다(2026-10-02).** 라인과 기즈모 삼각형 정점 버퍼를 `MAX_FRAMES` 개씩 만들고, 기록할 때 펜스로 보호되는
> 지금 슬롯(`frameIdx`)의 버퍼에만 씁니다. 실기 세션 전체에서 검증 레이어 메시지 0건이었습니다.

> **확인(2026-10-02):** 정점 버퍼가 하나(`GeometryPass::lineVertBuf_`)이고 기록 중에 `memcpy` 합니다. 렌더 루프는
> 현재 슬롯의 펜스만 기다리므로(`MAX_FRAMES = 2`) 직전 프레임이 아직 읽는 동안 덮어쓸 수 있습니다. 나중에 붙은
> 기즈모 삼각형 버퍼(`triVertBuf_`)도 같은 구조입니다. 둘 다 프레임별 버퍼로 나눕니다.

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

> **남은 한계(2026-10-02 해결: DEF-43 의 트랜스폼 확정 훅이 스케일 변경 때 바디를 다시 만듭니다)**: 바디는 생성 시점의 스케일로 만들어집니다. 에디터에서 기즈모로 스케일을 바꿔도 이미 만들어진 바디는 따라오지 않습니다. `RigidBodyComponent` 의 `resetIfLive` 가 `halfExtent` 세터에만 걸려 있기 때문이며, 스케일 변경을 여기에 연결하는 것은 DEF-43(커밋 훅)과 같은 작업 묶음입니다.

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

### 11-8. ✅ [DEF-43] [해결됨 · 2026-10-02] 세터 안의 `resetIfLive` 로 인한 매 프레임 바디 재생성 (원래 LOW)

> **수정했습니다(2026-10-02).** 셰이프·모션·Half·질량 세터는 값을 바꾸고 표시만 합니다. 바디 재생성은 새 확정 훅
> `Component::onPropsCommitted` 에서 한 번 합니다(숫자 칸 드래그 끝·입력 확정, 열거·토글 클릭, 그 편집의 undo·redo).
> 같은 묶음으로 `Component::onTransformCommitted` 를 두어(기즈모 드래그 끝, 디테일 TRANSFORM 확정, 그 undo·redo),
> 스케일이 바뀌었으면 바디를 다시 만들고 위치·회전만 바뀌었으면 바디를 그 자리로 옮깁니다. DEF-41 의 "남은 한계" 도
> 이것으로 풀렸습니다. 게임 코드가 필드를 직접 바꾸면 `RigidBodyComponent::rebuildBody()` 를 부릅니다.
> `physics_lifecycle` 테스트가 "드래그 10회 동안 재생성 0회, 확정 때 1회" 를 검산합니다.

> **확인(2026-10-02):** Shape · Motion · HalfX/Y/Z 세터가 조건 없이 `resetIfLive` 를 부르고, `SNumberField` 는
> 드래그 중 마우스가 움직일 때마다 세터를 부릅니다. 물리는 플레이 중에만 틱하므로 증상도 플레이 중에만
> 납니다. 재생성을 드래그 끝(`compEnd`)으로 옮기고, 기즈모 스케일 변경이 바디를 다시 만들게 하는 일(DEF-43
> 원래 묶음)과 함께 합니다. 반대 방향의 문제(Friction 등이 반영되지 않음)는 DEF-81 입니다.

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

### 11-9. ✅ [DEF-44] [해결됨 · 2026-10-02] `RIGID:` 의 enum 서수 직렬화 (원래 LOW)

> **수정했습니다(2026-10-02).** `RIGID:box static` 처럼 이름으로 기록합니다. 읽을 때는 이름과 옛 순번(0~2)을 모두
> 받고, 모르는 값은 오류 한 줄을 남기고 기본값(box · dynamic)으로 둡니다. 열거형에는 옛 순번과 같은 명시값을
> 박았습니다. 실기에서 `RIGID:0 0` 이 `box static` 으로 읽혀 이름으로 다시 저장되고, `RIGID:9 dynamic` 은 오류
> 한 줄과 함께 box 로 읽히는 것을 확인했습니다.

> **확인(2026-10-02):** 저장 파일에 `RIGID:0 0`, `RIGID:0 1` 처럼 셰이프·모션 순번이 기록되고, 열거형에
> 명시값이 없으며 읽을 때 범위 검사 없이 캐스트합니다. PRIM · AUDIO_3D 처럼 이름으로 쓰고 옛 숫자도 읽게 합니다.

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

### 11-11. [DEF-46] [해결됨 · 2026-10-04, 원래 MEDIUM] 에디터 float 스텝이 0.01로 고정되어 1cm 미만을 편집할 수 없음

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

---

## 커맨드 스택 수명 결함 (DEF-55, 2026-09-08)

| ID | 심각도 | 내용 | 상태 |
|---|---|---|---|
| DEF-55 | HIGH | 컴포넌트만 제거되면 프로퍼티 스냅샷 커맨드가 걷히지 않아 undo 가 해제된 메모리에 씀 | **해결 2026-09-08** (`purgeComponent` 경로 신설) |

### 재현

1. 라이트 액터를 선택하고 디테일 패널에서 Intensity 를 조작합니다 → `PropsSnapshotCmd` 가 스택에 쌓입니다.
2. 같은 패널에서 LightComponent 의 `x` 버튼으로 컴포넌트를 제거합니다.
3. Ctrl+Z → `PropsSnapshotCmd::undo()` 가 `pd.setF(...)` 를 호출하고, 그 람다가 캡처한
   컴포넌트 포인터는 이미 해제되어 있습니다.

### 원인

`PropDesc` 의 `getF`/`setF` 는 `Reflect.hpp` 의 `XPROP_*` 매크로가 만드는 클로저이고,
그 클로저는 **컴포넌트 원시 포인터를 캡처**합니다. 그래서 커맨드가 스택에 남아 있는 동안
대상 컴포넌트도 살아 있어야 한다는 불변식이 생깁니다.

이 불변식을 지키는 장치는 `HCommandStack::purge(const Actor*)` 하나였고, 그것을 부르는 곳은
`onActorDestroyed` 구독 한 곳뿐이었습니다. 그런데 **컴포넌트가 사라지는 경로는 둘**입니다.

| 경로 | 이벤트 | 기존 처리 |
|---|---|---|
| 액터 파괴 → `notifyDestroy` → 컴포넌트 동반 해제 | `onActorDestroyed` 발화 | `purge(Actor*)` 로 걷힘 |
| 디테일 패널의 컴포넌트 `x` → `Actor::removeComponent<T>()` | **발화 없음** | **걷히지 않음** |

두 번째 경로에서는 액터가 살아남으므로 이벤트가 발화하지 않고, `refersTo(actor)` 도 참이 될 수
없습니다. `PropsSnapshotCmd` 의 주석이 선언한 불변식이 앞쪽 경로만 막고 있었습니다.

### 수정

- `ICommand` 에 `refersToComponent(const Component*)` 를 추가하고(기본 false),
  `HCommandStack::purgeComponent(const Component*)` 로 undo·redo 양쪽을 걷어냅니다.
  **redo 스택도 함께 비워야 합니다** — undo 한 뒤 컴포넌트를 지우고 redo 해도 같은 참조가 남습니다.
- `PropsSnapshotCmd` 가 소유 액터와 **대상 컴포넌트를 함께** 보관하고 두 판정을 모두 구현합니다.
- 디테일 패널의 제거 콜백 5분기가 `removeComponent<T>()` **직전에** `purgeComponent` 를 부릅니다.
  대상은 `getComponent<T>()` 로 얻어, 실제로 제거되는 것(첫 매치)과 기준을 맞춥니다.

### 회귀 테스트

`tests/CommandStackTest.cpp` (`command_stack_purge`). 액터 파괴와 컴포넌트 제거가 각각 자기 몫만
걷어내는지, redo 스택에서도 걷히는지, 대상을 지정하지 않은 커맨드는 보존되는지를 검산합니다.
Vulkan·창·World 를 링크하지 않는 헤더 전용 테스트입니다.

### 남은 위험

이 수정은 **경로를 하나 더 막은 것이지 원인을 없앤 것이 아닙니다.** 값 접근자가 원시 포인터를
캡처하는 구조가 그대로이므로, 컴포넌트를 해제하거나 **이동시키는** 경로가 새로 생기면 같은 유형이
다시 나옵니다. 근본 해결은 접근자를 주소 캡처에서 `{타입, 필드}` 기술자로 바꾸는 것이고,
그 판단은 [ECS_Decision.md](ECS_Decision.md) 에 트리거와 함께 적어 두었습니다.

---

## UI · 뷰포트 잔여 결함 (DEF-56 ~ DEF-69, 2026-09-17 등재)

**처리 시점: 종합 오류 검출 작업에서 일괄 처리합니다(사용자 결정 2026-09-17).** 개별로 먼저
착수하지 않습니다.

출처는 상태바 착수 전에 수행한 HSlate 위젯 결함 조사(33건)입니다. 그중 21건은 2026-09-09 커밋
네 개로 고쳤고([Editor_Layout_Design.md](Editor_Layout_Design.md) §7.11), 아래는 미룬 12건에
**계획에는 있었으나 실제 수정에서 빠진 1건(DEF-62)** 과 조사 목록의 마지막 항목(DEF-69)을 더한
것입니다. 2026-09-17 에 현재 코드와 다시 대조해 전부 남아 있음을 확인했습니다. 줄 번호는 커밋마다
어긋나므로 파일과 함수 이름으로 적습니다.

| ID | 심각도 | 내용 | 위치 |
|---|---|---|---|
| DEF-56 | **해결 2026-10-03** · 원래 MEDIUM (클립 스택 + CPU 사각형 자르기, 도크 리프·스크롤 상자가 자름) | 클리핑 API 가 호출자 0인 죽은 코드입니다. 긴 목록과 문자열이 이웃 패널을 침범합니다 | `HPaintContext` 의 `pushClip`/`popClip`/`currentClip`, `HDrawElement` 에 클립 필드 없음 |
| DEF-57 | **해결 2026-10-03** · 원래 MEDIUM (컨테이너가 휠을 전달, 디테일·콘텐츠 브라우저·GPU Stats 스크롤 상자, 아웃라이너·Output Log 우회 제거. 캡처 중 휠은 2026-10-02) | 휠이 위젯에 도달하지 않습니다(어떤 컨테이너도 `onScroll` 을 전달하지 않음). 포인터 캡처 중의 휠은 `onMouseMove` 로 잘못 라우팅됩니다 | `HSlateRenderer` 입력 분기, 컨테이너 위젯 전부 |
| DEF-58 | **해결 2026-10-03** · 원래 MEDIUM (누름·OS 반복 횟수 `keyPressCount`, 텍스트 칸 백스페이스) | 키 리피트가 없어 백스페이스를 누르고 있어도 한 글자만 지워집니다(모든 텍스트 필드) | `GlfwWindow::keyCallback` 이 `GLFW_REPEAT` 를 즉시 버림, 키는 폴링 |
| DEF-59 | **해결 2026-10-02** (크기가 같으면 오프셋만 저장) | `setViewportRect` 가 오프셋만 바뀌어도 씬 렌더 타깃을 전량 재생성합니다. 스플리터 드래그 중 매 프레임 `vkDeviceWaitIdle` 과 TAA 히스토리 소실이 일어납니다 | `Renderer_swap.cpp` 의 비교식이 오프셋을 포함. 오프셋 소비처는 `PostPass` 하나 |
| DEF-60 | **해결 2026-10-03** · 원래 MEDIUM (그리기와 같은 advance 로 실측, 툴바 사다리를 측정으로) | `STextBlock` 의 폭 계산(`0.6 × fontSize` 근사)이 실제 글리프 advance 와 다릅니다 | `STextBlock::computeDesiredSize` vs `HFontAtlas::measureText` |
| DEF-61 | MEDIUM | Output Log 의 표시 줄 수가 도킹 이후 갱신되지 않는 시드 상수 `bottomH_` 로 계산됩니다 | **해결 2026-09-17** (로그 L3: 행 영역 실측 높이로 행 수 결정) |
| DEF-62 | **해결 2026-10-02** (1패스에서 Fill 슬롯 패딩도 고정 소비로 셈) | Fill 슬롯의 패딩을 1패스는 합산하지 않고 2패스는 더해, 마지막 슬롯이 컨테이너 밖으로 밀려납니다. Fill 슬롯에 패딩을 준 곳에서만 드러납니다 | `SVerticalBox`·`SHorizontalBox` 의 `onArrangeChildren` |
| DEF-63 | **해결 2026-10-02** (두 위젯에 프레스 추적, 헤더 누름은 body 로 안 감) | `SCollapseSection`·`STabBar` 가 프레스 추적 없이 마우스-업으로 발화합니다. `SCollapseSection` 헤더의 마우스-다운은 body 로 새어 들어갑니다 | 두 위젯의 `onMouseButton` |
| DEF-64 | **해결 2026-10-02** (우회 경로만 고침. 바깥 첫 클릭으로 닫기는 사용자 결정으로 의도된 동작) | 메뉴 팝업 캐처가 메뉴 바 행을 제외한 전체를 덮어, 메뉴가 열린 동안 하단 크롬의 첫 클릭이 닫기에만 쓰입니다. 의도된 동작으로 볼지 먼저 판단해야 합니다 | `EditorApp::rebuildMenuPopup` 의 캐처 |
| DEF-65 | **해결 2026-10-02** (`HReply::merge` 로 응답을 합침) | `onMouseMove` 경로에서 자식이 돌려준 `HReply`(캡처·해제 요청)가 버려집니다 | `SOverlay`·`SBorder`(반환값 무시), `SHorizontalBox`·`SVerticalBox`(마지막 Handled 만 보존) |
| DEF-66 | **해결 2026-10-02** (`IWindow::isCtrlDown`·`isShiftDown`, 좌우 구분 없음) | 오른쪽 Ctrl 로는 복사·붙여넣기·전체 선택·실행 취소·저장·찾기가 동작하지 않습니다 | `EditorApp` 이 `KEY_LEFT_CTRL` 만 확인, `IWindow.hpp` 에 오른쪽 상수 없음 |
| DEF-67 | **해결 2026-10-02** (텍스트 입력 중에는 게임 입력이 모든 키를 뗀 것으로 봄) | 게임 입력계가 텍스트 포커스를 무시해, 타이핑이 바인딩된 게임 액션을 함께 발화시킵니다. 현재 바인딩이 0건이라 증상은 없습니다 | `HInputSystem::update`(포커스 게이트 없음), `Engine::run` 이 매 프레임 호출 |
| DEF-68 | **해결 2026-10-02** (같은 카운터로 셈, 라벨 `Target recreate/s`) | `recreateRenderTargets` 가 계측되지 않아, 뷰포트 통계의 `Recreate/s` 가 뷰포트 크기 변경에 의한 재생성을 세지 않습니다 | `Renderer_swap.cpp`(`recreateSwapChain` 만 카운트) |
| DEF-69 | **해결 2026-10-03** · 원래 LOW (Window 메뉴 + 닫을 수 있는 탭의 x) | Window 메뉴가 없고, `PanelDef::closable`·`PanelRegistry::order()` 를 읽는 곳이 0입니다 | `dock/DockTree.hpp`, `EditorApp::buildMenus` |

### 처리할 때 지켜야 할 순서 제약

(2026-10-03: 아래 제약을 모두 지켜 처리했습니다. 처리 내용은 문서 끝 "기반 공사 수정 결과" 절.)

- **DEF-60 은 뷰포트 툴바 폭 사다리와 함께 고칩니다.** 사다리 임계값(640/575/520/435/345)과 승격
  임계값이 이 근사 위에서 튜닝되어 있어, 폭 계산만 실측으로 바꾸면 툴바가 엉뚱한 단계에서 접힙니다
  (§7.9).
- **DEF-69 는 탭 닫기 버튼보다 먼저 합니다.** 순서가 바뀌면 닫은 패널을 되살릴 수단이 없습니다.
- **DEF-56(클리핑)과 DEF-57(휠 라우팅)은 스크롤 목록 전반의 선행 조건입니다.** 지금 아웃라이너는
  `EditorApp::tick` 의 단일 휠 판독 지점에서 직접 히트테스트하는 우회로 스크롤합니다. 두 결함을 고치면
  이 우회를 위젯 경로로 되돌릴지 함께 결정합니다. **Output Log 도 같은 우회를 씁니다**(로그 L4, 2026-09-25).
  되돌릴 때는 두 곳을 함께 옮겨야 휠이 한쪽으로만 가는 일이 없습니다.
- **DEF-61 은 로그 설계의 L3(행 풀)에서 해소했습니다(2026-09-17)**([Log_Category_Design.md](Log_Category_Design.md)).
- **DEF-64 를 고칠 때는 로그 Categories 팝업의 방식을 참고하십시오.** 그 팝업은 전면 캐처 없이, 프레스
  순간에 바깥이면 닫기만 하고 클릭을 흘려보냅니다(2026-09-17, 설계 문서 §7.2).

### 디테일 패널 클릭 결함 (DEF-73 ~ DEF-75) — 닫음: 자동 조작 환경의 산물 (2026-10-02)

사운드 기초 작업을 실기 확인하다가 관찰해 등재했고, 같은 날 종합 오류 검출에서 **엔진 결함이 아님**을
확인해 닫았습니다. 근거는 두 가지입니다.

1. **화면 오른쪽을 Claude 데스크톱 앱 창이 덮고 있었습니다.** 그 지점의 창을 `WindowFromPoint` 로 물으면
   `claude`(Chrome_WidgetWin_1, topmost 레이어드 창)가 나오고, 범위는 물리 좌표 (1372,20)~(1908,748)
   이었습니다. 이 창은 자동 조작 스크린샷에서는 가려져 보이지 않지만 입력은 받습니다. 최대화한 에디터의
   디테일 패널이 바로 그 범위에 있었습니다. 에디터 창을 그 범위 밖(0,0,1368,1032)으로 옮기자 숫자 칸 클릭이
   정상으로 입력 모드에 들어갔습니다(누름·뗌이 위젯에 처리되고 `focusedField` 가 잡힘).
2. **그 범위로 커서를 옮겨도 에디터는 이동 이벤트를 받지 못했습니다.** 엔진은 커서 위치를 OS 에 묻지 않고
   이동 콜백이 받아 둔 값(`GlfwWindow::lastMouseX`)을 씁니다. 그래서 다음 클릭이 직전 위치에 떨어졌습니다.
   진단 로그에서 "받아 둔 위치 (55,246) / 실제 위치 (1741,208)" 가 찍혔고, 그 클릭이 아웃라이너 같은 행을
   다시 눌러 선택을 풀었습니다(DEF-74 의 정체). DEF-75 의 "Ground 선택 + 카메라 이동" 도 같은 구조로,
   직전 위치(뷰포트)에 떨어진 클릭이 픽과 팬이 된 것입니다.

실제 마우스는 언제나 이동 이벤트를 만들므로 손으로는 재현되지 않습니다(체크리스트 2-A2 에서 사람이 확인).
처음에 적었던 DEF-74 의 원인 설명("이름 칸 `SButton` 이 떼는 위치를 재검사하지 않음")은 틀렸습니다.
`SButton` 은 2026-09-09 부터 누른 곳과 뗀 곳이 모두 자기 위일 때만 발화합니다. `addOutlinerRow` 의 그 주석이
낡은 것입니다(DEF-88 정리 대상과 함께 고침). 자동 조작 쪽 요령은 [Editor_QA_Checklist.md](Editor_QA_Checklist.md)
머리말에 적었습니다.

(2026-10-03 해소: 디테일 패널이 휠로 스크롤됩니다.) 함께 적어 둘 한계: 디테일 패널은 스크롤이 없어(DEF-57) 내용이 패널보다 길면 아래가 잘립니다. 메시와
오디오를 함께 가진 액터는 기본 레이아웃에서 오디오 컴포넌트 행이 보이지 않으므로, 지금은 하단 패널
분할선을 내려 디테일 패널을 늘려야 합니다.

### 2026-10-02 재확인 — DEF-56 ~ DEF-69 남은 13건

코드 대조로 13건 모두 남아 있음을 다시 확인했고, 그중 실기로 드러나는 것은 기준선을 남겼습니다
([Editor_QA_Checklist.md](Editor_QA_Checklist.md) 5절). 판단이 필요한 점만 적습니다.

- **DEF-59:** 기본 레이아웃에서는 분할선을 끌 때 뷰포트 크기도 함께 바뀌므로, 매 프레임 재생성은 대부분
  정당합니다. 오프셋만 바뀌는 프레임은 위치와 크기를 따로 내림하는 데서 우연히 생깁니다. TAA 가 꺼져 있어
  "히스토리 소실" 은 지금 보이지 않습니다. 고칠 것은 "크기가 같으면 오프셋만 저장하고 반환" 한 줄입니다.
- **DEF-64:** (사용자 결정 2026-10-02: 권고대로 의도된 동작으로 두고 우회 경로만 고쳤습니다.) 메뉴가 열린 동안
  바깥 첫 클릭을 닫기에만 쓰는 것은 데스크톱 메뉴의 표준이라 **의도로 두기를 권합니다**(클릭을 흘려보내면 뷰포트 픽이 선택을 지웁니다). 진짜 결함은 메뉴보다 먼저 도는 히트테스트입니다.
  탭·스플리터·기즈모·RMB 카메라 판정이 열린 드롭다운을 무시해서, File > Load Scene 행 왼쪽 끝(Outliner 탭과
  겹치는 곳)을 누르면 탭 누름이 클릭을 가져가 메뉴가 열린 채로 남습니다(실기 재현).
- **DEF-66:** 오른쪽 Shift 도 같은 이유로 무시됩니다.
- **DEF-62 · DEF-65 · DEF-67:** 지금 화면에서 드러나는 곳이 없습니다(코드로만 확인).

---

## 종합 오류 검출 신규 결함 (DEF-76 ~ DEF-94, 2026-10-02 등재 · DEF-94 는 10-03)

**처리 시점: 종합 오류 검출 작업(이번 묶음).** 정적 검출(코드 대조 세 갈래)과 실기 기준선 확인에서
나왔습니다. "실기" 칸은 2026-10-02 세션의 결과이고, 재현 절차는 [Editor_QA_Checklist.md](Editor_QA_Checklist.md)
2-C 절에 있습니다.

| ID | 심각도 | 내용 | 위치 | 실기 |
|---|---|---|---|---|
| DEF-76 | **해결 2026-10-02** · 원래 HIGH | 포인터 캡처가 raw 포인터라, 캡처된 위젯이 리빌드로 해제되면 다음 이동 이벤트가 해제된 위젯을 호출합니다(UAF). 디테일 숫자 칸을 누른 채 Delete, 또는 플레이 중 게임 코드가 선택 액터를 지우면 도달합니다 | `HSlateRenderer::capturedWidget_`, `handlePointerEvent`, 삭제 경로 `EditorApp.cpp` 의 `delEdge` | 생략(크래시 위험, ASan 빌드 권장) |
| DEF-77 | **해결 2026-10-02** · 원래 HIGH | 숫자 칸이 자기 위에서 눌린 적 없는 마우스-업에도 입력 모드에 들어가고, 낡은 시작값(새 칸이면 0)으로 버퍼를 채웁니다. 어디서 확정하든 그 값이 들어가 원래 값이 사라지고 undo 로도 돌아오지 않습니다 | `SNumberField::onMouseButton`(프레스 추적 없음) | 재현: 제목 줄에서 눌러 Location Y 위에서 떼자 `0.000|`, 확정 후 -0.1 → 0, Ctrl+Z 무효 |
| DEF-78 | **해결 2026-10-02** · 원래 MEDIUM | 휠을 아웃라이너·Output Log 밖에서 굴리면 위치와 상관없이 뷰포트 카메라 돌리로 갑니다 | `EditorApp::tick` 휠 라우팅, `tickEditorCamera`(뷰포트 판정 없음) | 재현: 디테일 위 휠 3칸 → 카메라 전진 |
| DEF-79 | **해결 2026-10-02** · 원래 MEDIUM | 왼쪽 버튼을 누른 채 움직인 양이 어디서든 쌓였다가 다음 뷰포트 오빗에서 한꺼번에 적용돼 카메라가 튑니다 | `GlfwWindow::cursorPosCallback`(누적), `tickEditorCamera`(오빗일 때만 소비) | 재현: UI 위 250px 드래그 → 뷰포트 클릭 시 카메라 크게 이동 |
| DEF-80 | **해결 2026-10-02** · 원래 MEDIUM | 플레이 중 비활성화한 액터의 물리 바디가 남아 시뮬레이션·충돌하고 위치를 되씁니다(A-2 에서 미룬 항목). 고칠 때는 지지하던 바디가 사라지면 그 위에서 잠든 바디를 깨워야 합니다 | `PhysicsSystem::tick`, `RigidBodyComponent`(onDisable 없음) | 재현: 바닥 Static 바디를 끄자 그 위 상자가 3초간 Y 0.58 그대로 |
| DEF-81 | **해결 2026-10-02** · 원래 LOW | 플레이 중 Friction · Restitution · Mass 를 바꿔도 이미 만든 바디에 반영되지 않습니다(Shape · Half 는 반대로 매 프레임 재생성, DEF-43) | `RigidBodyComponent::buildProps` | 코드 확인 |
| DEF-82 | **해결 2026-10-02** · 원래 LOW | 버튼 상태를 프레임마다 폴링해서, 한 프레임 안에 끝난 누름·뗌(터치패드 탭, 낮은 FPS)이 사라집니다. 키도 같습니다 | `Engine::run`(폴링), sticky 모드 미사용 | 재현: 빠른 클릭의 누름·뗌이 같은 시각에 들어와 UI 디스패치 없음 |
| DEF-83 | **해결 2026-10-02** · 원래 LOW | 아웃라이너 검색창에 포커스가 있을 때 행을 한 번 누르면 선택되지 않습니다. 누름이 포커스를 풀며 같은 프레임에 아웃라이너를 다시 만들어, 방금 눌린 행 버튼이 사라집니다 | `handleInput` 첫머리의 포커스 해제 → `flushOutlinerRebuild` | 재현 |
| DEF-84 | **해결 2026-10-02** · 원래 LOW | 입력보다 먼저 같은 프레임에 리빌드된 위젯은 배치 전이라 크기가 0 이고, 그 프레임의 클릭을 받지 못합니다(구조). 게임이 매 프레임 액터를 만들면 아웃라이너가 계속 이 상태가 됩니다 | `EditorApp::tick` 리빌드 → `handleInput`, 배치는 `record` 에서만 | 코드 확인 |
| DEF-85 | **해결 2026-10-02** · 원래 LOW | 로그 6곳의 문자열이 한글이라 ASCII 전용 Output Log 에서 물음표로 보입니다 | `AssetManager.cpp:110`, `Actor.cpp:64`, `PrimitiveBuilder.cpp:623`, `PhysicsSystem.cpp:184`, `GpuScene.cpp:77`·`:104` | 코드 확인 |
| DEF-86 | **해결 2026-10-02** · 원래 LOW · 잠재 | IBL 반정밀도 변환이 지수 범위를 처리하지 않고 형 변환으로 aliasing 위반입니다. 지금 하늘 값이 0.05 이상이라 드러나지 않습니다 | `Renderer::generateIBLCubemaps` 의 `genHalfFloat` | 코드 확인 |
| DEF-87 | **해결 2026-10-02** · 원래 LOW | 콘텐츠 브라우저 항목이 가운데 정렬로 보입니다(버튼 기본 정렬 Center, 아웃라이너는 Fill 로 고침) | `EditorApp::rebuildBottomPanel` | 재현 |
| DEF-88 | **해결 2026-10-02** · 원래 LOW | 회전 없는 액터의 ROTATION Y 가 `-0.000` 으로 보입니다. 같은 정리 묶음: `addOutlinerRow` 의 낡은 주석 | `syncEulerCache` · `SNumberField` 표시 | 재현 |
| DEF-89 | **해결 2026-10-02** · 원래 LOW | 빌드 경고 C4505: `uploadCubemapMip` 는 쓰이지 않는 함수입니다(업로드는 영역 배열로 직접 함) | `Renderer.cpp:306` | 전체 재빌드에서 경고는 이것 하나 |
| DEF-90 | **해결 2026-10-02** · 원래 LOW | 디테일 TRANSFORM 을 접어도 선택을 바꾸면 다시 펼쳐집니다(`expanded(true)` 고정) | `EditorApp::rebuildDetails` | 재현 |
| DEF-91 | LOW · 잠재 | Slate·휠 우회는 창 좌표를, `inViewport` 는 프레임버퍼로 바꾼 좌표를 씁니다. Windows 에서는 배율과 상관없이 두 좌표가 같아 드러나지 않고(GLFW Win32 에서 창 크기 = 프레임버퍼 크기), macOS Retina · Wayland 에서만 드러납니다 | `handleInput`, `tick` 휠 판정, `mouseToFb` | 코드 확인 |
| DEF-92 | **해결 2026-10-02** · 원래 HIGH | 디테일 패널이 대상 액터가 파괴된 뒤에도 한 프레임 더 그 액터와 컴포넌트를 읽습니다(UAF 읽기). 값 접근자가 raw 포인터를 붙잡는데, 패널 리빌드는 다음 tick 의 선택 변경 감지에서야 일어나고 그사이 페인트·디스패치가 돕니다. Stop · Load · 아웃라이너 x 로 선택 액터를 지우거나 게임 코드가 지우면 도달합니다. DEF-76 을 고치다 찾았습니다 | `EditorApp` 의 `onActorDestroyed` 구독(디테일 리빌드 요청 없음), `rebuildDetails` 의 접근자 | 코드 확인(실기에서 Stop · Load · 삭제 뒤 크래시 없음은 확인) |
| DEF-93 | **해결 2026-10-02** · 원래 MEDIUM | UI 패스의 정점 버퍼가 하나라, 렌더 루프가 지금 슬롯의 펜스만 기다리는 동안 직전 프레임의 UI 패스가 아직 읽는 정점을 덮어씁니다(DEF-40 과 같은 유형). 수정 뒤 독립 검토에서 찾았습니다 | `HSlateRenderer` 의 `vertexBuf_` · `vertexPtr_`, `record` 가 `frameIdx` 를 버림 | 코드 확인 |
| DEF-94 | **해결 2026-10-03** · 원래 LOW | 탭 하나를 다른 리프로 끌어내면, 그 탭이 활성 탭보다 앞에 있었을 때 활성 탭이 다른 패널로 바뀝니다. 재도킹이 탭을 뺀 뒤 활성 인덱스를 범위로만 맞췄기 때문입니다. 기반 공사(DEF-69)에서 탭 빼기를 공용으로 만들다가 찾았습니다 | `EditorApp::performRedock` 1단계 | 코드 확인 |

**코드로 성립을 확인한 기존 "미검증" 항목:** DEF-40(라인·기즈모 정점 버퍼 하나를 두 프레임이 공유,
렌더 루프는 현재 슬롯 펜스만 기다림), DEF-43(플레이 중 Shape · Half 드래그가 매 프레임 바디 재생성),
DEF-44(`RIGID:0 1` 처럼 순번 기록, 실기 저장으로 확인). 위 매트릭스와 각 절의 표기를 함께 고쳤습니다.

### 수정 결과 (2026-10-02)

"확인" 칸의 `slate_input` · `physics_lifecycle` 은 이번에 만든 단위 테스트입니다. 두 테스트 모두 고치기 전
코드에서 해당 검사가 실패하는 것을 먼저 확인했습니다. 실기는 [Editor_QA_Checklist.md](Editor_QA_Checklist.md)
5절 "수정 후" 표에 있습니다.

| ID | 수정 | 확인 |
|---|---|---|
| DEF-76 | 캡처한 위젯을 `std::weak_ptr` 로 붙잡습니다. 위젯이 사라졌으면 그 누름의 뗌은 아무 데도 보내지 않고, 이동·휠은 보통 경로로 보냅니다. 캡처는 버튼보다 오래 살지 않습니다(버튼이 떨어진 이동에서 풂) | `slate_input`, 실기(숫자 칸을 누른 채 Delete → 크래시 없음) |
| DEF-77 | 숫자 칸에 프레스 추적을 넣었습니다. 입력 버퍼는 지금 값으로 채우고, 아무것도 입력하지 않았으면 확정해도 값을 다시 쓰지 않습니다(예전에는 들어갔다 나오기만 해도 소수 셋째 자리로 잘림). 판정은 글자 비교가 아니라 입력 여부(`edited_`)입니다. 플레이 중 값이 바뀐 칸에 처음과 같은 글자를 쳐도 그 값이 들어갑니다. 입력 중 같은 칸을 다시 눌러도 치던 글자를 지킵니다 | `slate_input`, 실기(기준선 재현 절차에서 입력 모드 진입 없음) |
| DEF-92 | 디테일이 보여 주는 액터(`detailsActor_`)를 선택 포인터와 따로 기억합니다. 파괴 구독이 리빌드를 요청하고, `tick` · `handleInput` 첫머리에서 디스패치·페인트보다 먼저 소진합니다 | 실기(Stop · Load · 삭제 뒤 정상) |
| DEF-78 | 남은 휠은 `inViewport` 일 때만 카메라 돌리로 씁니다 | 실기 |
| DEF-79 | 좌버튼 이동량을 오빗이 아닐 때도 매 프레임 비웁니다 | 실기(240px 드래그 뒤 뷰포트 클릭, 카메라 그대로) |
| DEF-80 | `RigidBodyComponent::onDisable` 이 바디를 지우고, `PhysicsSystem::removeBody` 가 지운 영역에 닿는 바디를 깨웁니다(`ActivateBodiesInAABox`). 트랜스폼 확정으로 바디를 옮길 때(`teleport`)도 옮기기 전후 영역의 바디를 깨웁니다 | `physics_lifecycle`(깨우기를 빼면 실패함을 확인), 실기(바닥을 끄자 그 위의 구가 떨어짐) |
| DEF-81 | 마찰·반발은 살아 있는 바디에 바로 씁니다(`setMaterial`). 질량은 확정 때 재생성으로 반영합니다. 재생성은 바디를 만들 때 쓴 값(`created*`)과 지금 값이 다를 때만 하므로, 마찰만 바꾼 편집의 undo 나 제자리로 돌린 드래그는 바디를 다시 만들지 않습니다 | `physics_lifecycle` |
| DEF-82 | `GlfwWindow` 가 누름 이벤트를 걸쇠에 걸고 다음 `pollEvents` 에서 비웁니다. 키·버튼 상태는 "지금 눌림 또는 직전 폴링 이후 눌린 적 있음" 입니다 | 실기(바로 떼는 클릭·키가 동작. 자동 조작의 `left_click` · `key` 도 이제 통함) |
| DEF-83 | 아웃라이너 목록 위에서 누른 동안에는 목록 리빌드를 미뤄 눌린 행 버튼이 뗄 때까지 살아 있게 했습니다. 검색 포커스 전환은 목록을 다시 만들지 않습니다(표시는 `Attr`) | 실기 |
| DEF-84 | `handleInput` 첫머리에서 배치를 한 번 돌립니다(`HSlateRenderer::arrange`) | 코드 확인 |
| DEF-85 | 로그 6곳과 메시 검증 보고 문구(디버그 빌드에서 로그로 나감)를 영어로 바꿨습니다 | 실기(구 콜라이더 경고가 영어로 보임) |
| DEF-86 | `glm::packHalf1x16` 로 바꿨습니다. 음수·NaN 은 0, 범위를 넘는 값은 65504 | 코드 확인 |
| DEF-87 | 콘텐츠 브라우저 항목 버튼을 `hAlign(Fill)` 로 | 실기 |
| DEF-88 | 반올림하면 0 이 되는 음수를 0 으로 표시합니다(`SNumberField::formatValue`). `addOutlinerRow` 의 낡은 주석도 고쳤습니다 | `slate_input`, 실기 |
| DEF-89 | 쓰이지 않던 `uploadCubemapMip` 를 지웠습니다 | 전체 재빌드 경고 0 |
| DEF-90 | TRANSFORM 펼침 상태를 에디터가 기억합니다(`SCollapseSection::onToggle`) | 실기 |
| DEF-93 | UI 정점 버퍼를 프레임 슬롯마다 두고 `frameIdx` 로 고릅니다. 슬롯 수는 `static_assert` 로 `MAX_FRAMES` 와 묶었습니다 | 실기(UI 정상, 검증 메시지 0) |

**행동 변화로 알아 둘 것.** (1) 한 프레임 안에 눌렀다 뗀 키·버튼도 그 프레임에는 눌린 것으로 보입니다(DEF-82).
(2) 플레이 중 기즈모·디테일로 액터를 옮기면 물리 바디도 그 자리로 옮겨지고, 스케일을 바꾸면 바디를 다시 만듭니다
(DEF-43 의 확정 훅). (3) 뷰포트 통계의 `Swapchain recreate/s` 는 `Target recreate/s` 로 바뀌었고 뷰포트 크기 변경에
의한 재생성도 셉니다(DEF-68). (4) 메뉴가 열린 동안에는 뷰포트 픽·기즈모·RMB 카메라·휠이 동작하지 않습니다(DEF-64).

**남은 것.** 기반 공사 묶음 DEF-56(클리핑) · DEF-57(위젯 휠 경로) · DEF-58(키 반복) · DEF-60(글자 폭) · DEF-69(Window 메뉴)는
사용자 결정으로 따로 진행합니다(2026-10-03 처리, 아래 "기반 공사 수정 결과" 절). DEF-91 은 Windows 에서 GLFW 창 좌표와 프레임버퍼 좌표가 같아 드러나지 않으므로, 좌표계가
갈리는 플랫폼(macOS 등)으로 갈 때 입력 좌표를 프레임버퍼 하나로 통일하면서 함께 고칩니다.

---

### 기반 공사 수정 결과 (2026-10-03)

"확인" 칸의 `slate_input` 은 2026-10-02 에 만든 단위 테스트에 더한 검사입니다(46 → 79개, 검토 반영 포함). 실기는
[Editor_QA_Checklist.md](Editor_QA_Checklist.md) 5절 "2026-10-03 기반 공사 수정 후" 표에 있습니다.

| ID | 수정 | 확인 |
|---|---|---|
| DEF-56 | `HPaintContext` 에 실제 클립 스택을 두었습니다. 안쪽 클립은 바깥 클립과의 교집합이고, 그리는 요소마다 그때의 클립을 받아 둡니다. `HSlateRenderer::flush` 가 글리프까지 펼친 뒤 사각형마다 CPU 에서 잘라 냅니다(UV 도 같은 비율로 줄여 잘린 글자가 찌그러지지 않음). 요소가 모두 축 정렬 사각형이라 정확하고, 시저처럼 클립이 바뀔 때마다 배치를 끊지 않아도 됩니다. 자르는 곳은 도크 리프(탭 줄과 패널 내용)와 새 위젯 `SScrollBox`, 그리고 `SBorder::clipChildren(true)` 를 켠 곳입니다. 자르는 위젯은 영역 밖의 이동을 자식에게 "바깥" 좌표(`HOutsideEvent`)로 보내, 잘려 안 보이는 자리에서 호버가 켜지지 않습니다 | `slate_input`(클립 교집합 · 요소 클립 · UV 자르기 · 바깥 이동), 실기(디테일 · GPU Stats · 뷰포트 통계가 패널 경계에서 잘림) |
| DEF-57 | 컨테이너(`SOverlay` · `SVerticalBox` · `SHorizontalBox` · `SBox` · `SBorder` · `SCollapseSection`)가 휠을 커서 아래 자식에게 전달합니다. 누름과 같은 규칙으로, 오버레이는 맨 위 슬롯부터 보내고 받은 곳에서 멈춥니다. `SBorder::onWheel` 은 자식이 받지 않은 휠의 처리기입니다. 에디터는 휠을 한 번 읽어 RMB 비행 속도를 먼저 처리하고, 나머지를 배치 뒤 위젯 경로로 보내며, 위젯이 받지 않은 휠만 뷰포트 돌리로 씁니다. 디테일과 하단 패널(Content Browser · GPU Stats)은 스크롤 상자에 담았고, 아웃라이너와 Output Log 는 보이는 행만 만드는 목록이라 `onWheel` 로 시작 행을 옮깁니다(예전 `tick` 의 직접 히트테스트 우회는 둘 다 지움). Output Log 탭은 스크롤 상자에 넣지 않습니다. 행 수를 행 영역의 실측 높이로 정하는데, 스크롤 상자 안에서는 그 높이가 내용을 따라 늘어나 행 수가 함께 불어나기 때문입니다. 열린 메뉴의 캐처와 Categories 팝업은 휠을 받아 두어 아래 패널이 굴러가지 않습니다 | `slate_input`(휠 경로 · 겹친 슬롯 · 스크롤 상자 · 안쪽 상자 우선), 실기 |
| DEF-58 | `GlfwWindow` 가 키마다 직전 `pollEvents` 이후의 누름과 OS 자동 반복(`GLFW_REPEAT`) 횟수를 셉니다(`IWindow::keyPressCount`). 텍스트 칸(리네임 · 검색 · 로그 검색 · 숫자 칸)의 백스페이스는 그 횟수만큼 지웁니다. 반복 지연과 속도는 OS 설정을 따릅니다. 포커스에 들어올 때 이미 눌려 있던 백스페이스는 뗄 때까지 무시합니다(엣지로 판정하던 때와 같은 규칙). 키 상태(걸쇠)와 키 로그에는 반복을 넣지 않고, Delete · Enter 처럼 반복하면 안 되는 키는 계속 엣지로 판정합니다 | 실기(누름 1 + 반복 5 → 6글자). 합성 `hold_key` 는 OS 반복을 만들지 않아 `PostMessage` 로 확인 |
| DEF-60 | `HFontAtlas::measureText` 가 그리기와 같은 글자별 advance 를 더합니다(그리기가 쓰지 않는 커닝은 뺌, 여러 줄이면 가장 긴 줄). 폰트 파일 읽기(`loadFont`)를 GPU 초기화와 떼어, 폰트를 읽은 아틀라스가 레이아웃의 글자 폭 기준(`HFontAtlas::metrics()`)으로 등록됩니다. `STextBlock` 은 그 기준으로 원하는 폭과 정렬을 잡고, 같은 문자열이면 다시 재지 않습니다. 폰트가 없을 때(폰트 없이 도는 테스트)만 예전 근사를 씁니다. 뷰포트 툴바 폭 사다리는 손으로 계산한 임계값 표를 없애고, 단계마다 라벨이 가장 길어지는 값으로 툴바를 세워 재서 요구 폭을 얻습니다(`measureVpToolbarLevels`, 배율·폰트가 바뀌면 다시 잼). 줄일 때 20px, 넓힐 때 40px 여유를 두고 넓히기에 실패하면 한 칸만 물러나는 규칙은 그대로입니다. 메뉴 바 버튼은 글자에 맞는 폭(최소 58px)을 씁니다 | `slate_input`(측정 = advance 합 · 여러 줄 · 원하는 폭 · 오른쪽 정렬), 실기(사다리 4단계 전환과 복귀) |
| DEF-69 | 메뉴 바에 Window 를 두었습니다. 등록된 패널을 등록 순서로 보이고, 도크 트리에 있는 패널에는 체크 표시가 붙습니다. 누르면 열린 패널은 그 탭을 앞으로 가져오고, 닫힌 패널은 다시 엽니다. 닫을 수 있는 패널(`PanelDef::closable`)의 탭에는 x 가 있고, 버튼처럼 누른 x 위에서 뗄 때만 닫습니다. 다시 열 때는 닫을 때 같은 리프에 있던 패널이 열려 있으면 그 리프에 탭으로 돌아가고, 아니면 기본 배치의 자리에 가깝게 붙습니다(하단 패널은 도크 전체 아래, 아웃라이너는 뷰포트 왼쪽, 나머지는 뷰포트 오른쪽). 닫은 패널은 레이아웃 파일에서 빠지고, 위젯은 레지스트리에 남아 다시 열면 상태가 그대로입니다. 도크가 바뀌어 화면에서 사라진 패널의 텍스트 포커스와 Categories 팝업은 정리합니다 | 실기(세 패널 닫기 · 다시 열기, 체크 표시, x 를 누른 채 밖에서 떼면 그대로) |
| DEF-94 | 탭 빼기를 공용 함수(`dockRemoveTab`)로 모으고, 뺀 탭이 활성 탭보다 앞이면 활성 인덱스를 하나 당깁니다. 재도킹의 가장자리 분할도 다시 열기와 같은 함수(`dockAttachSplit`)를 씁니다(재도킹은 몫 0.5 로 예전과 같음) | 코드 확인 |

**행동 변화로 알아 둘 것.** (1) 패널 내용이 패널 밖으로 그려지지 않습니다. 예전에 넘쳐서 보이던 부분(좁은 디테일의
ADD COMPONENT 버튼 등)은 이제 잘리고, 디테일은 휠로 스크롤해 봅니다. (2) 디테일 · Content Browser · GPU Stats 위의 휠은
그 패널을 스크롤합니다. 넘치지 않으면 아무 일도 일어나지 않습니다(카메라로 가지 않는 것은 2026-10-02 와 같음).
(3) 메뉴가 열린 동안에는 휠도 아래 패널로 가지 않습니다. (4) 텍스트 칸에서 백스페이스를 누르고 있으면 계속 지워집니다.
(5) 뷰포트 툴바가 예전보다 좁은 폭에서도 전체 라벨을 보입니다(예전 근사가 글자를 넓게 잡아 일찍 접었음).
(6) Outliner · Details · Content Drawer 탭에 x 가 생겼습니다. 닫은 패널은 Window 메뉴로 되살립니다.

**독립 검토 반영.** 수정 뒤 코드 재검토에서 나온 것을 모두 확인하고 고쳤습니다. 크래시나 해제된 메모리 접근은 없었고,
모두 "잘리거나 숨은 UI 를 원래 자리로 판정하는" 유형이었습니다. 에디터가 위젯 밖에서 직접 하는 판정은 자르는 조상의 영역도
함께 봐야 한다는 것이 교훈입니다.

| 지적 | 수정 | 확인 |
|---|---|---|
| 좁은 리프에서 잘려 안 보이는 탭이 클릭을 받음. 왼쪽 리프의 숨은 탭이 오른쪽 리프의 보이는 탭보다 먼저 잡혀, 숨은 x 가 패널을 닫음 | `DockTabHit` 에 탭 줄 영역(`row`)을 두고, 누름과 x 의 뗌 모두 그 영역 안일 때만 받음 | 실기(시험 레이아웃에서 숨은 Details x 자리를 눌러도 닫히지 않고, 보이는 부분은 정상) |
| 스크롤로 가려진 숫자 칸이 포커스를 놓지 않음. 그 자리(아래 패널)를 눌러도 편집이 끝나지 않아, 나중의 Enter 가 낡은 값을 확정하거나 리빌드가 친 값을 버림 | `focusWidgetContains` 가 숫자 칸은 디테일 스크롤 상자 영역, 로그 검색은 하단 패널 영역도 함께 봄. Categories 버튼 판정도 같음 | 실기(가려진 B 칸 자리의 Output Log 를 누르자 편집 확정, 포커스 해제) |
| Outliner 를 닫은 채 Ctrl+F · F2 를 누르면 보이지 않는 검색창·리네임으로 포커스가 들어감 | 아웃라이너가 보이지 않으면 먼저 `showPanel` 로 꺼낸 뒤 포커스 | 실기(닫은 뒤 Ctrl+F → 닫기 전 리프에 탭으로 돌아오고 검색 동작) |
| 백스페이스를 누른 채 숫자 칸을 클릭하면 OS 반복으로 바로 지워짐(숫자 칸은 `setTextFocus` 를 거치지 않아 막는 장치가 걸리지 않음) | `tickRenameInput` 이 포커스가 바뀐 프레임을 직접 잡아, 그때 눌려 있던 백스페이스를 뗄 때까지 막음 | 실기(누른 채 진입 → 반복 5번 무시, 뗀 뒤 새 누름 → 한 글자) |
| (가능성) 폰트를 읽은 아틀라스가 정리 없이 사라지면 글자 폭 기준 포인터가 남음 | `HFontAtlas` 소멸자가 등록을 풀고, 복사를 막음 | `slate_input` |
| (사소) 스크롤 막대가 내용의 오른쪽 끝(컴포넌트 삭제 x)을 덮음 | 넘칠 때 막대 자리를 비우고 그만큼 좁게 배치 | `slate_input` |
| (사소) 루트가 이미 세로 분할일 때 하단 패널을 다시 열면 세로 분할이 두 겹이 됨 | 같은 방향 분할이면 감싸지 않고 맨 앞·뒤 자식으로 끼움 | 코드 확인 |

**남은 것.** DEF-91(잠재)은 그대로입니다. 스크롤 막대는 위치 표시만 하고 끌어서 스크롤하지는 못합니다. 아웃라이너 ·
Output Log 의 휠은 예전처럼 한 프레임에 3줄씩 셉니다(휠 값의 크기를 쓰지 않음). 실제 키보드의 자동 반복과 실제
터치패드 휠은 사람이 확인할 항목입니다.

## 로그 경로 결함 (DEF-70 ~ DEF-72, 2026-09-17 등재)

로그 카테고리 설계를 위한 조사에서 확인했습니다. 셋 다 설계의 L1 단계에서 구조적으로 해소되므로 따로
고치지 않습니다. 설계와 근거는 [Log_Category_Design.md](Log_Category_Design.md) §2 입니다.

| ID | 심각도 | 내용 | 상태 |
|---|---|---|---|
| DEF-70 | LOW | 로그가 한 줄 들어올 때마다 하단 패널 전체를 리빌드합니다. **Content Browser·GPU Stats 탭이 활성이어도** 리빌드되어, 그 사이에 누른 Content Browser 버튼의 프레스 상태가 사라지고 클릭이 삼켜집니다. 매번 200줄 전체 복사도 따라옵니다 | **해결 2026-09-17** (L1 활성 탭 검사 → L3 행 풀로 리빌드 제거) |
| DEF-71 | LOW | 표준 출력 가로채기를 `EditorApp::init` 에서 설치해, 그 전에 나온 부팅 로그(GPU 이름·MSAA·힙 존·BRDF LUT·IBL·오디오·물리 초기화)가 Output Log 에 들어오지 않습니다 | **해결 2026-09-17** (L1 가로채기를 `main` 으로 이동) |
| DEF-72 | LOW | 심각도를 로그 문자열의 부분 일치(`ERROR`/`WARN`/`FATAL`)로 판정합니다. 71개 호출부 중 이 단어를 포함하는 것은 2곳뿐이라 **오류 로그가 사실상 전부 회색으로 표시됩니다**. Vulkan 검증 콜백은 심각도 플래그를 인자로 받으면서도 버리고 있습니다 | **해결 2026-09-17** (L1 심각도 필드로 판정, L3 상태바 색 `Attr` 화) |

**키 입력 로그**는 결함이 아니라 소음원이므로 번호를 부여하지 않았습니다. 누를 때와 뗄 때 한 줄씩
쌓여 200줄 링을 밀어냈고, 줄마다 DEF-70 의 리빌드를 일으켰습니다. 2026-09-17 에
`GlfwWindow::keyCallback` 의 스위치(`kLogKeyEvents = false`)로 껐고, L2 에서 `Input` 카테고리의
Verbose 로 옮깁니다.

## 1단계 정리 묶음 수정 결과와 신규 결함 (2026-10-04)

사용자 결정("추천대로 진행하되, 그 전에 다른 엔진들은 해당 문제를 어떻게 해결했는지 조사를 선행하고 알아낸 방법이
적용가능한지 확인한 뒤에 보고")에 따라, 남은 결함마다 UE · Unity · Godot · Filament · Bevy 등의 해법을 조사하고 Haruka
코드에 맞는지 확인했습니다. 1단계 정리 묶음은 그 결과대로 고쳤습니다. UE 소스는 EULA 대상이라 방식만 참고했습니다.

**수정 결과**

| 결함 | 다른 엔진의 방법 | 수정 | 확인 |
|---|---|---|---|
| DEF-03 | Godot · Granite 는 장치 버전에 맞춰 기능을 조회하고 지원하는 것만 켭니다 | 쓰는 곳이 없는 `drawIndirectCount` 요구를 지웠습니다 | 빌드, 실기 |
| GPU 컬링 잔재 | Quest 용 자료(Meta · Qualcomm)는 GPU 주도 컬링을 요구하지 않습니다 | `CullPass` · `cull.comp` · 컬링 버퍼 · 구 목록 · `cullViewProj`(매 프레임 매핑 메모리 되읽기)를 지웠습니다. 다시 필요하면 스테레오 · 인스턴스 기준으로 새로 설계합니다 | 화면 픽셀 동일 |
| DEF-10 (진단) | Godot 의 Jolt 모듈은 바디 생성 실패를 한 번 알리고 늘릴 설정 이름을 보여 줍니다 | 행 예산을 넘긴 메시를 `Dropped` 통계로 따로 세고 한 번 경고합니다(예전에는 `Culled` 에 섞임). 바디 생성 실패는 `createFailed_` 로 표시해 매 틱 재시도를 멈추고 한 번 경고합니다. `PhysicsSystem::Update` 의 오류 값을 확인합니다. 상한 설정화는 남았습니다 | 실기(통계 `Dropped: 0`), 코드 확인 |
| DEF-11 | CMake 3.21 부터 Visual Studio 생성기도 depfile 을 지원합니다 | `glslc -MD -MF` 와 `DEPFILE` 로 include 를 추적합니다(최소 CMake 3.21). 목록 밖 셰이더 파일은 구성 단계에서 경고합니다. 셰이더 명령이 바뀐 `.spv` 를 실행 폴더로 바로 복사해 셰이더 타깃만 빌드해도 핫 리로드됩니다. 쓰지 않던 `triangle.*` · `noise.glsl` · `sdf.glsl` 은 지웠습니다(휴지통) | 공용 헤더의 시각만 바꾸자 그것을 include 하는 셰이더 5개만 다시 컴파일 |
| DEF-14 | NVIDIA nvpro 샘플의 C++/GLSL 공용 헤더 방식 | `shaders/include/haruka_shared.h` 를 C++ 와 GLSL 이 함께 include 합니다. `RenderTypes.hpp` 가 크기 · 오프셋을 `static_assert` 로 확인합니다. 헤더에는 vec4 · ivec4 · mat4 만 둡니다 | 화면 픽셀 동일 |
| DEF-15 | Godot · Bevy 는 효과를 켰을 때만 히스토리를 만듭니다. Quest 의 프레임 합성(AppSW)은 눈별 이전 행렬 · 물체별 이전 모델 행렬 · 모션 벡터를 요구합니다 | 히스토리 이미지 · 블릿 · TAA 분기 · 타임스탬프 칸 · "TAA blit" 통계 줄 · `prevViewProj` 를 지웠습니다. 모양이 달라 자리는 남기지 않았습니다 | 화면 픽셀 동일 |
| DEF-28 | UE 는 기본 머티리얼, Unity 는 자홍색 오류 셰이더로 그립니다 | 머티리얼이 없는 메시를 자홍색 오류 머티리얼(알베도 · 이미시브)로 그리고 액터마다 한 번 경고합니다(사용자 결정). `World::getVisibleMeshes` 는 머티리얼로 거르지 않습니다. 그래도 null 항목이 오면 세 서브패스 모두 건너뜁니다(컬러에서만 빼면 깊이만 남아 뒤를 가림) | 실기(시험 빌드에서 Ground 머티리얼을 비우자 자홍색 + 경고 1회) |
| DEF-29 | 현재 Vulkan 사양은 장치 레이어 값을 0 으로 두도록 정합니다 | 장치 레이어 설정을 지웠습니다 | 검증 메시지 0 |
| DEF-30 | UE · Unity · Godot 모두 물체마다 거리 키를 한 번 계산합니다. AMD · VMA 문서는 이런 메모리를 CPU 로 읽지 말라고 합니다 | 행을 배정할 때 거리 제곱을 함께 저장해 정렬합니다(같은 거리면 행 번호 순). 키가 같아 순서는 예전과 같습니다 | 코드 확인 |
| DEF-35 | UE · Unity · Godot · Filament 모두 반경을 0 보다 크게 강제하고 거리에 최솟값을 둡니다. "0 = 무한" 은 Filament 에만 있습니다 | 거리 0 보호(`inversesqrt(max(d², ε))`)와 반경 최솟값 0.01m(UI · 씬 불러오기 · 업로드). 감쇠 공식은 같은 날 실제 점광원과 같은 역제곱으로 바꿨습니다(아래 "화면 기준 재정렬") | `HarukaActorTest` 4개 검사(옛 setter 에서 2개 실패 확인) |
| DEF-98 (신규) | UE 는 바뀐 셰이더를 다시 컴파일해 적용하고 실패하면 재시도합니다 | 다시 읽기 전에 SPIR-V 크기 · 매직 넘버를 확인합니다. 다섯 패스 모두 새 파이프라인을 만든 뒤에만 옛 것을 지워, 실패하면 이전 것으로 계속 그립니다. 감시는 변경이 300ms 멈춘 뒤 알리고, 버퍼 넘침도 변경으로 칩니다 | 실기(덜 쓴 파일 → 건너뜀 경고 · 에디터 유지, 되돌리면 정상 리로드, 셰이더 타깃만 빌드해도 반영) |

사실과 다른 표기도 고쳤습니다: 메모리 로그의 "UMA"(ReBAR 를 켠 독립 GPU 도 같은 메모리 타입이 있음), Zone C 용량 주석
(512MB → 64MB), `kMaxDraws` 설명, 코드 주석과 문서 4곳의 canted 주장.

**신규 결함 (조사에서 발견)**

| ID | 심각도 · 상태 | 내용 | 위치 |
|---|---|---|---|
| DEF-95 | MEDIUM · **해결 2026-10-04** | 카메라 투영의 깊이 범위가 Vulkan 의 [0,1] 이 아니라 [-1,1] 입니다. `Reflect.hpp` 가 `GLM_FORCE_DEPTH_ZERO_TO_ONE` 없이 GLM 을 먼저 포함해, 뒤에 정의한 매크로가 효과가 없습니다. 실제 근평면이 0.1m 가 아니라 약 0.2m 이고, 그림자 투영([0,1])과 규약이 섞여 있으며, SSAO 선형 깊이 계산은 [0,1] 을 가정합니다 | `CameraComponent.cpp` 의 `glm::perspective`, `Reflect.hpp`, `linDepth.frag` |
| DEF-96 | MEDIUM · **해결 2026-10-04** | 감마가 두 번 적용됩니다. `post.frag` 가 `pow(1/2.2)` 를 한 값을 sRGB 스왑체인에 써서 GPU 가 한 번 더 변환합니다. 화면이 의도보다 밝고 흐리며, 노출은 이 상태에 맞춰져 있습니다. UI 도 같은 스왑체인에 그리므로 고치는 방법에 따라 UI 색도 바뀝니다 | `post.frag`, `GlfwSwapchain::chooseFormat` |
| DEF-97 | HIGH · 열림 (Quest 이식) | 엔진이 Vulkan 1.4 장치를 전제합니다. 1.3 · 1.4 기능 구조체를 조건 없이 연결하고, 1.4 코어 이름 `vkCmdPushDescriptorSet` 를 6곳에서 직접 부릅니다. Quest 2 · Pro 는 1.1, Quest 3 은 1.3 입니다(push descriptor · synchronization2 는 확장으로 있음). DEF-04 와 함께 처리합니다 | `Renderer::createLogicalDevice`, 6개 호출부 |
| DEF-98 | LOW · **해결 2026-10-04** | 셰이더 핫 리로드가 덜 복사된 .spv 로 파이프라인을 만들다 예외를 잡지 못해 에디터가 종료될 수 있었습니다(옛 파이프라인을 먼저 지움) | `HotReloader`, `Renderer::reloadPipelines`, 각 패스 `rebuildPipelines` |
| DEF-99 | MEDIUM · **해결 2026-10-04** (VR) | Kinematic 바디 동기화가 스텝 루프 뒤에 한 번 돌아 한 프레임 늦고, 한 프레임에 스텝이 여럿이면 목표를 지나칩니다. VR 손과 잡은 물체에서 드러납니다 | `PhysicsSystem::tick` |
| DEF-100 | LOW · **해결 2026-10-04** | `AOPass::destroy` 가 Zone C 할당을 `freeB` 로 돌려줘, Zone B 빈 목록에 960MB 이상 범위가 들어갑니다. 지금은 종료할 때만 일어납니다 | `AOPass.cpp`, `HarukaAllocator::freeB`(구역 확인 없음) |
| DEF-101 | LOW · **해결 2026-10-14** (DEF-22) | `StaticMeshComponent::transparent` 는 디테일에서 바꿀 수 있지만 저장되지 않고, 불러올 때 에셋 값으로 다시 정해집니다 | `SceneSerializer`, DEF-22 와 함께 |
| DEF-102 | LOW · **해결 2026-10-04** (DEF-21) | 씬 카메라의 yaw · pitch 가 디테일에 없고 저장되지 않아, 씬 카메라는 방향을 정할 수 없고 저장하면 방향을 잃습니다 | `CameraComponent`, DEF-21 과 함께 |
| DEF-103 | LOW · 열림 (Quest 이식) | `AndroidWindow` 가 `IWindow` 의 순수 가상 클립보드 함수 두 개를 구현하지 않아 추상 클래스입니다 | `AndroidWindow.hpp`, DEF-08 과 함께 |

**문서 정정**
- Quest 2 · Pro · 3 의 두 눈 시야는 기울어져(canted) 있지 않습니다(HMD 기하 데이터베이스의 시야 회전 0°). 9-1절,
  Core_Stabilization_Spec.md, Layer_Architecture.md, VR_Transition_Masterplan.md 의 같은 주장을 고쳤습니다.
- DEF-32 는 컬링 · 렌더 · 제출이 한 프레임의 같은 자세를 쓰면 필요 없습니다. 늦은 자세 갱신(late latching)이나 과주사
  (overscan)를 넣을 때만 필요하고, 그때는 `d` 만이 아니라 평면 법선을 바깥으로 돌려야 합니다.
- DEF-02 는 문서보다 위험합니다. 지금 Zone C 는 시작할 때만 할당하는 상주 영역처럼 쓰여, 매 프레임 할당하는 코드가
  생기면 첫 랩어라운드가 상주 버퍼를 덮습니다.
- DEF-06 의 손해는 대역폭이 아니라 메모리 점유입니다(렌더 패스는 이미 CLEAR/DONT_CARE 와 인라인 resolve).
- DEF-91 은 Windows 에서는 배율과 상관없이 드러나지 않습니다(GLFW Win32 에서 창 크기 = 프레임버퍼 크기). macOS
  Retina · Wayland 에서 드러납니다.
- DEF-20 의 해결 방향은 `IWindow` 가 아니라 별도 XR 모듈입니다(UE · Unity · Godot 모두 그렇습니다). OpenXR 이 Vulkan
  장치를 고르므로 XR 시스템은 렌더러보다 먼저 만들어야 합니다.

**검증.** 수정 전 빌드를 두 번 찍어 뷰포트가 픽셀까지 같음(결정적)을 확인한 뒤, 수정 후 빌드와 비교해 차이 0 픽셀입니다.
검증 레이어 메시지 0, 엔진 Error 0, `Clean exit`. 빌드 경고 0, 단위 테스트 7개 통과(`HarukaActorTest` 에 반경 검사 4개 추가).
실기 절차는 [Editor_QA_Checklist.md](Editor_QA_Checklist.md) 5절 "2026-10-04 1단계 정리 묶음".

### 화면 기준 재정렬 (2026-10-04)

사용자 결정: 감마(DEF-96)와 깊이 규약(DEF-95)을 고치고, 감쇠 공식은 "현실은 어떤지 알아보고 거기에 맞춰".

| 항목 | 수정 | 확인 |
|---|---|---|
| DEF-95 깊이 규약 | `GLM_FORCE_DEPTH_ZERO_TO_ONE` 를 소스 12곳의 개별 `#define` 대신 CMake `add_compile_definitions` 한 곳에서 정의합니다. 엔진·테스트 대상에만 붙고 외부 라이브러리(GLFW)에는 붙지 않도록 외부 라이브러리를 다 불러온 뒤에 둡니다. 카메라 투영이 [0,1] 이 되어 근평면이 설계값 0.1m 로 돌아오고, SSAO 선형 깊이 공식과 맞습니다. 피킹 · 기즈모 광선은 깊이 0 · 1 의 두 점으로 방향만 구해 영향이 없습니다 | 빌드 산출물에서 정의 범위 확인(엔진 · `HarukaActorTest` 있음, GLFW 없음), 실기 |
| DEF-96 감마 | `post.frag` 의 `pow(1/2.2)` 를 지웠습니다. sRGB 스왑체인은 GPU 가 저장할 때 선형 → sRGB 로 바꿉니다. 스왑체인이 UNORM 으로 떨어진 기기에서만 셰이더가 sRGB 곡선(IEC 61966-2-1)을 적용합니다(`encodeSrgb` 푸시 상수). UI 는 같은 스왑체인에 그리지만 바꾸지 않아 지금 색 그대로입니다 | 실기 |
| 감쇠 공식 | 실제 점광원은 역제곱 법칙을 따릅니다(같은 빛이 넓이 4πd² 인 구면에 퍼지므로 거리가 두 배면 1/4). 광원 크기의 5배보다 가까우면 어긋나므로 1cm 안쪽은 1cm 로 봅니다(Frostbite · Filament 값). 반경에서 0 이 되도록 창 함수 (1 - (d/r)⁴)² 를 곱합니다(UE · Frostbite · Filament). 세기는 상대값 그대로이고 intensity 는 1m 거리의 밝기입니다. 역제곱에서는 라이트 바로 앞의 매끈한 면이 16비트 HDR 최대값을 넘을 수 있어 최종 색을 60000 에서 자릅니다. 예전 `(1 - d/r)²` 은 Unity 가 "Legacy" 라고 부르는 반경 기준 2차 감쇠와 같은 계열이었습니다 | 실기(실행 폴더의 씬 사본에만 바닥 25cm 위 점광원을 넣어 확인: 밝은 원이 거리에 따라 부드럽게 어두워짐, 깨진 픽셀 없음, 검증 메시지 0. 사본은 원본으로 되돌림) |

**화면 변화.** 기준 화면과 비교해 뷰포트 전 픽셀이 바뀌었습니다(감마 때문). 그늘진 바닥의 평균 코드값이 122 에서 47 로,
먼 바닥은 159 에서 88 로 내려가 화면이 어두워지고 대비가 커졌습니다. 구 아래쪽에 보이던 SSAO 점무늬는 깊이 규약을 고친
뒤 줄었습니다. 노출(0.4)은 예전 화면(감마 두 번)을 보며 "흰 평면이 코드값 169" 가 되도록 맞춘 값입니다. 계산하면 노출을
약 1.0 으로 올리면 같은 목표로 돌아갑니다. **사용자 결정으로 노출을 1.0 으로 올렸습니다.** 해가 비치는 먼 바닥은
예전 화면 159 · 노출 0.4 일 때 88 · 노출 1.0 에서 152 로 돌아왔고, 그늘은 122 · 47 · 96 이라 예전보다 대비가 큽니다.

## 2단계 숫자 칸 수정 결과 (DEF-46, 2026-10-04)

사용자 결정: 수정 키는 UE 식, 끄는 동안 커서는 고정하지 않음. UE 의 숫자 칸(SSpinBox)에 Shift · Ctrl · Ctrl+Alt 배율이
있다는 것은 API 문서로 확인했지만 기본값은 문서에 없어, Shift 10배(빠르게) · Ctrl 0.1배(정밀)로 정했습니다(Unity 도
Shift 가 빠르게 4배). 해법은 [Primitive_And_Lighting_Design.md](Primitive_And_Lighting_Design.md) 7-7절의 단위 표시 계획과
같은 방향입니다.

| 문제 | 수정 | 확인 |
|---|---|---|
| 끌기 감도가 값 크기와 상관없이 1cm/px | `PropDesc::unit`(`PropUnit`)과 `SNumberField::unit`(`ENumberUnit`)을 두었습니다. 거리 1cm/px, 크기는 끌기 시작값의 1%/px(최소 step 의 1/10), 각도 0.5°/px, 비율 0.005/px, 일반은 step 그대로. 트랜스폼은 위치 = 거리, 회전 = 각도, 배율 = 크기. 컴포넌트는 라이트 세기 · 반경 = 크기, 색 = 비율, 원뿔 각 = 각도, 물리 마찰 · 반발 = 비율, 질량 · 반 크기 = 크기, 사운드 볼륨 = 비율, 거리 = 크기 | `slate_input`(10 에서 10px → 11, 0 에서도 멈추지 않음) |
| 수정 키 없음 | `HPointerEvent` 에 `shift` · `ctrl` 을 싣고 `EditorApp::handleInput` 이 채웁니다. Shift 10배, Ctrl 0.1배 | `slate_input` |
| 끄는 도중 수정 키를 누르면 값이 튐 | 끈 거리 전체에 스텝을 곱하던 것을 이동마다 변화량만 지금 값에 더하는 방식으로 바꿨습니다. 세터가 범위로 자른 뒤 되돌아올 때 헛도는 구간도 없습니다 | `slate_input`(제자리 Shift 에 값 그대로, 옛 공식에서 6개 실패) |
| 데드존을 넘는 순간 4px 만큼 한꺼번에 변함 | 데드존(3px)을 지난 지점부터 셉니다 | `slate_input` |
| 1mm 미만이 0.000 으로 보임 | 끝의 0 을 지우는 고정 소수점(소수 한 자리는 남김, 유효 숫자 7자리, 지수 표기 없음). 거리 · 크기 다섯째, 일반 · 비율 넷째, 각도 둘째 자리. 반올림하면 0 인 음수는 0.0(DEF-88 유지). 입력 모드 버퍼는 자릿수를 넉넉히(각도 3, 나머지 6) | `slate_input`, 실기(Ground 선택 시 Location Y -0.1 · Scale 20.0 / 0.2 / 20.0) |
| 아주 작은 값의 편집이 undo 에 남지 않음 | 변화 판정을 절대값 1e-6 에서 값 크기에 비례한 기준으로 바꿨습니다 | `slate_input`(0.0000005 입력이 기록됨, 옛 기준에서 실패) |

검증: `slate_input` 79 → 97개 검사(표시 규칙이 바뀐 기존 3개의 기대값도 새 규칙으로 고침). 네 가지 핵심(수정 키 · 이동마다
누적 · 크기 비례 감도 · undo 기준)을 하나씩 옛 식으로 되돌리면 새 검사가 실패함을 확인했습니다(2 · 6 · 2 · 1개). 빌드 경고 0,
테스트 7개 통과, 실기 검증 메시지 0 · `Clean exit`. 실제 키보드의 Shift · Ctrl 끌기 감각은 사람이 확인할 항목입니다.

## 3단계 VR 첫걸음 수정 결과 (2026-10-04)

**사용자 결정.** 검증 런타임은 Quest Link + 헤드셋(코드를 다 쓴 뒤 사용자가 준비), 물리 규칙은 UE 식, 물리 스텝은 XR 세션
동안 헤드셋 주사율. 조사 보고의 권장안(두 층 배열 스왑체인, 기본 액션 세트, 부모 아래 바디 UE 식, DEF-32 보류, FFR 멤버
삭제)은 그대로 따랐습니다. 이 PC 에는 OpenXR 런타임(Meta Quest Link · SteamVR)이 없습니다. Meta XR Simulator 는 Vulkan
지원이 "제한적"이라고 공식 문서에 적혀 있어 쓰지 않았습니다. 작업 도중 사용자 지시("창 띄우지마")로 에디터 창을 띄우는 확인은
멈췄고, 그 뒤 변경은 단위 테스트와 빌드로 확인했습니다.

| 항목 | 다른 엔진의 방법 | 수정 | 확인 |
|---|---|---|---|
| DEF-21 카메라 자세 | UE 는 카메라가 HMD 자세를 따르고, Unity · Godot 은 원점 액터 아래에 카메라를 둡니다. 셋 다 뷰는 컴포넌트의 월드 트랜스폼에서 만듭니다 | `CameraComponent::viewMatrix` 를 액터의 월드 회전(쿼터니언) · 월드 위치의 역행렬로 바꿨습니다(크기는 넣지 않음). yaw · pitch 필드는 지우고 에디터(`EditorApp`)만 갖습니다. 눈별 비대칭 투영(`projFromFov`)과 XR 원점(`xrOrigin`: 카메라 위치 + 요만)을 두었습니다. 오디오 청취자도 카메라의 위 벡터를 넘겨 롤이 소리에 반영됩니다 | `actor_lifecycle` 7개(옛 yaw/pitch 와 같은 뷰 · 부모 합성 · 롤 · 크기 무시, 옛 식에서 2개 실패), `audio_system` 1개(롤, 옛 식에서 실패) |
| DEF-102 | | 씬 카메라의 방향이 액터 회전이 되어 디테일의 Rotation 으로 정하고 `ROT:` 로 저장됩니다(DEF-21 로 해결). 옛 씬 파일의 카메라는 늘 pitch 0.15 였으나 저장소의 씬에는 카메라 액터가 없습니다 | 코드 확인 |
| DEF-09 두 눈 렌더 | Khronos 예제는 눈마다 따로 그리고, Unity 도 PC 에서 두 번 그리는 방식을 둡니다. Godot 은 배열 스왑체인에 멀티뷰로 그립니다 | 첫 단계는 눈마다 두 번 그립니다. `FrameUBO` 에 눈별 `viewProj[2]` · `eyePos[2]` 를 두고 셰이더는 푸시 상수 눈 번호(`HARUKA_VIEW_INDEX`)로 고릅니다. 멀티뷰로 갈 때는 그 번호만 `gl_ViewIndex` 로 바꿉니다. 섀도는 한 번, 깊이 · AO · Bloom 중간 타깃은 두 눈이 번갈아 쓰고 눈 사이에 배리어를 둡니다. AO 데이터 UBO 는 눈마다 따로 둡니다. 쓰지 않던 멀티뷰 기능 요청은 지웠습니다 | 단안 화면: 1단계 기준과 차이 0 픽셀(100% 배율 모니터). 스테레오 흉내 모드 화면 확인. 아래 "화면 확인" |
| DEF-31 두 눈 컬링 | Godot 은 두 눈을 감싸는 절두체를 만들고, UE 는 한쪽 눈만 써서 물체가 사라지는 버그가 보고된 적이 있습니다 | 눈마다 판정해 하나라도 통과하면 그립니다(`FrustumCull.hpp`). LOD 와 투명 정렬은 두 눈의 가운데를 기준으로 합니다 | `stereo_views` 10개(오른쪽 눈에만 보이는 물체를 살림, 옛 단안 판정에서 실패) |
| DEF-32 | Godot 은 한 프레임의 자세 하나로 컬링과 렌더를 해서 여유를 두지 않습니다 | 보류. 규칙("한 프레임에 자세 하나로 컬링 · 렌더 · 제출")을 코드 주석과 9-2절에 적었습니다 | |
| VR 첫걸음(OpenXR) | UE · Unity · Godot 모두 XR 모듈이 렌더러보다 먼저 Vulkan 장치를 정합니다(XR_KHR_vulkan_enable2) | `src/xr/XrSystem`: OpenXR 1.0 인스턴스, 헤드셋 확인, 런타임이 고른 GPU 로 Vulkan 인스턴스 · 장치 생성(`IVulkanDeviceHooks`), 세션, LOCAL 원점 공간, 두 층 배열 스왑체인 하나, 프레임 루프(대기 · 시작 · 자세 · 이미지 · 제출), 세션 상태 처리. 원점은 활성 카메라의 위치 + 요라 헤드셋을 쓰면 에디터 카메라 자리에서 봅니다. 렌더러는 오른쪽 눈 → 왼쪽 눈 순으로 그리고 왼쪽 눈을 에디터 뷰포트에 다시 톤매핑해 비춥니다(가운데 자르기, UE 기본 관전 화면과 같음). 세션 동안 헤드셋이 루프 속도를 정하고, 데스크톱 표시는 기다리지 않는 방식을 고릅니다. 로더는 Khronos OpenXR-SDK release-1.1.63 을 FetchContent 로 받아 정적 링크합니다 | 런타임이 없을 때 `--xr`: "No OpenXR runtime is available ... Continuing in desktop mode" 경고 뒤 데스크톱 화면이 일반 실행과 같음(제목 · 통계 숫자 제외 0 픽셀). 헤드셋 확인은 남음 |
| DEF-20 입력 | UE · Unity · Godot 모두 XR 입력을 창과 분리된 XR 모듈에서 다룹니다 | 예전 사양(창에 넣기)을 고쳐 XR 모듈이 기본 액션 세트(Touch 컨트롤러 · Khronos 단순 컨트롤러 바인딩)로 입력을 만들고, `HInputSystem` 이 보관합니다. 아날로그 축(`bindAxis` · `axis`), 컨트롤러 버튼을 디지털 액션에 묶기(`bindXrButton`), 머리 · 손 자세와 손 속도(`xr()`). 세션이 멈추면 빈 입력이 들어갑니다. 헤드셋에서는 손에 축 세 개와 가리키는 선이 보입니다(트리거로 색, 그립으로 길이가 바뀜) | `xr_input` 9개 |
| DEF-19 물리 API | Unity · UE · Godot 모두 힘 · 충격량 · 속도 · 레이캐스트를 둡니다 | 중력 세터(잠든 바디도 깨움), 힘 · 위치에 거는 힘 · 토크(그 프레임의 충격량을 그 프레임의 스텝들에 나눠 넣고, 스텝이 없으면 다음으로 넘김), 충격량, 선속도 · 각속도, 레이캐스트(액터 · 지점 · 법선 · 거리), 구 겹침(잡기용). `RigidBodyComponent` 에서도 부릅니다. 게임 코드가 Dynamic 액터를 옮기면 순간 이동하고 속도를 유지합니다 | `physics_lifecycle` 25개 추가(스텝 60 · 120 · 30Hz 에서 같은 속도 변화, 옛 방식에서 3개 실패) |
| DEF-99 Kinematic | | 스텝 루프 안에서 틱 시작 자세부터 목표까지 스텝마다 나눠 옮깁니다. 그 프레임에 목표에 닿고 지나치지 않습니다 | `physics_lifecycle`(옛 식에서 2개 실패) |
| DEF-53 남은 부분 | UE 는 시뮬레이션하는 바디를 부모에서 떼고, Godot 은 월드를 로컬로 바꿔 씁니다 | `Actor::setWorldPosition` · `setWorldRotation` 을 두고 물리가 월드 자세로 되씁니다. 쓴 로컬 값을 기억해 게임 코드의 이동만 순간 이동으로 칩니다. 그래서 부모가 움직여도 Dynamic 자식은 끌려가지 않습니다(UE 같음) | `physics_lifecycle`(옛 식에서 3개 실패) |
| 물리 스텝 주기 | Godot 의 XR 문서는 물리 주기를 헤드셋 주사율에 맞추라고 권합니다 | `PhysicsSystem::setStepRate`(30 ~ 240Hz). XR 세션 동안 엔진이 헤드셋 주사율(XR_FB_display_refresh_rate, 없으면 예상 표시 주기)로 맞추고 끝나면 60Hz 로 돌립니다 | 헤드셋 확인 남음 |
| FFR 멤버 | OpenXR 에서는 런타임이 밀도 맵 이미지를 줍니다 | 선언만 있던 멤버를 지웠습니다 | 빌드 |
| DEF-100 | | AOPass 가 Zone C 버퍼를 하나씩 돌려주지 않게 했고, `freeB` 가 Zone B 가 아닌 할당을 받으면 경고하고 무시합니다 | 빌드 |
| DEF-104 (신규) | | 아래 신규 결함. 프레임 시작에 그래픽 단계 전체를 묶는 배리어 하나를 두었습니다 | 실기 검증 메시지 0, 단안 화면 배리어 전과 0 픽셀 |
| 스테레오 흉내 모드 | UE 의 `-emulatestereo` | `--emulate-stereo`: 헤드셋 없이 뷰포트를 좌우로 나눠 두 눈(64mm, 평행, 코 쪽 45% · 바깥 55% 비대칭 시야)을 그립니다. 이 모드에서 피킹 · 기즈모는 단안 기준이라 맞지 않습니다. `run.bat` 은 설정 다음 인자를 엔진에 넘깁니다(`run.bat Release --xr`) | 화면 확인(두 눈 화면이 좌우에 나란히, 같은 물체가 비대칭 시야만큼 다른 자리) |

**신규 결함**

| ID | 심각도 · 상태 | 내용 | 위치 |
|---|---|---|---|
| DEF-104 | LOW · **해결 2026-10-04** | 깊이 · MSAA · AO · Bloom 중간 타깃은 프레임 슬롯끼리 같이 쓰는데, 렌더 패스 외부 의존성이 조각 셰이더 읽기를 포함하지 않아 앞 프레임의 읽기(AO 가 깊이를, 톤매핑이 AO · Bloom 을 읽음)와 다음 프레임의 쓰기가 겹칠 수 있었습니다. 동기화 검증을 켜야 보이는 잠재 결함입니다. 톤매핑 패스의 외부 의존성도 스왑체인 획득 단계(컬러 출력)와 이어지도록 보강했습니다 | `Renderer::drawFrame`, `PostPass` |

**화면 확인.** 단안 화면은 1단계 기준과 차이 0 픽셀이었습니다(100% 배율 모니터, 두 눈 렌더 개편 직후). 그 뒤 주 모니터가 150%
배율로 바뀌어 예전 기준과 비교할 수 없게 되어, 커밋 `cbae320` 을 별도 작업 트리로 빌드해 같은 조건에서 새 기준을 찍었습니다.
통계 숫자 줄을 빼면 161픽셀이 코드값 1(한 곳은 2) 다릅니다. 카메라 뷰 행렬을 `lookAt` 대신 쿼터니언으로 만든 데서 오는
부동소수점 반올림 차이입니다(행렬 차이 1e-5 미만, 단위 테스트로 확인). 그 캡처 뒤의 변경(멀티뷰 기능 요청 삭제, 컨트롤러
선, XR 실패 시 데스크톱 대체, DEF-104 배리어, 물리)은 단안 화면에 그리는 것이 없거나 Play 때만 돕니다.

**헤드셋으로 확인할 것(남음).** Quest Link 앱 설치 · 헤드셋 연결 뒤 `run.bat Release --xr` 로 띄워: 런타임 · 헤드셋 이름 로그,
세션 상태가 FOCUSED 까지 가는지, 두 눈이 맞게 보이는지(원근 · 깊이감 · 크기), 에디터 뷰포트에 왼쪽 눈이 비치는지, 머리를 돌릴 때
물체가 한쪽 눈에서 사라지지 않는지, 컨트롤러 축 · 선이 손을 따라가는지(트리거 · 그립 반응), 물리 스텝 로그(72 · 90Hz),
헤드셋을 벗었다 쓸 때 세션이 이어지는지, 검증 메시지 0.

**검증.** 빌드 경고 0. 단위 테스트 7 → 9개 통과(`stereo_views` 10개 · `xr_input` 9개 신설, `actor_lifecycle` 31 → 38,
`audio_system` 47 → 48, `physics_lifecycle` 29 → 54). 새 검사는 고친 곳을 하나씩 예전 식으로 되돌리면 실패함을 확인했습니다
(카메라 2 · 롤 1 · 두 눈 판정과 Vulkan 투영 3 · 물리 8). 에디터 실기에서는 실행 폴더의 씬 사본에 정적 바닥과 동적 구를 넣고
Play 해, 구가 바닥 윗면에 정확히 내려앉고 Stop 하면 되돌아오는 것을 확인했습니다(검증 메시지 0).

## DEF-16 자동 인스턴싱 수정 결과 (2026-10-14)

**사용자 결정.** 자동 인스턴싱만 넣고(명시적 인스턴싱 컴포넌트는 파편장 착수 때), 행 예산은 16384 로 올리며, 패스 확장
지점(DEF-17 · 18)은 그다음에 고정 지점 + 좁은 인터페이스로 진행합니다. 화면 확인을 위해 에디터 창을 띄우는 것은 허락받았습니다.

| 항목 | 다른 엔진의 방법 | 수정 | 확인 |
|---|---|---|---|
| DEF-16 인스턴싱 | UE 는 상태가 같은 메시 드로우 명령을 정렬해 나란히 모은 뒤 인스턴스 드로우 하나로 합칩니다(Dynamic Instancing). Bevy 도 메시 · 머티리얼이 같으면 자동으로 묶습니다. Godot MultiMesh · UE Instanced Static Mesh · Unity `RenderMeshInstanced` 는 명시적 인스턴싱입니다. 반투명은 인스턴스 사이 정렬이 깨져 보통 묶지 않습니다 | `src/renderer/DrawBatching.hpp`(`hbatch::build`)가 컬링을 통과한 메시를 같은 지오메트리 · 머티리얼끼리 묶어 행을 연속으로 배정합니다. 셰이더는 이미 `gl_InstanceIndex` 로 행을 읽으므로 바꾸지 않았습니다. `GeometryPass::DrawItem` 에 `instanceCount` 를 두고 세 서브패스와 `ShadowPass::record` 가 그 값으로 그립니다. 투명은 묶지 않고 뒤에서 앞으로 정렬합니다. 묶음 안에서 라이트 안의 인스턴스를 앞에 모아 섀도는 그 앞부분만 그리고, 라이트에만 보이는 캐스터는 지오메트리만으로 묶습니다. 묶음 순서는 처음 나온 순서라 같은 것이 없는 씬에서는 행 번호와 드로우 순서가 예전과 똑같습니다 | `draw_batching` 26개 신설. 묶기를 끄면 8개, 섀도 앞부분 정렬을 빼면 2개가 실패함을 확인했습니다 |
| 행 예산 | | `kMaxDraws` 1024 → 16384. 행은 64바이트라 프레임당 1MB 이고 Zone C 에 시작할 때 한 번 할당합니다(매 프레임 할당이 아니라 DEF-02 의 위험은 그대로) | 빌드 · 실기 |
| 통계 | UE 는 드로우 수와 그리는 프리미티브 수를 따로 보입니다 | 뷰포트 통계와 GPU Stats 에 `Inst`(불투명으로 그리는 메시 수)를 더했습니다. `Draw` 는 눈 하나의 불투명 드로우 수입니다. Culled 는 인스턴스 기준으로 셉니다 | 실기 |

**신규 결함**

| ID | 심각도 · 상태 | 내용 | 위치 |
|---|---|---|---|
| DEF-105 | LOW · **해결 2026-10-14** (일괄 점검) | 빌드가 `assets` 를 실행 폴더로 복사할 때(`copy_directory`) 파일의 수정 시각이 복사 시각으로 바뀝니다. GLTF 바이너리 캐시(`GassetCache`)가 원본 수정 시각으로 유효성을 판단하므로 빌드할 때마다 캐시가 무효화되고, 빌드 직후 첫 실행만 GLTF 를 다시 읽습니다. 그 실행에서만 `model.glb` 의 머티리얼 경고가 나와 경고 개수가 실행마다 달라 보입니다(W 1 → 다음 실행부터 W 0) | `CMakeLists.txt` 의 에셋 복사, `GassetCache::isValid` |

**화면 확인.** 지금 빌드(수정 전)를 두 번 찍어 뷰포트가 픽셀까지 같음(결정적)을 확인한 뒤, 수정 후 빌드와 비교했습니다. 기본 시작
씬은 메시 · 머티리얼이 모두 같은 쌍이 없어 묶음이 생기지 않으므로, 실행 폴더의 씬 사본에만 같은 메시 · 머티리얼의 상자 · 구와 카메라
뒤에서 그림자만 화면에 떨어지는 상자를 더한 씬도 비교했습니다. 두 씬 모두 통계 글자를 빼면 뷰포트 차이가 0 픽셀입니다. 사본 씬의
통계는 수정 전 `Draw: 12` 에서 수정 후 `Draw: 8 Inst: 12` 로 바뀌었습니다(Culled 7 은 같음). 검증 메시지 0, 엔진 Error 0,
`Clean exit`. 설정 폴더와 시작 씬은 매번 원본으로 되돌렸습니다(해시 · 내용 일치).

**검증.** 빌드 경고 0. 단위 테스트 9 → 10개 통과(`draw_batching` 26개 신설).

## DEF-17 · 18 패스 확장 지점 수정 결과 (2026-10-14)

**사용자 결정.** 렌더 그래프가 아니라 고정 지점 + 좁은 인터페이스. 작업 도중 사용자 지시("에디터 실행은 잠시 미루자. 개선 점검은
추후 일괄로 진행한다")로 에디터를 띄우는 확인은 하지 않았고, 아래 "일괄 점검 때 확인할 것"에 모았습니다.

| 항목 | 다른 엔진의 방법 | 수정 | 확인 |
|---|---|---|---|
| DEF-17 확장 지점 | Godot CompositorEffect 는 불투명 전 · 후, 하늘 뒤, 반투명 전 · 후 다섯 곳에 끼웁니다. Unity URP 는 Renderer Feature 가 `RenderPassEvent` 로 지점을 고르고 카메라마다 패스를 넣습니다. UE SceneViewExtension 은 베이스 패스 전 · 후와 포스트 프로세스 각 단계 뒤에 구독합니다. Bevy 는 렌더 그래프의 노드 · 간선으로 배치하고 뷰마다 실행합니다 | `src/renderer/RenderExtension.hpp`: 지점 넷(`OpaqueEnd` 불투명 서브패스 끝, `TransparentEnd` 반투명 뒤 · 기즈모 앞, `AfterScene` 메인 패스 뒤 · AO 앞, `AfterTonemap` 톤매핑 뒤 · UI 앞). 메인 패스는 서브패스 3개의 한 렌더 패스(Quest 타일 GPU 용)라 앞의 둘은 그 서브패스 안에서 그리고, 확장은 `mainPass` 와 서브패스 번호로 파이프라인을 만듭니다. `IRenderExtension` 이 `HPass` 수명주기에 `create` · `record(HRenderStageContext&)` 를 더하고, 눈마다 눈 번호 · 행렬 · FrameUBO 세트를 받습니다. XR 거울 화면에도 톤매핑 뒤 지점을 부릅니다(`mirror`). 지오메트리 패스는 불투명 끝 뒤에 반투명 서브패스가 다시 설정하지 않는 상태(눈 번호 푸시 상수, 정점 · 인덱스 버퍼)를 되돌립니다. `AfterScene` 앞뒤에는 전체 배리어를 둡니다. 지점에 확장이 없으면 기록하는 명령이 예전과 같습니다 | `render_extensions` 17개 신설 |
| DEF-17 패스 레지스트리 | UE · Unity 모두 기본 패스와 확장 패스를 같은 프레임 흐름에서 다룹니다 | `RenderExtensionRegistry` 가 기본 패스 다섯(shadow → geometry → ao → bloom → post)과 확장을 한 목록으로 돌립니다. 획득 · 파이프라인 재생성은 기본 패스 → 확장, 해제 · 파괴는 그 반대입니다(확장이 기본 패스의 HDR · 깊이를 쓸 수 있으므로). 크기 변경 · 핫 리로드 · 파괴에서 손으로 열거하던 코드가 루프가 됐습니다. 생성은 Zone B 의 영구 자원을 먼저 할당하는 순서 때문에 손으로 둡니다 | 같은 테스트. 해제 순서를 뒤집으면 1개, 지점 구분을 없애면 4개 실패 |
| DEF-18 게임 모듈의 통로 | UE 는 `FSceneViewExtensions::NewExtension`, Godot 은 Compositor 리소스, Bevy 는 플러그인이 렌더 서브앱에 등록합니다 | `EngineContext::render` 가 `IRenderExtensionHost`(등록 · 해제만)입니다. 등록하면 바로 create → 크기 의존 자원 → 파이프라인을 밟고, 실패하면 오류를 남기고 넣지 않습니다. 해제는 장치가 쉬기를 기다린 뒤 파괴합니다. 렌더러가 끝날 때 남은 확장은 기본 패스보다 먼저 정리합니다 | 빌드 |

**일괄 점검 때 확인할 것(에디터 실행).** ① 확장이 없을 때 뷰포트가 수정 전과 픽셀까지 같은지(기본 씬 · 묶음 씬). ② 시험용
확장을 잠시 넣어 네 지점에서 그려지는지: 불투명 끝(깊이 테스트로 불투명 물체 뒤에 가려짐), 반투명 끝(반투명 위에 그려지고 기즈모
아래), 메인 패스 뒤(HDR 에 쓴 것이 Bloom · 톤매핑에 반영), 톤매핑 뒤(UI 아래). 단안 · 스테레오 흉내 모드 모두, 창 크기를 바꾼 뒤와
셰이더 핫 리로드 뒤에도. ③ 검증 메시지 0, `Clean exit`. 시험용 확장은 확인 뒤 지웁니다.

**검증.** 빌드 경고 0. 단위 테스트 10 → 11개 통과(`render_extensions` 17개 신설).

## DEF-22 직렬화 타입 등록 수정 결과 (2026-10-14)

**사용자 결정.** 명시 등록, 기본값도 모두 기록, 등록되지 않은 타입은 경고 + 원문 보존, 범위는 DEF-22 + 에디터 컴포넌트 추가 버튼 +
DEF-101. 에디터 실행은 사용자 지시로 미뤄 화면 확인은 아래 "일괄 점검 때 확인할 것"에 모았습니다.

| 항목 | 다른 엔진의 방법 | 수정 | 확인 |
|---|---|---|---|
| 타입 등록 | Bevy 는 `register_type` 으로 등록한 타입만 씬에 저장 · 읽기 하고 없으면 오류(`SceneSpawnError::UnregisteredComponent`). Godot 은 `ClassDB::register_class` 로 등록한 클래스만 씬을 읽을 때 만듭니다. UE 는 클래스가 정적으로 자동 등록됩니다 | `ComponentRegistry`(`src/core/ComponentRegistry.hpp`): 타입 이름 → 생성 · 찾기 함수, 옛 이름(별칭), 에디터 버튼 라벨. 엔진이 시작할 때 기본 5종을 등록하고(`registerCoreComponents`), Layer 2 는 `EngineContext::components` 로 등록합니다. 전역 객체로 스스로 등록하는 방식은 정적 라이브러리에서 링커가 버릴 수 있어 쓰지 않았습니다 | `scene_components` 신설 |
| 값 기록 | UE 는 값마다 이름 · 형을 붙여 쓰고(tagged property), Godot `.tscn` · Unity YAML 도 `이름 = 값` 입니다. Unity 는 이름을 바꾸면 `FormerlySerializedAs` 로 옛 이름도 받습니다. UE · Godot 은 기본값과 다른 값만 쓰고(기본값을 바꾸면 저장한 값이 "변경 없음"이 되는 문제가 UE-88414 로 보고됨), Unity 는 모두 씁니다 | `Component::writeFields` · `readField` · `onFieldsLoaded`. 씬 v4 · 프리팹 v2 의 액터 블록 안에 컴포넌트마다 `COMPONENT:타입` · `enabled=` · `이름=값` 줄 · `END_COMPONENT` 를 씁니다(`ComponentSerializer`). 순서와 상관없고, 모르는 이름은 경고하고 건너뛰며, 없는 이름은 기본값, 깨진 값은 기본값을 두고 오류를 남깁니다. 기본값도 모두 씁니다. 종류 · 셰이프 · 모션은 이름으로 쓰고 옛 숫자도 받습니다. `LIGHT:` 줄의 위치 기반 확장(예약 슬롯 포함) 문제가 사라졌습니다 | 같은 테스트, `physics_lifecycle` · `audio_system` 에 강체 · 오디오 왕복 추가 |
| 모르는 타입 | Unity 는 스크립트를 못 찾으면 Missing Script 로 데이터를 보존하고, Bevy 는 오류를 냅니다 | 경고하고 `UnknownComponent` 에 원래 이름과 값을 담아, 다시 저장할 때 글자 그대로 씁니다. Layer 2 모듈 없이 씬을 열었다 저장해도 데이터가 사라지지 않습니다 | 같은 테스트 |
| 옛 형식 | | v3 이하의 `LIGHT:` · `CAMERA:` · `AUDIO*:` · `RIGID*:` · `PRIM*:` · `MODEL:` 줄은 예전 경로로 계속 읽고, 쓰기는 v4 만 합니다. 저장소의 `LightingProbe.hscene` · `AudioProbe.hprefab` 은 v3 · v1 그대로 두었습니다(일괄 점검 때 에디터로 저장해 바꿈) | 빌드(예전 경로 그대로) |
| 에디터 추가 버튼 | UE · Unity 는 등록된 컴포넌트 클래스 목록으로 추가 메뉴를 만듭니다 | 디테일 패널의 "+ 라벨" 버튼을 레지스트리에서 만듭니다. 순서 · 라벨은 예전과 같습니다(Light · Camera · Mesh · Audio · RigidBody) | 일괄 점검 |
| DEF-101 | | 메시의 `transparent` 를 저장합니다. 파일에 있으면 그 값을, 없으면 에셋의 값을 씁니다 | 일괄 점검 |

**행동 변화.** 메시도 경로도 없는 빈 메시 컴포넌트(+ Mesh 만 누른 것)는 예전에는 저장할 때 사라졌지만, 이제 `visible` ·
`transparent` 만 가진 블록으로 남습니다. 메시 · 도형을 불러오지 못한 컴포넌트도 컴포넌트는 남고 경로 · 도형 값을 지켜 다시 저장해도
사라지지 않습니다(예전에는 컴포넌트를 붙이지 않았음).

**일괄 점검 때 확인할 것(에디터 실행).** ① 시작 씬(v3)이 예전과 같이 읽히고 화면이 `744cc69` 와 같은지. ② Play → Stop(v4 를 쓰고
다시 읽음) 뒤 모든 액터 · 컴포넌트 값이 그대로인지(라이트 종류 · 색온도 · 그림자, 카메라, 오디오 3D, 강체, 절차 도형과 머티리얼).
③ Save → Load(scene.hscene 이 v4 로 써지는지, 다시 읽어 같은지). ④ 프리팹 저장 · 불러오기(v2), 옛 `AudioProbe.hprefab`(v1) 읽기.
⑤ 디테일의 컴포넌트 추가 버튼이 예전과 같은지. ⑥ `transparent` 를 바꿔 저장 · 불러오기(DEF-101). ⑦ 저장소의 시작 씬 · 프리팹을
v4 · v2 로 저장해 바꿀지 사용자가 정함.

**검증.** 빌드 경고 0. 단위 테스트 11 → 12개 통과(`scene_components` 32개 신설, `physics_lifecycle` 54 → 60, `audio_system` 48 → 52).
새 검사는 원문 보존과 옛 이름 찾기를 끄면 7개가 실패함을 확인했습니다.

## DEF-33 · 10 수정 결과 (2026-10-14)

**사용자 결정.** DEF-33 은 화면 밖 거르기 + 상한 초과 시 중요도, DEF-34 는 실측 뒤로 보류, DEF-10 은 설정 파일, DEF-13 은 첫 사용처까지
보류. 에디터 실행은 미뤄 화면 확인은 아래 "일괄 점검 때 확인할 것"에 모았습니다.

| 항목 | 다른 엔진의 방법 | 수정 | 확인 |
|---|---|---|---|
| DEF-33 라이트 선택 | Unity URP Forward 는 물체 하나에 라이트 9개(메인 1 + 추가 8)까지이고 넘치면 덜 밝은 라이트부터 버립니다. Godot Mobile 은 메시당 점광원 8 + 스팟 8, 화면당 256 + 256 이고 넘치면 깜빡일 수 있다고 적습니다 | `hlight::select`: 영향 구(위치, Attenuation Radius)가 어느 눈의 절두체와도 겹치지 않는 점광원 · 스팟은 올리지 않습니다. 겹치지 않으면 보이는 픽셀에 기여할 수 없으므로 화면은 그대로입니다. 상한 안이면 순서를 바꾸지 않고, 넘으면 방향광을 먼저(원래 순서), 나머지는 두 눈 가운데에서의 거리/반경이 작은 순 · 같으면 밝은 순으로 채운 뒤 원래 순서로 올립니다. 첫 방향광(그림자 캐스터) 선택이 바뀌지 않습니다. GPU Stats 에 `Lights`(올린 수 / 켜진 수)를 더했습니다 | `light_selection` 신설. 거르기를 끄면 3개, 방향광 우선을 빼면 2개 실패 |
| DEF-34 클러스터드 | Unity Forward+ 는 화면 타일로 물체당 상한을 없애고, Godot Forward+ 는 클러스터(화면당 요소 512, 설정 가능)입니다. 클러스터 논문(Olsson 2012)과 Avalanche 엔진은 라이트 배정을 CPU 에서도 합니다 | 보류(착수 조건이 측정). 할 때는 클러스터 방식을 권합니다. Godot Mobile 식 물체별 목록은 인스턴스마다 목록이 달라 DEF-16 과 맞지 않습니다 | 일괄 점검 · Quest 측정 |
| DEF-10 상한 설정 | Godot 은 Jolt 바디 상한을 프로젝트 설정 `physics/jolt_physics_3d/limits/max_bodies`(기본 10240)로 둡니다 | 실행 폴더의 `Config/engine.ini` 에 `render.maxDrawRows`(16384) · `render.maxLights`(256) · `physics.maxBodies`(4096). 시작할 때 한 번 읽어 transform · 라이트 버퍼와 Jolt 를 그 크기로 만듭니다. 파일이 없으면 기본값과 설명으로 만들고, 범위 밖 값은 범위 안으로(행은 Zone C 예산 때문에 131072 까지), 깨진 값 · 모르는 이름은 알립니다. 시작 로그에 상한을 한 줄로 남기고, 초과 경고는 설정 이름을 알려 줍니다. 기본값은 예전 상수 그대로입니다 | 같은 테스트 |
| DEF-13 | | 보류. 렌더 확장이 CPU 쪽 눈별 행렬을 받으므로 필요한 확장이 직접 넘길 수 있습니다 | |

**일괄 점검 때 확인할 것(에디터 실행).** ① 첫 실행에서 `Config/engine.ini` 가 만들어지고 시작 로그에 "Limits: draw rows 16384, lights 256,
physics bodies 4096" 이 남는지. ② 값을 바꾸면(예: render.maxLights=2) 다시 실행했을 때 적용되고, 범위 밖 값이 경고와 함께 고쳐지는지.
③ 화면이 `744cc69` 와 같은지(기본 씬의 라이트는 방향광 하나). ④ 실행 폴더의 씬 사본에 점광원 여러 개를 두고 카메라를 돌릴 때, 화면
가장자리의 빛이 갑자기 사라지지 않는지와 GPU Stats 의 Lights 수가 줄어드는지.

**검증.** 빌드 경고 0. 단위 테스트 12 → 13개 통과(`light_selection` 16개 신설).

## 일괄 점검 결과와 신규 결함 (2026-10-14)

**사용자 지시.** "Compact 이후 실측을 위해 에디터 직접 열고 검증 진행하고 해당 과정에서 나온 문제점들 정확히 파악해서 내 판단 없이
스스로 작업 진행해. 모든 수정이 완료된 뒤 종합해서 보고해." 그동안 "일괄 점검 대기"로 모아 둔 화면 확인을 모두 했습니다.

**방법.** 에디터 창을 (0,0)-(1368,1032)에 두고 `PrintWindow` 로 찍었습니다(150% 배율 모니터, 물리 픽셀 좌표). 조작은 실제 마우스 ·
키보드가 아니라 창 메시지로 보냈습니다(글자는 `WM_CHAR` 만. `WM_KEYDOWN` 을 같이 보내면 GLFW 의 `TranslateMessage` 가 글자를 한 번 더
만듭니다). 검증 레이어를 켰고, 설정 폴더와 시작 씬은 매번 원본으로 되돌렸습니다. 기준 화면은 같은 날 찍은 DEF-16 커밋(`744cc69`)
시점의 사진이며, 뷰포트 (137,147)-(1160,747)에서 통계 글자 상자를 뺀 영역을 비교했습니다.

**점검 결과**

| 항목 | 결과 | 비고 |
|---|---|---|
| DEF-17 · 18 확장 없음 | 통과 | 기본 씬 · 묶음 씬 모두 기준과 뷰포트 차이 0 픽셀. 묶음 씬 통계도 `Draw: 8 Inst: 12 Culled: 7` 로 같음 |
| DEF-17 · 18 네 지점 | 통과 | 임시 시험 확장으로 확인하고 지웠습니다. 렌더 패스 안 세 지점은 사각형 지우기(`vkCmdClearAttachments`), 메인 패스 뒤는 HDR 이미지에 HDR 30 블록 복사(레이아웃을 바꿨다 되돌림). 반투명 끝의 초록(HDR 4)과 메인 패스 뒤의 파랑은 Bloom 으로 번지고, 톤매핑 뒤의 노랑은 번지지 않습니다(불투명 끝의 빨강은 휘도 0.85 로 Bloom 문턱 아래). 스테레오 흉내에서 두 눈 각각, 창 크기 변경 뒤, 셰이더 핫 리로드 뒤에도 그려지고(핫 리로드 전후 0 픽셀), 수명주기 로그(create · 크기 의존 해제/획득 · rebuildPipelines · destroy)가 순서대로 남습니다. 검증 메시지 0 |
| DEF-22 시작 씬(v3) | 통과 | 기본 씬 화면이 기준과 0 픽셀 |
| DEF-22 Play → Stop | 고친 뒤 통과 | 처음에는 Play 를 누르자 충돌(DEF-107), 고친 뒤에는 Save 결과에서 회전 하나가 `-9.999999` → `-9.999998` 로 바뀜(DEF-108). 둘 다 고친 뒤 컴포넌트 다섯 종을 모두 둔 시험 씬에서 Play → Stop 세 번 뒤의 Save 결과가 처음과 글자까지 같습니다. Play 중 공의 높이는 0.3m(받침대 윗면 0.1 + 반지름 0.2) |
| DEF-22 Save → Load(v4) | 통과 | Load 한 뒤 다시 Save 한 결과가 처음과 같음. 오디오 값(볼륨 0.5 · 반복 · 3D · Inverse 1 ~ 50m)도 디테일에 그대로 |
| DEF-22 프리팹 | 통과 | Save as Prefab 이 v2(COMPONENT 블록 · QUAT 줄)로 쓰고, 콘텐츠 브라우저에서 새 프리팹과 저장소의 옛 `AudioProbe`(v1)를 꺼내면 메시 · 오디오 값이 파일 그대로. 시험 프리팹은 지움 |
| DEF-22 컴포넌트 추가 버튼 | 고친 뒤 통과 | 순서 · 라벨은 예전과 같지만 한 줄이라 잘림(DEF-109). 고친 뒤 두 줄로 나뉘어 모두 보임 |
| DEF-101 `transparent` | 통과 | 끄고 Save 하면 `transparent=0`, Load 뒤에도 꺼진 채 불투명으로 그려짐(통계 Transp 0) |
| 저장소 시작 씬 · 프리팹 형식 | 바꾸지 않음 | 일괄 점검 지시에 따라 스스로 정했습니다. `LightingProbe.hscene`(v3) · `AudioProbe.hprefab`(v1)은 그대로 둡니다. 실행할 때마다 옛 형식 읽기를 실제로 거치는 유일한 파일이고, 에디터로 다시 저장하면 머리 설명 주석이 사라지며, 바꿔서 얻는 기능이 없습니다 |
| DEF-10 상한 설정 | 통과 | 첫 실행에서 `Config/engine.ini` 가 기본값과 설명으로 생기고 시작 로그에 `Limits: draw rows 16384, lights 256, physics bodies 4096`. 값을 바꾸자 `render.maxDrawRows=500` → 1024 로 고침, `physics.maxBodies=lots` → 기본값, 모르는 이름은 건너뜀을 각각 경고하고 `Limits: draw rows 1024, lights 8, ...` 로 적용. 상한 초과 경고는 한 번만 |
| DEF-33 라이트 거르기 | 통과 | 카메라를 둘러싼 점광원 · 스팟 40개 씬에서, 거르기를 끈 진단 빌드와 뷰포트가 0 픽셀 같습니다(단안 · 스테레오 흉내). 감쇠의 창 함수가 반경에서 정확히 0 이라, 영향 구가 절두체와 겹치지 않는 라이트를 빼도 결과가 비트까지 같습니다. 따라서 카메라를 돌릴 때 가장자리에서 빛이 사라지는 일은 상한 안에서는 생길 수 없습니다. 상한 8 로 낮추면 GPU Stats 에 `Lights 8 / 41` |
| DEF-34 측정 | 보류 유지 | 아래 표 |
| 최소화 | 고친 뒤 통과 | 최소화한 채 창을 닫으면 끝나지 않았습니다(DEF-106) |
| 빌드 뒤 경고 | 고친 뒤 통과 | DEF-105. 고친 뒤에는 에셋을 다시 복사해 수정 시각을 바꿔도 경고 0(형식이 바뀐 첫 실행에서만 한 번 다시 읽음) |
| 최종 화면 | 통과 | 모든 수정 뒤 기본 씬 · 묶음 씬이 기준과 0 픽셀, 검증 메시지 0, 엔진 Error 0, `Clean exit` |

**DEF-34 측정(PC).** 바닥 위 격자에 반경 2.5m 점광원 N개를 두고 `render.maxLights=4096` 으로 올려, 프레임 다섯 개의 메인 패스 GPU
시간을 읽었습니다(검증 레이어 끔).

| 점광원 | 메인 패스 | FPS |
|---|---|---|
| 0 (방향광 하나) | 0.10ms | 555 |
| 32 | 0.17ms | 542 |
| 128 | 약 1.1ms(첫 프레임 0.45) | 440 |
| 512 | 1.5 ~ 3.1ms(GPU 클럭에 따라 둘로 갈림) | 257 |
| 2048 | 8.4 ~ 8.8ms(GPU 가 프레임을 정함) | 109 |

부하가 낮을 때는 GPU 클럭이 내려가 작은 값이 흔들리지만, 많이 둘수록 라이트 하나에 약 4µs 로 수렴합니다. 기본 상한 256 에서는 약
1ms 라 PC 에서는 병목이 아닙니다. Quest Pro 는 두 눈 화소가 약 11배, GPU 성능이 20 ~ 25분의 1 이라 라이트 하나에 약 1ms 로
어림됩니다. 이 값은 어림이므로 Quest 이식 때 실측해 클러스터 배정 착수를 정합니다.

**신규 결함 (모두 등재와 함께 해결)**

| ID | 심각도 · 상태 | 내용 | 다른 엔진의 방법 | 수정 | 확인 |
|---|---|---|---|---|---|
| DEF-106 | MEDIUM · **해결 2026-10-14** | 창을 최소화하면 프레임버퍼가 0x0 이 되어 표시가 실패하고, 스왑체인을 다시 만드는 함수가 복원될 때까지 이벤트만 기다렸습니다. 그동안 메인 루프 전체가 멈춰 시뮬레이션 · XR 도 멈추고, 최소화한 채 작업 표시줄에서 창을 닫으면 엔진이 끝나지 않았습니다(복원해야 끝남). 일괄 점검 중 캡처 창이 최소화되며 드러났습니다 | UE 의 Slate 는 최소화한 창을 그리지 않고 엔진 루프는 계속 돌립니다. Godot 도 그릴 수 없는 창은 건너뛰고 루프는 쉬어 가며 돕니다. Vulkan 입문서의 "복원될 때까지 기다리기"가 Haruka 에 남아 있던 것입니다 | `ISwapchain::hasArea()`. 최소화 중에는 데스크톱 화면을 건너뛰고(XR 프레임이면 두 눈만 그려 헤드셋은 계속 보임), 스왑체인 다시 만들기는 복원된 뒤 첫 프레임이 합니다. 에디터 UI 배치는 0 크기를 다루지 않으므로 최소화 중에는 부르지 않고, 시뮬레이션 · 소리는 계속 돕니다. 그릴 것이 없을 때는 약 60Hz 로 쉬어 갑니다 | 최소화한 채 닫으면 0.42초에 `Clean exit`(단안 · 스테레오 흉내). 최소화 중 CPU 1% 안팎(보일 때 90%). 복원 뒤 화면 0 픽셀. Play 중 2.5초 최소화한 동안 공이 떨어져 받침대 위에 놓임. 검증 0. 헤드셋에서 최소화 중 두 눈이 계속 그려지는지는 Quest Link 확인 항목 |
| DEF-107 | HIGH · **해결 2026-10-14** | 반높이가 Jolt 의 기본 모서리 반경(0.05m)보다 작은 상자, 곧 두께 10cm 이하의 바닥 · 벽 콜라이더는 셰이프 생성이 실패하는데, 결과를 확인하지 않고 꺼내 썼습니다. Jolt 의 `Result` 는 오류 문자열과 결과를 같은 자리에 담아, 실패한 결과를 꺼내면 쓰레기 포인터가 나옵니다. 그런 상자가 하나라도 있으면 Play 를 누르는 순간 엔진이 접근 위반으로 죽었습니다(오류 기록 없음). 크기 0 인 구 · 상자도 같았습니다 | Godot 의 Jolt 모듈과 UE Chaos 는 모서리 반경(collision margin)을 가장 짧은 변에 맞춰 줄입니다. Godot 은 만들 수 없는 크기면 오류를 남기고 셰이프를 만들지 않습니다 | 상자의 모서리 반경을 `min(0.05, 가장 짧은 반변의 절반)` 으로(두께 10cm 이상이면 예전과 같음). 셰이프 생성 결과의 오류를 확인하고, 스케일 적용 뒤 0.1mm 보다 작은 반지름 · 반변은 만들지 않고 경고합니다(Play · 속성 편집 때 다시 시도) | `physics_lifecycle` 에 5개 추가(두께 10cm · 1cm 상자에 바디가 생기고 구가 그 위 y=0.25 에 놓임, 두께 0 상자 · 반지름 0 구는 실패로 표시). 반경 조정을 빼면 3개 실패, 예전 코드로 되돌리면 테스트가 충돌합니다. 실기에서 Play 정상 |
| DEF-108 | LOW · **해결 2026-10-14** | 씬 · 프리팹은 회전을 오일러 각(`ROT:`)으로만 썼습니다. 엔진이 들고 있는 값은 쿼터니언이라 쓰고 읽을 때마다 마지막 자리가 흔들렸고, Play → Stop 은 씬을 글로 써 두었다가 다시 읽으므로 아무것도 바꾸지 않아도 Play 할 때마다 회전이 조금씩 바뀌었습니다. 무작위 회전 2000개 중 1988개가 한 번 왕복으로 값이 바뀝니다. 위치 · 색은 예전에 이미 정밀도를 잃지 않게 고쳐 두었습니다 | Unity 는 쿼터니언을 저장하고 오일러 각은 편집 화면용으로 따로 둡니다. Godot 은 행렬을 저장합니다. UE 는 오일러 각(FRotator)이 원본 값이라 변환이 없습니다 | `ROT:` 다음 줄에 쿼터니언을 정확한 값으로 함께 씁니다(`QUAT:x y z w`, `TransformText.hpp`). 읽을 때 QUAT 에서 다시 구한 오일러 각이 ROT 와 비트까지 같으면 QUAT 를, 다르면(사람이 ROT 를 손으로 고침) ROT 를 씁니다. QUAT 가 없는 옛 파일은 예전처럼 읽고, 옛 엔진은 모르는 줄로 건너뜁니다 | `scene_components` 에 7개 추가(무작위 2000개가 다섯 번 왕복해도 비트까지 같음, ROT 를 고치면 ROT 가 이김, 옛 파일 · 깨진 QUAT). QUAT 를 무시하면 2개, 늘 QUAT 를 쓰면 1개 실패. 실기에서 Play → Stop 세 번 · Load → Save 뒤 저장 결과가 같음 |
| DEF-109 | MEDIUM · **해결 2026-10-14** | 디테일의 컴포넌트 추가 버튼을 한 줄에 늘어놓아, 기본 창 크기(150% 배율)에서 컴포넌트가 없는 액터는 `+ Audio` 가 잘리고 `+ RigidBody` 는 보이지 않아 누를 수 없었습니다. DEF-22 전부터 있던 배치입니다 | UE · Unity 는 추가 버튼 하나가 검색할 수 있는 목록을 엽니다 | 패널 폭에 맞춰 여러 줄로 나눕니다. 폭은 스크롤 막대 자리를 늘 뺀 값이라 막대가 생기고 없어져도 줄 나눔이 바뀌지 않고, 패널 폭이 바뀌면(4px 양자화) 디테일을 다시 만듭니다. Layer 2 컴포넌트가 많아지면 UE 식 목록이 낫습니다(에디터 Phase 4) | 실기: 빈 액터는 `+ Light + Camera + Mesh` / `+ Audio + RigidBody` 두 줄, Glass 는 세 개 / 한 개 |
| DEF-105 | LOW · **해결 2026-10-14** | (DEF-16 절에서 등재) 빌드의 에셋 복사가 수정 시각을 바꿔 GLTF 캐시가 빌드마다 무효화되고, 그 실행에서만 에셋 경고가 나왔습니다 | Godot 의 가져오기는 수정 시각이 다르면 내용 해시(md5)로 다시 확인합니다. UE · Unity 의 파생 데이터 캐시도 내용 해시를 키로 씁니다 | 캐시 머리에 원본 크기 · 내용 해시(FNV-1a 64)를 함께 기록합니다(형식 버전 2). 크기가 다르면 무효, 수정 시각이 같으면 유효, 다르면 해시를 비교해 같으면 유효로 보고 기록한 수정 시각을 고쳐 다음부터 해시를 생략합니다 | `asset_cache` 8개 신설(임시 폴더에서 수정 시각만 바꾸면 유효, 같은 크기 · 다른 내용과 다른 크기는 무효, 버전 1 은 다시 만듦). 해시 비교를 빼면 3개, 해시를 보지 않으면 1개 실패. 실기에서 에셋을 다시 복사해도 경고 0 |

**남은 것.** Quest Link 확인 목록에 "데스크톱 창을 최소화해도 헤드셋 두 눈이 계속 그려지는지"(DEF-106)를, Quest 이식 목록에
DEF-34 측정을 더했습니다.

**검증.** 빌드 경고 0. 단위 테스트 13 → 14개 통과(`asset_cache` 8개 신설, `physics_lifecycle` 60 → 65, `scene_components` 32 → 39).
점검에 쓴 진단 코드(충돌 스택 기록, 라이트 거르기 끄기, 시험 렌더 확장, 물리 로그)는 모두 지웠습니다.
