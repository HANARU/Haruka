# 로그 카테고리 설계 (Log Category Design)

> **상태: 구현 완료 (2026-09-25).** §11 의 결정 5건을 권장안대로 확정했고, L0 ~ L4 를 모두 마쳤습니다.
> 구현하면서 바뀐 부분은 해당 절에 "구현에서 설계를 바꿨습니다" 로 표시했습니다. 남은 기능 후보는 §12 입니다.
>
> 관련 문서: [Editor_Layout_Design.md](Editor_Layout_Design.md) §7.11(상태바),
> [Threading.md](Threading.md) 규칙 4(로그 스레드 안전성),
> [Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) DEF-61·DEF-70 ~ DEF-72.

---

## 1. 목표와 비목표

이 설계는 세 가지 요구를 한 체계로 해결합니다.

1. **임의의 카테고리를 코드 한 줄로 선언하고 그 카테고리로 로그를 남깁니다.** 새 카테고리를 만들 때
   중앙의 enum 이나 목록을 고치지 않아도 되어야 합니다. 게임 모듈도 같은 방식으로 자기 카테고리를 만듭니다.
2. **카테고리마다 어디까지 기록할지(기록 수준)를 실행 중에 바꿉니다.** 꺼진 로그는 `<<` 오른쪽의 인자조차
   평가하지 않습니다.
3. **Output Log 에서 무엇을 볼지(표시 필터)를 카테고리·심각도·검색어로 고릅니다.** 필터를 바꾸면 이미
   쌓인 로그에도 소급 적용됩니다.

다음 항목은 이 설계의 범위가 아닙니다.

- 콘솔 명령 체계([Editor_Layout_Design.md](Editor_Layout_Design.md) §7.11 에서 선행 조건 부재로 기각했습니다).
- 구조화 로그(JSON), 네트워크 전송, 카테고리별 파일 분리.
- 비동기 로깅 스레드(§5.7 에서 근거와 함께 기각합니다).
- 한글 로그의 화면 렌더(폰트 아틀라스가 ASCII 전용이며, 폰트 확장은 별도 과제입니다).

---

## 2. 현재 상태 (2026-09-17 실측)

### 2.1 로그가 흐르는 경로

- **71개 호출부(20개 파일)가 `std::cout`/`std::cerr` 에 `[Tag]` 접두어를 직접 씁니다.** 접두어는 관례일
  뿐이고 강제하는 장치가 없습니다.
- `EditorApp::init` 이 두 스트림의 streambuf 를 `LogStreamBuf` 로 교체해 줄 단위로 가로챕니다. 가로챈 줄은
  `EditorContext` 의 200줄 deque 에 들어가고, `logDirty` 플래그가 켜집니다.
- `EditorApp::tick` 은 `logDirty` 가 켜져 있으면 `rebuildBottomPanel()` 을 호출합니다. Output Log 는 마지막
  N줄을 `STextBlock` 으로 새로 만들고, 상태바는 마지막 한 줄을 잘라 표시합니다.
- `run.bat` 은 exe 출력을 PowerShell `Tee-Object` 로 `Logs/latest.txt` 에 기록합니다. 콘솔 없이 실행하면
  `main.cpp` 가 stdout 을 작업 디렉터리의 `HarukaEngine.log` 로 돌립니다.

### 2.2 태그 현황: 20종, 분류 기준 없음

같은 줄에 태그가 있는 호출부를 센 결과입니다.

| 태그 | 수 | 문제 |
|---|---|---|
| `[Haruka]` | 14 | 할당자 존 준비(8), GPU 이름·MSAA·타임스탬프 쿼리·파이프라인 핫리로드(4), FATAL·정상 종료(2)가 한 태그에 섞여 있습니다 |
| `[Editor]` | 12 | |
| `[Asset]` · `[AssetManager]` · `[GassetCache]` | 5 · 4 · 4 | 같은 에셋 계층이 태그 셋으로 나뉘어 있습니다 |
| `[Prefab]` | 5 | |
| `[Renderer]` · `[GpuScene]` · `[Swapchain]` · `[Vulkan]` | 3 · 2 · 1 · 1 | `GpuScene.cpp` 는 `[GpuScene]` 과 `[Renderer]` 를 섞어 씁니다 |
| `[Physics]` · `[Audio]` | 3 · 3 | |
| `[Scene]` | 2 | `SceneSerializer` 와 `DefaultGameModule` 이 함께 씁니다 |
| `[Primitive]` | 2 | `AssetManager` 와 `PrimitiveBuilder` 가 함께 씁니다 |
| `[Input]` | 2 | 키 입력 로그. 2026-09-17 에 비활성화했습니다 |
| `[HotReloader]` · `[HFontAtlas]` · `[HSlate]` | 2 · 2 · 1 | |
| `[DefaultGameModule]` · `[Actor]` | 1 · 1 | |

나머지 한 곳(`GltfLoader.cpp` 의 머티리얼 경고)은 `std::cout` 과 태그가 다른 줄에 있고, 한 문장 안에서
`[Asset]` 접두어를 7번 반복해 여러 줄을 출력합니다.

### 2.3 결함 (결함 정본에 등재)

