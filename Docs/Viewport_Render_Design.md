# 뷰포트 크기 렌더 + A-4 구조 설계 (D 갈래, 2026-08-26)

사용자가 D 갈래에서 "구조 먼저(뷰포트+A-4)" 경로를 선택했습니다. 이 문서는 씬을 창 전체가 아니라
3D 뷰포트 크기로 렌더하도록 `renderExtent`를 분리하고, 무효 GPU 컬링(A-4)을 정리하는 구조 변경을
상세 설계합니다. 반투명 수정(A1)·가짜 TAA 비활성(T-c)은 이 구조가 안정된 뒤 [Transparency_Temporal_Design.md]
에 따라 적용합니다. 구현은 이 설계 승인 이후입니다.

**관통 불변식(깨지면 DEF-25류 재발)**: 깊이 프리패스가 그리는 집합 == 불투명 컬러가 그리는 집합 ==
반투명이 depthTest로 의존하는 깊이, 그리고 세 투영(CPU 프러스텀 / GPU cullViewProj / aoCam.invProj)이
**같은 renderExtent에서 유도**되어야 합니다.

---

## 1. 현재 구조 (코드 근거)

- 모든 씬 타깃이 `swapchain_->extent()`로 크기가 잡힘: HDR·깊이(GeometryPass `rc_->extent`),
  AO(AOPass `extent/2`), Bloom(BloomPass `extent>>i`), TAA 히스토리(PostPass `rc_->extent`).
- 세 투영의 aspect가 스왑체인 extent에서 파생: `updateUBO:164`(카메라 VP), `buildDrawCommands:213`
  (CPU 프러스텀), `extractRenderView:320`(aoCam.invProj). GPU `cullViewProj`는 `view.cullViewProj`
  (= viewProj[0], 같은 카메라 VP)에서 옴(`drawFrame:449`).
- 메인 패스는 풀 스왑체인 크기 프레임버퍼에 그림. post(`recordTonemap`, `drawFrame:482`)가 풀
  스왑체인 이미지에 씀. UI(`uiRenderer_->record`, `:499`)가 그 위에 패널을 불투명하게 덮음 →
  데스크톱에서 픽셀 51.3%가 UI 밑에 깔려 낭비(2-J 실측: 2005x1050에서 1.52→0.92ms).
- 컬 이중화: 서브패스 0 깊이 프리패스는 GPU 인다이렉트(cull 결과 `opaqueDrawBuf`)로, 서브패스 1
  불투명 컬러는 CPU 리스트(`view.opaqueItems`)로 그림. CPU가 `buildDrawCommands`에서 이미
  `sphereInFrustum`으로 컬링해 업로드하므로, GPU 컬은 같은 집합을 재판정해 **하나도 못 버림**(A-4).
  두 경로가 같은 집합인 것은 CPU 사전 컬링 덕분에 성립하는 불변식.

좌표(현행): A-1에서 씬이 프레임버퍼 전체에 그려진다는 전제로 `WorldViewport`가 마우스를 프레임버퍼
전체로 매핑합니다. 뷰포트 렌더로 바꾸면 이 매핑도 뷰포트 rect 기준으로 전환됩니다(좌표 근본 해결).

---

## 2. renderExtent 추상화

`renderExtent_`(3D 뷰포트의 픽셀 크기)를 `swapchain_->extent()`(창/UI 크기)와 분리해 도입합니다.
에디터가 매 프레임 뷰포트 rect(오프셋 + 크기, 프레임버퍼 픽셀)를 렌더러에 전달합니다.

**renderExtent로 크기를 잡을 타깃**: HDR, 깊이, AO(renderExtent/2), Bloom(renderExtent>>i),
TAA 히스토리. 이들은 뷰포트 변경 시 재생성됩니다(스왑체인 재생성과 유사한 경로).

**스왑체인 extent로 남는 것**: 스왑체인 이미지 자체, post의 최종 합성 대상, UI.

