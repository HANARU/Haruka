# 에셋 핫리로드 · 가져오기 설정 (2026-11-20)

[Engine_Foundation_Gaps.md](Engine_Foundation_Gaps.md) 의 13위 후보입니다. 지금은 에디터가 셰이더(`.spv`)만 다시 읽고, 모델 · 텍스처 · 씬 파일을
밖에서 바꾸면 에디터를 다시 켜야 보입니다. 가져오기 설정(배율 · LOD · sRGB)을 둘 곳도 없습니다. 이 문서는 작업 지시서가 아니라
**완료 조건 체크리스트**이고, 항목을 모두 만족하면 끝입니다.

## 1. 조사 요약

### 1-1. 지금 코드 (2026-11-20)

| 곳 | 지금 | 문제 |
|---|---|---|
| `editor/HotReloader` | 실행 폴더 `shaders/` 를 `ReadDirectoryChangesW` 로 감시, `.spv` 가 바뀌고 300 ms 조용하면 파이프라인을 다시 만듦 | 셰이더만 |
| `AssetManager` | 정식 논리 경로 → `AssetPair` 목록 캐시, `releaseUnused` 는 렌더러의 은퇴 목록(`retireMesh` · `retireMaterial`)으로 지연 해제 | 같은 경로를 새로 읽는 길이 없음 |
| 컴포넌트 | `StaticMeshComponent` · `SkinnedMeshComponent` 가 `modelPath` · `meshIndex` 와 핸들(메시 · LOD · 머티리얼 · 충돌 · 스킨)을 직접 들고 있음 | 모델을 새로 올리면 핸들을 바꿔 끼울 곳이 필요 |
| 머티리얼 | 그릴 때마다 `MaterialHandle` 의 이미지 뷰를 푸시 디스크립터로 씀(디스크립터 캐시 없음) | 핸들만 바꾸면 다음 프레임부터 새 이미지 |
| 텍스처 | 따로 된 텍스처 에셋이 없음. glTF 머티리얼의 이미지(.glb 안 · .gltf 옆 파일)를 가져올 때 팩터를 곱해 머티리얼로 올림 | 텍스처가 바뀌면 그 이미지를 쓰는 모델을 다시 가져와야 함 |
| `.gasset` 캐시 | 원본 크기 · 수정 시각(다르면 내용 해시)으로 판정 | **.gltf 가 가리키는 바깥 파일(.bin · 이미지)은 보지 않음** → 텍스처만 바꾸면 옛 캐시를 계속 씀 |
| 씬 파일 | `openScene` 이 읽고 `SceneDocument` 가 수정 여부를 셈 | 밖에서 바뀌어도 모름 |
| 가져오기 설정 | 없음. 자동 LOD 는 실행 단위 `--no-auto-lod` 뿐. UID · `.meta` 는 만들지 않기로 함([Object_References.md](Object_References.md) D6) | 에셋별 배율 · LOD · sRGB 를 둘 곳이 없음 |

### 1-2. 다른 엔진

- **Unreal:** `FDirectoryWatcher`(Windows 는 `ReadDirectoryChangesW`) → 콘텐츠 브라우저 "Auto Reimport"(감시 폴더 · 경로 와일드카드 규칙,
  변경 뒤 잠깐 기다렸다가 묶어서 처리). 가져오기 설정은 에셋(.uasset) 안의 `AssetImportData` 에 저장. 다시 가져오면 같은 UObject 를
  고쳐 쓰므로 참조는 그대로이고, 렌더 자원은 렌더 스레드에서 지연 해제.
- **Unity:** `AssetDatabase` 가 창이 다시 활성화될 때(또는 Auto Refresh) 바뀐 파일을 찾아 다시 가져옴. 설정은 에셋마다 `.meta`(ModelImporter
  `globalScale` · `sRGBTexture` 등), 폴더 단위 기본값은 Preset. 캐시(Library) 키에 가져오기 설정 해시가 들어감.
- **Godot:** `EditorFileSystem` 이 파일 시스템을 훑어 바뀐 원본을 다시 가져오고, 설정은 `.import` 파일, 캐시(.godot/imported) 이름에 설정
  해시를 섞음. 리소스를 다시 읽으면 같은 `Resource` 객체를 고쳐 씀.

### 1-3. 공통 구조

