# 수학 보강 (2026-11-17)

엔진의 수학이 렌더 · 소리 · 물리 기능을 보조하는 수준에 머물러 있다는 사용자 지적에서 시작한 조사와 적용 기록입니다. 코드를 읽어 빠진 수학을
찾고(일반 · 최적화 · 소리 · 충돌), 판단이 필요 없는 항목부터 구현했습니다. 판단이나 측정이 필요한 항목은 마지막 절에 남깁니다.

## 1. 조사 결과 요약

- 렌더러 수학은 깊습니다: SH9 확산 조도(`SkyIbl.hpp`), GGX 프리필터, Gribb-Hartmann 절두체(`FrustumCull.hpp`), 섀도 텍셀 스냅(`ShadowFit.hpp`).
- 물리는 Jolt 에 맡깁니다(고정 스텝 + Kinematic slerp 분할, 힘 충격량을 스텝에 나눔).
- 소리 수식은 맞습니다: 거리 감쇠 세 모델(OpenAL 식), 차폐 차단 주파수의 로그 보간, 1차 저역 통과 계수 exp(−2π·fc/fs), dt 독립 지수 평활,
  RT60 ↔ 콤 되먹임(t = 3τ / −log10 g), 잔향 구역 가중치, HRTF 쌍선형 보간.
- 빠진 것: 공통 수학 계층(`src/math`)이 없어 광선 · 평면 판정을 기능마다 따로 짜고 허용 오차 리터럴이 흩어져 있음, 곡선 · 스프링 · 노이즈 · 시드
  난수 없음, 연속 충돌 없음, 볼록 · 메시 충돌체 없음, 도플러 · 공기 흡수 없음, 들리지 않는 소리도 믹싱.

## 2. 적용한 것

