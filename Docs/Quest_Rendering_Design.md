# Quest 렌더링 경로 설계 (Mobile 렌더 티어)

작성 2026-11-25. 상태: **설계 초안, 사용자 결정 대기.**

## 1. 목표

- Quest Pro 단독 실행에서 **최소 72 Hz(13.9 ms), 목표 90 Hz(11.1 ms)** 를 지킵니다.
- GPU 예산은 컴포지터(타임워프)와 흔들림 여유를 빼고 잡습니다.
  - 90 Hz: 앱 GPU **9.5 ms 이하**
  - 72 Hz: 앱 GPU **12 ms 이하**
- 그 가운데 **고정 비용(빈 장면) 목표는 4 ms 이하**입니다. 나머지 5.5 ms 이상을 콘텐츠(물체 · 라이트 · 파티클 · 패널)에 씁니다.

## 2. 현재 상태 (2026-11-25 Quest Pro 단독 실측)

눈당 1440×1584, 4× MSAA, 멀티뷰, 90 Hz 요청. 결과는 `Docs/Performance_Budget.md`와 `Logs/check/20261125_*_Quest_*`에 있습니다.

| 항목 | 값 | 읽는 법 |
|---|---|---|
| XrDemo 기본 | GPU 26.3 ms(main 19.9 · bloom 4.2 · tone 2.0), 43.6 FPS | 예산의 2.4배입니다 |
| 빈 장면(바닥 사각형 하나, 하늘은 지우기 색) | 24.6 ~ 30 ms | 콘텐츠와 무관한 고정 비용이 대부분입니다 |
| 해상도 0.8배(픽셀 0.64배) | main 13.3 ms(0.67배), 58 FPS | 비용이 픽셀 수에 비례합니다 |
| MSAA 2× | main 18.0 ms(−1.9) | 샘플 수에 비례하는 몫은 작습니다 |
| 블룸 끔 | 21.5 ms(−4.8) | 후처리만 5 ms 가까이 씁니다 |
| 전체 화면 반투명 한 겹 | +4.8 ms | 화면 전체에 `pbr.frag`를 한 번 돌리는 값으로 볼 수 있습니다 |
| 점광원 하나(그림자 없음) | +3.0 ms | 라이트마다 모든 픽셀에서 계산합니다 |

### 2.1 GPU 계측기로 나눈 결과 (`ovrgpuprofiler`, XrDemo, 2026-11-25 15:45 ~ 15:48)
엔진을 고치지 않고 Quest 내장 계측기로 쟀습니다. 산출물은 `Handoff/next1124_mp/VS1b/mv/prof1_rt.txt` · `prof2_trace*.txt`이고, 요약 스크립트는 같은 폴더의 `trace_sum.py` · `trace_metrics.py`입니다. 렌더 단계 추적은 계측 부담 때문에 시간이 1.4배쯤 늘어나므로, 아래 ms는 추적 비율을 보통 실행(main 19.6 ms, 전체 26.0 ms)에 맞춰 환산한 값입니다.

| 몫 | 보통 실행 환산 | 근거 |
|---|---|---|
| 메인 패스 픽셀 셰이딩 | **약 8.5 ms** | 픽셀당 명령어 **524개**, 텍스처 읽기 **약 22번**, 전부 32비트 연산 |
| 깊이 전처리가 별도 패스로 갈라짐 | **약 4 ms** | 드라이버가 서브패스 0(깊이만, 288×128 타일 65개)과 서브패스 1(색 + 깊이, 96×112 타일 225개)을 합치지 못했습니다. 그래서 깊이를 메모리에 저장했다가 다시 읽습니다 |
| 블룸 · 톤매핑 | 6.2 ms | 실측값입니다(눈마다 따로 돎) |
| HDR resolve 저장 | 약 2.8 ms | 메인 패스의 StoreColor 단계입니다 |
| 컴포지터 선점 | 약 2 ms | 시스템의 타임워프가 긴 메인 패스를 중간에 끊고 들어옵니다. 패스가 짧아지면 함께 줄어듭니다 |
| 그림자 | 0.2 ms | |

- 메모리 버스 사용률은 15 %, 메모리 대기는 0.2 %였습니다. **대역폭이 꽉 찬 것은 아닙니다.** 다만 저장 · 다시 읽기 단계는 그 자체로 시간을 씁니다.
- 결론: 비용은 **무거운 픽셀 셰이더**, **갈라진 깊이 전처리**, **데스크톱식 후처리** 세 가지에서 나옵니다. 처음에 "설명 안 되는 15 ms"라고 적은 몫은 대부분 깊이 전처리 분리 · resolve 저장 · 선점이었습니다.

## 3. 현재 구조에서 모바일에 맞지 않는 점

코드 확인 결과입니다.

