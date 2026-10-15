# 라이팅 용어·설계 (UE식, 2026-08-27)

사용자가 라이팅 용어를 언리얼 엔진(UE)식으로 정의하고 그에 맞춰 설계하기로 했습니다. 이는 확정된
엔진 방향(엔티티 모델=유니티, 네이밍·관례=언리얼 하이브리드)과 정합합니다. **이 문서는 용어 정의와
설계이며 구현이 아닙니다.** 근거는 3-렌즈 조사(2026-08-27, UE 5.x 공식 문서 대조 + 코드 감사)입니다.

---

## 1. 진단 요약

Haruka 라이팅의 성격은 **All-Movable · 무단위(Unitless) · 하드튜닝**입니다.

- **All-Movable**: 베이킹/라이트맵 파이프라인이 없어(Lightmass 없음, 간접광은 로드 시 CPU가 구운 IBL
  큐브맵) 모든 라이트가 UE의 Movable에 해당합니다. → Mobility 축(Static/Stationary)이 통째로 성립하지
  않습니다. Lumen·VSM·Distance Field·Baked GI 도 같은 이유로 스킵입니다.
- **무단위**: `intensity`(기본 3.0)는 물리 단위가 아니라 UE의 legacy Unitless 모드에 해당합니다.
  Point/Spot 감쇠도 `(1-d/range)²`라는 비물리식이라 lumen/candela 의미가 성립하지 않습니다.
- **하드튜닝**: 태양 3.0 / 하늘 0.30(kSkyRadianceScale) / 노출이 **한 세트로 묶여** 있습니다. 노출은 0.4 였다가
  감마 이중 적용(DEF-96)을 고친 2026-10-04 에 같은 중간톤 목표로 돌아가도록 1.0 으로 올렸습니다.
  하나만 바꾸면 밝기 균형이 깨집니다.

따라서 UE 용어 작업의 대부분은 **이미 있는 것에 이름표 붙이기**이고, 새로 만들 것은 소수(색온도·
라이트 채널 등 저비용 항목)입니다.

---

## 2. Phase 0 — 긴급: 직렬화 데이터 모델이 깨져 있음 (실제 버그)

용어 재설계 이전에 **데이터 모델부터 무너져 있습니다.** 현 `SceneSerializer`는 라이트의
`color/intensity/ambient/enabled`만 저장하고 **`lightType/range/spotInner/spotOuter/direction`을
저장하지 않습니다.** 결과:

- 씬을 **저장·로드하면 Point/Spot 라이트가 Directional 기본값으로 리셋**됩니다.
- 정작 **렌더에 영향 없는 사문(dead) 필드 `ambient`만 저장**되는 아이러니.

어떤 UE식 필드를 정의해도 라운드트립이 이를 담지 못하면 무의미하므로, **이것부터 고쳐야 합니다.**

---

## 3. 용어 사전 (UE → Haruka)

채택 시점: **now**(있음·이름표만 또는 저비용 신규) / **soon**(중간 작업) / **advanced**(고비용·후순위) /
**skip**(베이킹/RT/Quest 부적합 → 용어만 예약).

### 라이트 타입
| UE 용어 | 정의 | Haruka 현재 | 채택 |
|---|---|---|---|
| Directional Light | 무한 원거리 평행광(태양). 방향만, 위치 무관 | 있음(Sun), worldDirection() | now |
| Point Light | 한 점에서 전방위, 반경 내 역제곱 감쇠 | 있음(2026-10-04 부터 역제곱 + 창 함수) | now |
| Spot Light | Point를 원뿔로 제한 | 있음 | now |
| Rect Light (Area) | 사각 면광원, LTC 해석 적분 | 없음 | advanced |
| Sky Light | 하늘 큐브맵으로 앰비언트+반사 | **이미 있음**(IBL, 타입은 아님) | now(재명명) |

### Mobility (전 축 붕괴 — Movable만 유효)
| UE 용어 | Haruka | 채택 |
|---|---|---|
| Movable(완전 동적, 무베이크) | Haruka의 모든 라이트 | now |
| Stationary / Static | 베이킹 전제라 불가 | skip(예약어) |

### 강도·단위
| UE 용어 | 정의 | Haruka | 채택 |
|---|---|---|---|
| Unitless(legacy) | 물리 의미 없는 상대값 | **현재 이 모드** | now(명시) |
| Intensity Units(Candela/Lumen/EV/Lux) | 라이트별 단위 선택 | 없음 | soon(역제곱 선행) |
| Intensity — Directional=lux | 조도 lm/m² | 무단위 3.0 | soon |
| Intensity — Point/Spot=lumen/candela | 광속/광도 | 무단위 | soon |

