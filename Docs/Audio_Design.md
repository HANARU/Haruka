# 오디오 설계 — 사운드 기초 정리

작성 2026-10-02. 로드맵에서 합의한 순서(PRIM 직렬화 → **사운드 기초** → 종합 오류 검출 → VR)의
두 번째 항목입니다. 오디오는 그동안 뼈대만 있었고, 재생 규칙과 청취자에 결함이 있었습니다.
이 문서는 고친 내용, 지금의 규칙, 검증 방법, 남은 일을 정리합니다.

---

## 1. 구조

| 요소 | 위치 | 역할 |
|---|---|---|
| miniaudio 0.11.21 | `CMakeLists.txt`(FetchContent) | `ma_engine` 하나, 48 kHz 스테레오 |
| 구현부 번역 단위 | `src/audio/MiniaudioImpl.cpp` | miniaudio·stb_vorbis 구현만 담습니다(9만 줄 넘는 구현을 따로 컴파일) |
| `AudioSystem` | `src/audio/AudioSystem.*` | 엔진 모듈. 사운드 생성, 매 틱 위치·설정 반영, 청취자 갱신, 일시정지 |
| `AudioComponent` | `src/components/AudioComponent.*` | 파일·볼륨·Loop·AutoPlay·3D 설정. 활성 변화에 따라 멈춤·재생 |
| `AudioAttenuationSettings` | `src/audio/AudioAttenuation.*` | 3D 설정 값과 씬 파일 텍스트 형식(`AUDIO_3D`) |

- **지원 형식**: WAV·FLAC·MP3(miniaudio 내장), OGG Vorbis(stb_vorbis). 예전에는 콘텐츠 브라우저가
  `.ogg` 를 보여 주는데도 디코더가 없어 불러오기가 항상 실패했습니다. stb_vorbis 는 miniaudio 저장소의
  `extras/` 에 있는 것을 써서 miniaudio 태그와 버전이 함께 고정됩니다. 이 파일은 `/W4` 에서 경고를
  70건 넘게(좁히는 변환 C4244 등) 내므로 `MiniaudioImpl.cpp` 의 그 include 구간에서만 경고를 끕니다.
- **월드 그룹**: 월드 사운드는 전부 `ma_sound_group` 하나에 붙습니다. 그룹은 공간화가 꺼져 있고(섞인
  소리를 다시 감쇠하지 않음), 피치도 끕니다(`MA_SOUND_FLAG_NO_PITCH`). 피치가 켜져 있으면 그룹이 섞인
  소리 전체를 배율 1 로 리샘플링하느라 연산을 쓰고, 한 프레임 늦게 내보내서 멈춘 소리의 마지막 샘플이
  다음 블록 첫머리에 남았습니다(오디오 테스트 5절이 잡았습니다).
- **사운드 생성 시점**: 플레이 중에 `AudioSystem::tick` 이 컴포넌트를 처음 만날 때 만듭니다. 에디터
  상태에서는 사운드가 없습니다. 불러오기는 `MA_SOUND_FLAG_ASYNC` 이지만 miniaudio 가 디코더 초기화까지
  기다렸다 돌아오므로(`WAIT_INIT` 강제), 없는 파일과 모르는 형식은 그 자리에서 실패하고 이유가 로그에
  남습니다(`failed to load '<경로>': <miniaudio 설명>`).

## 2. 재생 규칙