1. **MSAA 깊이를 메모리에 저장합니다.** `GeometryPass.cpp`에서 `atts[1].storeOp = STORE`이고, 이미지가 SAMPLED라서 지연 할당(lazily allocated)도 못 합니다. 읽는 쪽은 AO(Quest에서 꺼짐)와 렌더 확장 `AfterScene`뿐입니다.
2. **HDR 중간 이미지(RGBA16F)를 메모리에 resolve 합니다.** 그 뒤 블룸(임계값 → 축소 4 → 확대 3)과 톤매핑이 각각 별도 패스로 이 이미지를 다시 읽습니다. 이 블룸과 톤매핑은 멀티뷰가 아니라 **눈마다 따로** 기록됩니다.
3. **스왑체인에 직접 그리지 않습니다.** 그래서 런타임 FFR(`XR_FB_foveation`)이 메인 패스에 걸리지 않습니다.
4. **픽셀 셰이더가 데스크톱 기준입니다.**
   - `pbr.frag`는 텍스처 5장, 그림자 4탭, 라이트 루프(라이트 수만큼 모든 픽셀), IBL 큐브맵 2 ~ 3번, BRDF LUT, 데칼 루프, 고도 안개를 돕니다.
   - `mediump`(16비트 정밀도)를 전혀 쓰지 않습니다. Adreno는 16비트 연산이 32비트보다 거의 두 배 빠릅니다.
5. **깊이 전처리 서브패스가 있습니다.** Adreno에는 하드웨어 LRZ(저해상도 깊이로 미리 버리기)가 있어서 이 서브패스의 이득이 데스크톱보다 작고, 정점 비용은 두 배가 됩니다. 지금은 정점이 병목이 아니므로 우선순위는 낮습니다.

## 4. 다른 엔진은 어떻게 하는가

- **Meta 권장(네이티브 · Unity · Unreal 공통):**
  - MSAA 색과 깊이는 `storeOp = DONT_CARE`로 두고, 같은 서브패스의 resolve 첨부로 타일 안에서 resolve 합니다. 둘 다 지연 할당 메모리로 만듭니다.
  - 전체 화면 이미지를 하나 더 만들 때마다 resolve가 한 번 더 생기고, 그 비용은 약 1 ms입니다.
  - 가능하면 스왑체인에 직접 그립니다. 중간 타깃에서 복사하면 FFR과 MSAA가 메인 패스에 걸리지 않습니다.
  - 톤매핑처럼 같은 픽셀만 읽는 처리는 **서브패스**로 둡니다(resolve 비용 없음). 블룸 · 블러처럼 이웃 픽셀을 읽는 처리는 별도 패스가 필요하고 그만큼 비쌉니다.
- **Unreal(Quest):**
  - Mobile Forward 렌더러를 씁니다. Mobile HDR를 끄거나, 켜면 `r.Mobile.TonemapSubpass=1`로 톤매핑을 메인 패스의 마지막 서브패스에서 합니다. Meta 자료에 따르면 추가 비용은 약 0.6 ms입니다.
  - 블룸 · AO 같은 후처리는 Quest 권장 설정에서 끕니다.
- **Unity URP(Quest):** HDR · 후처리를 끄고 스왑체인에 직접 그리는 것이 권장 경로입니다. FFR은 `XR_FB_foveation`으로 겁니다.

정리하면 Quest 엔진들은 **"데스크톱 경로에서 옵션을 끈 것"이 아니라 별도의 모바일 경로**를 둡니다. 이 설계도 그 방식을 따릅니다.

## 5. 설계: 렌더 티어 두 개

### 5.1 티어 선택
- `Desktop` 티어: 지금 구조 그대로입니다. 기준 캡처(base7)와 UI 덤프가 바뀌지 않아야 합니다.
- `Mobile` 티어: Quest(Android)의 기본값입니다.
- PC에서도 `--render-tier=mobile`로 고를 수 있게 합니다. 합성 눈(`--synthetic-eyes`) 캡처로 모바일 경로의 회귀를 PC 헤드리스에서 확인하기 위해서입니다.
- 티어는 시작 때 한 번 정하고, 실행 중에는 바꾸지 않습니다(렌더 패스 · 파이프라인을 다시 만들지 않기 위해).

### 5.2 Mobile 티어의 메인 패스
렌더 패스 하나에 서브패스 세 개를 둡니다.

| 서브패스 | 하는 일 | 첨부 |
|---|---|---|
| 0 | 깊이 전처리(M0 결과에 따라 뺄 수 있음) | MSAA 깊이 |
| 1 | 불투명 · 반투명 · 파티클 · 3D 패널 · 라인 | MSAA HDR 색(RGBA16F), MSAA 깊이 |
| 2 | 톤매핑 · 노출 · 색 보정 · 비네트 · 패널 HDR 알파 처리, 그 뒤 `AfterTonemap` 확장 | 입력 = MSAA HDR(`subpassInputMS`), 출력 = **XR 스왑체인 이미지(1×, sRGB)** |

