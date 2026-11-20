# 텍스처 압축 (2026-11-21)

[Engine_Foundation_Gaps.md](Engine_Foundation_Gaps.md) 의 7위 후보입니다. 지금은 머티리얼 텍스처를 모두 RGBA8 비압축으로 올리고, 밉은
업로드할 때 GPU 블릿으로 만듭니다. 이 문서는 작업 지시서가 아니라 **완료 조건 체크리스트**이고, 항목을 모두 만족하면 끝입니다.

## 1. 조사 요약

### 1-1. 지금 코드 (2026-11-21)

| 곳 | 지금 | 문제 |
|---|---|---|
| `GltfLoader::extractTexture` | tinygltf(stb_image)가 PNG · JPEG 를 RGBA8 로 풀고, 머티리얼 팩터를 화소에 곱해 굽습니다(DEF-47) | 결과는 언제나 RGBA8 |
| `.gasset` 캐시 | 메시마다 다섯 장(albedo · normal · metalRough · ao · emissive)의 RGBA8 화소를 그대로 저장 | 압축 데이터 자리가 없음 |
| `GpuScene::uploadMaterial` | 다섯 장을 한 명령 버퍼에 기록, albedo · emissive 는 `R8G8B8A8_SRGB`, 나머지는 `UNORM` | 화소당 4바이트(밉 포함 5.33바이트) |
| `MipChain.hpp` | 0단계를 복사한 뒤 선형 블릿으로 밉 사슬(DEF-165), 주석에 "ASTC · KTX2 를 들일 때 옮김" | 압축 형식은 블릿할 수 없음 |
| 비동기 로딩 | 워커가 `AssetStreamer::decode`(캐시 → glTF), 본 스레드가 예산 안에서 `uploadModel` | 압축 텍스처를 붙일 단계가 없음 |
| HarukaPak | 목록 파일(`Packaging/game.pack.txt`)대로 낱개 파일을 묶음(.glb 는 원본 그대로) | 묶을 때 하는 변환이 없음 |
| 장치 생성 | `textureCompressionBC` · `textureCompressionASTC_LDR` 기능을 켜지 않음 | 켜지 않은 압축 형식은 쓸 수 없음 |
| 샘플 콘텐츠 | 모델 다섯 개 모두 이미지 텍스처가 없음(팩터만, 1x1) | 측정할 텍스처가 없음 → 시험용 모델을 새로 만듦 |

### 1-2. 다른 엔진

- **Unreal:** 가져올 때 원본을 보관하고, 쿠킹할 때 플랫폼별 형식으로 압축(PC = BC1 · BC3 · BC5 · BC7, Android = ASTC · ETC2)합니다.
  텍스처마다 "Compression Settings"(Default · Normalmap · Masks 등)가 형식을 고르고, 결과는 파생 데이터 캐시(DDC)에 원본 해시로 저장합니다.
  노멀 맵은 BC5(두 채널)이고 셰이더가 Z 를 복원합니다. 밉은 쿠킹 때 미리 만듭니다.
- **Unity:** 텍스처 형식(Default · Normal map · Single channel)과 플랫폼별 덮어쓰기(Windows = BC7 · DXT5, Android = ASTC 블록 크기 선택)가
  가져오기 설정에 있고, 결과는 Library 캐시에 둡니다. 장치가 형식을 지원하지 않으면 실행할 때 RGBA32 로 풀어 올립니다.
- **Godot 4:** 가져올 때 VRAM 압축(데스크톱 = BPTC(BC7) · S3TC, 모바일 = ASTC · ETC2)을 `.ctex` 로 저장, 노멀 맵은 RG 두 채널(BC5 · ASTC)이고
  셰이더가 Z 를 복원합니다. 지원하지 않는 형식은 실행할 때 비압축으로 풉니다.
- **Khronos KTX2:** 머리(형식 · 크기 · 단계 수) · 단계 색인(오프셋 · 길이) · 데이터 형식 설명(DFD) · 키값 · 데이터 순서의 컨테이너.
  단계 데이터는 작은 밉부터 저장하고 블록 크기 배수로 정렬합니다. Basis 범용 형식(트랜스코딩)과 zstd 같은 supercompression 은 선택입니다.

### 1-3. 공통 구조

