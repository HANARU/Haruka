# 비동기 로딩 · 레벨 전환 · 언로드 (2026-11-20)

[Engine_Foundation_Gaps.md](Engine_Foundation_Gaps.md) 의 2위 후보입니다. 지금은 glTF 읽기 · 디코드 · GPU 업로드가 모두 메인 스레드에서
동기로 돌고, 텍스처를 올릴 때마다 `vkQueueWaitIdle` 로 큐가 빌 때까지 기다리며, 메가 버퍼는 메시를 해제하지 못합니다. 게임은 시작 씬
하나만 엽니다. 이 문서는 작업 지시서가 아니라 **완료 조건 체크리스트**이고, 항목을 모두 만족하면 끝입니다.

## 1. 조사 요약

### 1-1. 지금 코드 (2026-11-20)

| 곳 | 지금 | 문제 |
|---|---|---|
| `AssetManager::loadAll` | 캐시(`.gasset`) 또는 glTF 를 메인 스레드에서 읽고 바로 업로드 | 큰 모델을 읽는 동안 프레임이 멈춤(XR 에서는 멀미 · 심사 문제) |
| `GpuScene::uploadMaterial` · `uploadTexture` | 한 명령 버퍼에 기록 → `RenderContext::endOneTimeCmd` 의 `vkQueueWaitIdle` | 머티리얼마다 GPU 가 빌 때까지 메인 스레드가 기다림 |
| `GpuScene::freeMaterial` | `vkDeviceWaitIdle` 뒤 바로 해제 | 해제할 때마다 장치 전체 대기 |
| 메가 버퍼(정점 128 MB · 인덱스 64 MB) | 범프 할당, `freeMesh` 는 핸들만 지움 | 씬을 바꿔도 공간이 돌아오지 않아 레벨을 여러 번 오가면 넘침 |
| 게임 앞단(`GameFrontend`) | `openInitialScene` 만 | 실행 중 씬 전환 API 가 없음 |
| [Threading.md](Threading.md) | 엔진 데이터는 메인 스레드 전용, 예약된 확장 "워커에서 I/O + 디코드, 메인에서 GPU 업로드" | 규칙 5(블로킹 금지 구역)의 알려진 위반이 glTF 동기 로드 |

### 1-2. 다른 엔진

- **Unreal:** 비동기 로딩 스레드(패키지 직렬화) + `FStreamableManager::RequestAsyncLoad`(완료 콜백), 레벨 스트리밍 · 심리스 트래블(전환 중에도
  작은 전환 맵을 계속 그림), 로딩 화면은 별도 스레드의 MoviePlayer. VR 은 OpenXR 레이어(정지 화면)로 머리 추적을 유지합니다.
- **Unity:** `SceneManager.LoadSceneAsync` + `allowSceneActivation`(다 읽은 뒤 바꾸는 시점을 게임이 정함), 비동기 업로드 파이프라인의
  **프레임당 업로드 시간 조각**(`QualitySettings.asyncUploadTimeSlice`, 기본 2 ms)과 링 버퍼.
- **Godot:** `ResourceLoader.load_threaded_request` → `load_threaded_get_status`(진행률) → `load_threaded_get`. 워커 스레드가 디코드하고 GPU 자원은
  메인(렌더) 스레드에서 만듭니다.
- **Meta Quest 지침:** 로딩 중에도 매 프레임 제출을 멈추지 말 것(멈추면 런타임이 마지막 프레임을 재투영하다 검은 화면 · 심사 실패),
  필요하면 컴포지터 레이어로 로딩 화면.

### 1-3. 공통 구조

"워커 스레드에서 파일 읽기 · 디코드 → 메인 스레드에서 프레임마다 정해진 예산만큼 GPU 업로드 → 다 되면 게임이 정한 시점에 바꿈 → 쓰지
않는 자원을 GPU 가 다 쓴 뒤 해제". 세 엔진 모두 이 순서이고, 이 구조는 [Threading.md](Threading.md) 의 규칙 1 ~ 3(엔진 데이터는 메인, 교차는
뮤텍스 큐)과 맞습니다.