| 상황 | 동작 | 예전 |
|---|---|---|
| Play 시작 | 다음 틱에 사운드를 만들고 AutoPlay 면 재생합니다. 지난 플레이에서 실패한 파일은 다시 시도합니다. | 같음 |
| Pause | 월드 그룹을 멈춥니다. 그룹 아래 사운드는 믹싱에서 읽히지 않아 재생 위치가 그대로 멈춥니다. | **소리가 계속 났습니다** |
| Resume | 그룹을 다시 시작합니다. 멈춘 자리에서 이어집니다. | — |
| Stop(에디터) | 월드를 스냅숏으로 되돌리면서 사운드가 정리됩니다. Pause 상태에서 Stop 해도 다음 Play 는 정상입니다(`onEndPlay` 가 그룹 정지를 풉니다). | — |
| 액터 `setActive(false)`, 부모 비활성, 컴포넌트 끄기 | 멈추고 처음으로 되감습니다(`onDisable`). | **소리가 계속 났습니다** |
| 다시 켜기 | AutoPlay 사운드를 처음부터 재생합니다(`onEnable`, Unity 의 Play On Awake 와 같은 규칙). | — |
| `AudioComponent::stop()` | 멈추고 되감습니다. 다음 `play()` 는 처음부터입니다. | 일시정지처럼 이어서 재생했습니다 |
| 디테일 패널에서 값 변경 | 볼륨·Loop·3D 설정을 매 틱 사운드에 반영하므로 플레이 중에도 바로 적용됩니다. | Loop 는 만들 때만 적용됐습니다 |
| 엔진 종료 | `AudioSystem::shutdown` 이 남은 사운드를 정리하고 컴포넌트의 포인터를 비운 뒤 엔진을 닫습니다. | World 가 `onDestroy` 없이 해제돼 사운드가 정리되지 않았습니다 |

일시정지는 엔진 모듈의 새 훅으로 전달합니다. `IEngineModule` 에 `onEndPlay()` 와
`onPauseChanged(bool)` 을 추가했고, `Engine::run` 이 상태 전이를 감지해 부릅니다. 일시정지 통지는
시작·끝 처리 **뒤**에 보냅니다. Pause 상태에서 곧바로 Stop 하면 끝 처리가 먼저 소리를 멈추므로, 뒤따르는
재개 통지가 남은 소리를 다시 들려주지 않습니다. 일시정지 동안 Engine 은 `tick` 을 부르지 않지만 믹싱은
오디오 스레드에서 계속 돌기 때문에, 모듈이 직접 멈춰야 합니다.

## 3. 3D 설정과 청취자

### 3-1. 청취자

활성 카메라(에디터에서는 에디터 카메라)의 위치와 방향을 매 틱 넘깁니다. 예전에는 **위치만** 넘겨서
청취자가 늘 -Z 를 보고 있었고, 카메라를 돌려도 소리의 좌우가 바뀌지 않았습니다. 방향은
`CameraComponent::forward()` 하나에서 나오며, 렌더(`viewMatrix`)와 에디터 카메라 이동도 같은 함수를
씁니다. 좌표계는 오른손, +Y 위, yaw=pitch=0 이면 -Z 를 보고 오른쪽이 +X 입니다(miniaudio 기본값과 같음).

miniaudio 는 방향 × 위 벡터로 청취자의 오른쪽을 정합니다. 카메라 자세가 액터 회전이 된 뒤로(DEF-21, 2026-10-04)
롤이 있을 수 있으므로 월드 +Y 가 아니라 카메라의 위 벡터(`CameraComponent::up()`)를 넘깁니다.

**XR 세션 중에는 머리 자세를 따릅니다(2026-10-15).** 헤드셋을 쓰면 카메라는 XR 원점(위치 + 요)일 뿐이라,
카메라를 따르면 고개를 돌려도 소리의 좌우가 바뀌지 않습니다. XR 세션이 입력을 받는 동안(FOCUSED) Engine 이
매 프레임 `카메라 XR 원점 × 머리 자세`(손 자세와 같은 기준)를 `AudioSystem::setListenerPose` 로 넘기고, 세션이
멈추거나 머리 추적을 잃으면 `clearListenerPose` 로 다시 카메라를 따르게 합니다. AudioSystem 은 입력 모듈을
모르고 자세만 받으므로 장치 없는 테스트로 검산합니다.

패닝은 miniaudio 의 단순 모델입니다. 스피커 방향(FL = (-0.707, 0, -0.707), FR = (+0.707, 0, -0.707))과
음원 방향의 내적을 0~1 로 옮긴 값이 채널 게인이고, 반대편 채널은 0.2 아래로 내려가지 않습니다. 정면
음원은 채널마다 0.854 입니다. HRTF 가 아니므로 위아래·앞뒤 구분은 거의 없습니다.

### 3-2. 감쇠 설정