"원본은 그대로 두고, 묶을(쿠킹) 때 플랫폼별 블록 압축 + 미리 만든 밉", "용도(색 · 노멀 · 마스크)가 형식을 고름", "결과는 내용 해시로 캐시",
"노멀은 두 채널 + 셰이더 Z 복원", "장치가 형식을 지원하지 않으면 비압축으로 대체", "런타임에는 인코더가 없음".

## 2. 결정

- **D1 컨테이너는 KTX2, supercompression 없음.** 읽기 · 쓰기는 외부 라이브러리 없이 `geometry/Ktx2` 가 합니다(머리 80바이트 · 단계 색인 ·
  DFD 기본 블록 하나 · 키값 없음). 단계 데이터는 규격대로 작은 밉부터, 16바이트(블록 크기와 4의 최소공배수) 정렬입니다. 읽기는 식별자 ·
  1개 면 · 배열 아님 · 깊이 0 · supercompression 0 · 단계 수 · 각 단계가 파일 안에 있고 크기가 형식과 크기로 계산한 값과 같은지 봅니다.
  DFD 는 범위만 확인하고 내용은 믿지 않습니다(형식은 `vkFormat` 하나로 정함). zstd 를 쓰지 않는 이유: 묶음 파일이 이미 LZ4 를 고를 수 있고,
  BC · ASTC 블록은 일반 압축이 잘 줄이지 않으며(LZ4 로 몇 % 수준), zstd 를 넣으면 런타임 외부 의존이 생깁니다.
- **D2 형식 표(용도 × 대상).** 용도는 셋입니다: 색(albedo · emissive, sRGB) · 노멀 · 선형(metalRough · ao).

  | 용도 | PC(BC) | Android(ASTC) |
  |---|---|---|
  | 색 | `BC7_SRGB` | `ASTC_6x6_SRGB`(3.56 bpp) |
  | 노멀 | `BC5_UNORM`(RG) | `ASTC_5x5_UNORM`(5.12 bpp), 스위즐 RG01 |
  | 선형 | `BC7_UNORM` | `ASTC_6x6_UNORM` |

  노멀은 두 채널입니다(BC5 의 B 는 0 으로 읽히고, ASTC 는 B = 0 · A = 1 로 스위즐해 압축). `pbr.frag` 는 노멀 텍스처의 B 가 0 이면
  Z = √(1 − x² − y²) 로 복원합니다. 접공간 노멀 맵의 B 바이트 0 은 Z = −1(표면 안쪽)이라 실제 노멀 맵에는 나오지 않으므로, 기존 RGBA8 노멀
  (기본값 128,128,255 포함)은 같은 계산을 그대로 탑니다(기준 PNG 그대로). ASTC 노멀을 Meta · ARM 권장(4x4 ~ 6x6) 안에서 색보다 한 단계 작은
  5x5 로 둔 것은 노멀 오차가 조명에 바로 드러나기 때문입니다. 한 변이 4보다 작은 텍스처(팩터만 있는 1x1 등)는 압축하지 않습니다.
- **D3 품질.** ASTC 는 astcenc `medium` 사전값(60), 색은 sRGB 프로필 + 지각 가중치 없음(기본), 노멀은 `ASTCENC_FLG_MAP_NORMAL` 없이
  RG01 스위즐(그 깃발은 X 를 RGB, Y 를 A 에 넣는 rrrg 배치를 요구해 셰이더 복원 규칙이 둘이 됨). BC7 은 bc7enc(모드 1 · 6) uber 1 ·
  분할 64 · 지각 가중치(색), 선형 가중치(선형). BC5 는 rgbcx `encode_bc5_hq`. 수치는 4절.
- **D4 밉은 오프라인에서.** 2x2 상자 필터, 크기 규칙은 Vulkan 과 같음(`max(1, n >> i)`, 홀수 변은 마지막 화소를 반복). 색은 선형으로 풀어
  평균한 뒤 sRGB 로 다시 씀(지금 GPU 블릿과 같은 감마 처리), 알파는 그대로 평균, 노멀은 벡터로 평균한 뒤 정규화. 1x1 까지 만듭니다.