전달 경로: `EditorApp`은 `viewportRect(winW,winH)`(스케일 적용된 3D 뷰포트)를 이미 압니다. 이를
프레임버퍼 픽셀로 변환해(`mouseToFb`와 같은 스케일) 렌더러에 `setViewportRect(x,y,w,h)`로 넘깁니다.
게임(비에디터) 빌드는 뷰포트 = 스왑체인 전체이므로 renderExtent = swapchain extent(현행과 동일).

---

## 3. 좌표 불변식 유지

세 투영의 aspect를 **모두** renderExtent에서 유도하도록 세 곳을 한꺼번에 바꿉니다:
`updateUBO:164`, `buildDrawCommands:213`, `extractRenderView:320`. 하나라도 스왑체인 extent로
남으면 CPU 프러스텀·GPU cullViewProj·SSAO 재구성이 갈려 프리패스/컬러 집합 불일치(DEF-25류)가
재발하고, 반투명이 잘못된 불투명 깊이에 depthTest하여 오배치됩니다.

`cullViewProj`는 카메라 VP에서 파생되므로 aspect만 renderExtent로 바꾸면 자동으로 정합합니다.

**WorldViewport 전환**: 마우스→월드 좌표 생산자(`worldVP`)를 프레임버퍼 전체가 아니라 뷰포트 rect
기준으로 바꿉니다. 뷰포트 안 마우스 → 뷰포트 로컬 픽셀(renderExtent 좌표계). 기즈모 투영·픽도 같은
기준을 쓰므로, A-1이 만든 "단일 진입점" 구조가 그대로 재사용되고 매핑 대상만 뷰포트 rect로 바뀝니다.

---

## 4. 합성 (renderExtent HDR → 스왑체인 서브렉트)

post가 renderExtent HDR을 스왑체인의 **뷰포트 서브렉트**에 합성합니다. 방법 두 가지:

- **(권장) 뷰포트/시저 오프셋**: post 드로우의 viewport/scissor를 뷰포트 rect로 설정하고, HDR을
  풀스크린 삼각형이 아니라 뷰포트 rect에 매핑. 스왑체인의 나머지 영역은 UI가 채움. 추가 blit 없음.
- (대안) HDR을 스왑체인 서브렉트로 blit. 여분 blit 비용.

UI(`uiRenderer_->record`)는 스왑체인 전체 좌표로 패널을 그리므로 변경 최소. 뷰포트 rect 안쪽은 post가,
바깥쪽(패널)은 UI가 채웁니다. **주의**: 뷰포트 rect가 스왑체인보다 작으므로, 스왑체인의 초기 로드/
클리어가 필요합니다(뷰포트 밖이 UI로 완전히 덮이지 않는 한 프레임을 대비). 에디터 레이아웃상 뷰포트
바깥은 항상 패널이라 실무상 안전하나, 클리어를 명시해 미정의 픽셀을 없앱니다.

**TAA 히스토리 정합**: 히스토리를 renderExtent로 잡고 renderExtent HDR과 1:1로 섞습니다. HDR을 풀로
두고 서브렉트만 그리면 뷰포트 밖 stale 영역이 히스토리·블룸에 유입되어 오염되므로, HDR/깊이/AO/블룸/
히스토리를 renderExtent로 함께 축소하는 이 설계가 정합적입니다(가짜 TAA는 T-c로 곧 끄지만, 크기 정합은
블룸·향후 진짜 TAA에도 필요).

---

## 5. A-4 결정 (컬 집합 불변식)

A-4는 "GPU 컬이 아무것도 못 버린다"는 순수 오버헤드입니다. 정리 선택지 둘:

- **옵션 1 (권장, 이번 범위): GPU 컬 dispatch 제거, 깊이 프리패스도 CPU 집합으로.**
  CPU가 이미 프러스텀 컬링한 `view.opaqueItems`를 깊이 프리패스도 그대로 그리게 바꾸면(불투명 컬러와
  같은 경로), GPU cull dispatch·배리어·atomic이 통째로 사라집니다. "프리패스 집합==컬러 집합"이
  같은 CPU 리스트라 자명하게 성립. 낭비 제거. 단 GPU 인다이렉트 프리패스 → CPU 드로우 프리패스로
  경로 변경 필요(GpuScene/cull.comp 사용 중단, 향후 GPU 구동 재도입 시 복구).