| ID | 내용 |
|---|---|
| DEF-70 | 로그 한 줄마다 하단 패널 전체를 리빌드합니다. Content Browser 탭이 활성이어도 리빌드되어, 그 사이에 누른 버튼의 프레스 상태가 사라집니다 |
| DEF-71 | 가로채기를 `EditorApp::init` 에서 설치해, 그 전의 부팅 로그(GPU·MSAA·힙 존·BRDF LUT·IBL·오디오·물리)가 Output Log 에 없습니다 |
| DEF-72 | 심각도를 부분 문자열(`ERROR`/`WARN`/`FATAL`)로 판정하는데, 71곳 중 이 단어를 포함하는 곳은 2곳뿐입니다. 오류가 사실상 전부 회색으로 표시됩니다 |
| DEF-61 | Output Log 의 표시 줄 수를 도킹 이후 갱신되지 않는 상수 `bottomH_` 로 계산합니다 |

### 2.4 설계가 감안해야 하는 제약

- **비ASCII 로그:** 9개 문장이 한글을 포함합니다. 폰트 아틀라스가 ASCII 전용이라 Output Log 에서 빈 칸으로
  그려집니다. 상태바는 이미 `?` 로 치환하고 있습니다.
- **`Logs/latest.txt` 는 UTF-16 LE 로 저장됩니다.** Windows PowerShell 5.1 `Tee-Object` 의 기본 인코딩이며,
  실제 파일의 BOM 으로 확인했습니다. 일반 텍스트 도구로 검색하기 불편합니다. exe 가 내보내는 UTF-8 한글이
  콘솔 코드페이지 해석을 거치며 깨지는지는 아직 확인하지 않았습니다.
- **HSlate 에는 클리핑과 위젯 휠 라우팅이 없습니다**(DEF-56·DEF-57). 스크롤 목록은 아웃라이너처럼
  `EditorApp::tick` 의 단일 휠 판독 지점에서 직접 히트테스트해야 합니다. (2026-10-03 해소: 클리핑과 위젯 휠 경로가
  생겼고, Output Log 행 영역은 `SBorder::onWheel` 로 휠을 받습니다. §7.4 참고.)
- **`STextBlock` 의 색은 값으로 고정되어 있습니다.** 텍스트는 `Attr` 로 매 프레임 바꿀 수 있지만 색은 그렇지
  않습니다.
- **로그는 메인 스레드 밖에서도 들어옵니다.** HotReloader 워처 스레드, Vulkan 검증 콜백, 이후의 Jolt 작업
  스레드가 그렇습니다.
- **Android(Quest) 빌드가 존재합니다.** 그곳에는 stdout 을 받아 줄 곳이 없고, logcat 이 표준 출력처입니다.

---

## 3. 핵심 결정: 기록 수준과 표시 필터를 분리합니다

"로그를 끈다" 는 말에는 서로 다른 두 동작이 섞여 있습니다. 이 설계는 둘을 별개의 장치로 둡니다.

| | 기록 수준 (capture level) | 표시 필터 (view filter) |
|---|---|---|
| 소속 | 코어 `HLogCategory` | 에디터 Output Log |
| 판정 시점 | 로그를 남기는 순간 | 화면에 그리는 순간 |
| 꺼져 있을 때 | 인자 평가·문자열 조립·링 버퍼·파일·콘솔을 전부 생략합니다 | 링 버퍼와 파일에는 남고 화면에서만 숨깁니다 |
| 다시 켰을 때 | 이미 버린 로그는 복구할 수 없습니다 | 과거 로그가 소급해서 나타납니다 |
| 용도 | 소음원 차단(키 입력, 캐시 적중, 메시 업로드) | 조사할 때 관심 카테고리에 초점 맞추기 |
| 스레드 | 모든 스레드가 읽습니다(atomic) | 메인 스레드 전용입니다 |

**둘을 한 스위치로 합치지 않습니다.** 합치면 "화면에서 숨겼더니 파일에서도 사라졌다" 거나 "파일에 남기려고
켰더니 화면이 넘친다" 는 상황이 생깁니다.

키보드 입력 로그는 기록 수준의 대상입니다. 메시지를 `Verbose` 로 남기고 `Input` 카테고리의 기본 기록 수준을
`Log` 로 두면, 기본 상태에서는 인자 평가조차 일어나지 않습니다. 필요할 때 에디터에서 `Input` 을 `Verbose`
로 올리면 됩니다.

---

## 4. 심각도 단계

**`Error`, `Warning`, `Log`, `Verbose` 의 4단계를 씁니다.** 기록 수준에는 여기에 `Off` 가 더해집니다.

UE 의 7단계(Fatal, Error, Warning, Display, Log, Verbose, VeryVerbose)에서 셋을 뺐습니다.

- **Fatal:** 치명적인 상황은 로그 단계가 아니라 예외나 abort 로 처리해야 합니다. `main` 의 `catch` 가
  `Error` 로 남기고 종료하면 충분합니다.
- **Display:** UE 에서 Display 와 Log 의 차이는 콘솔 에코 여부입니다. Haruka 에는 출력처 구분이 없어 의미가
  없습니다.
- **VeryVerbose:** 필요해지면 enum 끝에 추가하면 됩니다. 기존 호출부는 바뀌지 않습니다.

