# 크래시 보고 · 검사 매크로

2026-11-19 시작. "여러 시스템이 기대는데 없던 기반" 3순위입니다(1 · 2순위는 [Object_References.md](Object_References.md)).
이 문서는 **완료 조건 체크리스트**입니다. 각 줄은 확인 방법을 함께 적고, 만족하면 `[x]` 로 바꿉니다. 창이나 헤드셋이 있어야 확인할 수 있는 줄은
`(창)` · `(헤드셋)` 으로 표시하고 사용자가 정한 때 확인합니다.

## 0. 조사

| 엔진 | 검사 매크로 | 크래시 처리 |
|---|---|---|
| UE 5 | `check` · `checkf`(실패하면 멈춤, Shipping 에서 빠짐), `verify`(식은 늘 실행), `ensure` · `ensureMsgf`(로그 + 콜 스택, 사이트마다 한 번, 계속 진행), `checkSlow`(Debug 만) | 예외 필터 → 미니덤프 · 로그 · 컨텍스트를 `Saved/Crashes/<이름>/` 에 쓰고 CrashReportClient 실행. 스택 넘침은 미리 띄운 스레드가 처리 |
| Unity | `Debug.Assert`(개발 빌드), 엔진 내부 `AssertMsg` | `crashes/<날짜>/` 에 `crash.dmp` · `error.log` · 플레이어 로그 |
| Godot 4 | `CRASH_COND`(멈춤), `ERR_FAIL_COND` · `ERR_FAIL_COND_V`(로그 + 반환) | 시그널 · 예외 처리기가 콜 스택을 출력(덤프 없음) |
| Breakpad · Crashpad | — | 프로세스 밖(또는 미리 띄운 스레드)에서 덤프를 써서 스택 넘침 · 힙 손상에도 견딤 |

Haruka 의 지금 상태(조사 2026-11-19):
- `main.cpp` · `AndroidMain.cpp` 는 `std::exception` 만 잡고 `FATAL:` 한 줄을 남깁니다. 잘못된 메모리 접근 · `std::terminate` · 순수 가상 호출 ·
  스택 넘침은 아무 기록 없이 끝납니다. Quest 에서는 Android tombstone 만 남습니다.
- `assert` 는 6곳뿐이고 Release 에서는 꺼집니다. 프로젝트 공통 검사 매크로가 없습니다.
- Release 빌드에는 PDB(디버그 기호)가 없어, 크래시 주소를 함수 이름으로 바꿀 수 없습니다.
- 로그 파일은 줄마다 flush 하므로 크래시 직전 로그는 남습니다(HLog). 메모리 링 버퍼(2048줄)도 있습니다.

## 1. 결정

- **D1 매크로(`src/core/HCheck.hpp`):** `HCHECK(식)` · `HCHECK_MSG(식, 스트림 식)` 은 **모든 구성에서 켜짐**(Haruka 의 Release 는 UE 의
  Development 와 Shipping 을 겸하므로, 비용은 분기 하나). 실패하면 Fatal 로그 → 크래시 보고서 → 끝. `HENSURE(식)` 은 식의 참 · 거짓을 돌려주는
  표현식이고, 실패하면 사이트마다 한 번 Error 로그 + 콜 스택을 남기고 계속합니다(디버거가 붙어 있으면 멈춤). `HCHECK_SLOW` 는 Debug 만
  (기존 `assert` 의 자리, 프레임마다 도는 곳).
- **D2 처리기(`src/core/CrashHandler.hpp/.cpp`):** Windows 는 처리되지 않은 예외 필터 · `std::terminate` · 순수 가상 호출 · CRT 잘못된 인자 ·
  `SIGABRT` 를 잡습니다. 덤프는 **미리 띄운 도우미 스레드**가 씁니다(스택 넘침 · 망가진 스택에서도 씀, Crashpad 와 같은 생각).
  Android 는 `SIGSEGV` · `SIGBUS` · `SIGFPE` · `SIGILL` · `SIGABRT` 를 대체 스택(`sigaltstack`)에서 받아 글 보고서를 쓰고, 원래 처리기로
  다시 보내 tombstone 도 남깁니다(시그널 안에서는 메모리 할당 · 잠금을 하지 않음).
- **D3 보고서 자리:** `<쓰기 루트>/Logs/Crashes/<YYYYMMDD-HHMMSS>_<실행 파일>/` 에 `crash.txt`(이유 · 주소 · 모듈+오프셋 · 콜 스택 · 최근 로그 64줄)와
  `crash.dmp`(Windows 미니덤프). 최근 10개만 남깁니다. 다음 실행은 지난 크래시를 경고 로그로 알립니다(`pending.txt`).
