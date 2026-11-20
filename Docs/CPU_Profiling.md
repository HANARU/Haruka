# CPU 구간 계측 · 추적 파일 (2026-11-20)

[Engine_Foundation_Gaps.md](Engine_Foundation_Gaps.md) 의 5위 후보입니다. 지금은 고정 18구간의 평균(`FrameTimings`)과 GPU 타임스탬프만 있어서,
"어느 함수가 · 어느 스레드에서 · 몇 번째 프레임에" 시간을 썼는지 볼 수 없습니다. 이 문서는 작업 지시서가 아니라 **완료 조건 체크리스트**이고,
항목을 모두 만족하면 끝입니다.

## 1. 조사 요약

### 1-1. 지금 코드 (2026-11-20)

| 곳 | 지금 | 문제 |
|---|---|---|
| `core/FrameTimings.hpp` | 고정 18구간(펜스 · 획득 · 기록 · 제출 · 표시 · 틱 · 물리 …), 지수 이동 평균 + 이번 프레임 값, `ScopedCpuTimer` | 구간이 고정이고 이름이 없음. 메인 스레드 전용 |
| `runtime/FrameStats` · `FramePacing` | 프레임 시간 분포 · 끊김 로그 20건 · XR 세션 끝의 중앙값 · 99분위 | 끊긴 프레임 안에서 무엇이 길었는지 모름 |
| 렌더러 GPU 타임스탬프 | shadow · main · ao · bloom · tonemap · ui · total | GPU 쪽만 |
| 에디터 `GpuStatsPanel` | 위 두 가지를 표로 보임 | 추적 파일이 없어 나중에 볼 수 없음 |
| 다른 스레드 | 에셋 워커(`AssetStreamer`), IBL · 반사 캡처 면별 스레드 5개 | 계측이 하나도 없음 |
| CVar · 콘솔(`CVarRegistry`) | 엔진 명령을 `addCommand` 로 등록, `--console-at` 로 창 없이 실행 | 계측 시작 · 끝 명령이 없음 |

### 1-2. 다른 엔진 · 도구

- **Unreal:** `TRACE_CPUPROFILER_EVENT_SCOPE(Name)` · `SCOPED_NAMED_EVENT` 가 스레드별 버퍼에 기록하고, Unreal Insights 가 `.utrace` 를 읽습니다.
  명령줄 `-trace=cpu,frame` 과 콘솔 `Trace.Start` · `Trace.Stop` 로 켜고 끕니다. `CPUPROFILERTRACE_ENABLED` 가 꺼지면 매크로가 비고, Shipping
  은 기본으로 끕니다.
- **Unity:** `ProfilerMarker.Auto()` · `Profiler.BeginSample`. 개발 빌드에서만 기록하고, 일반 빌드에서는 호출이 빈 함수가 됩니다. 프로파일러
  창이 실행 중인 플레이어에 붙어 프레임별 계층을 봅니다.
- **Godot:** 디버거의 Profiler 탭(함수 · 프레임별 시간). 디버그 템플릿에서만 동작합니다.
- **Tracy:** `ZoneScoped` · `ZoneScopedN("name")`, 이름은 정적 위치 정보(`static constexpr SourceLocationData`), 스레드별 무잠금 큐,
  `TRACY_ENABLE` 가 없으면 매크로가 비어 비용 0 입니다.
- **Chrome Trace Event Format:** JSON 의 `traceEvents` 배열. `ph:"X"`(시작 `ts` · 길이 `dur`, 마이크로초), `ph:"M"` 의 `thread_name` ·
  `process_name` 메타. `chrome://tracing` 과 Perfetto UI(ui.perfetto.dev)가 그대로 읽습니다.

### 1-3. 공통 구조

"정적 수명의 이름을 단 RAII 스코프 → 스레드별 버퍼에 (이름, 시작, 끝) 기록 → 수집해 파일 · 도구로 → 컴파일 스위치로 매크로를 비우고, 켠 빌드는
기록 중일 때만 시간을 읽음". 외부 보기 도구(Insights · Tracy · Perfetto)에 맡기고 엔진은 기록과 파일만 맡습니다.

## 2. 결정

- **D1 매크로:** `HPROF_SCOPE("이름")`(블록 끝까지), `HPROF_FUNCTION()`(`__func__`), `HPROF_THREAD_NAME("이름")`(이 스레드의 표시 이름, 글은
  복사). 스코프 이름은 **정적 수명**이어야 합니다(글자 그대로 · `__func__` · `IEngineModule::name()` 처럼 프로그램 끝까지 사는 글). 기록에는
  포인터만 남기므로 기록 비용이 이름 길이와 상관없습니다. 이름은 `분류.구간`(예 `Engine.Tick`, `Renderer.Submit`)으로 씁니다.