| 디테일 행 | 필드 | 기본값 | 의미 |
|---|---|---|---|
| Spatialize | `spatialize` | 켜짐 | 끄면 2D: 위치와 무관하게 감쇠·패닝 없이 들립니다(음악·UI용). 끄면 아래 행이 숨겨집니다. |
| Attenuation | `model` | Inverse | 거리 감쇠 곡선(아래 식) |
| Min Distance | `minDistance` | 1 m | 이 거리 안에서는 줄지 않습니다 |
| Max Distance | `maxDistance` | 100 m | 이 거리 밖에서는 더 줄지 않습니다(Linear 는 여기서 0) |
| Rolloff | `rolloff` | 1 | 곡선 기울기. 0 이면 거리 감쇠가 없습니다 |

d 는 [Min, Max] 로 자른 거리입니다.

- Inverse: `min / (min + rolloff·(d − min))`
- Linear: `1 − rolloff·(d − min) / (max − min)`, 0 아래는 0
- Exponential: `(d / min)^(−rolloff)`

**설계 판단 두 가지.**

1. **UE 이름(Inner Radius / Falloff Distance)을 쓰지 않았습니다.** 라이트는 "UE 명칭만" 따랐지만, 그때는
   이름과 뜻이 맞았습니다. UE 의 Falloff Distance 는 "그 밖은 무음"을 뜻하는데, miniaudio 의 Inverse·
   Exponential 은 Max Distance 밖에서 줄지 않고 그 값에 머뭅니다. 이름만 빌리면 뜻이 어긋나므로
   실제 의미대로 Min/Max Distance(OpenAL·Unity 와 같은 이름)를 씁니다.
2. **miniaudio 의 `none` 모델은 목록에 두지 않았습니다.** `none` 은 거리 감쇠뿐 아니라 좌우 패닝까지 꺼서
   Spatialize 를 끈 것과 같습니다. 방향은 살리고 거리 감쇠만 없애려면 Rolloff 를 0 으로 둡니다.

Min 과 Max 는 서로를 넘지 못합니다(간격 0.01 m). miniaudio 는 min ≥ max 이면 감쇠를 통째로 끕니다.

## 4. 씬 형식

```
AUDIO:assets/audio/Test_BeepLoop.wav
AUDIO_VOL:1 1 1 1          (볼륨, Loop, AutoPlay, 컴포넌트 enabled)
AUDIO_3D:1 inverse 1 100 1 (spatialize, 모델 이름, min, max, rolloff)
```

- `AUDIO_3D` 는 2026-10-02 에 추가했습니다. `AUDIO_VOL` 끝에 토큰을 덧붙이지 않고 새 키로 넣었습니다.
  LIGHT 줄이 위치 기반 확장을 세 번 거치며 취약해진 것을 되풀이하지 않기 위해서입니다. 그래서 이 줄은
  토큰 다섯 개를 정확히 요구하고, 나중에 값이 늘면 다른 키로 추가합니다.
- 모델은 순번이 아니라 이름(`inverse`/`linear`/`exponential`)으로 씁니다(DEF-44 의 순번 기록 문제를 피함).
- 값이 깨졌으면(토큰 수, 모르는 모델, 0 < min < max 위반, 음수 rolloff, NaN·inf) 기본 설정으로 두고
  `[Scene]` 오류를 남깁니다. 소리 자체는 버리지 않습니다(PRIM_MAT 과 같은 규칙).
- 옛 파일에는 이 줄이 없어 기본값이 쓰입니다. 예전 동작과의 차이는 Max Distance 가 무한에서 100 m 로
  바뀐 것뿐인데, Inverse 기본 곡선에서 100 m 의 게인이 0.01(-40 dB)이라 들리는 차이는 없습니다.
- 씬 헤더는 v3 그대로입니다. 줄이 없으면 기본값이고, 옛 엔진은 모르는 키를 건너뛰기 때문입니다.
- **[2026-10-14 갱신]** 씬 형식은 2026-10-14 에 v4 로 바뀌어 컴포넌트를 COMPONENT 블록의 이름 붙은 값으로 씁니다(DEF-22). v3 파일도 계속 읽습니다. `AUDIO:` · `AUDIO_VOL:` · `AUDIO_3D:` 는 오디오 블록의 `file` · `volume` · `loop` · `autoPlay` · `attenuation` 이 됐고, `attenuation` 의 텍스트 형식은 `AUDIO_3D:` 와 같습니다.