- **D5 무엇을 열쇠로 찾나.** 열쇠 = XXH3-64(최종 RGBA8 화소) 와 너비 · 높이 · 용도 · 압축 판(`kTexCookVersion`)을 섞은 64비트. "최종"은
  glTF 로더가 팩터를 굽고 가져오기 설정(`/Config/import.ini` 의 sRGB 등)을 적용한 뒤의 화소입니다. 그래서 묶음 도구는 런타임과 **같은
  glTF 로더 코드**를 링크해 같은 화소를 얻습니다. 열쇠가 맞지 않으면(원본이 바뀌었는데 다시 묶지 않음 등) 찾지 못하고 RGBA8 로 갑니다(안전한 실패).
  논리 경로는 `textures/<bc|astc>/<열쇠 16자리>.ktx2` 입니다.
- **D6 묶을 때 압축(HarukaPak).** 목록 파일에 `textures <대상> <캐시 폴더> <모델 패턴>...` 줄을 둡니다. 대상은 `-D TEXTURE_TARGET`(Windows
  묶음 = `bc`, Android 묶음 = `astc`, `off` 면 건너뜀). 묶음에 들어가는 모델(.glb · .gltf, exclude 뒤)마다 로더로 읽어 텍스처를 압축하고,
  결과를 `<캐시>/textures/<대상>/<열쇠>.ktx2` 에 둡니다. 파일이 있고 읽히면 다시 압축하지 않습니다(캐시). 묶음에는 Stored 로 넣습니다.
  원본 .glb 는 그대로 묶음에 남습니다(대체 경로 · 충돌 형상 · 에디터가 씀). `HarukaPak cook-textures <프로젝트> <대상> <폴더>` 는 묶음 없이
  압축만 합니다(개발 · 측정용).
- **D7 외부 라이브러리는 HarukaPak · 시험에만.** astc-encoder 5.7.1(Apache-2.0, SSE4.1 · POPCNT 정의로 소스를 직접 컴파일, CLI 제외) 과
  bc7enc_rdo(MIT 또는 공개 도메인, `bc7enc.cpp` · `bc7decomp.cpp` · `rgbcx.cpp` 세 파일만). 둘 다 `URL` + `URL_HASH SHA256` 로 고정하고
  저장소의 CMake 는 쓰지 않습니다. 인코더 감싸개는 `tools/TextureEncode`, 쿠킹은 `tools/TextureCook`. 게임 런타임 · Android 실행 파일에는
  들어가지 않습니다(`HarukaRuntime` 은 셰이더 목록 때문에 HarukaPak 에 기대므로, 로더 소스를 HarukaPak 에 직접 넣음). 아키텍처 규칙의 `tools`
  모듈에 `math · geometry · assets` 를 더했습니다. 에디터 가져오기 쪽 압축은 하지 않았습니다(에디터는 D8 대로 원본을 씀, 남음).
- **D8 런타임 선택.** `--texture-compression=off|auto|bc|astc`(기본 `auto`). `auto` 는 게임 묶음을 마운트한 게임 실행 파일이고 `--headless` 가
  아닐 때만 플랫폼 대상(PC = bc, Android = astc), 그 밖(에디터 · 창 없는 실행 · 낱개 파일)은 `off` 입니다. 그래서 기준 PNG 는 그대로입니다.
  `--texture-dir=<폴더>` 는 `cook-textures` 결과 폴더를 `textures/` 로 마운트합니다(개발 · 측정). 대상은 렌더러를 만들기 전에 정하고(D10),
  `off` 이면 시작 로그도 Verbose 로만 남깁니다(에디터 Output Log 를 늘리지 않음).
- **D9 붙이는 곳은 워커.** `AssetStreamer::decode` 가 캐시 · glTF 를 읽은 뒤 대상이 있으면 텍스처마다 열쇠를 계산하고 VFS 에서 KTX2 를 읽어
  `TexSourceData::compressed` 에 붙입니다(본 스레드는 읽지 않음). RGBA8 화소는 대체 경로 때문에 남깁니다(업로드 뒤 원본과 함께 풀림).