기존 호출부를 옮길 때의 기준은 다음과 같습니다. `std::cerr` 는 문장을 보고 `Error` 와 `Warning` 중에서
판정하고, `std::cout` 은 `Log` 로 옮깁니다. 반복적으로 쏟아지는 로그(키 입력, 캐시 적중, 메시 업로드,
머티리얼 덤프)는 `Verbose` 로 내립니다.

---

## 5. 코어 API (`src/core/HLog.hpp`)

코어(Layer 1)에 둡니다. 에디터·렌더러·게임 모듈이 모두 쓰므로 에디터에 의존하면 안 됩니다.

### 5.1 카테고리 선언

```cpp
enum class ELogVerbosity : uint8_t { Off = 0, Error, Warning, Log, Verbose };

// 여러 파일이 함께 쓰는 카테고리: 서브시스템 헤더에 inline 변수로 선언합니다.
inline constinit HLogCategory LogRenderer{ "Renderer", ELogVerbosity::Log };

// 한 파일만 쓰는 카테고리: 그 cpp 에 static 으로 선언합니다.
static constinit HLogCategory LogHotReload{ "HotReload", ELogVerbosity::Log };
```

선언에 매크로를 쓰지 않습니다. `inline`/`static` 과 `constinit` 이 드러나 있어야 수명 규칙을 읽을 수 있기
때문입니다.

### 5.2 로그 남기기

```cpp
HLOG(LogRenderer, Log)     << "BRDF LUT generated (" << w << "x" << h << ")";
HLOG(LogRenderer, Warning) << "megabuffer usage " << pct << "%";
HLOG(LogInput, Verbose)    << "PRESS " << name;
```

- 줄 끝에 `"\n"` 을 쓰지 않습니다. 레코드 하나가 한 줄입니다.
- 메시지 안에 개행이 있으면 제출할 때 줄마다 레코드를 나눕니다. 행 높이가 고정이고, 파일에서도 줄마다
  접두어가 붙어야 검색이 되기 때문입니다.
- **스트림 문법을 유지합니다.** 기존 71곳을 기계적으로 옮길 수 있고, 기존과 같은 서식 규칙이 보장됩니다.
  `std::format` 은 Android NDK 버전에 따라 지원 범위가 달라 Quest 이식 때 확인할 부담이 생깁니다.

### 5.3 매크로 전개

```cpp
#define HLOG(Cat, Verb)                                          \
    if (!(Cat).isEnabled(ELogVerbosity::Verb)) {} else           \
        HLogLine((Cat), ELogVerbosity::Verb)
```

- 꺼져 있으면 `<<` 오른쪽 식을 평가하지 않습니다. 비용은 atomic 읽기 한 번과 분기 하나입니다.
- `if {} else` 형태이므로 호출부를 감싼 바깥 `if/else` 와 결합이 꼬이지 않습니다.
- `HLogLine` 은 임시 객체입니다. 내부의 `std::ostringstream` 에 조립하고, 문장이 끝날 때 소멸자에서 제출합니다.
- **켜진 로그 한 줄은 힙 할당을 동반합니다.** 그래서 매 프레임 실행되는 경로에서는 `Verbose` 로만 로그를
  남깁니다. 기본 기록 수준에서 꺼져 있으므로 평상시 비용은 분기 하나입니다.

### 5.4 카테고리 객체와 레지스트리

```cpp
class HLogCategory {
public:
    constexpr HLogCategory(const char* name, ELogVerbosity defaultLevel);
    bool          isEnabled(ELogVerbosity v) const;   // 첫 호출에서 레지스트리에 등록
    const char*   name() const;
private:
    const char*                                 name_;
    ELogVerbosity                               default_;
    mutable std::atomic<HLogCategoryState*>     state_{ nullptr };
};

HLogCategory& HLog::category(std::string_view name);   // 실행 중 이름으로 찾거나 만듦
```

- **같은 이름은 같은 상태를 공유합니다.** 두 cpp 가 각자 `static` 으로 `"Asset"` 을 선언해도 기록 수준과
  표시 필터가 하나로 움직입니다. 기본 수준이 서로 다르면 먼저 등록된 쪽을 따르고, `Logging` 카테고리로
  경고를 남깁니다.
- **상수 초기화와 지연 등록을 조합합니다.** 전역 객체의 동적 초기화 순서는 번역 단위 사이에서 정해져 있지
  않습니다. 생성자를 `constexpr` 로 두면 초기화 순서 문제가 원천적으로 사라지고, 레지스트리 등록은
  `isEnabled` 의 첫 호출에서 일어납니다.
- **레지스트리와 상태 객체는 해제하지 않습니다(의도적 누수).** 정적 소멸 중에 로그를 남기는 경로가 이미
  해제된 레지스트리를 건드리지 않게 하기 위해서입니다.
- 이 방식의 대가로, **한 번도 실행되지 않은 카테고리는 목록에 나타나지 않습니다.** 꺼진 로그도
  `isEnabled` 를 호출하므로, 코드 경로가 한 번이라도 지나가면 목록에 나타납니다.
- `HLog::category(name)` 은 레거시 출력 어댑터(§5.8)와, 이후 데이터로 정의되는 카테고리를 위한 경로입니다.