## 2. 결정

- **D1 백그라운드 읽기:** `AssetStreamer`(assets) 가 워커 스레드 N 개(기본 2, Android 1)로 VFS 읽기 · 캐시 읽기 또는 glTF 디코드 · 캐시 쓰기만
  합니다. 결과(`MeshSourceData` 목록)는 뮤텍스 큐로 메인에 넘깁니다. 워커는 World · 렌더러 · Vulkan 을 만지지 않습니다(규칙 1).
  같은 경로를 두 번 요청하면 한 작업을 함께 씁니다.
- **D2 프레임 예산 업로드:** 메인 스레드의 `AssetManager::pump(budgetMs)` 가 완료된 결과를 업로드합니다. 기본 예산 2 ms(Unity 기본값),
  예산을 넘기면 다음 프레임으로 미룹니다. 동기 `loadAll` 은 그대로 두되, 같은 경로가 진행 중이면 그 작업을 기다려 결과를 씁니다(두 번 읽지 않음).
- **D3 대기 없는 업로드:** 텍스처 업로드는 펜스와 함께 제출하고 스테이징 버퍼 · 명령 버퍼는 펜스가 끝난 뒤 놓습니다(`vkQueueWaitIdle` 없음).
  렌더러와 같은 그래픽 큐에 먼저 제출하고 업로드 끝 배리어가 프래그먼트 셰이더 읽기를 막으므로, 뒤 프레임의 그리기는 순서가 보장됩니다.
- **D4 지연 해제:** `freeMesh` · `freeMaterial` 은 바로 지우지 않고 그 프레임 번호와 함께 은퇴 목록에 넣고, `MAX_FRAMES` 프레임 뒤(그 프레임을
  쓴 제출이 끝난 뒤) 지웁니다. `vkDeviceWaitIdle` 을 없앱니다. 종료 때는 장치 대기 뒤 모두 지웁니다.
- **D5 메가 버퍼 재사용:** 정점 · 32비트 인덱스 · 16비트 인덱스 구역마다 빈 구간 목록(첫 맞춤, 이웃 합치기) 할당자를 둡니다. 해제한 메시
  구간은 은퇴가 끝나면 돌아옵니다. 스킨 구간은 한 번 잡고 놓지 않습니다.
- **D6 씬 전환:** `IEngineFrontend::requestScene(path)` → 씬 글을 읽고 참조한 모델(`hrefs::refsInText`)을 미리 요청 → 기다리는 동안 지금 씬을
  계속 틱 · 그림(XR 제출 유지) → 모두 올라가면 한 프레임에 바꿈(액터를 모두 지우고 `readScene`, 이때 모델은 캐시에 있음) → 새 씬이 쓰지 않는
  모델을 해제(`AssetManager::releaseUnused`). 게임 실행 파일에 넣고, 에디터는 지금처럼 동기로 엽니다(편집 Undo 가 경로로 다시 읽으므로 해제하지 않음).
- **D8 정점 캐시 순서를 가져올 때로(측정 뒤 추가):** 첫 측정에서 큰 모델 하나(정점 9만 · 삼각형 18만, LOD 셋)의 업로드가 메인 스레드에서
  250 ms 였고, 그중 225 ms 가 업로드 때마다 하던 정점 캐시 정렬(O3)이었습니다. 정렬을 glTF 가져오기(워커)로 옮기고 결과를 캐시(v11)에 담아,
  업로드는 정렬을 건너뜁니다(`MeshSourceData::cacheOrdered`). 같은 함수를 같은 입력에 한 번 쓰므로 결과 순서는 예전과 같습니다(기준 PNG 그대로).
- **D7 범위 밖:** OpenXR 컴포지터 레이어 로딩 화면, 씬 일부만 더하는 스트리밍(서브레벨), 텍스처 압축 · 밉 오프라인 생성(7위 후보), 업로드 전용
  전송 큐.