- **옵션 2: CPU 프러스텀 컬 끄고 전량 업로드, GPU가 실제로 컬.**
  GPU 컬을 살리지만, 불투명 컬러 패스는 CPU 푸시-디스크립터 드로우라 GPU 컬 결과를 못 읽어
  **컬러 집합 = 전량 ≠ 프리패스 집합 = GPU 컬**이 되어 불변식이 깨집니다. 이를 살리려면 컬러 패스를
  GPU 인다이렉트(bindless 머티리얼)로 재설계해야 하는 큰 작업. 이번 범위 밖.

**권장**: 이번엔 옵션 1로 무효 GPU 컬을 제거해 단순화합니다. GPU 구동 렌더링은 별도 큰 트랙으로 두고,
그때 컬러 패스까지 인다이렉트로 올리며 GPU 컬을 되살립니다. (A-4를 뷰포트 작업과 분리해 나중으로
미루는 것도 가능하나, 둘 다 컬 집합 불변식을 건드리므로 한 번에 정리하는 편이 안전합니다.)

---

## 6. 리사이즈·뷰포트 변경 처리

- 창 리사이즈: 스왑체인 재생성(기존 경로). 이때 뷰포트 rect도 갱신 → renderExtent 타깃 재생성.
- 스플리터 드래그로 뷰포트만 바뀜: 스왑체인은 그대로, renderExtent 타깃만 재생성. 매 프레임 rect를
  비교해 변했을 때만 재생성(TAA 히스토리 `taaHistValid_` 무효화 포함).
- renderExtent 재생성은 HDR·깊이·AO·블룸·히스토리·관련 프레임버퍼·디스크립터를 함께 다시 만드는
  묶음이라, 스왑체인 재생성 로직을 참고해 `recreateRenderTargets(renderExtent)` 하나로 캡슐화합니다.

---

## 7. 단계별 구현 순서

1. `renderExtent_` 멤버 + `setViewportRect` 도입. 게임 빌드는 기본값 = 스왑체인 extent.
2. 씬 타깃 크기를 renderExtent로 전환(HDR·깊이·AO·Bloom·TAA 히스토리) + `recreateRenderTargets`.
3. 세 투영 aspect를 renderExtent로(세 곳 동시). 컬 집합 불변식 확인.
4. post 합성을 뷰포트 서브렉트로(viewport/scissor 오프셋) + 스왑체인 클리어.
5. `WorldViewport`를 뷰포트 rect 기준으로 전환(좌표 근본 해결). 기즈모·픽 검증.
6. A-4 옵션 1: GPU 컬 제거, 깊이 프리패스를 CPU 집합으로.
7. 각 단계 후 빌드 + 실기 확인(도형 클릭·기즈모 정합, 씬이 뷰포트에만 그려지는지, GPU 시간 감소).

각 단계는 독립 검증 가능하도록 나눕니다. 특히 3·5는 좌표 회귀(A-1에서 두 번 겪은 유형)를 만들 수
있으므로 실기 클릭 확인을 반드시 거칩니다.

---

## 8. 위험·미지수

- **불변식 파손**: 세 투영 중 하나라도 extent가 갈리면 DEF-25류(그리기 순서가 깊이를 이김) 재발.
  3번 단계에서 세 곳을 반드시 함께 바꿉니다.
- **VR 이득 0**: 뷰포트 렌더 자체는 데스크톱 에디터 전용(눈 버퍼 위엔 UI가 없음). 다만 renderExtent
  분리 배관은 향후 렌더스케일/FFR(2-K)의 선결 구조라 VR 관점의 재사용 가치가 있습니다.
- **좌표 회귀**: A-1에서 기즈모·픽이 좌표 불일치로 두 번 깨졌습니다. 5번에서 WorldViewport 전환 후
  실기 클릭 검증이 필수입니다.
- **A-4 옵션 1의 방향성**: GPU 구동 렌더링 야심을 일시 후퇴시킵니다. 향후 bindless 컬러 패스와 함께
  되살리는 것을 전제로 합니다.