### 색
| UE 용어 | 정의 | Haruka | 채택 |
|---|---|---|---|
| Light Color | RGB 틴트 | 있음 | now |
| Use Temperature + Temperature(K) | 흑체 색온도(6500K 중성) | 없음 | **now(최저비용·고효용)** |

### 감쇠·소스
| UE 용어 | 정의 | Haruka | 채택 |
|---|---|---|---|
| Attenuation Radius | 도달 반경·컬링 경계 | `range`(감쇠식에 섞임) | now(개명) |
| Inverse Square Falloff | 물리 1/d² + 윈도잉 | 비물리 `(1-d/r)²` | soon |
| Inner/Outer Cone Angle | 스팟 콘·페이드 | spotInner/OuterDeg | now(개명) |
| Source Radius/Length | 소프트섀도·면적 스페큘러 | 없음 | advanced |

### 그림자
| UE 용어 | 정의 | Haruka | 채택 |
|---|---|---|---|
| Cast Shadows(per-light) | 라이트별 그림자 토글 | 없음(방향광 1개 고정) | now(정확성 선행) |
| Constant/Slope Bias | 깊이 오프셋 | 있음(0.0004/1.75) | now(명칭화) |
| Shadow Resolution | 섀도맵 해상도 | 고정 2048²+PCF3×3 | now |
| Cascaded Shadow Maps(CSM) | 카메라 추종 다단 섀도 | 없음(단일 40m ortho) | soon |
| Contact Shadows | 화면공간 접촉 그림자 | 없음 | soon |
| Distance Field/VSM | SDF·가상 섀도맵 | 없음 | skip |

### 노출·하늘·안개
| UE 용어 | 정의 | Haruka | 채택 |
|---|---|---|---|
| Manual Exposure + EV Compensation | 고정 노출·EV 오프셋 | 있음(0.4, 배선 미완) | soon |
| Auto Exposure(Eye Adaptation) | 자동 순응 | 없음 | **skip(VR 멀미)** |
| Ambient Occlusion(SSAO) | 화면공간 차폐 | 있음(D-3 수정됨) | now(재명명) |
| Exponential Height Fog | 해석 고도 안개 | 없음 | soon |
| Sky Atmosphere | 물리 대기 산란 | 없음(정적 틴트) | advanced |
| Volumetric Fog / Light Shafts | 프록셀 참여매질 | 없음 | skip |

### 고급·기타
| UE 용어 | Haruka | 채택 |
|---|---|---|
| Light Channels(비트마스크) | 없음(한 줄이면 됨, Quest 무료) | now |
| IES Profile | 없음(candela 단위 선행) | soon |
| Reflection Capture(프로브) | 전역 큐브맵 1장뿐 | soon |
| Light Function(쿠키/고보) | 없음(머티리얼 시스템 부재) | advanced |
| Indirect Lighting Intensity | 베이킹 정의로는 없음 | skip(IBL 세기로 리매핑 가능) |
| Lumen / Lightmass / Ray Tracing | 없음, Quest 부적합 | skip |

---

## 4. UE식 LightComponent 설계 (필드·타입·네이밍 확정, 물리 구현은 미포함)

세 성격을 분리합니다: **이름표 교체** + **사문 필드 정리** + **미래 예약 필드**.

```
LightComponent (UE 어휘):
  mobility        enum { Movable(기본), Stationary, Static }  // Movable만 유효, 나머지 예약, UI 미노출 권고
  lightType       enum { Directional, Point, Spot }           // Rect 예약
  intensity       float = 3.0                                  // 개념명 'Intensity'
  intensityUnits  enum { Unitless(기본), Candela, Lumen, EV, Lux } // 초기 Unitless 고정
  color           vec3                                         // 선형 RGB
  useTemperature  bool = false                                 // 신규
  temperatureK    float = 6500                                 // 신규, CPU CCT→RGB로 color 에 곱
  attenuationRadius  float = 10   // range 개명
  innerConeAngle  float = 15      // spotInnerDeg 개명 (규약·값 불변)
  outerConeAngle  float = 30      // spotOuterDeg 개명
  castShadows     bool             // 신규 per-light (Directional 기본 true)
  shadowBiasConstant/Slope  float = 0.0004 / 1.75  // 명칭화
  lightChannels   uint = 0x1       // 신규 비트마스크
  sourceRadius    float = 0        // 예약(소프트섀도)
  worldDirection()                 // 유지
```