## 5. 검증

### 5-1. 오디오 테스트 (`HarukaAudioTest`, 12절 48항목)

출력 장치 없는 miniaudio 엔진(`noDevice`)에서 출력 샘플을 직접 당겨 숫자로 검산합니다. 소리 결함은 화면에
보이지 않고, 좌우 부호가 뒤집힌 것 같은 결함은 귀로 들어도 놓치기 쉽기 때문입니다. 재생 위치는 프레임
i 의 값이 `i·1e-5` 인 32비트 float 램프 파일로 읽습니다(공간화를 끄면 출력 값이 곧 재생 위치).

| 절 | 확인하는 것 |
|---|---|
| 1 | 장치 없는 엔진 생성과 출력 당기기 |
| 2 | 청취자 방향: +X 음원이 정면(-Z)일 때 오른쪽, 뒤돌면 왼쪽, 음원 쪽을 보면 좌우 같음 |
| 2-1 | XR 머리 자세: 넘긴 자세의 방향 · 위치를 따르고, 지우면 다시 카메라를 따름(2026-10-15) |
| 3 | 감쇠: Inverse 1/2·1/8, Max 밖 유지, Linear 절반·Max 무음, Exponential 1/4, Rolloff 0, Spatialize 끔 |
| 4 | 일시정지 중 무음, 재생 상태 유지, 재개하면 멈춘 자리에서 이어짐 |
| 5 | 액터·컴포넌트·부모를 끄면 멈춤(첫 샘플부터 무음), 다시 켜면 처음부터 |
| 6 | stop 뒤 play 는 처음부터 |
| 7 | 플레이 중 Loop 켜기·끄기 반영 |
| 8 | 없는 파일·오디오가 아닌 파일은 즉시 실패, 다음 플레이에서 재시도 표시 |
| 9 | 저장소 시험 음원 세 개(OGG 포함)가 불러와지고 소리가 남 |
| 10 | Pause 중 플레이 종료 뒤 다음 플레이가 무음이 아님 |
| 11 | shutdown 이 컴포넌트 포인터를 비우고, 그 뒤 액터 파괴가 안전함 |
| 12 | `AUDIO_3D` 텍스트 왕복·정밀도·깨진 값 11종 거부 |

### 5-2. 실기 확인 (2026-10-02, 에디터 + 실제 출력 장치)

시스템 출력을 WASAPI 루프백으로 2~3초씩 받아 좌우 RMS 만 계산했습니다(소리는 저장하지 않음).
AudioProbe 프리팹을 시작 카메라의 오른쪽 앞에 스폰한 상태입니다.

| 상태 | 왼쪽 RMS | 오른쪽 RMS |
|---|---|---|
| 에디터(Play 전) | 0 | 0 |
| Play | 0.0093 | 0.0206 |
| Pause | 0 | 0 |
| Resume | 0.0093 | 0.0206 |
| 액터 끄기 | 0 | 0 |
| 다시 켜기 | 0.0092 | 0.0199 |
| Stop | 0 | 0 |
| 음원을 화면 왼쪽(X = -1.25)으로 옮긴 뒤 Play | 0.0212 | 0.0074 |

Save 하면 `AUDIO_3D` 줄이 기록되고, 손으로 바꾼 값(`0 linear 2 20 0.5`)은 Load→Save 를 거쳐 그대로
남았습니다. 깨진 값(`1 log 1 100 1`)은 오류 한 줄을 남기고 기본값으로 저장됐습니다.

## 6. 시험용 음원

모두 ffmpeg 로 합성한 소리라 저작권 문제가 없습니다. `-fflags +bitexact` 로 만들어 다시 만들어도 같은
파일이 나옵니다(WAV 헤더 44바이트, 메타데이터 없음).