- **D2 컴파일 스위치와 기본값:** CMake 옵션 `HARUKA_PROFILER`(기본 **ON**, Release · Debug · Android 모두). 끄면 `HARUKA_PROFILER=0` 이 정의되고
  매크로가 `static_cast<void>(0)` 이 됩니다(코드 · 시간 비용 0, 끈 번역 단위의 `constexpr` 함수 안에서 매크로를 쓸 수 있는 것으로 시험이 확인).
  **근거:** Quest 실측은 Release 로 해야 하므로 Release 에서 빠지면 도구가 쓸모없습니다. 켠 빌드도 **기록하지 않을 때는 스코프마다 원자 변수
  한 번(relaxed load)과 분기 하나**뿐이고 시간을 읽지 않습니다(4절 측정). 기록은 기본으로 꺼져 있고, 명령줄 · 콘솔로 켜고 끕니다. 배포용
  빌드가 생기면 그 구성에서 옵션을 끕니다(UE Shipping 과 같은 자리).
- **D3 기록:** 스레드마다 버퍼 하나(`thread_local`)에 `{이름 포인터, 시작 ns, 끝 ns}`(24 바이트)를 스코프가 **끝날 때** 넣습니다. 버퍼의
  잠금은 그 스레드만 잡으므로(수집할 때만 다툼) 경합이 없습니다. 전역 잠금은 스레드가 처음 기록할 때(등록)와 시작 · 끝에만 잡습니다.
  스레드당 100만 건을 넘으면 버리고 버린 수를 셉니다. 스레드가 끝나도 버퍼는 수집할 때까지 남습니다(IBL 면별 스레드처럼 짧은 스레드).
- **D4 시계:** `std::chrono::steady_clock`(FrameTimings 와 같음). 그래서 기존 구간과 같은 두 시각을 그대로 나눠 쓸 수 있습니다(D7).
- **D5 추적 파일:** Chrome Trace Event JSON. `ph:"X"` 완료 이벤트(`ts` · `dur` 마이크로초, 소수 셋째 자리), `pid` 1, `tid` 는 등록 순서 번호,
  `ph:"M"` 메타로 `process_name`(Haruka) · `thread_name` · `thread_sort_index`. 스레드마다 (시작 오름차순, 끝 내림차순)으로 정렬해 같은 시각에
  시작한 부모가 자식보다 먼저 나옵니다. `otherData` 에 버린 이벤트 수를 적습니다. 시각은 기록을 시작한 때를 0 으로 합니다.
- **D6 켜고 끄기:** 둘 다 둡니다.
  - 명령줄 `--trace=<파일>`: 엔진을 만드는 첫 줄부터 끝날 때까지 기록합니다(시작 · IBL 굽기 · 첫 씬 읽기 포함). `--trace-frames=<a>-<b>` 를
    함께 주면 프레임 a 를 그린 뒤 시작해 프레임 b 를 그린 뒤 멈추고 씁니다.
  - 콘솔 명령 `trace.start [파일]` · `trace.stop`(에디터 콘솔 줄 · `--console-at` 이 같은 길). 파일을 안 주면 `trace_<날짜_시각>.json`.
  - 파일은 언제나 **쓰기 루트의 `Traces/` 아래**입니다. 상대 경로만 받고 `..` · 절대 경로는 거절합니다.
- **D7 FrameTimings 와의 관계: 공존, 측정 지점은 공유.** `FrameTimings::Avg` 에 정적 이름(`Renderer.Submit` 등)을 붙이고, `ScopedCpuTimer` 와
  수동 측정(`record`)이 평균에 더하는 **같은 두 시각**으로 추적 이벤트도 남깁니다(시계를 두 번 읽지 않음). FrameTimings 는 화면 표 · 끊김 로그용
  실시간 평균으로 남기고, 그 구간에는 `HPROF_SCOPE` 를 따로 넣지 않습니다.
- **D8 넣은 지점:** 프레임(`Frame`), `Engine::run` 의 단계(스트리밍 · XR 시작 · 시점 · 입력 · Play 상태 · 틱 · 앞단 입력 · 렌더 · 루프 끝),
  월드 틱 · 물리 뒤 틱 · 모듈별 틱(모듈 이름, 물리 · 소리), 물리 한 걸음, 렌더(추출 · 기록 · 그림자 · 메인 패스 · 후처리 · UI · 제출 · 표시 ·
  펜스 · 획득), 에셋 업로드 · 디코드(워커), IBL 굽기와 면별 스레드, 반사 캡처, 씬 읽기.