"운영체제의 변경 알림(또는 훑기) → 잠깐 조용해질 때까지 모음 → 원본 종류별로 다시 가져오기 → 참조하는 쪽이 새 자원을 보게 함(같은 객체를
고치거나 핸들을 바꿔 끼움) → 옛 GPU 자원은 GPU 가 다 쓴 뒤 해제", "가져오기 설정은 캐시 키에 들어감". 설정 위치만 엔진마다 다릅니다(.uasset ·
.meta · .import). 이 엔진은 `.meta` 를 만들지 않기로 했으므로 프로젝트 단위 표로 둡니다.

## 2. 결정

- **D1 감시 방식:** 에디터 전용 `editor/AssetWatcher` 가 프로젝트 폴더를 `ReadDirectoryChangesW`(하위 폴더 포함, 쓰기 · 이름 · 크기 변경)로
  감시합니다. 시각 폴링은 쓰지 않습니다(폴더가 커지면 매번 훑는 비용, UE 와 같은 방식). 감시 스레드는 바뀐 파일 이름만 모으고, 나머지는
  모두 메인 스레드가 합니다. 대상은 `Content/` 아래(→ `/Game/...`)와 `Config/import.ini`(→ `/Config/import.ini`)이고 `Saved/` 등은 버립니다.
  알림 버퍼가 넘치면(무엇이 바뀌었는지 모름) "모두 다시 확인"으로 처리해, 올라간 모델 중 캐시가 무효가 된 것만 다시 읽습니다.
  게임 실행 파일은 감시자를 만들지 않고, Android · 비 Windows 에서는 감시자가 아무것도 하지 않습니다.
- **D2 모으기(디바운스):** `AssetChangeQueue`(assets, 순수 계산)가 경로별로 마지막 알림 시각을 들고, 300 ms 동안 새 알림이 없으면 한 번만
  내보냅니다(같은 경로의 여러 알림은 하나로). 셰이더 감시자의 `kQuietMs` 와 같은 값입니다(제작 도구가 파일을 여러 번 나눠 씀).
- **D3 분류:** `.glb` · `.gltf` → 그 모델, 올라간 모델이 의존하는 파일(.gltf 옆의 `.bin` · 이미지) → 그 모델들, `.hscene` → 씬, `/Config/import.ini`
  → 가져오기 설정, 그 밖 → 무시(Verbose 로그). 올라가지 않은 모델은 다시 읽지 않습니다(다음에 읽을 때 캐시 판정이 새로 가져옴).
- **D4 의존 파일 기록 · 캐시 판정(DEF-174):** `GltfLoader` 가 가상 파일 시스템 콜백으로 읽은 바깥 파일(정식 논리 경로) 목록을 돌려주고,
  `.gasset` 머리(버전 12)에 가져오기 설정 해시와 함께 의존 파일마다 크기 · 수정 시각 · 내용 해시를 기록합니다. 캐시 판정은 원본처럼 의존
  파일도 봅니다(크기가 다르면 무효, 시각이 같으면 유효, 시각만 다르면 내용 해시로). 예전에는 .gltf 의 텍스처만 바꾸면 옛 캐시를 계속
  썼습니다(결함으로 등재). 버전이 바뀌므로 첫 실행에서 캐시를 한 번 다시 만듭니다(결과는 같음).
- **D5 다시 읽기 단위 = 모델:** 텍스처만 바뀌어도 그 이미지를 쓰는 모델을 통째로 다시 가져옵니다. 텍스처는 따로 된 에셋이 아니라 머티리얼
  팩터를 곱해 가져오기에서 머티리얼이 되고, 캐시도 모델 단위이기 때문입니다. 다시 읽기는 메인 스레드에서 동기로 합니다(편집 중 드문 일이고,
  비동기로 하면 반쯤 바뀐 상태를 다뤄야 함). 새 핸들을 올리고(D6), 다시 묶은 뒤, 옛 메시 · LOD · 머티리얼은 렌더러의 은퇴 목록으로 보내
  GPU 가 다 쓴 뒤 지웁니다(비동기 로딩 D4 와 같은 길). 은퇴 목록은 `hreload::retireList` 가 정합니다(겹치는 핸들 · 새 핸들은 빼고 한 번씩).
- **D6 다시 묶기:** `components/AssetRebind` 의 `hreload::rebindModel(World&, 정식 경로, 새 AssetPair 목록)` 이 `modelPath` 가 같은
  `StaticMeshComponent`(메시 · LOD · 충돌 · 머티리얼 · 정점 수) · `SkinnedMeshComponent`(기본 메시 · 머티리얼 · 스킨 · 바인드 정점)의 핸들을
  바꿔 끼웁니다. `meshIndex` 가 새 목록 밖이면 마지막 것으로 줄이고 경고합니다. `transparent` 는 사용자가 정한 값을 지킵니다. 같은 객체를 고쳐
  쓰는 방식(UE · Godot)은 쓰지 않습니다: LOD 수 · 스킨 유무 · 충돌 형상이 바뀔 수 있어 어차피 컴포넌트를 고쳐야 하기 때문입니다.
  물리 몸의 충돌 형상은 다시 만들지 않습니다(남음).