- **D4 기호:** MSVC Release 도 PDB 를 만듭니다(`/Zi` + 링커 `/DEBUG /OPT:REF /OPT:ICF`, 최적화는 그대로). 실행 파일에는 PDB 파일 이름만 넣습니다
  (`/PDBALTPATH:%_PDB%`, 빌드 폴더의 절대 경로가 배포 파일에 들어가지 않게). PDB 는 배포 ZIP 에 넣지 않습니다(UE 처럼 따로 보관해 보고서의
  모듈+오프셋을 나중에 이름으로 바꿈).
- **D5 시험 스위치:** `--debug-crash=segv|check|ensure|throw|abort|stack|purecall` 로 실제 실행 파일에서 몇 프레임 뒤 일부러 일으킵니다(UE 의
  `debug crash` 명령과 같은 자리). 자동 실행(`--frames` 등)에서 `HENSURE` 가 실패했으면 종료 코드 5(`kExitEnsure`).

## C — 체크리스트

- [x] C-1 `HCHECK` · `HCHECK_MSG` · `HENSURE` · `HCHECK_SLOW`. `HENSURE` 는 사이트마다 한 번만 보고하고 실패 수를 셈.
      확인: `HarukaCrashTest`(자식 프로세스에서 check 실패 → 보고서에 식 · 파일:줄 · 메시지, ensure 두 번 → 로그 한 번 · 계속 진행).
- [x] C-2 Windows 처리기: 잘못된 메모리 쓰기 · 잡히지 않은 예외(`std::terminate`) · `abort` · 스택 넘침 · 순수 가상 호출에서 보고서 폴더에
      `crash.txt` 와 `crash.dmp`(0 보다 큼). `crash.txt` 에 이유 · 예외 코드 · 콜 스택(PDB 가 있으면 함수 이름)과 최근 로그.
      확인: `HarukaCrashTest`(종류마다 자식 프로세스).
- [x] C-3 Release PDB: `HarukaEngine.pdb` · `HarukaGame.pdb` 가 생기고, 실행 파일의 디버그 경로에 빌드 폴더 절대 경로가 없음, 배포 ZIP 에 PDB 없음.
      확인: `dumpbin /headers`(디버그 경로 `HarukaGame.pdb` 처럼 이름만), `HarukaGame.exe` 에 로컬 경로 문자열 0, `Samples-0.1.0-win64.zip`
      12개 파일에 PDB 없음. `%_PDB%` 는 CMake 가 Visual Studio 프로젝트에 `%%` 로 써서 실행 파일에 `%이름.pdb%` 가 들어가므로
      MSBuild 의 `$(TargetName).pdb` 를 씁니다.
- [x] C-3b (재확인) 기준 PNG 7장 그대로. 6장은 같고 `defaultR` 만 c283f276 → 4be92554(512² 중 2화소가 밝기 1단계). 다른 두 화소는
      시작할 때 떨어지는 물리 물체(빨간 상자) 위이고, 이번 실행은 사용자의 Unreal Editor 가 GPU 1 을 97% 쓰던 때라 프레임 시간이
      평소의 3.5배였습니다(물리 단계 수가 실제 경과 시간을 따름). 같은 조건 세 번은 모두 4be92554. **2026-11-20 GPU 1 이 빈 때 다시 그려
      7장 모두 기준값(defaultR c283f276), 검증 오류 0** → 코드 차이가 아니라 프레임 시간 차이였음을 확인했습니다(`base7.sh c3b`).
- [x] C-4 보고서 관리: 최근 10개만 남김, 다음 실행이 지난 크래시를 경고로 알리고 한 번만 알림. 확인: `HarukaCrashTest`.
- [x] C-5 엔진 연결: `main.cpp` · `AndroidMain.cpp` 가 처리기를 로그보다 먼저 설치하고, 잡은 `std::exception` 도 보고서를 씀(FATAL 줄 유지).
      `--debug-crash` 로 실제 `HarukaGame` · `HarukaEngine`(숨은 창, `--gpu=1`)에서 보고서가 생김. 확인: `build/chk/crash_check.sh`
      (게임 segv · check · throw · ensure, 에디터 stack · purecall, 다음 실행의 알림).