- **D9 전역 상태:** 기록기는 `HLog` 과 같은 **진단용 프로세스 전역**입니다. 워커 · IBL 스레드처럼 `EngineContext` 를 받지 않는 스레드에서도
  기록해야 하므로 시스템으로 꿰지 않습니다. `static T& get()` 형 싱글턴이 아니라 자유 함수(`hprof::start` 등)이고, 게임 · 렌더링 로직이
  기록기에 의존하면 안 됩니다(읽는 쪽은 추적 파일뿐).
- **D10 범위 밖:** Tracy 연동(외부 의존을 넣지 않음. 매크로를 `ZoneScopedN` 으로 바꾸는 층은 나중 선택지), 에디터 CPU 스코프 표(선택 항목, 5절 남음),
  Android 추적 파일 꺼내기 도구(5절), 메모리 할당 추적 · 잠금 대기 추적 · GPU 타임스탬프를 같은 파일에 넣기.

## 3. 체크리스트

### P1 기록기 (core)

- [x] P1-1 `HPROF_SCOPE` 중첩: 자식의 [시작, 끝] 이 부모 안, 같은 스레드에서 끝난 순서대로 기록, 정렬 뒤 부모가 먼저.
- [x] P1-2 스레드 구분: 두 워커 스레드의 이벤트가 다른 `tid`, `HPROF_THREAD_NAME` 이 `thread_name` 메타로, 끝난 스레드의 기록도 수집됨.
- [x] P1-3 기록하지 않을 때는 이벤트 0, 시작 → 멈춤 → 다시 시작하면 앞 기록이 남지 않음, 상한을 넘으면 버리고 센 수가 파일에.
- [x] P1-4 JSON: 파서로 다시 읽어 `traceEvents` · `ph` · `ts` · `dur` · `pid` · `tid` · 메타 확인, 이름 안의 따옴표 · 역슬래시 이스케이프.
- [x] P1-5 끈 번역 단위(`HARUKA_PROFILER 0`): 매크로를 `constexpr` 함수 안에서 쓸 수 있음(`static_assert`), 기록 중에 불러도 이벤트 0.
- [x] P1-6 `ScopedCpuTimer` 가 평균과 같은 시각으로 이름 붙은 이벤트를 남김(공유 측정 지점).

### P2 엔진 (runtime · renderer · assets · physics)

- [x] P2-1 `--trace=<파일>` · `--trace-frames=<a>-<b>` 옵션, 틀린 값은 모르는 옵션으로 알림.
- [x] P2-2 콘솔 `trace.start [파일]` · `trace.stop`, 이미 기록 중 · 기록 중 아님 · 틀린 경로를 알림.
- [x] P2-3 D8 의 지점에 스코프, 워커 · IBL 스레드 이름.
- [x] P2-4 창 없는 실행으로 기본 장면 `trace.json` 을 만들고 스크립트로 확인: 유효한 JSON, 메인 · 워커 스레드 이벤트가 모두 있음, 프레임 수가 맞음.
- [x] P2-5 비용: 600 프레임 CPU 프레임 시간(기록 안 함 · 기록 함), 증가 < 0.05 ms 목표.

### G 공통

- [x] G-1 Release · Debug 경고 0, ctest 전부 통과(새 시험 포함), 아키텍처 PASS(정적 포인터 0, 싱글턴 목록 그대로, 허용 목록 0).
- [x] G-2 기준 PNG 7장 그대로, UI 덤프 같음.
- [x] G-3 Android 빌드 통과.

## 4. 결과 (2026-11-20)

