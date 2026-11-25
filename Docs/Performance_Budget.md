# Haruka 성능 예산표 (Performance Budget)

Quest Pro 단독 실행(목표 90 Hz, 프레임 예산 **11.1 ms**) 기준으로 콘텐츠 한계를 측정하고 관리하기 위한 기준 문서입니다.

## 측정 방법: 부하 램프 (`--stress`)
- 한 실행에서 하나의 축 부하를 시작값부터 단계적으로 증가시키며 GPU · CPU 시간을 기록합니다.
- **기준(0개) 단계:** 모든 축은 먼저 `n = 0`(추가 물체 없음)을 재어 기본 비용(`base ms`)을 구하고, 이후 각 단계 줄에 기준 대비 증가분(`+Δ ms`)을 함께 적습니다.
- **버림 구간:** 각 단계의 첫 1초는 셰이더 · 캐시 · 전환 흔들림을 빼기 위해 버리고, 나머지 구간의 산술 평균을 사용합니다.
- **단위 비용 · 추정 한도:** 전체 단계의 최소제곱 선형 회귀 기울기로 축 단위 비용(100개당 ms, `tris`는 10만 삼각형당 ms)을 구하고, `(예산 - 기준 ms) ÷ 기울기`로 추정 한도를 계산합니다. 기준이 이미 예산을 넘으면 `over at base`로 표기합니다.
- **CPU work ms vs frame ms:** `frame ms`는 루프 간격(`xrWaitFrame` · 펜스 대기 포함)이고, `CPU work ms`는 대기를 뺀 실제 CPU 작업 시간입니다.
- **고정 비용 (`empty`):** 하늘과 바닥만 있는 빈 장면을 10초 측정해 AO · 블룸 · 톤매핑 · 해상도의 기본 비용을 먼저 확인합니다.
- **무릎점 (Knee point):** 프레임 예산을 넘지 않은 마지막 단계(`last under`)와 처음 넘은 단계(`first over`)로 정의합니다.
- **멈춤 조건:** GPU total이 `max(예산, 기준 ms) × 1.3`을 2단계 연속 넘거나 최대값에 도달하면 종료하고 CSV(`Saved/Stress/<축>_<시각>.csv`)를 저장합니다.

## 축 정의
| 축 | 측정 대상 | 기본 범위 (`start:step:max`) | 비고 |
|---|---|---|---|
| `empty` | 하늘 + 바닥 고정 비용 | 10초 단일 | AO · 블룸 · 톤매핑 · 해상도 기준선 |
| `draws` | 서로 다른 머티리얼 상자 `n`개 | `64:64:1024` | DEF-16 인스턴싱 묶음 없이 드로우 호출 `n` |
| `instances` | 같은 메시 · 머티리얼 상자 `n`개 | `128:128:4096` | 드로우 1회, 인스턴스 `n` |
| `tris` | 고정 16드로우 UV 구 분할 단계 | `8:8:96` | 물체당 삼각형 증가 |
| `lights` | 그림자 없는 점광원 `n`개 | `8:8:128` | 현재 구조상 방향광 1개만 그림자 캐스터 지원 |
| `overdraw` | 시야를 덮는 반투명 평면 `n`겹 | `4:4:64` | 픽셀 채움 · 블렌딩 비용 |
| `particles` | 파티클 이미터 입자 `n`개 | `500:500:16000` | `vfx::kMaxParticles` (65,536) 이내 |
| `panels` | 3D UI 패널 `n`개 | `4:4:64` | 월드 UI 패널 비용 |

## Quest Pro 실측 결과표 (90 Hz, 예산 11.1 ms)
> 내일 헤드셋 실측(`Handoff\Check_Quest.bat` → `[6] Stress ramp`) 후 채웁니다.
> Quest(Android)는 기본으로 `--render-tier=mobile`(깊이 전처리 끔 · AO 끔 · 블룸 끔)로 실행됩니다. Desktop 경로와 비교할 때는 `--render-tier=desktop`을, Mobile 티어에서 블룸을 켜고 비교할 때는 `--bloom`을 함께 넘깁니다.

| 축 | 기준 ms | 단위 비용 | 추정 한도 | Last Under | First Over | 목표 Hz | 비고 |
|---|---|---|---|---|---|---|---|
| `empty` | - | - | - | - | - | 90 | |
| `draws` | - | - | - | - | - | 90 | |
| `instances` | - | - | - | - | - | 90 | |
| `tris` | - | - | - | - | - | 90 | |
| `lights` | - | - | - | - | - | 90 | |
| `overdraw` | - | - | - | - | - | 90 | |
| `particles` | - | - | - | - | - | 90 | |
| `panels` | - | - | - | - | - | 90 | |

## 참고 기준 (Meta 권장)
- Meta Quest 개발자 가이드라인(Quest 2 기준): 프레임당 삼각형 **75만 ~ 100만**, 그리기 호출 **수백 수준**.
- Quest Pro(Adreno 650, 90 Hz)에서 고정 비용(`empty`)이 이미 11.1 ms에 근접하거나 넘으면 콘텐츠 축 측정 전에 해상도(`--xr-scale`) · 후처리(`--no-ssao`, `--no-bloom`) 최적화가 우선입니다.

## 고정 포비티드 렌더링 후보 (FFR 조사)
현재 엔진은 메인 패스가 HDR 중간 이미지(`hdrImage_`)에 그리고, 별도 톤매핑 패스(`PostPass`)가 XR 스왑체인에 씁니다. 따라서 런타임 FFR(`XR_FB_foveation`, 스왑체인 이미지에 붙는 밀도 맵)은 기본적으로 마지막 톤매핑 패스에만 걸립니다. 아래 세 가지 후보의 구조를 비교하고, Quest 실측(`perf` · `stress`) 결과를 본 뒤 적용 방향을 정합니다.