## 3. 체크리스트

### L1 업로드 · 해제 (렌더러)
- [x] L1-1 텍스처 · 머티리얼 업로드가 큐 대기 없이 끝남(제출 뒤 바로 돌아옴), 스테이징은 펜스가 끝난 뒤 해제. 검증 오류 0.
- [x] L1-2 `freeMesh` · `freeMaterial` 이 `MAX_FRAMES` 뒤 해제(은퇴 목록), 종료 때 남은 것을 모두 해제(누수 0, 검증 오류 0).
- [x] L1-3 구간 할당자: 첫 맞춤 · 이웃 합치기 · 넘침 거절 · 사용량, 무작위 할당 · 해제 1만 번에서 겹침 0(단위 시험).
- [x] L1-4 메가 버퍼가 구간 할당자를 씀: 메시를 지우고 다시 올리면 같은 공간을 다시 씀(사용량이 원래대로).

### L2 백그라운드 읽기 · 예산 업로드
- [x] L2-1 `AssetStreamer`: 워커 스레드에서 읽은 결과가 동기 `GltfLoader` · 캐시 결과와 같음(정점 · 인덱스 · 스킨 · 클립).
- [x] L2-2 같은 경로 중복 요청은 작업 하나, 없는 파일은 실패 상태(엔진은 기본 상자로), 종료 때 진행 중 작업을 기다리고 끝남.
- [x] L2-3 여러 경로를 동시에 요청해도 결과가 맞음(스레드 안전: VFS · 캐시 쓰기 · 로그).
- [x] L2-4 `AssetManager::requestLoad` · `isReady` · `pump(budget)`: 예산을 넘기면 다음 프레임으로, 동기 `loadAll` 이 진행 중 작업을 기다려 씀.

### L3 씬 전환 · 언로드
- [x] L3-1 게임에서 씬 전환 요청 → 모델을 미리 읽는 동안 지금 씬이 계속 그려짐 → 다 되면 바뀜. 로그에 요청 · 준비 프레임 수 · 바꾼 시간.
- [x] L3-2 바꾼 뒤 새 씬이 쓰지 않는 모델이 해제되고, A → B → A 를 여러 번 오가도 메가 버퍼 사용량이 늘지 않음.
- [x] L3-3 전환으로 연 씬의 PNG = 처음부터 그 씬을 연 PNG(`--fixed-dt`).
- [x] L3-4 전환 중 프레임 시간: 큰 모델을 읽는 동안 가장 긴 프레임이 동기 방식보다 짧음(같은 장면 비교, 수치 기록).

### G 공통
- [x] G-1 Release · Debug 경고 0, ctest 두 구성 통과, `HarukaArchTest` PASS.
- [x] G-2 기준 PNG 7장 그대로, UI 덤프 기존 장면 그대로.
- [x] G-3 Android 빌드 통과(워커 1개), 바꾼 파일의 경고 0.
- [ ] G-4 (헤드셋) Quest 에서 씬 전환 중 화면이 멈추지 않음.

## 4. 결과 (2026-11-20)