| 항목 | 결과 |
|---|---|
| 빌드 · 시험 | Release · Debug 경고 0, ctest 두 구성 36/36 통과, 검사 2,885(새 `cpu_profiling` 85), 아키텍처 PASS(정적 포인터 0, 싱글턴 목록 그대로, 허용 목록 0), 기준 PNG 7장 그대로 · 검증 오류 0, UI 덤프 57장면 같음, Android 빌드 통과(새 경고 없음) |
| 새 시험 `cpu_profiling` | 85 검사(P1-1 ~ P1-6, 추적 경로 · 기록기 · 콘솔 · 프레임 구간). 변이 확인: 정렬을 빼면 "같은 시작이면 부모 먼저" · "JSON 안 순서"가, 이스케이프를 빼면 "다시 읽힘"이 실패 |
| 끈 번역 단위 | `ProfilerOffTest.cpp`(`HARUKA_PROFILER 0`)의 `constexpr` 함수 안에서 세 매크로를 쓰고 `static_assert` 통과, 매크로 식의 형이 `void`. 기록 중에 불러도 이벤트 0 |
| 기본 장면 `--trace` | 120 프레임: 이벤트 3,491개, 스레드 6개(Main · IBL face 1 ~ 5), 파일 335 KB, `Frame` 스코프 121개(그린 120 + 루프를 끝내는 1). 스레드별 구간 어긋남 0 |
| 시작 구간 | `Engine.Startup` 900 ms 중 `Startup.Renderer` 779 ms(Vulkan · 파이프라인 · `IBL.Bake` 9 ms + 면별 스레드 각 약 5 ms) · `Startup.Modules` 82 ms · `Startup.FirstScene` 36 ms(`Scene.Read`) |
| 씬 전환 `--switch-scene` | 에셋 워커 스레드 둘에 `Asset.Decode` 2건(합 16.7 ms), 메인에 `Asset.Pump` 90건 · `Scene.Read` 2건 |
| `--trace-frames=100-200` | 프레임 100 을 그린 뒤 시작해 200 뒤 씀: `Frame` 99개(101 ~ 199, 시작 전에 열린 100 과 멈춘 뒤 닫힌 200 은 빠짐), 이벤트 2,898개(프레임당 약 29) |
| 콘솔 | `--console-at=50:trace.start console.json` · `80:trace.stop` → `Frame` 29개. 기록 중 아닐 때 `trace.stop` · `..` 경로의 `trace.start` 는 알리고 거절 |
| 스코프 비용(단위 시험, Release) | 기록하지 않을 때 1.1 ns, 기록할 때 63 ~ 66 ns(잠금 · 시계 두 번 · 넣기) |
| 프레임 비용(기본 장면, GPU 1, 600 프레임 × 3번) | CPU 프레임 중앙값: 기록 안 함 0.44 · 0.44 · 0.44 ms, 기록 함 0.44 · 0.44 · 0.44 ms. 99분위 0.55 ~ 0.59 대 0.55 ~ 0.63(실행마다 흔들림). 프레임당 이벤트 약 29개 × 65 ns ≈ 0.002 ms 이고, 기록하지 않을 때는 29 × 1.1 ns ≈ 0.00003 ms 라 목표(< 0.05 ms) 안 |

**끈 빌드와의 비교:** 끈 빌드는 매크로가 사라지므로 켠 빌드(기록 안 함)와의 차이는 스코프마다 원자 변수 읽기 하나(위 1.1 ns)입니다. 옵션을 꺼서
빌드 폴더를 다시 구성하는 측정은 하지 않았습니다(일괄 작업 규칙: 빌드 폴더를 새로 구성하지 않음).

**보는 법:** 쓰기 루트의 `Traces/<파일>` 을 ui.perfetto.dev 에 끌어 놓거나 `chrome://tracing` 에서 Load 합니다. 메인 스레드에서 `Frame` 아래로
`Engine.*` 단계, `Renderer.DrawFrame` 아래로 펜스 · 기록 · 제출 · 표시가 겹쳐 보입니다.

## 5. 남음

- **[선택] 에디터 CPU 스코프 표:** `GpuStatsPanel` 옆에 이번 프레임 · 평균 상위 N 구간. 기록기가 늘 켜져 있어야 하므로 "가벼운 상시 모드"(프레임마다
  집계만 하는 버퍼)를 따로 설계해야 합니다. 이번에는 하지 않았습니다.
- **Android 추적 파일 꺼내기:** 쓰기 루트는 외부 앱 폴더이므로 `adb pull /sdcard/Android/data/<패키지>/files/Traces/<파일>` 로 가져옵니다. 헤드셋에서
  켜는 방법(명령줄 대신 `cvars.ini` · 콘솔)과 실측은 헤드셋 확인 때.
- **Tracy 연동(나중 선택지):** `HPROF_SCOPE` 를 `ZoneScopedN` 으로 바꾸는 층만 더하면 됩니다. 외부 의존이라 이번 범위 밖.
- 메모리 할당 · 잠금 대기 추적, GPU 타임스탬프를 같은 파일에 넣기, `Frame` 마다 프레임 번호 인자(`args`).
- 창 · 헤드셋 확인: Perfetto · Chrome 추적 보기로 파일을 열어 중첩 · 스레드 이름 확인, 에디터 콘솔 줄의 `trace.start` · `trace.stop`, Quest 실측.