- **D7 씬 파일:** 바뀐 파일이 지금 연 씬이고, 디스크의 내용 해시가 에디터가 마지막으로 읽거나 쓴 내용과 다를 때만 반응합니다(에디터 자신의
  저장은 무시). 씬이 수정되지 않았고 Play 중이 아니면 "Scene Changed on Disk" 대화상자(다시 열기 · 그대로 두기)를 띄우고, `--auto-reload-scene`
  이면 묻지 않고 다시 엽니다(덤프 · 자동 확인용). 수정 중이거나 Play 중이면 경고 로그만 남깁니다(편집 내용을 덮지 않음).
- **D8 가져오기 설정 위치:** 프로젝트의 `/Config/import.ini`(프로젝트 `Config/` 폴더, `project.ini` 옆, 저장소 · 패키지에 함께 감)에 경로 패턴
  표로 둡니다. `.meta` 를 만들지 않는 결정(참조 체계 D6)과 맞고, 같은 종류 에셋을 폴더 단위로 묶어 정하는 일이 많아서입니다(Unity Preset ·
  UE Auto Reimport 규칙과 비슷한 자리). 형식:

  ```ini
  [/Game/models/props/**]
  scale   = 0.01          # 가져올 때 정점에 곱함(0.0001 ~ 10000)
  autoLod = false         # 자동 LOD 를 만들지 않음(기본 true)
  srgb    = false         # albedo · emissive 이미지가 선형으로 저장됨(기본 true)
  ```

  패턴은 정식 논리 경로에 맞추고 대소문자를 가리지 않습니다(`*` 는 `/` 를 넘지 않음, `**` 는 넘음, `?` 한 글자). 맞는 절을 파일 순서대로
  겹쳐 적용하므로 뒤 절이 이깁니다. 설정 값 묶음의 해시가 `.gasset` 캐시 키에 들어가, 설정을 바꾸면 그 모델만 다시 가져옵니다. 에디터는
  `import.ini` 가 바뀌면 표를 다시 읽고, 결과 설정이 바뀐 올라간 모델을 다시 읽습니다. 게임 실행 파일 · Android 도 시작할 때 같은 파일을
  읽어 에디터와 같은 결과를 냅니다.
  - `scale`: 정적 메시 · 모프 메시의 정점에 곱합니다(LOD · 충돌 · 바운드도 따라감). 뼈대 스킨 메시는 관절 · 클립의 이동값까지 바꿔야 해
    적용하지 않고 경고합니다(남음).
  - `srgb = false`: albedo · emissive 이미지 바이트를 선형 값으로 보고 가져올 때 sRGB 로 인코딩합니다(GPU 형식은 그대로 SRGB 라 샘플 결과가
    파일의 선형 값). 법선 · metalRough · AO 는 원래 선형이라 영향이 없습니다.
  - `autoLod = false`: 가져올 때 LOD 를 만들지 않습니다. 실행 단위 `--no-auto-lod`(올리지 않음)는 그대로 둡니다.
  - 압축: 엔진에 텍스처 압축 형식(BC · ASTC)이 아직 없어 칸을 만들지 않습니다(남음). 모르는 키 · 잘못된 값은 줄 번호와 함께 경고합니다.
- **D9 창 없는 확인 스위치:** 에디터 `--touch-asset-at=<프레임>:<논리 경로>`(여러 번). 그 프레임에 파일의 수정 시각만 지금으로 바꾸고(내용은
  그대로라 저장소 파일이 바뀌지 않음), 감시자 알림이 모여 처리될 때까지 최대 3 초 기다립니다. 처리 결과는 `HotReload` 분류 로그
  (`Hot reload: <경로> (N components rebound, retired M meshes, K materials)`)로 남습니다. 내용이 같으면 캐시는 그대로 쓰지만 업로드 · 다시
  묶기 · 은퇴는 모두 탑니다.
- **D10 범위 밖:** C++ 코드 핫리로드, Android 기기 위 핫리로드, 프리팹 파일 변경(다음 스폰부터 반영), 물리 충돌 형상 다시 만들기,
  [선택] 오디오 파일 다시 읽기 · 콘텐츠 브라우저 "다시 가져오기" 메뉴(남음 참고).