**제거·이관**:
- `ambient`(0.15) 사문 필드: LightComponent 에서 **제거**. 앰비언트는 UE 규약대로 Sky Light 소관 →
  신설 `SkyLightComponent.iblIntensity`로 이관. ("앰비언트 슬라이더가 렌더에 영향 없는" 오해 제거.)
- `FrameUBO.timeParams.w`(사문 exposure 항): 제거하거나 실노출 경로와 일원화.

**신설 SkyLightComponent** (별도 컴포넌트):
```
  sourceType    enum { SpecifiedCubemap(절차 그라디언트), CapturedScene(예약) }
  iblIntensity  float = 0.30   // = 현 kSkyRadianceScale, 실시간 노브
  zenithColor / horizonColor / groundColor   // 색 변경 시 큐브맵 재생성(GPU 인플라이트 동기화)
```

---

## 5. 단계별 채택

- ✅ **Phase 0 — 데이터 모델 복구(선결·버그성)**: SceneSerializer 에 lightType/range/spotInner/Outer/direction
  추가로 라운드트립 복원. **완료(2026-08-27).**
- ✅ **Phase 1 — 무비용 용어화(리네이밍 + UI)**: **완료(2026-08-27).** LightComponent 필드를 UE 어휘로 개명
  (`range`→`attenuationRadius`, `spotInnerDeg/OuterDeg`→`inner/outerConeAngle`). 사문 `ambient` **제거**
  (SSBO/UBO/pbr.frag 어디에도 미업로드 확인, 직렬화는 예약 슬롯으로 위치만 유지). 디테일 패널에 **Type
  드롭다운** 신설 + **조건부 노출**(Point/Spot 만 Attenuation Radius, Spot 만 Cone Angle). 렌더 결과 불변.
  - **의도적 보류(저가치)**: Mobility enum 은 **추가 안 함** — 유효값이 Movable 하나뿐이라 방금 제거한
    `ambient` 처럼 또 다른 사문 필드가 될 뿐(열린 항목 #5 의 '숨김' 방향과 정합). ACES→'Filmic Tonemapper',
    SSAO→'Ambient Occlusion' 라벨 개명은 내부/표기 수준이라 필요 시 일괄 처리.
- ⏳ **Phase 2 — 저비용 신규**: **색온도 완료(2026-08-27)** — `useTemperature`(기본 꺼짐)/`temperatureK`,
  `effectiveColor()`=color×kelvinToRGB(Tanner-Helland), 직렬화 trailing 확장. 기본 꺼짐이라 렌더 불변.
  - **남음 — Light Channels**: per-object 채널 마스크를 프래그먼트 셰이더까지 배선해야 함(파이프라인
    레이아웃·푸시 상수/머티리얼 SSBO·StaticMeshComponent 필드·직렬화·UI). 표에는 '한 줄'이라 적었으나
    실제로는 중간 규모. GpuLight 여유 슬롯(spotParams.zw)은 라이트 쪽에만 있음.
  - **남음 — per-light Cast Shadows**: 그림자 정확성(DEF-26/27: 캐스터 라이트 슬롯 불일치, 섀도가 카메라
    컬링 목록 사용) **선결**. 그 전에는 토글이 무의미.
- **Phase 3 — 노출 배선(soon)**: exposure 를 PostPass 하드코딩에서 UBO 실값으로 배선, 'Manual Exposure'
  명명 + per-camera EV/Compensation. Auto Exposure 는 채택 안 함.
- **Phase 4 — 물리 단위·감쇠(soon, 한 묶음)**: Inverse Square Falloff 도입 → Attenuation Radius 를 순수
  윈도잉으로 분리, Candela/Lumen/Lux 활성화. **태양/하늘/노출 세트 + bloom/ssao/taa 상수 전부 동시 재보정**
  (R16F 상한 65504 고려).
- **Phase 5 — 그림자 확장(soon)**: 단일 40m ortho → CSM(2~3 캐스케이드, 카메라 추종), Dynamic Shadow
  Distance. Quest 는 2 캐스케이드로 시작. 필요 시 방향광 Contact Shadows.
- **Phase 6 — 고급/예약(advanced, 수요 시)**: Rect Light(LTC)+Source Radius, IES, Reflection Capture,
  Exponential Height Fog, 축소형 Sky Atmosphere, 정적 Light Function.
- **영구 스킵(예약어만)**: Static/Stationary·Lightmass·Baked GI, Distance Field·Volumetric·VSM·Lumen·RT,
  material Light Function.

---

## 6. 사용자 결정 필요 (열린 항목)

1. ✅ **결정(2026-08-27): "UE 명칭만".** UE 이름·드롭다운(Intensity/Attenuation Radius/Candela/Kelvin 등)
   은 도입하되 **내부 값은 영구 상대값(π 정규화)** 으로 유지합니다. 물리 단위(lux/candela/lumen)의 실제
   물리값 산입·노출계(EV100)·역제곱 물리 감쇠·커플드 세트 재보정은 **하지 않습니다.** `intensityUnits`
   드롭다운은 UI·문서용 명목 표기이고, 하드튜닝 세트(태양 3.0/하늘 0.30/노출 0.4, 2026-10-04 부터 노출 1.0)는 그대로 둡니다.
   → **Phase 4(물리 단위·감쇠)는 명목 명칭만 남기고 물리 구현은 스킵.** Inverse Square Falloff 는 필요 시
   시각 개선용으로만 선택 도입(단위 의미와 무관).
   **(2026-10-04 갱신)** 사용자 지시("현실은 어떤지 알아보고 거기에 맞춰")로 Point·Spot 의 감쇠 **모양**은 실제 점광원과
   같은 역제곱 + 창 함수로 바꿨습니다(1cm 최소 거리, 반경은 꼬리를 자르는 지점). 단위는 여전히 상대값이고(intensity =
   1m 거리의 밝기) 물리 단위 산입·노출계는 하지 않습니다. 근거와 확인은 Core_Defects_Task_Spec.md "화면 기준 재정렬".
2. **exposure 재보정 범위**: 물리 단위/역제곱을 건드리면 커플드 세트 전부(intensity·exposure·skyRadiance +
   bloom·ao·taa) 재조정 필요 — 한 번에 vs 단계적.
   **[정리 · 2026-10-15]** 물리 단위는 1번 결정으로 하지 않으므로, 물리 단위에 대해서는 이 질문이 해당하지 않습니다.
   역제곱 감쇠 모양은 2026-10-04 에 바꿨고, 그때 감마 이중 적용(DEF-96) 수정과 함께 노출만 0.4 → 1.0 으로 다시 정했습니다
   (사용자 결정). 가짜 TAA 는 지워져 재조정 대상에서 빠졌습니다.
3. ✅ **결정: 완전 제거(2026-08-27).** LightComponent 에서 `ambient` 삭제. 직렬화는 옛 파일 호환을 위해
   토큰 자리만 예약 슬롯으로 유지. Sky Light iblIntensity 이관은 SkyLightComponent 신설 시점으로 미룸.
4. **라이트 방향 표현**: 현 로컬 direction+회전 혼합 유지 vs UE식 -Z(forward) 규약으로 흡수(기존 값 재해석).
   ✅ **결정(2026-10-15): 회전에서만 유도, 축은 -Z.** `direction` 필드를 없애고 액터 회전의 -Z 를 빛이 나아가는 방향으로
   씁니다(카메라 `forward()` · OpenXR · Godot 과 같은 축. UE 는 +X, Unity 는 +Z 이지만 모두 트랜스폼 회전만 씀). 옛 씬은 읽을 때
   `direction` 을 액터 회전에 접어 넣어 화면이 바뀌지 않게 합니다. 시점은 다음 라이팅 작업 때입니다. 지금 코드는 아직 혼합
   방식입니다(월드 방향 = 액터 회전 × 로컬 `direction`, `worldDirection`). [Primitive_And_Lighting_Design.md](Primitive_And_Lighting_Design.md)
   D6 에서 "`direction` 유지"를 정하면서 이 항목을 따로 남겼던 것입니다.
5. ✅ **결정: enum 자체를 추가 안 함(2026-08-27).** 유효값이 Movable 하나뿐이라 필드로 만들면 방금 제거한
   `ambient` 처럼 또 다른 사문 필드가 됨. 문서(§3 Mobility 표)로만 남김.
6. **CSM 캐스케이드 수**(Quest 예산): 2 vs 3. **(2026-10-15 현재 열림)** CSM 을 구현할 때 Quest 예산과 함께 정합니다.