- 서브패스 2는 샘플 4개를 각각 톤매핑한 뒤 평균을 냅니다. HDR에서 resolve 한 뒤 톤매핑하는 지금 방식보다 밝은 경계의 계단이 덜 보이는 것이 알려진 장점입니다.
- MSAA 색과 깊이는 모두 `DONT_CARE` · TRANSIENT · 지연 할당입니다. **HDR가 타일 밖으로 나가지 않습니다.** 메모리에 쓰는 것은 스왑체인 1× sRGB 한 번뿐입니다.
- 패널의 "HDR 알파 = 후처리 제외 가중치" 방식은 그대로 둡니다. 알파가 타일 안에만 있으므로 RGBA16F를 유지해도 대역폭 비용이 없습니다.
- **위험:** DEF-183(Adreno가 여러 서브패스에 걸친 MSAA resolve를 빠뜨림)과 비슷한 드라이버 문제가 날 수 있습니다. 이 설계는 resolve 첨부를 쓰지 않고 셰이더에서 샘플을 직접 읽으므로 그 경로를 피합니다. 그래도 M2의 첫 확인은 헤드셋에서 합니다.

### 5.3 Mobile 티어에서 빠지는 것
- 블룸 · AO: 기본으로 끕니다. 둘 다 이웃 픽셀을 읽어야 해서 HDR를 메모리에 내보내야 합니다. 필요해지면 나중에 "싼 블룸"(낮은 해상도 한 단계)을 따로 설계합니다.
- 렌더 확장 `AfterScene`: HDR · 깊이 이미지가 메모리에 없으므로 부르지 않습니다. 이 단계를 쓰는 확장이 등록되어 있으면 경고를 한 번 남깁니다.
- 데스크톱 거울 화면: Quest 단독에는 없습니다. PC에서 `--render-tier=mobile`로 실행할 때는 왼쪽 눈 스왑체인을 복사해 보여 줍니다.

### 5.4 FFR
- 메인 패스가 스왑체인에 직접 그리므로 `XR_FB_foveation` + `XR_FB_foveation_configuration`(스왑체인 방식, `Performance_Budget.md`의 (c)안)으로 메인 패스 전체에 FFR이 걸립니다.
- `project.ini`에 `xr.foveation = off | low | medium | high`(기본 `medium`)를 둡니다. 동적 FFR(`XR_FB_foveation` 동적 옵션)은 켤 수 있게만 하고 기본은 끕니다.
- Meta 문서는 셰이더가 단순하면 FFR 이득이 작다고 합니다. 그래서 M4에서 켠 것과 끈 것을 실측해 기본값을 정합니다.

### 5.5 셰이더 (Mobile 변형)
특수화 상수(spec constant)로 같은 `pbr.frag`에서 변형을 만듭니다. 셰이더 파일을 둘로 나누지 않습니다.
- 색 · 조명 계산에 `mediump`를 씁니다. 위치 · 깊이 · UV는 32비트를 유지합니다.
- **라이트는 물체마다 고릅니다.** CPU가 물체 경계와 겹치는 라이트를 최대 4개 골라 인덱스로 넘깁니다. 지금은 모든 픽셀이 장면의 라이트 전부를 돕니다. Unreal Mobile도 물체당 동적 점광원 수를 제한합니다.
- 그림자: 하드웨어 PCF 1탭(또는 4탭 유지)을 M3 실측으로 정합니다.
- 반사 캡처 큐브맵 경로와 데칼 루프는 Mobile 티어에서 끕니다(필요하면 나중에 켭니다).
- 안개는 지금처럼 밀도가 0이면 건너뜁니다.

## 6. 단계 (구현 지시 크기: 빌드 2 ~ 3회)

| 단계 | 내용 | 확인 | 예상 효과 |
|---|---|---|---|
| **M0** 비용 나누기 | 진단 옵션: 깊이 저장 끄기 · 깊이 전처리 끄기 · 단순 셰이더(unlit) · 그리기 없이 지우기만. 헤드셋에서 `ovrgpuprofiler`(Quest 내장 GPU 계측기)로 bin 수 · 메모리 읽기/쓰기 · 셰이더 시간을 함께 기록 | perf 표 하나 | 설명되지 않은 15 ms의 정체를 밝힘 |
| **M1** 저렴한 수정 | 깊이 `DONT_CARE` · 지연 할당(AO 꺼짐 · `AfterScene` 없음일 때), 블룸 · 톤매핑을 멀티뷰로 한 번에 기록 | base7 그대로, 헤드셋 perf | 1 ~ 3 ms |
| **M2** Mobile 티어 메인 패스 | 5.1 ~ 5.3: 톤매핑 서브패스, 스왑체인 직접 출력, 블룸 · AO 끔 | 합성 눈 캡처 새 기준, 헤드셋 perf · 화면 | 후처리 6 ms → 0.6 ms, HDR resolve 제거 |
| **M3** 셰이더 변형 | 5.5: `mediump`, 물체당 라이트 4개, 그림자 · 캡처 · 데칼 정리 | 합성 눈 캡처 비교, stress `lights` · `overdraw` | 픽셀당 비용 감소(M0 결과로 추정) |
| **M4** FFR | 5.4 | 헤드셋 perf(off · low · medium · high) | main의 10 ~ 25 % |
| **M5** 예산 확정 | stress 다시 실행, `Performance_Budget.md` 표 채우기, 72 Hz · 90 Hz 기본값과 해상도 배율 결정 | 표 | |