### 5.5 레코드

```cpp
struct HLogRecord {
    uint64_t      seq;        // 1부터 단조 증가
    float         time;       // 부팅 후 경과 초
    uint16_t      category;   // 레지스트리 인덱스
    ELogVerbosity verbosity;
    std::string   text;       // 접두어와 개행을 뺀 본문(UTF-8 원문)
};
```

### 5.6 링 버퍼와 소비 API

- **용량은 2048 레코드로 고정합니다**(현재 200줄). 필터가 생기면 소음이 많은 카테고리가 중요한 로그를
  밀어내는 속도가 문제가 되기 때문입니다. 메모리는 대략 수백 KB 수준입니다.
- **카테고리별 Error·Warning 누계를 링과 별도로 셉니다.** 링에서 밀려난 오류도 개수는 남습니다.
- 소비 API 는 두 개입니다.

  ```cpp
  uint64_t HLog::latestSeq();                                              // atomic
  size_t   HLog::copySince(uint64_t seq, std::vector<HLogRecord>& out);    // 반환: 이미 밀려나 못 준 개수
  ```

  `logDirty` 불리언을 대체합니다. 소비자(Output Log, 상태바)가 각자 "마지막으로 본 seq" 만 기억하므로,
  소비자가 늘어나도 서로의 갱신 신호를 가져가지 않습니다.

### 5.7 싱크(출력처)

제출된 레코드는 **하나의 뮤텍스 안에서 순서대로, 동기적으로** 모든 싱크에 전달됩니다.

| 싱크 | 대상 | 조건 |
|---|---|---|
| 링 버퍼 | 에디터 UI | 항상 |
| 표준 출력 | 교체 전에 저장해 둔 원래의 stdout·stderr streambuf | 데스크톱. Error·Warning 은 stderr, 나머지는 stdout(현재 관례 유지) |
| logcat | `__android_log_write` | Android. 태그 `Haruka`, 심각도를 logcat 우선순위로 대응 |
| 파일 | `Logs/HarukaEngine.log` (UTF-8) | 데스크톱. §11-4 에서 도입을 확정했습니다. 줄마다 `[경과 초] [카테고리]` 형식이고 줄마다 flush 합니다 |

- **파일 경로는 작업 디렉터리 기준입니다.** `run.bat` 은 실행 파일 폴더(`build/Release`)로 들어가서 엔진을
  실행하므로, 실제 위치는 `build/Release/Logs/HarukaEngine.log` 입니다. 저장소 루트의 `Logs/`(빌드 로그와
  `run.bat` 의 UTF-16 `latest.txt`)와는 다른 폴더입니다. `Config/` 의 설정 파일들도 같은 규칙을 따릅니다.
- **파일은 읽기 공유로 엽니다.** MSVC 의 `fopen_s` 는 파일을 독점해서 열기 때문에, 엔진이 도는 동안에는
  편집기나 `Get-Content -Wait` 로 로그를 볼 수 없었습니다. L4 에서 `_fsopen(..., _SH_DENYWR)` 로
  바꿨습니다(쓰기만 막음). `HarukaLogTest` 가 열려 있는 파일을 다른 스트림으로 읽어 확인합니다.
- **비동기 로깅 스레드를 두지 않습니다.** 로그 양이 적어 이득이 없고, 비동기 방식은 크래시 직전의 로그를
  잃습니다. stdout 은 이미 버퍼 없이(`_IONBF`) 설정되어 있습니다.
- **출력 형식은 기존 파일 검색 습관을 유지합니다.**

  ```
  [Renderer] BRDF LUT generated (512x512 R16G16)
  [Renderer] Warning: megabuffer usage 91%
  [Asset] Error: Load failed (assets/models/x.glb): ...
  ```

### 5.8 레거시 출력 어댑터

- `LogStreamBuf` 를 코어로 옮기고 **`main()` 의 첫 부분에서 설치합니다.** 부팅 로그가 전부 포착되어 DEF-71 이
  해소됩니다. `EditorApp` 은 더 이상 streambuf 를 건드리지 않습니다.
- 어댑터는 받은 줄의 `[Tag]` 접두어를 떼어 `HLog::category(Tag)` 로 제출합니다. 접두어가 없으면 `Stdout`
  카테고리로 제출합니다. `std::cout` 은 `Log`, `std::cerr` 는 `Warning` 으로 취급합니다.
- **무한 루프를 막기 위해** 표준 출력 싱크는 `std::cout` 이 아니라 교체 전에 저장한 원래 streambuf 에 씁니다.
  어댑터 자신은 원래 streambuf 로 직접 전달하지 않습니다(전달하면 두 번 출력됩니다).
- L2 이관이 끝나면 엔진 코드에는 `std::cout`/`std::cerr` 가 남지 않습니다. 어댑터는 서드파티 출력과 이관
  누락을 잡는 안전망으로만 남으며, **`Stdout` 카테고리에 무언가 보이면 이관 누락 신호입니다.**
- C 의 `fprintf(stderr, ...)`(Vulkan 로더 등)는 streambuf 를 거치지 않아 포착되지 않습니다. 현재도 같습니다.

### 5.9 스레드 규칙 ([Threading.md](Threading.md) 규칙 4 갱신 대상)