| 파일 | 내용 | 용도 |
|---|---|---|
| `assets/audio/Test_BeepLoop.wav` | 모노 1초. 0.15초 비프(880 Hz + 배음) 뒤 무음, 끝과 처음이 0 이라 이음매 없이 반복 | 방향·거리 확인 |
| `assets/audio/Test_Chime.wav` | 모노 1.5초 감쇠 차임 | 한 번 재생(Loop 끔) |
| `assets/audio/Test_NoiseLoop.ogg` | 모노 2초 핑크 노이즈, OGG Vorbis | Vorbis 경로, 넓은 대역 위치감 |
| `assets/prefabs/AudioProbe.hprefab` | 작은 초록 구 + `Test_BeepLoop.wav` 반복 재생 | 콘텐츠 브라우저에서 눌러 스폰 → Play |

```
ffmpeg -f lavfi -i "aevalsrc='if(lt(t,0.15),(0.32*sin(2*PI*880*t)+0.16*sin(2*PI*1760*t)+0.08*sin(2*PI*2640*t))*sin(PI*t/0.15),0)':s=48000:d=1" -ac 1 -c:a pcm_s16le -map_metadata -1 -fflags +bitexact -flags:a +bitexact Test_BeepLoop.wav
ffmpeg -f lavfi -i "aevalsrc='0.22*min(1,t*400)*exp(-2.5*t)*(sin(2*PI*660*t)+0.5*sin(2*PI*1320*t)+0.3*sin(2*PI*1980*t)+0.2*sin(2*PI*2970*t))':s=48000:d=1.5" -af "afade=t=out:st=1.25:d=0.25" -ac 1 -c:a pcm_s16le -map_metadata -1 -fflags +bitexact -flags:a +bitexact Test_Chime.wav
ffmpeg -f lavfi -i "anoisesrc=d=2:c=pink:r=48000:a=0.2:s=1" -ac 1 -c:a libvorbis -q:a 3 -map_metadata -1 -fflags +bitexact -flags:a +bitexact Test_NoiseLoop.ogg
```

콘텐츠 브라우저에서 음원 파일을 누르면 선택한 액터에 지정합니다. 오디오 컴포넌트가 없으면 붙이고,
선택이 없으면 경고 한 줄을 남깁니다(예전에는 아무 반응이 없었습니다).

## 7. 남은 일

- ~~VR 청취자~~: 2026-10-15 에 해결했습니다(3-1). 헤드셋에서 귀로 확인하는 일만 남았습니다.
- **헤드셋으로 소리 보내기**: 엔진은 Windows 기본 재생 장치로 소리를 냅니다. Quest Link 에서 헤드셋 장치(Oculus Virtual Audio
  Device)가 기본 장치가 아니면 소리가 헤드셋으로 나가지 않습니다(2026-10-15 헤드셋 확인에서 찾음). 런타임은
  `XR_OCULUS_audio_device_guid` 로 헤드셋 장치를 알려 줍니다. 그 장치를 자동으로 쓸지는 소리를 확인할 때 정합니다.
- ~~부모가 있는 카메라~~: DEF-21(2026-10-04) 뒤로 `viewMatrix` 와 청취자가 모두 액터의 월드 위치 · 월드 회전을 씁니다.
- **긴 음악**: 지금은 파일 전체를 메모리에 올리고 믹싱 중에 디코딩합니다. 긴 음악은 스트리밍
  (`MA_SOUND_FLAG_STREAM`), 짧은 효과음은 미리 디코딩(`MA_SOUND_FLAG_DECODE`)이 맞습니다.
- **보이스 예산**: 동시 사운드 수 제한과 우선순위(Layer_Architecture.md 의 SoundPrioritization 후보).
- **공간 음향 품질**: HRTF(Steam Audio 등). 지금 패닝은 좌우만 구분합니다.
- **버스와 볼륨**: SFX/음악/UI 그룹과 마스터 볼륨. 에디터 미리듣기·UI 소리는 월드 그룹 **밖**에 붙여
  일시정지와 무관하게 둡니다.
- **기능**: Pitch, 도플러(속도), 지향성 음원(콘), 한 번 재생하고 잊는 API.
- **에디터**: 감쇠 반경(Min/Max) 와이어 표시(Spatial_Representation_Design.md 4단계), 콘텐츠 브라우저 미리듣기.