**M1 상태 (2026-11-25):** 구현 완료. Android 기본 `mobile` 티어에서 깊이 전처리 끄기(서브패스 1개, 불투명 깊이 쓰기) · 깊이 첨부 `DONT_CARE`/`TRANSIENT`/`LazyPreferred` · AO 끄기 · 기본 블룸 끄기(`--bloom`으로 비교 가능) 적용. PC 헤드리스 · Debug 검증 레이어 오류 0 확인. M1b에서 DEF-185(Desktop 반투명 서브패스 번호 2 → 1)를 함께 고쳤습니다.

**M1 헤드셋 실측 (Quest Pro 단독, XrDemo, 90 Hz 요청, 2026-11-25 16:46 ~ 16:49):**

| 실행 | GPU 합계 | main | bloom | tone | FPS 평균 |
|---|---|---|---|---|---|
| `--render-tier=desktop`(이전 방식) | 24.5 ms | 18.2 | 4.2 | 2.0 | 42.3 |
| `mobile --bloom` | 20.8 ms | 14.3 | 4.2 | 2.0 | 47.4 |
| **`mobile`(Quest 기본)** | **16.6 ms** | 15.0 | 0 | 1.4 | **57.3** |

깊이 전처리를 끈 효과가 main −3.2 ~ −3.9 ms, 블룸을 끈 효과가 −4.2 ms(톤매핑도 −0.6 ms)로, 계측에서 예상한 값과 맞습니다. 합계는 −7.9 ms(−32 %)입니다. 남은 16.6 ms 가운데 main 15 ms에는 셰이딩 약 8.5 ms, HDR resolve 저장 약 2.8 ms, 컴포지터 선점이 들어 있습니다.

**2.1 계측 뒤 바뀐 점:** M0의 대부분은 계측기로 끝났습니다. M1에 **Quest에서 깊이 전처리 끄기**를 넣습니다(Adreno의 LRZ가 이른 깊이 버림을 대신함, 약 4 ms). 그다음 효과 순서는 M2(후처리 · resolve 저장, 약 8 ms) → M3(셰이더, 약 8.5 ms 중 절반 이상) → M4(FFR)입니다. 합치면 26 ms에서 10 ms 안팎이 되리라 예상하고, 90 Hz 예산(9.5 ms)에 남는 몫은 FFR과 해상도 배율로 메웁니다.

## 7. 결정할 점

1. **티어 방식:** Mobile 티어를 별도 경로로 둘지(권장), 지금 경로에 옵션만 더할지.
2. **Quest의 블룸:** 기본으로 끌지(권장), 싼 블룸을 M2에 같이 넣을지.
3. **고정 비용 목표:** 빈 장면 4 ms 이하(90 Hz 기준)로 둘지.
4. **72 Hz 운용:** 90 Hz를 못 지키는 장면은 72 Hz로 내리는 정책을 둘지, 해상도 배율만 내릴지(M5에서 실측 뒤 결정해도 됨).

## 참고 자료
- Meta: [Loads, Stores, Passes, and Advanced GPU Pipelines](https://developers.meta.com/horizon/blog/loads-stores-passes-and-advanced-gpu-pipelines/)
- Meta: [PC Rendering Techniques to Avoid when Developing for Mobile VR](https://developers.meta.com/horizon/blog/pc-rendering-techniques-to-avoid-when-developing-for-mobile-vr/)
- Meta: [Using Vulkan Subpasses in UE4 for Performant Tone Mapping on Quest](https://developers.meta.com/horizon/blog/graphics-showcase-using-vulkan-subpasses-in-ue4-for-performant-tone-mapping-on-quest/)
- Meta: [Unreal tone mapping](https://developers.meta.com/horizon/documentation/unreal/unreal-tonemapping/)
- Meta: [Fixed foveated rendering (native)](https://developers.meta.com/horizon/documentation/native/android/os-fixed-foveated-rendering/)
- Khronos: [Tile Based Rendering Best Practices](https://docs.vulkan.org/guide/latest/tile_based_rendering_best_practices.html)