- `HLOG` 는 어느 스레드에서 호출해도 안전합니다.
- 기록 수준을 바꾸는 쓰기는 메인 스레드(에디터 UI)에서만 합니다. 읽기는 atomic 이므로 어디서나 됩니다.
- **싱크 안에서 `HLOG` 를 호출하지 않습니다.** 제출 뮤텍스에 재진입해 교착합니다.
- **레지스트리 락을 쥔 채로 로그를 남기지 않습니다.** 기본 수준 충돌 경고(§5.4)는 락을 푼 뒤에 남깁니다.
  레지스트리 락과 제출 락은 서로 다른 뮤텍스입니다.

---

## 6. 카테고리 체계 (확정: 태그 20종 → 카테고리 14종 + 시스템 2종)

| 카테고리 | 흡수하는 기존 태그 | 선언 위치 | 기본 기록 수준 | Verbose 로 내릴 로그 |
|---|---|---|---|---|
| `Engine` | `[Haruka]` 중 FATAL·정상 종료 | `core/HLog.hpp` | Log | |
| `Memory` | `[Haruka]` 중 Core Heap·Zone | `HarukaAllocator.cpp` (static) | Log | |
| `Renderer` | `[Renderer]`, `[GpuScene]`, `[Swapchain]`, `[Haruka]` 중 GPU·MSAA·타임스탬프·핫리로드 | `renderer/RendererLog.hpp` (inline) | Log | 메시 업로드 |
| `Vulkan` | `[Vulkan]` | `Renderer.cpp` (static) | Log | 없음. 콜백의 심각도 플래그를 Error·Warning 으로 대응합니다 |
| `Asset` | `[Asset]`, `[AssetManager]`, `[GassetCache]`, `[Prefab]` | `assets/AssetLog.hpp` (inline) | Log | 캐시 적중, 머티리얼 덤프 |
| `Primitive` | `[Primitive]` | `geometry/PrimitiveLog.hpp` (inline) | Log | |
| `Scene` | `[Scene]`(SceneSerializer), `[Actor]` | `core/SceneLog.hpp` (inline) | Log | |
| `Game` | `[DefaultGameModule]`, `[Scene]`(DefaultGameModule) | `DefaultGameModule.cpp` (static) | Log | |
| `Physics` | `[Physics]` | `PhysicsSystem.cpp` (static) | Log | |
| `Audio` | `[Audio]` | `AudioSystem.cpp` (static) | Log | |
| `Input` | `[Input]` | `platform/IWindow.hpp` (inline) | Log | **키 입력 전부** |
| `UI` | `[HSlate]`, `[HFontAtlas]` | `ui/slate/SlateLog.hpp` (inline) | Log | |
| `Editor` | `[Editor]` | `EditorApp.cpp` (static) | Log | |
| `HotReload` | `[HotReloader]` | `HotReloader.cpp` (static) | Log | |
| `Stdout` (시스템) | 접두어 없는 레거시·서드파티 출력 | `core/HLog.cpp` | Log | |
| `Logging` (시스템) | 로그 체계 자신의 경고 | `core/HLog.cpp` | Log | |

`[Haruka]` 태그는 없어집니다. 엔진 이름은 분류 정보가 아니기 때문입니다.

---

## 7. Output Log 패널 (표시 필터)

### 7.1 배치

```
┌ 필터 행 (Fixed 24) ──────────────────────────────────────────────────────────┐
│ [Search...        ] [Categories]  [Err] [Warn] [Log] [Verb]   E 2  W 5  [Clear] │
├ 로그 행 풀 (Fill) ───────────────────────────────────────────────────────────┤
│   12.34  Renderer   BRDF LUT generated (512x512 R16G16)                         │
│   12.40  Asset      Warning: material "Material.001" has no textures ...        │
│   ...                                                                            │
│                                                        [v 12 new lines]          │  ← 스크롤 고정 중일 때만
└───────────────────────────────────────────────────────────────────────────────┘
```

- **심각도 토글 4개는 기본으로 전부 켭니다.** Verbose 는 기록 수준이 이미 거르고 있으므로, 기록된 Verbose 는
  누군가 일부러 켠 것입니다. 표시 필터가 한 번 더 숨기면 "켰는데 안 보인다" 는 혼란만 생깁니다.
- `E 2  W 5` 는 링에 남은 개수가 아니라 부팅 이후 누계입니다. 누르면 해당 심각도만 보이게 합니다.
- **Clear 는 코어 링을 비우지 않습니다.** Output Log 가 기억하는 `clearedSeq_` 만 올리므로, 파일과 상태바에는
  영향이 없습니다.

### 7.2 Categories 팝업

```
┌ Categories ─────────────────────────────────┐
│ [All] [None]                  [Reset levels] │
│ Show  Category       Count   Capture         │
│ [x]   Asset            128   [Log    ]       │
│ [x]   Editor            14   [Log    ]       │
│ [x]   Input              0   [Verbose]  ← 기본값과 다르면 강조색
│ [ ]   Vulkan             3   [Log    ]       │
└──────────────────────────────────────────────┘
```