- [x] C-6 기존 `assert` 6곳: 초기화의 2곳은 `HCHECK`, 프레임마다 도는 4곳은 `HCHECK_SLOW`. 확인: `grep "assert("` 0.
- [x] C-7 자동 실행 종료 코드: `HENSURE` 실패가 있으면 5. 확인: `--debug-crash=ensure --frames=10` 의 종료 코드.
- [x] C-8 Android: `build/android` 가 빌드되고 이번에 바꾼 파일에서 경고가 없음(시그널 처리기 · 대체 스택 · 보고서 쓰기).
- [ ] C-9 (헤드셋) Quest 에서 `Config/args.txt` 에 `--debug-crash=segv` 를 넣고 실행 → `<외부 파일 폴더>/Logs/Crashes/` 에 `crash.txt`, tombstone 도 남음.

## G — 공통

- [x] G-1 Release · Debug 경고 0, ctest 두 구성 통과, `HarukaArchTest` PASS.
- [x] G-2 기준 PNG 7장 · UI 덤프 기존 장면 그대로. UI 덤프 52장면은 UI · 뷰포트 모두 같음(`uidump.sh cr rf`), PNG 는 C-3b 재확인으로 7장 그대로.
- [x] G-3 문서: 이 문서 결과 절, `Engine_Specification.md` 시험 표, `Docs/README.md` 색인.

## 결과 (2026-11-19 밤, 미커밋)

| 항목 | 결과 |
|---|---|
| 빌드 | Release · Debug 경고 0(우리 코드). 컴파일 옵션이 바뀌어 전체를 다시 빌드하면서 외부 라이브러리 GLFW 헤더의 C4819(코드 페이지) 경고가 한 번 보임(외부 코드, 전과 같음) |
| ctest | 두 구성 31/31 통과, 검사 2,478(새 `crash_reports` 83) |
| 아키텍처 | `HarukaArchTest` PASS(검사 28) |
| 실제 실행 파일(`crash_check.sh`) | 게임: segv → 보고서 + 덤프(콜 스택 `debugCrash` ← `Engine::endLoop` ← `Engine::run` ← `main`), check → 종료 코드 1, throw → main 이 잡아 보고서(덤프 없음) · 종료 코드 1, ensure → 로그 한 번 · 계속 · 종료 코드 5. 에디터: 스택 넘침 · 순수 가상 호출 → 보고서 + 덤프. 다음 실행이 지난 보고서를 경고로 알림 |
| Android | `build/android` 빌드 통과, 이번에 바꾼 파일의 경고 0(원래 있던 `pNext` 초기화 · `HLog.cpp` 의 if 괄호 경고 등은 그대로) |
| 배포 | 게임 실행 파일에 로컬 경로 0, PDB 이름만, ZIP 에 PDB 없음 |
| 기준 PNG | 6장 같음, `defaultR` 2화소(물리 물체 위) 다름 → 2026-11-20 GPU 1 이 빈 때 다시 그려 7장 모두 같음(C-3b) |

**새 파일:** `src/core/HCheck.hpp` · `CrashHandler.hpp/.cpp`, `tests/CrashTest.cpp`. **바꾼 곳:** `HLog::tryRecentLines`, `main.cpp` · `AndroidMain.cpp`
(설치 · 지난 크래시 알림 · 잡은 예외 보고서 · ensure 종료 코드), `Engine::endLoop`(개발 스위치), `EngineOptions`(`--debug-crash`, `kExitEnsure`),
`assert` 6곳, CMake(Release PDB · 새 시험).

**쓰는 법:** 엔진 코드에서 "절대 일어나면 안 되는" 조건은 `HCHECK` · `HCHECK_MSG`, "일어나면 버그지만 계속 돌 수 있는" 조건은 `if (!HENSURE(x)) return;`,
프레임마다 도는 비싼 검사는 `HCHECK_SLOW`. 크래시가 나면 `<쓰기 루트>/Logs/Crashes/` 의 `crash.txt` 를 먼저 보고, 더 필요하면 `crash.dmp` 를
같은 빌드의 PDB 와 함께 Visual Studio 로 엽니다(배포한 빌드는 그 빌드의 PDB 를 따로 보관해야 함).

**남긴 것(범위 밖 · 다음 후보):**
- (헤드셋) C-9 Quest 에서 시그널 보고서 확인.
- 크래시 보고서를 보내는 도구(UE CrashReportClient), 다른 스레드의 콜 스택(지금은 크래시한 스레드만, 덤프에는 모두 있음), 에디터 시작 때 지난
  크래시를 대화상자로 알리기(지금은 경고 로그), 기존 코드의 `throw std::runtime_error` 132곳을 `HCHECK` · 오류 반환으로 나눌지 검토.
- Android 의 `HLOG` 를 괄호 없는 if 안에 쓴 곳에서 나는 clang 경고(원래 있던 것).