| 항목 | 결과 |
|---|---|
| 빌드 | Release · Debug 경고 0 |
| ctest | 두 구성 33/33 통과, 검사 2,611(새 `async_loading` 30) |
| 아키텍처 | `HarukaArchTest` PASS. `AssetStreamer` 는 assets, `RangeAllocator` 는 renderer(헤더만), `SceneTransition` 은 runtime |
| 기준 PNG 7장(`base7.sh asy`, Debug 다시 빌드 뒤) | 모두 그대로(ce6def24 · 73dd1f02 · c283f276 · b4bd1fee · 5d6e3e5f · 9cf28353 · 201fad0e), 검증 오류 0 |
| UI 덤프(`uidump.sh asy an`) | 54장면 모두 UI · 뷰포트 같음(업로드 시간 로그는 Verbose 로 두어 출력 로그 칸이 바뀌지 않음) |
| 씬 순환(L3-2) | AnimProbe → HandProbe → AnimProbe → HandProbe → AnimProbe → HandProbe(`--switch-scene`): 메가 버퍼가 Anim 상태 65,584 정점 · Hand 상태 68,694 정점으로 매번 같은 값, 은퇴 목록 0, 검증 오류 0 |
| 전환 PNG(L3-3) | AnimProbe 에서 HandProbe 로 전환한 PNG = 처음부터 HandProbe 를 연 PNG(cb7b3478) |
| 큰 모델 전환(L3-4) | 사본 프로젝트(`build/chk/async/proj`, 정점 9만 · 2048² 텍스처 두 장 모델, GPU 1, `--fixed-dt`). 캐시 없음: 동기는 한 프레임 592 ms 멈춤, 비동기는 638 ms 동안 3,119 프레임을 계속 그리고 가장 긴 프레임 26 ms. 캐시 있음: 동기 59 ms 멈춤, 비동기 가장 긴 프레임 26 ms(60 ms 동안 계속 그림) |
| 업로드(D8) | 그 모델의 메인 스레드 업로드 250 ms → 23 ms(메시 6.5 · LOD 1.1 · 나머지는 2048² 텍스처 두 장 32 MB 복사) |
| Android | `build/android` 빌드 통과(워커 1개), 바꾼 파일의 새 경고 0 |
| 찾아 고친 것 | 전환으로 지운 액터를 그 프레임의 시점(카메라 · 리그 포인터)이 가리켜 크래시(크래시 보고서로 바로 찾음) → 전환을 프레임 처음, 시점 계산 전으로 옮김. 창 없는 실행은 앞단 tick 을 건너뛰므로 전환 갱신을 따로 부름 |

**새 파일:** `src/renderer/RangeAllocator.hpp`, `src/assets/AssetStreamer.hpp/.cpp`, `src/runtime/SceneTransition.hpp/.cpp`, `tests/LoadingTest.cpp`.

**바꾼 곳:** `GpuScene`(구간 할당자 · 펜스 업로드 · 은퇴 목록 · 정렬 건너뛰기), `RenderContext`(`submitOneTimeCmdAsync` · `freeOneTimeCmd`),
`Renderer`(`freeMesh` · `freeMaterial` = 은퇴, 프레임마다 정리), `AssetManager`(`requestLoad` · `isReady` · `pump` · `releaseUnused`, 동기 `loadAll` 이 진행 중
작업을 기다림), `GltfLoader`(가져올 때 정점 캐시 정렬), `GassetCache`(v11), `MeshSource`(`cacheOrdered`), `Engine`(프레임 처음 `streamAssets`,
`--switch-scene` · `--switch-sync`, 워커 수), `IEngineFrontend`(`requestScene` · `updateSceneTransition`), `EngineContext::requestScene`, `GameFrontend`.

**쓰는 법:** 게임 모듈에서 `ctx.requestScene("/Game/scenes/Next.hscene")`. 모델은 백그라운드에서 읽고, 다 올라가면 다음 프레임 처음에 바꾸며, 새 씬이
쓰지 않는 모델은 해제됩니다. 미리 읽어 두려면 `ctx.assets.requestLoad(path)`.

**남긴 것(범위 밖 · 다음 후보):**
- 큰 텍스처를 여러 프레임에 나눠 올리기(지금은 모델 하나를 한 프레임에 올림, 2048² 두 장 = 약 23 ms). 텍스처 압축(기반 공백 7위)과 함께 다루면 좋습니다.
- OpenXR 컴포지터 레이어 로딩 화면, 씬 일부 스트리밍, 전송 전용 큐, 에디터 씬 열기의 비동기화와 해제(Undo 가 경로로 다시 읽음).
- 버려진 전환이 요청한 모델은 다음 전환 때 해제됩니다(그 사이에는 남음).
- (헤드셋) G-4.