- **체크박스는 표시 필터이고, 오른쪽 순환 버튼은 기록 수준입니다**(Off → Error → Warning → Log → Verbose).
  두 장치를 한 행에 나란히 두되 열 제목(Show / Capture)으로 구분합니다.
- **기록 수준이 기본값과 다르면 강조색으로 표시합니다.** 키 입력 로그를 켜 둔 채 잊어버리는 상황을 막기
  위해서입니다. `Reset levels` 는 모든 카테고리를 선언 기본값으로 되돌립니다.
- 팝업이 열리는 방향은 패널 위치로 정합니다. 화면 아래쪽에 있으면 위로, 위쪽에 있으면 아래로 엽니다.
  Output Log 는 도킹으로 어디든 옮겨질 수 있기 때문입니다.
- **구현에서 설계를 바꿨습니다: 메뉴 팝업의 전면 캐처를 재사용하지 않습니다.** 캐처를 쓰면 DEF-64(바깥
  첫 클릭이 닫기에만 쓰임)가 그대로 따라옵니다. 대신 `handleInput` 이 프레스 순간에 팝업과 버튼 밖이면
  닫기만 하고 클릭은 그대로 흘려보냅니다. 팝업 안의 빈 곳(여백·열 제목)은 같은 자리에 깐 투명 버튼이
  받아, 아래 패널로 새지 않습니다. 팝업이 뷰포트 위로 열리므로 `inViewport` 가 팝업 영역을 뷰포트
  툴바와 같은 방식으로 배제합니다(기즈모 드래그는 UI 디스패치보다 먼저 판정되기 때문입니다).
- **기각한 대안: 패널 왼쪽에 카테고리 열을 고정하는 방식.** 하단 도크의 기본 높이(260px)에서 필터 행을 빼면
  16px 행이 13개가량 들어가는데, 카테고리는 16개입니다. 클리핑과 스크롤이 없어 넘칩니다.

### 7.3 행 풀: 로그가 들어와도 리빌드하지 않습니다

- 로그 행 위젯을 **패널의 실측 높이에 맞춘 N개만** 만들고, 각 행의 텍스트와 색을 `Attr` 로 뷰 캐시에서
  읽습니다. 새 로그가 와도 `clearSlots` 가 일어나지 않습니다. 상태바([Editor_Layout_Design.md](Editor_Layout_Design.md) §7.11)와 같은 규율입니다.
  이것으로 **DEF-70 과 DEF-61 이 함께 해소됩니다.**
- 리빌드는 N 이 바뀔 때(패널 높이 변화)와 필터 행의 구성이 바뀔 때만 일어납니다.
- **선행 변경:** `STextBlock::color` 에 `Attr<glm::vec4>` 오버로드를 추가합니다.
- 뷰 캐시 갱신 순서는 다음과 같습니다. 매 프레임 `latestSeq` 를 비교하고, 달라졌으면 `copySince` 로 새
  레코드만 받아 로컬 미러(최대 2048개)에 붙이고, 필터를 통과한 인덱스 목록을 증분으로 갱신합니다.
  필터가 바뀐 프레임에만 미러 전체를 다시 검사합니다.
- **표시 문자열은 비ASCII 바이트를 `?` 로 치환합니다.** 상태바와 같은 도우미를 공유합니다. 레코드의 원문은
  UTF-8 그대로 남으므로 파일과 콘솔에는 영향이 없습니다.
- 행 폭을 넘는 문자열은 말줄임으로 자릅니다. 자르는 비용이 폰트 실측 반복이므로, 행마다 `(seq, 폭)` 을
  키로 결과를 캐시합니다.

### 7.4 스크롤

- 휠은 아웃라이너와 같은 방식으로 처리합니다. `EditorApp::tick` 의 단일 휠 판독 지점에서 커서가 로그 행 영역
  위에 있으면 가로챕니다(DEF-57 우회). 한 프레임에 들어온 휠 입력은 크기와 무관하게 보이는 행 3줄로
  셉니다(아웃라이너와 같음). 하단 패널이 비활성 탭이면 가로채지 않습니다(`bottomVisible_`, 아웃라이너의
  `outlinerVisible_` 와 같은 이유).
- 로그 행은 위젯을 다시 만들지 않으므로, 아웃라이너와 달리 좌버튼을 누른 채 굴려도 막지 않습니다.
- (2026-10-03 갱신, DEF-57) 위의 `tick` 우회는 지웠습니다. 휠은 위젯 경로로 오고, 행 영역을 감싼 `SBorder` 의
  `onWheel` 이 같은 규칙(한 프레임 3줄)으로 `logScrollReq_` 를 쌓습니다. 비활성 탭·닫힌 패널은 위젯 트리에 없으므로
  `bottomVisible_` 가드도 필요 없어 지웠습니다. Output Log 탭은 스크롤 상자에 넣지 않습니다(행 수를 행 영역의 실측
  높이로 정하는데, 스크롤 상자 안에서는 그 높이가 내용을 따라 늘어 행 수가 함께 불어나기 때문입니다).