## 3. 체크리스트

### H1 감시 · 모으기 (editor + assets)

- [x] H1-1 `AssetWatcher`(에디터 전용, Windows `ReadDirectoryChangesW`, 하위 폴더): 프로젝트 폴더의 바뀐 파일을 정식 논리 경로로 내보냄,
      `Saved/` 등은 버림, 버퍼 넘침은 "모두 다시 확인".
- [x] H1-2 `AssetChangeQueue`: 경로별 300 ms 조용하면 한 번 내보냄, 분류(모델 · 의존 파일 · 씬 · 설정 · 무시).
- [x] H1-3 게임 실행 파일 · Android 에는 감시자가 없음(에디터 앞단만 만듦), 시작 로그는 Verbose(UI 덤프 그대로).

### H2 모델 · 텍스처 다시 읽기 (assets + components)

- [x] H2-1 `GltfLoader` 가 바깥 의존 파일 목록을 돌려주고 `.gasset`(버전 12)이 설정 해시 · 의존 파일을 기록 · 판정(DEF-174).
- [x] H2-2 `AssetManager::reloadModel`: 다시 가져와 올리고 → 다시 묶기 콜백 → 옛 메시 · LOD · 머티리얼을 은퇴 목록으로. 스킨 · LOD 포함.
- [x] H2-3 텍스처(의존 파일) 변경 → 그 이미지를 쓰는 올라간 모델들을 다시 읽음, 머티리얼이 새 이미지를 가리킴.
- [x] H2-4 `hreload::rebindModel`: 정적 · 스킨 메시 컴포넌트의 핸들을 바꿔 끼움, `meshIndex` 범위 처리.

### H3 씬 파일 (editor)

- [x] H3-1 열린 씬이 밖에서 바뀌면: 수정 없음 → 다시 열기 대화상자(`--auto-reload-scene` 이면 바로), 수정 중 · Play 중 → 경고만,
      에디터 자신의 저장은 무시.

### H4 가져오기 설정 (assets + runtime)

- [x] H4-1 `/Config/import.ini` 패턴 표(scale · autoLod · srgb), 뒤 절이 이김, 잘못된 줄 경고.
- [x] H4-2 설정 해시가 캐시 키에 들어감, `import.ini` 가 바뀌면 결과 설정이 바뀐 모델만 다시 읽음.
- [x] H4-3 엔진(에디터 · 게임)이 시작할 때 읽음.

### H5 시험 · 확인

- [x] H5-1 단위 시험 `asset_hot_reload`: 임시 폴더 변경 감지 → 큐, 디바운스, 패턴 일치 · 설정 해시 · 캐시 키 변화, 의존 텍스처 변경 → 캐시 무효,
      다시 묶기, 옛 자원 은퇴 수. 변이 확인 1 ~ 2개.
- [x] H5-2 창 없는 에디터 `--touch-asset-at` 으로 모델 · 씬 "파일 바꿈 → 다시 읽음" 확인(로그 · 은퇴 수 · 검증 오류 0).

### G 공통

- [x] G-1 Release · Debug 경고 0, ctest 전부 통과, 아키텍처 PASS.
- [x] G-2 기준 PNG 7장 그대로, UI 덤프 그대로(바뀌면 이유).
- [x] G-3 Android 빌드 통과.

## 4. 결과 (2026-11-20)