| ID | 내용 | 위치 |
|---|---|---|
| A1 | **던지기 속도(DEF-168).** 잡은 물체는 손과 같은 각속도로 도는 강체이므로 질량 중심 속도는 v = v_손 + ω × (질량 중심 − 손)(강체 속도장). 여기에 리그의 스틱 이동 속도를 더하고, 놓는 순간 한 프레임 대신 최근 5프레임 평균을 씁니다. 질량 중심은 잡을 때 Jolt 에서 읽어 액터 기준으로 기억하고, 놓을 때 지금 손 자세로 옮깁니다(물체는 한 틱 늦은 자리). | `XrComponents` (`hxr::throwVelocity` · `VelocityHistory`, `RigLocomotionComponent::velocity`), `PhysicsSystem::centerOfMass` |
| A2 | **셰이더 시간.** `FrameUBO.timeParams.x` 가 프레임 번호 ÷ 60 이라 90 Hz 에서 1.5배 빨랐습니다. 엔진 틱 시간을 double 로 쌓아 3600 초로 접은 float 로 넘깁니다. | `Renderer::advanceTime` · `shaderTime` |
| O1 | **법선 · 접선 변환(DEF-169).** 법선은 정점마다 역행렬 대신 여인수 행렬 × 행렬식 부호(정규화하면 1/det 는 사라짐). 접선을 법선 행렬로 옮기던 오류를 모델 행렬로 고치고, 거울 변환이면 B 부호를 뒤집습니다. glTF 가져오기에도 같은 오류가 있어 고쳤습니다(캐시 버전 8). | `pbr.vert`, `GltfLoader.cpp`, `GassetCache.cpp` |
| O5 | **섀도 PCF 4탭.** 3×3 쌍선형 비교 9탭의 텍셀 가중치는 축마다 (1−f, 1, 1, f)/3 이라 앞 · 뒤 두 텍셀씩 쌍선형 탭 하나로 읽으면 같은 커널이 됩니다(Castaño). 차이는 하드웨어 쌍선형 가중치 반올림뿐(창 없는 비교에서 반그림자 경계 화소 0.1 % 만 1단계). | `pbr.frag` |
| O6 | **화면 크기 LOD.** 화면 크기 = r · projY / d(구의 투영 지름 ÷ 화면 높이, UE Screen Size 와 같은 정의)와 ±10 % 이력 띠. 예전 거리² 문턱은 시야각 · 해상도와 무관했고 채우는 코드도 없었습니다. | `ScreenSize.hpp`, `StaticMeshComponent::lodScreenSizes` |
| O7 | **작은 물체 컬링.** 지름 1화소 미만이면 카메라 목록에서 뺍니다(그림자 목록은 그대로). 두 눈을 덮는 합친 절두체는 이득(판정 6면 ↔ 12면)이 작고 캔트 헤드셋에서 틀릴 수 있어 하지 않았습니다. | `Renderer_render.cpp` |
| P1 | **연속 충돌.** RigidBody `continuous`(Jolt LinearCast), 던진 물체는 놓는 순간 그 바디만 켭니다. 90 Hz 에서 10 m/s 는 스텝당 11 cm 라 작은 물체가 얇은 벽을 뚫었습니다. 시험: 30 m/s · 지름 5 cm 공이 2 cm 벽을 끄면 뚫고(5.97 m) 켜면 막힘. | `RigidBodyComponent`, `PhysicsSystem::setContinuous` |
| P2 | **볼록 껍질 · 메시 충돌체.** 메시를 불러올 때 위치 · 인덱스를 CPU 에 남기고(`CollisionMesh`), RigidBody 셰이프 `convex` · `mesh` 가 같은 액터의 메시로 만듭니다. 점마다 월드 크기를 곱해 비균등 크기도 정확하고, 충돌체가 피벗과 무관하게 메시 자리에 있습니다. Mesh(오목)는 Static · Kinematic 전용이라 Dynamic 이면 경고 후 볼록 껍질(꼭짓점 256개 안으로 Jolt 가 줄임). | `geometry/CollisionMesh.hpp`, `AssetPair::collision`, `Component::collisionMesh`, `PhysicsSystem` |
| S3 | **들리지 않는 소리의 가상화.** 예상 게인(볼륨 × 거리 감쇠 × 차폐)이 −60 dB 아래면 보이스를 멈추고 재생 위치만 세다가 −54 dB 위로 오면 그 위치로 되감아 이어 냅니다(6 dB 이력, FMOD vol0virtualvol 과 같은 생각). 2D 소리는 제외, 일시정지 동안 위치 멈춤, 반복 아닌 소리는 들리지 않는 채로 끝나면 끝남. | `AudioAttenuation.hpp` (`hvirt`), `AudioSystem::updateVirtual` |
| S3b | **최대 거리 밖 무음.** `silentBeyondMax`(UE Falloff Distance 의미): 감쇠 곡선을 (g − g_max) / (1 − g_max) 로 다시 맞춰 최대 거리에서 0. miniaudio 곡선에 배율을 볼륨으로 곱합니다. 기본(끔)은 예전처럼 최대 거리 값(Inverse 기본 −40 dB)에 머뭅니다. | `AudioAttenuationSettings`, `AudioComponent` |

확인: 검사 1,549 → 1,606(`physics_lifecycle` 129 → 154, `audio_system` 191 → 210, `stereo_views` 18 → 31), Release · Debug ctest 25/25, 기준 PNG
기본 장면 `e2f7482c…`(512²) · `UIPanelProbe` `78c43c9d…`(Release = Debug, Debug 검증 0). 헤드셋(Quest Pro · Link) 점검은 시험 장면
`assets/scenes/MathProbe.hscene`(3 cm 벽 · 돌멩이 · Mesh 도넛 · Convex Hull 원뿔 · 무음 소리와 기본 소리) 으로 던지기 · 터널링 · 메시 충돌 · 소리 ·
그림자 모두 통과했습니다.

## 3. 남긴 것

판단이 필요하거나 측정이 먼저인 항목입니다.