- **D10 장치 지원 확인과 대체.** 장치 생성 때 그 실행의 대상이 쓰는 기능 하나만(`bc` → `textureCompressionBC`, `astc` →
  `textureCompressionASTC_LDR`) 장치가 지원할 때 켭니다(`hvk::planDevice` 의 `wantBC` · `wantASTC`). 대상이 `off` 면 아무것도 켜지 않아 장치 생성이
  예전과 같습니다. 처음에는 지원하면 늘 켰는데, Quest Pro 형 프로필 층(`qp.sh`)이 ASTC 를 지원한다고 알려 실제 RTX 에서 장치 생성이
  `VK_ERROR_FEATURE_NOT_PRESENT` 로 실패했습니다(기준 PNG 5 ~ 7). 쓰는 기능만 켜는 것이 Vulkan 의 원칙에도 맞습니다. 업로드할
  때 그 기능이 켜졌고 형식이 `SAMPLED_IMAGE` · `SAMPLED_IMAGE_FILTER_LINEAR` 를 지원하면(형식마다 한 번 질의) 압축 경로, 아니면 붙은 KTX2 를
  버리고 RGBA8 원본(블릿 밉)으로 올립니다. 형식마다 한 번 경고합니다. 런타임 디코더는 두지 않습니다(원본 화소가 이미 있음).
  예: 데스크톱 NVIDIA 는 ASTC 를 지원하지 않아 `--texture-compression=astc` 가 대체 경로 시험이 됩니다.
- **D11 업로드.** 압축 경로는 KTX2 의 단계 데이터를 스테이징 하나에 그대로 복사하고, 단계마다 `VkBufferImageCopy` 하나씩, 블릿 없이
  모든 단계를 한 번에 옮깁니다. 이미지 용도는 `TRANSFER_DST | SAMPLED`.
- **D12 메모리 기록.** `GpuScene` 이 머티리얼 텍스처의 데이터 바이트(올린 단계의 합)와 같은 텍스처를 RGBA8 + 밉으로 올렸을 때의 바이트를
  셉니다(기본값 1x1 은 제외, 지우면 뺌). 텍스처 수가 바뀐 모델 업로드마다 `Textures after <모델>:` 로그에 압축 · 비압축 장 수 · 바이트 ·
  비율과 핵심 힙 Zone B 사용량(`HarukaAllocator::zoneBUsed`, 빈 목록 합의 나머지)을 남깁니다. 압축 텍스처가 있을 때만 Log, 없으면 Verbose.
- **D13 시험 콘텐츠.** 샘플 모델에는 이미지가 없으므로 `src/tools/make_test_texture_model.py` 가 절차 텍스처(색 · 노멀 · 선형 · 방출,
  512² · 256²) 모델 `models/TexProbe.glb` 와 장면 `scenes/TextureProbe.hscene` 을 만듭니다. 측정과 PSNR 비교는 이 장면으로 합니다.

## 3. 체크리스트

- [x] [필수] 오프라인 압축: 색 = BC7 sRGB / ASTC 6x6 sRGB, 노멀 = BC5 / ASTC 5x5(RG01), 선형 = BC7 / ASTC 6x6, 밉 미리 생성, 결과 KTX2.
      PC 묶음은 BCn, Android 묶음은 ASTC, 캐시로 다시 압축하지 않음. (HarukaPak `textures` 줄 · `cook-textures`, 캐시 `build/texture_cache`)
- [x] [필수] 런타임 KTX2 읽기 + 장치 지원 확인(기능 · 형식 질의), 지원하지 않으면 RGBA8 원본으로 대체. (ASTC 를 이 PC 에서 고르면 대체 →
      원본과 같은 PNG)
- [x] [필수] 비동기 업로드 경로로 미리 만든 밉을 복사(블릿 생략), 1GB 힙 사용량 로그. (워커가 붙이고 `uploadMaterial` 이 단계별 복사,
      `Textures after` 로그)
- [x] [필수] 에디터 · 창 없는 실행은 원본이 기본(기준 PNG 7장 그대로), 압축은 `--texture-compression` 또는 묶은 게임.
- [x] [필수] 단위 시험: KTX2 머리 · 단계 파싱(정상 · 손상 14가지), 형식 선택 표, 대체 경로, 압축 → 디코드 PSNR 하한. (`texture_compression`
      72 검사, `vulkan_baseline` +4)
- [x] [필수] 측정: 텍스처 메모리(비압축 대비 BCn · ASTC 비율), 압축 경로 PNG 의 PSNR → 4절.
- [x] [필수] Android 빌드 통과, Android 묶음에 ASTC KTX2 가 들어가는지 묶음 목록으로 확인(실행은 창 확인 목록).
- [ ] [선택] 가져오기 설정에서 텍스처별 압축 끔 · 품질. → 하지 않음(4-4 남음).