- **구현에서 설계를 바꿨습니다: 위치를 꼬리로부터의 줄 수(`scrollFromTail_`)가 아니라 화면 맨 아래 행의
  `seq`(`logAnchorSeq_`)로 기억합니다.** 줄 수로 기억하면 고정 중에 새 로그가 들어오거나 필터·검색어가
  바뀔 때마다 보던 자리가 밀립니다. `seq` 는 사본 안에서 오름차순이라 이분 탐색으로 위치를 찾습니다.
  `logAnchorSeq_ == 0` 이 꼬리 추종입니다.
- 이동은 필터를 통과한 행 단위로 셉니다. 맨 위에 닿으면 화면을 채운 채 멈추고, 아래로 내려 꼬리에 닿으면
  고정이 저절로 풀립니다. 앵커가 사본에서 밀려나면 가장 오래된 자리로 붙습니다.
- 고정 중에는 오른쪽 아래에 버튼을 띄우고, 누르면 꼬리로 돌아갑니다. 라벨은 **고정을 시작한 뒤에 들어온**
  보이는 줄 수입니다(`v 3 new lines`). 원래 아래에 있던 줄은 세지 않으며, 새 줄이 없으면 `v Latest` 입니다.
  버튼은 항상 두고 가시성만 `Attr` 로 바꿉니다. 고정 상태가 바뀔 때마다 위젯을 만들면 §7.3 의 리빌드가
  되살아납니다.
- Clear 는 고정을 풀고 꼬리 추종으로 돌아갑니다.

### 7.5 검색

- `SSearchBox` 를 쓰고, 포커스 중재에 `EFocusTarget::LogSearch` 를 추가합니다. `setTextFocus`,
  `focusWidgetContains`, `statusFocusText` 세 곳을 함께 고칩니다.
- 대소문자를 무시하는 부분 일치이며, 카테고리 이름도 검색 대상에 포함합니다.

---

## 8. 상태바 연동

- **색을 부분 문자열이 아니라 레코드의 심각도로 정합니다.** DEF-72 가 해소됩니다.
- L3 에서 확인한 사실: 상태바의 `STextBlock` 은 색을 **값으로** 받고 있어서, 생성 시점의 회색에 고정되어
  있었습니다. L1 의 심각도 색 판정이 상태바에는 보이지 않았던 이유입니다. `STextBlock::color` 에
  `Attr<glm::vec4>` 오버로드를 추가하고 상태바가 그것으로 읽게 했습니다.
- **표시 대상은 가장 최근의, Verbose 가 아닌 레코드입니다.** Verbose 를 켜 두었을 때 상태바가 키 입력
  로그로 덮이는 일을 막습니다.
- **상태바는 Output Log 의 표시 필터를 따르지 않습니다.** Output Log 에서 숨긴 카테고리의 오류도 상태바에는
  나타나야 하기 때문입니다.

---

## 9. 설정 보존

- **기록 수준:** `Config/log_levels.txt` 에 `Input Verbose` 처럼 한 줄에 하나씩 기록합니다. 기본값과 같은
  항목은 쓰지 않습니다. 코어가 부팅할 때 읽어 레지스트리에 보관하고, 카테고리가 등록되는 순간에 적용합니다.
  지연 등록(§5.4)과 자연스럽게 맞물립니다.
  **이번 실행에서 한 번도 등록되지 않은 이름도 파일에서 지우지 않습니다.** 카테고리는 첫 사용 시점에 등록되므로,
  그 코드 경로가 이번 실행에서 지나가지 않았다는 이유로 사용자의 설정이 사라지면 안 되기 때문입니다.
  경로는 `HLog::kLevelOverridesPath` 한 곳에 둡니다. 부팅(`main`)이 읽고 에디터가 쓰므로, 두 곳이 서로 다른
  경로를 보면 설정이 조용히 사라집니다.
- **표시 필터:** 카테고리 체크 상태와 심각도 토글을 `Config/editor_log.txt` 에 기록합니다. 검색어는 보존하지
  않습니다. 형식은 한 줄에 하나씩 `severity Warning off` 와 `hide Memory` 이고, 모르는 줄은 건너뜁니다.
  숨긴 카테고리는 인덱스가 아니라 **이름**으로 기억합니다. 인덱스는 등록 순서라 실행마다 달라지고, 아직
  등록되지 않은 카테고리의 숨김도 기록 수준과 같은 이유로 지우면 안 되기 때문입니다.
- **저장 시점:** 두 파일 모두 바뀌는 순간 저장합니다(Categories 팝업의 버튼, 심각도 토글, `E`·`W` 누계
  버튼). 종료 시점에 한 번 쓰는 방식은 크래시하면 설정을 잃습니다.
- **명시적으로 모두 되돌리는 버튼은 등록되지 않은 항목까지 지웁니다.** 위의 "지우지 않는다" 는 규칙은
  설정이 저절로 사라지는 것을 막으려는 것이지, 사용자의 명시적인 요청을 막으려는 것이 아닙니다.
  - `Reset levels` 는 `HLog::resetLevels()` 로 적용 대기 항목까지 버립니다. 남기면 되돌린 뒤에 그 카테고리가
    처음 쓰이는 순간 예전 수준이 되살아납니다(키 입력 로그를 켜 둔 채 잊는 바로 그 상황입니다).
  - `All` 은 목록에 없는 카테고리의 숨김까지 풉니다. `None` 은 지금 등록된 카테고리만 숨깁니다.

---

## 10. 착수 순서 (커밋 단위)