| ID | 항목 | 남긴 이유 |
|---|---|---|
| O4 · O8 · O10 · O11 | FP16, SSAO 샘플 수, 포비티드, 동적 해상도 | Quest 단독 실행 측정 먼저(RTX 3080 은 FP16 이득을 잴 수 없음) |
| O9 | 클러스터드 라이트 | 지금은 CPU 가 라이트를 render.maxLights(256) 안으로 고르고 픽셀마다 전부 돕니다. 장면의 라이트가 수십 개를 넘을 때 효과가 있으며, 새 SSBO · 셰이더 경로 · 두 눈 처리를 함께 정해야 해서 결정이 필요합니다 |
| O12 | Hi-Z 가림 | GPU 컬링이 있어야 이득이 나는데, GPU 컬링은 2026-10-04 에 일부러 지웠습니다. 되살릴지는 결정이 필요합니다(CPU 되읽기 방식은 한 프레임 늦어 VR 에서 튐) |
| O14 | BVH | 매 프레임 다시 짓는 BVH 는 지금 물체 수에서 선형 구-절두체 판정보다 느립니다. 정적 물체를 따로 두는 구조(정적 기지와 같은 시점)와 함께 해야 이득이 납니다 |
| O13 | 메시 단순화 LOD(이차 오차) | meshoptimizer 내려받기 허락 |

## 4. 2차 적용(2026-11-17, "전부 진행")

| ID | 내용 | 확인 |
|---|---|---|
| DEF-170 | 거울 변환 감기 방향: 묶기 키 mirrored, 파이프라인 앞면 CW 변형, 메시 · 볼록 충돌체 크기 부호 | 묶기 시험 9절, 거울 장면 기준과 최대 1단계 |
| B1 · B2 | `src/math/{Tolerance, Geometry, Damping}.hpp`: Ray · Plane · Sphere · AABB · OBB 판정, 허용 오차, 시간 독립 감쇠 · 임계 감쇠 스프링 · 이징. 옮긴 판정: UIPanelPointer, EditorGizmo, floorBelow, FrustumPlane, 패널 따라오기 | HarukaMathTest |
| B3 · B4 · D1 · D2 | `Curves`(Catmull-Rom · Hermite · Bezier), `Random`(PCG32 · Perlin · 블루 노이즈), `Filter`(One Euro · 속도 추정), `Ik`(두 뼈 IK · 스윙-트위스트) | HarukaMathTest(검사 84) |
| P3 · P4 · P5 | 충돌체 중심 오프셋(RotatedTranslatedShape), 동적 바디 렌더 보간, 속도 추적 잡기(중력 0 + 연속 충돌, 상한 20 m/s · 40 rad/s) | 물리 시험 P3 ~ P5 절 |
| O2 · O3 | 아핀 · 강체 역행렬, 월드 행렬 세대 스탬프 캐시, 16비트 인덱스(메가 인덱스 버퍼 뒤 절반), Forsyth 정점 캐시 순서(ACMR 2.98 → 0.71) | 액터 시험 O2 절, 기하 시험 O3 절, 기준 PNG 그대로 |
| O15 | 월드 행렬을 3 행 아핀(48 B)으로 올리고 셰이더 `hkModel()` 로 되돌림 | 기준 PNG 최대 1단계 |
| C1 | Reverse-Z + 무한 원평면(GREATER, 깊이 지우기 0, 화면 광선 NDC 깊이 1 · 0.5, 퇴화 평면은 zFar 평면으로) | 스테레오 · 슬레이트 시험, 기준 PNG 0.15 % · 최대 1단계 |
| C2 | QTangent: CPU 정점(48 B)은 그대로, 메가 버퍼에만 28 B(`PackedVertex`, snorm16 쿼터니언 + w 부호 = 손잡이, w 바이어스 1/32767), `geometry/QTangent.hpp` · `hkDecodeQTangent` | 기하 시험 C2 절(오차 5e-5), 기준 PNG 기본 최대 1단계 · 패널 스치는 가장자리 최대 8단계 0.04 % |
| S1 · S2 · S4 ~ S6 | 도플러, 공기 흡수, 두께 투과, 차폐 표본 회전, dB · 로그 보간(`audio/AudioPropagation.hpp`) | 오디오 시험 15b · 15c 절 |

새 기준 PNG: 기본 75125933 · UIPanelProbe 7788eb5f(검증 오류 0).

알려진 한계: Play 중 메시를 바꿔도 RigidBody 는 다시 만들지 않음, 평평한 메시(판 하나)는 볼록 껍질을 만들 수 없어
Mesh 나 Box 를 씀, LOD 메시를 채우는 코드는 아직 없음(O6 은 선택 수학만).