## 4. 결과

### 4-1. 구현 위치

| 곳 | 내용 |
|---|---|
| `geometry/Ktx2` | KTX2 쓰기 · 읽기(D1), 블록 크기 · 단계 바이트 · 형식 이름 |
| `assets/TextureCompression` | 대상 · 용도 · 형식 표(D2), 열쇠 · 논리 경로(D5), `attachCompressed`(D9) |
| `assets/AssetStreamer` · `AssetManager` | 워커 · 동기 · 다시 읽기 경로에서 KTX2 붙이기, 텍스처 메모리 로그(D12) |
| `renderer/TextureUpload.hpp` · `GpuScene` | 장치 지원 판단(D10), 단계별 복사 업로드(D11), 텍스처 바이트 집계 |
| `renderer/VulkanBaseline.hpp` · `Renderer` | 대상이 쓰는 압축 기능만 켬(D10) |
| `shaders/pbr.frag` | 노멀 B = 0 이면 Z 복원(D2) |
| `runtime/Engine` · `EngineOptions` | `--texture-compression` · `--texture-dir`, auto 판단(D8) |
| `tools/TextureEncode` · `TextureCook` · `HarukaPak` | 밉 · 인코더 감싸개 · 쿠킹 캐시 · `textures` 줄 · `cook-textures`(D3 · D4 · D6) |
| `Packaging/game.pack.txt` · `cmake/HarukaProject.cmake` | `textures ${TEXTURE_TARGET} ${TEXTURE_CACHE} *.glb *.gltf`, Windows = bc · Android = astc |
| `src/tools/make_test_texture_model.py` | 시험 모델 `models/TexProbe.glb`(약 0.6 MB) · 장면 `scenes/TextureProbe.hscene`(D13) |

### 4-2. 측정 (2026-11-21, RTX 3080 · GPU 1)

**텍스처 메모리.** 샘플 프로젝트의 모델 6개 중 이미지 텍스처가 있는 것은 TexProbe 하나입니다(나머지 다섯은 팩터만 있어 1x1, 15장은
"너무 작음"으로 RGBA8 그대로). 아래는 압축할 크기의 서로 다른 텍스처 4장(albedo 512² · normal 512² · metalRough 256² = ao · emissive 256²)의 밉 포함 합입니다.

| 대상 | 바이트 | RGBA8 대비 | 묶음 안 KTX2 4개 |
|---|---|---|---|
| RGBA8 + 밉 | 3.33 MB | 100% | - |
| BC(PC) | 0.83 MB | 25.0% | 875 KB |
| ASTC(Android) | 0.44 MB | 13.3% | 468 KB |

실행 중 GPU 집계(`Textures after /Game/models/TexProbe.glb`, TextureProbe 장면): 원본 7장 3.67 MB → BC 5장 압축 + 2장 RGBA8 0.92 MB(25%),
핵심 힙 Zone B 사용량 486.6 MB → 482.0 MB(스테이징 · 정렬 포함, 896 MB 중).

**압축 경로 PNG 의 PSNR.** `TextureProbe.hscene` 합성 눈 1024², 60 프레임, `probe.sh` 와 같은 명령(아래).

| 실행 | PNG | 원본 대비 |
|---|---|---|
| 원본(RGBA8, `off`) | `c266fddc` | - |
| BC(`--texture-compression=bc --texture-dir=<cook-textures 결과>`) | `80bf638e` | PSNR 40.05 dB, 최대 차이 114(반사 하이라이트), 다른 화소 31% |
| BC, 묶은 게임(`game.hpak` 옆 실행 파일 + `--texture-compression=bc`) | `80bf638e` | 위와 같음(묶음 안 KTX2 찾기 확인) |
| ASTC(이 GPU 는 ASTC 없음 → 대체) | `c266fddc` | 원본과 같음(대체 경로 확인) |

Release · Debug 모두 같은 해시이고 검증 오류 0 입니다. 눈으로 보면 두 영상의 차이를 찾기 어렵고, 노멀(BC5 + Z 복원)의 조명도 같습니다.