### (a) 런타임 FFR을 스왑체인에만 적용
- **필요한 확장 · 드라이버 지원:** `XR_FB_foveation`, `XR_FB_foveation_configuration`. Quest 런타임 기본 지원.
- **현재 구조와의 충돌:** 충돌 없음. 단, 비용의 대부분을 차지하는 메인 패스(MSAA · 셰이딩)와 AO · 블룸은 `hdrImage_` 기준이라 줄어들지 않고, 마지막 `PostPass` 톤매핑만 영향을 받음.
- **예상 이득:** 매우 작음(`PostPass`가 이미 ~0.5 ms 이하 수준일 가능성이 높음).
- **작업 크기:** 소 (`XrSystem` 스왑체인 생성 시 포베이션 프로파일 연결).
- **다른 엔진 방식:** Meta Quest 공식 문서에서 스왑체인 기반 `XR_FB_foveation`은 단일 패스 포워드 렌더러(스왑체인 직접 출력) 전제로 안내됨.

### (b) `XR_FB_foveation_vulkan` + `VK_EXT_fragment_density_map`을 메인 패스에 부착
- **필요한 확장 · 드라이버 지원:** OpenXR `XR_FB_foveation` + `XR_FB_foveation_vulkan`, Vulkan `VK_EXT_fragment_density_map` (Adreno 650/Quest Pro 지원).
- **현재 구조와의 충돌:** 메인 패스(서브패스 2개 · 멀티뷰 · 4x MSAA resolve) 프레임버퍼에 밀도 맵 첨부 추가 필요. AO · 블룸은 화면 공간 후처리라 별도 밀도 맵 적용이 어렵거나 `hdrImage_` 크기 그대로 돎. 3D UI 패널 HDR 알파(후처리 가중치) 구조는 그대로 유지 가능.
- **예상 이득:** 메인 패스 프래그먼트 셰이딩 비용 절감(주변부 밀도 감소). 단, 스왑체인 직접 출력이 아닌 중간 HDR 타깃에서도 FDM 이득이 온전히 나는지 실측 확인 필요.
- **작업 크기:** 중 (`XrSystem`에서 밀도 맵 이미지/레이아웃 수명 관리, `Renderer` 렌더 패스·프레임버퍼·파이프라인 `VK_PIPELINE_CREATE_RENDERING_FRAGMENT_DENSITY_MAP_ATTACHMENT` 대응).
- **다른 엔진 방식:** Unreal Engine(Vulkan RHI + OpenXR/Meta XR) 및 Unity(OpenXR + Meta Core SDK)가 중간 렌더 타깃을 유지할 때 `VK_EXT_fragment_density_map`을 사용해 주변부 셰이딩 레이트를 낮추는 표준 경로.

### (c) Quest에서 메인 패스가 스왑체인에 직접 그리고 셰이더 안에서 톤매핑
- **필요한 확장 · 드라이버 지원:** 추가 확장 불필요(기본 `XR_FB_foveation`만으로 메인 패스에 FFR 적용 가능).
- **현재 구조와의 충돌:** HDR 중간 이미지와 별도 `PostPass` 제거 시 AO · 블룸(중간 텍스처 필요) 및 3D UI 패널의 HDR 알파(후처리 제외 가중치) 경로를 재설계해야 함. 또한 4x MSAA resolve를 스왑체인에 직접 할 때 색공간(sRGB)과 톤매핑 순서 정합성 필요.
- **예상 이득:** 큼(HDR 중간 타깃 메모리·대역폭 제거 + `PostPass` 제거 + 기본 `XR_FB_foveation` 메인 패스 적용).
- **작업 크기:** 대(Quest 전용 포워드 패스 분기, 후처리·3D UI 패널 가중치 대안 설계 필요).
- **다른 엔진 방식:** Unity URP / Unreal Mobile Forward의 Quest 권장 경로는 포스트프로세싱(블룸·AO)을 끄고 스왑체인에 직접 그리며 `XR_FB_foveation`을 활용함.

### 판단 기준 (실측 후 결정)
1. `perf`에서 `tonemap` 비용이 무시할 수준이고 `main` + `ao` + `bloom`이 지배적이면 (a)는 제외.
2. `ao` · `bloom`을 유지하면서 `main` 프래그먼트 비용을 줄여야 하면 (b)를 선택.
3. `empty` 고정 비용(중간 타깃 대역폭 · `ao` · `bloom` · `tonemap`) 자체가 11.1 ms 예산의 대부분이면 (c)(또는 `--no-ssao` + `--no-bloom` 조합)를 검토.

## 헤드셋 안 오버레이 (`stat vr` / `--vr-stats` / 왼손 X)
- Quest 단독과 PC VR Play 모두에서 헤드셋을 쓴 채 FPS · GPU 패스별 ms · CPU work ms · 해상도/MSAA/성능 레벨 · 힙 청크/RSS · 드로우/삼각형 수를 바로 확인할 수 있는 엔진 소유 3D UI 패널입니다(`VrStatsPanel`, 0.5초마다 갱신, 예산 색 구분).
- **토글:** 왼손 컨트롤러 X 버튼(`project.ini` `debug.vrStatsButton`, 기본 `x`, `y`/`none` 선택 가능) · 콘솔 명령 `stat vr` · 시작 인자 `--vr-stats`. 게임 입력이 해당 버튼에 바인딩된 경우에는 토글하지 않습니다(3D UI 패널 클릭은 트리거 사용).
- **측정 표시:** 오버레이가 켜진 동안에는 `--gpu-log` 줄과 `--stress` 단계 줄에 `overlay on`이 붙습니다.