| 항목 | 결과 |
|---|---|
| 빌드 | Release · Debug 경고 0. Android 빌드 통과(새 경고 0, 남은 `GltfLoader.cpp` 의 `if (!warn.empty()) HLOG` · `json.hpp` 는 예전 경고) |
| ctest | 두 구성 39/39 통과, 검사 3,089(새 `asset_hot_reload` 58, 나머지 시험은 같은 수) |
| 아키텍처 | `HarukaArchTest` PASS, 허용 목록 0. 감시자 · 대화상자는 editor, 표 · 모으기 · 은퇴 목록은 assets, 다시 묶기는 components |
| 기준 PNG 7장(`base7.sh b_04`) | 모두 그대로, 검증 오류 0(캐시 버전 12 로 다시 만든 뒤에도 같은 결과) |
| UI 덤프(`uidump.sh b_04 b_03`) | 뷰포트 57장면 모두 같음. UI 는 56장면 같고 `log_categories` 1장면만 다름: 감시자가 시작할 때 `HotReload` 분류가 등록되어 분류 창에 한 줄(on · Log · 0)이 늘고 창이 그만큼 커짐. 새 기준 `b_04` |
| 모델 다시 읽기(H5-2) | `LightingProbe` 에서 `--touch-asset-at=10:/Game/models/model.glb` → 알림 · 모으기 · 처리 313 ms, `Hot reload: /Game/models/model.glb (1 components rebound, retired 4 meshes, 1 materials)`(본 메시 + 자동 LOD 3 단계), 검증 오류 0 |
| 스킨 메시 | `HandProbe` 에서 `TestGlove_R.glb` → 컴포넌트 2개 다시 묶음 · 메시 1 · 머티리얼 1 은퇴, 검증 오류 0 |
| 씬 파일 | 같은 내용으로 터치 → 무시(에디터 자신의 저장과 같은 길). 임시 씬을 밖에서 고침 → `--auto-reload-scene` 이면 다시 엶(22 액터), 아니면 대화상자 "Scene Changed on Disk"(Reload · Keep Current)가 UI 덤프에 보임 |
| import.ini | 실행 중 `[/Game/models/model.glb] scale = 2 · autoLod = false` 를 만듦 → `Import settings reloaded (1 rule(s)); 1 model(s) reimported, 1 components rebound`, 다시 가져올 때 자동 LOD 줄이 없음 |
| 변이 확인 | 캐시의 의존 파일 판정을 빼면 2 실패, 은퇴 목록에서 새 핸들 제외를 빼면 1 실패, DEF-175 수정 전 로더로는 격자 모델을 열지 못함(`File not found : grid.bin`) |
| 기존 결함 | DEF-174(.gltf 옆 텍스처만 바꾸면 캐시 그대로) · DEF-175(`/Game/` 경로로 연 .gltf 가 바깥 파일을 못 찾음)를 고치고 등재([Core_Defects_Task_Spec.md](Core_Defects_Task_Spec.md) 끝 절) |

**새 파일:** `src/assets/ImportSettings.hpp/.cpp`, `src/assets/AssetChangeQueue.hpp`, `src/assets/AssetReload.hpp`, `src/components/AssetRebind.hpp/.cpp`,
`src/editor/AssetWatcher.hpp/.cpp`, `src/editor/EditorApp_reload.cpp`, `tests/HotReloadTest.cpp`.

**바꾼 곳:** `GltfLoader`(가져오기 설정 · 의존 파일 · DEF-175), `GassetCache`(버전 12), `AssetStreamer`(요청별 설정 · 의존 파일), `AssetManager`(설정 표 ·
모델별 설정 해시 · 의존 파일 · 다시 읽기), `Engine`(시작할 때 import.ini), `EngineOptions`(`--touch-asset-at` · `--auto-reload-scene`), `EditorApp`(감시 ·
씬 내용 해시), 글자 표(대화상자 네 키, ko 번역).

**쓰는 법:**

```ini
# Projects/<프로젝트>/Config/import.ini
[/Game/models/props/**]
scale = 0.01
[/Game/models/props/big_*.glb]
autoLod = false
```

```sh
# 창 없이 확인: 10 번째 프레임에 model.glb 를 터치하고 다시 읽기 로그를 봄
MSYS2_ARG_CONV_EXCL="--touch-asset-at=" HarukaEngine.exe --hidden-window --gpu=1 --frames=40 --write-root=<임시> \
    --scene=assets/scenes/LightingProbe.hscene --touch-asset-at=10:/Game/models/model.glb
```

### 남음

- 창 · 헤드셋 실기: 제작 도구(Blender 등)에서 내보낼 때 한 번만 다시 읽는지, 대화상자 Reload · Keep Current, 수정 중인 씬에서 경고만 남는지.
- [선택] 오디오 파일 다시 읽기(소리 자원은 재생 중인 보이스가 붙들고 있어 별도 설계), 콘텐츠 브라우저 "다시 가져오기" 메뉴.
- 프리팹 파일 변경(다음 스폰부터 반영), 물리 몸의 충돌 형상 다시 만들기(지금은 다음 Play · 씬 열기부터).
- 뼈대 스킨 메시의 가져오기 배율(관절 이동 · 클립 이동값까지), 텍스처 압축 설정(압축 형식이 생긴 뒤, 감사 7위).
- 다시 읽기는 메인 스레드 동기입니다. 아주 큰 모델이면 한 번 멈칫할 수 있어, 필요하면 비동기 로딩 워커로 옮깁니다.
- 의존 파일의 시각만 바뀌면 판정할 때마다 내용 해시를 계산합니다(원본처럼 기록을 고쳐 쓰지 않음).