| 단계 | 내용 | 검증 |
|---|---|---|
| **L0** (완료 2026-09-17) | 키 입력 로그를 `GlfwWindow::keyCallback` 의 스위치로 비활성화 | 빌드 경고 0 |
| **L1 코어** (완료 2026-09-17) | `HLog.hpp/.cpp`(카테고리·레지스트리·링·싱크), 어댑터를 `main` 으로 이동, `EditorContext` 로그 제거 후 `HLog` 소비로 교체, Output Log·상태바의 색을 심각도로 판정, 비활성 탭에서는 리빌드하지 않음. 테스트 `HarukaLogTest` 신설 | 부팅 로그가 Output Log 에 보이는지, 기존 로그가 어댑터를 거쳐 빠짐없이 들어오는지 |
| **L2 이관** (완료 2026-09-17) | 71개 호출부를 `HLOG` 로 이관(§6 통합표), 키 입력 스위치를 `LogInput` Verbose 로 교체, Vulkan 콜백 심각도 대응 | `std::cout`/`std::cerr` 검색 결과가 어댑터 외 0건 |
| **L3 Output Log** (완료 2026-09-17) | 필터 행, Categories 팝업(표시·기록), 행 풀, 검색, Clear, `STextBlock` 색 `Attr`, `LogSearch` 포커스 | 로그 폭주 중 Content Browser 클릭, 패널 높이 변경 시 줄 수 |
| **L4 보존·스크롤** (완료 2026-09-25) | 설정 파일 2종(변경 즉시 저장), 꼬리 추종 스크롤과 `v N new lines` 버튼, UTF-8 파일 싱크(읽기 공유), 헤드리스 stdio 를 `Logs/stdout.txt` 로 이동, `resetLevels` | 재시작 후 설정 복원, 고정 중 새 로그가 와도 화면 유지, 엔진 실행 중 로그 파일 읽기 |

### 10.1 `HarukaLogTest` 검산 항목

Vulkan·창·World 를 링크하지 않고 `HLog.cpp` 만 링크하는 테스트입니다.

- 꺼진 로그의 인자가 평가되지 않는지(부작용 카운터로 확인).
- 같은 이름으로 두 번 선언한 카테고리가 상태를 공유하는지.
- 기록 수준 변경이 즉시 반영되는지.
- 링 용량을 넘겼을 때 `seq` 가 연속인지, `copySince` 가 밀려난 개수를 보고하는지.
- 여러 줄 메시지가 줄마다 레코드로 나뉘는지.
- 어댑터가 `[Tag] msg` 를 파싱하는지, 접두어가 없으면 `Stdout` 으로 가는지, `cerr` 가 Warning 이 되는지.
- 4개 스레드가 각 10,000건을 동시에 제출해도 `seq` 에 중복·누락이 없는지.
- (L4) 기록 수준 파일을 읽으면 등록된 카테고리에는 즉시, 나중에 등록되는 카테고리에는 등록 순간 적용되는지.
  저장할 때 등록되지 않은 대기 항목을 보존하는지.
- (L4) 파일 출력처의 형식과, 열려 있는 동안 다른 스트림이 읽을 수 있는지.
- (L4) `resetLevels` 가 대기 항목까지 버려, 그 뒤 처음 등록되는 카테고리가 기본 수준인지.

---

## 11. 결정 사항 (2026-09-17 권장안대로 확정)

| # | 항목 | 확정안 | 근거 |
|---|---|---|---|
| 1 | 심각도 단계 수 | **4단계**(Error·Warning·Log·Verbose) | §4 |
| 2 | 표시 필터의 Verbose 기본값 | **켬** | 기록 수준이 이미 거르므로 이중 차단은 혼란만 만듭니다(§7.1) |
| 3 | 에디터에서 바꾼 기록 수준을 재시작 후에도 유지할지 | **유지** + 기본값과 다르면 강조색 + `Reset levels` | 유지하지 않으면 조사할 때마다 다시 켜야 합니다. 잊어버리는 위험은 강조색으로 막습니다 |
| 4 | 엔진이 직접 쓰는 UTF-8 파일 싱크(`Logs/HarukaEngine.log`) | **도입** | `run.bat` 의 UTF-16 기록과 헤드리스 실행 시 작업 디렉터리에 로그가 생기는 문제를 한 번에 해결합니다. 별건으로 띄워 둔 "main.cpp 로그 경로를 Logs/ 로 이동" 작업과 겹치므로 그 작업은 이 단계에 흡수합니다 |
| 5 | 카테고리 통합 | **§6 통합표** | 태그 20종의 중복과 혼용을 정리합니다 |

## 12. 보류 후보

지금 넣지 않지만, 필요가 확인되면 이 체계 위에 추가할 수 있는 기능입니다.

- 로그 행을 눌러 클립보드로 복사(`IWindow::setClipboardText` 가 이미 있습니다).
- 호출 위치(`__FILE__`/`__LINE__`) 기록과 소스 이동.
- 같은 로그의 반복 억제(`HLOG_ONCE` 또는 빈도 제한).
- Quest 릴리스 빌드에서 Verbose 호출을 컴파일 단계에서 제거.
- VeryVerbose 단계 추가.