```
HarukaPak cook-textures Projects/Samples bc <폴더>          (Git Bash 에서 /Game 인자는 MSYS2_ARG_CONV_EXCL="/Game")
HarukaGame --headless --no-xr --gpu=1 --synthetic-eyes=1024x1024 --frames=60 --scene=assets/scenes/TextureProbe.hscene \
           --texture-compression=bc --texture-dir=<폴더> --capture=<png>
```

**텍스처별 PSNR(0단계, 압축 → 풀기).** ASTC 는 이 PC 에서 그릴 수 없어 시험(`texture_compression` 8절)이 풀어서 잽니다.

| 텍스처 | BC | ASTC |
|---|---|---|
| TexProbe albedo 512²(색) | 49.88 dB | 45.11 dB(6x6) |
| TexProbe normal 512²(RG) | 46.84 dB | 43.88 dB(5x5) |
| TexProbe metalRough 256²(선형) | 53.10 dB | 46.25 dB(6x6) |
| TexProbe emissive 256²(색) | 53.17 dB | 50.42 dB(6x6) |
| 시험 영상 64²(기울기 · 바둑판 · 1화소 줄무늬, 색 / 선형) | 41.68 / 42.81 dB | 37.51 / 37.49 dB |
| 시험 노멀 64²(돌기 + 고주파 물결, RG) | 48.05 dB | 39.31 dB |

시험의 하한은 측정값보다 약 3 ~ 6 dB 낮게 두었습니다(BC7 ≥ 38, BC5 ≥ 42, ASTC 6x6 ≥ 30, ASTC 5x5 ≥ 36, TexProbe BC ≥ 42 · ASTC ≥ 38).

**쿠킹 시간 · 캐시.** TexProbe 4장을 BC 로 0.7 초(bc7enc 한 스레드), ASTC 로 0.4 초(astcenc 8 스레드). 다음 묶음은 캐시에서 읽어 0.0 초
(Android 두 번째 빌드 "0 encoded, 4 cached"). 내려받기는 astc-encoder 5.7.1 35.3 MB · bc7enc_rdo 15.1 MB(빌드 구성 때 한 번).

### 4-3. 검증

- 빌드 경고 Release 0 · Debug 0, ctest 42/42(Release · Debug), 검사 3,364 → 3,440(`texture_compression` 72 새로, `vulkan_baseline` 84 → 88).
- 변이 확인: KTX2 읽기의 단계 길이 확인을 빼면 1 실패, 열쇠에서 용도를 빼면 1 실패.
- 기준 PNG 7장 그대로(검증 오류 0). UI 덤프는 콘텐츠 브라우저에 새 장면 · 모델이 보이는 3장면만 바뀜(새 기준 `b_08`, 뷰포트 57 같음).
- Android 빌드 통과(바꾼 줄의 경고 없음), `build/android/pack/game.hpak` 에 `textures/astc/*.ktx2` 4개(`HarukaPak list`). 묶음은 지금처럼
  adb 로 넣습니다(APK 안에는 아직 없음, Packaging_Design).

### 4-4. 남음

- 헤드셋에서 ASTC 경로 실행(Quest 의 `Block-compressed textures: ASTC enabled` · `uploaded as is` 로그, 화면 · 메모리) — 창 확인 목록.
- 창 있는 묶은 게임(auto → bc)에서 눈 확인.
- [선택] 가져오기 설정(`import.ini`)의 텍스처별 압축 끔 · 품질 · 블록 크기. 에디터 가져오기 · 미리보기의 압축 경로.
- ASTC 를 그린 PNG 의 PSNR(장치에서), ASTC 노멀을 `ASTCENC_FLG_MAP_NORMAL`(rrrg) 로 했을 때와의 품질 비교.
- 여러 텍스처를 동시에 압축(지금은 텍스처를 차례로, ASTC 만 텍스처 안에서 여러 스레드), KTX2 키값(`KTXwriter` · 방향) 쓰기.
- Quest Pro 형 프로필 층으로 `--texture-compression=astc` 를 PC 에서 돌리면 층이 ASTC 를 있다고 알려 장치 생성이 실패합니다(층의 한계, 실제
  Quest 는 지원). 대상이 `off` 인 기준 PNG 에는 영향이 없습니다.
- 범위 밖: Basis 범용 압축(트랜스코딩), 스트리밍 밉, ETC2(ASTC 가 없는 Android 기기).
