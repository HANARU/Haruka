# Carbon 저장소 원본 대조 감사 보고서 (Carbon Source Audit)

> **이 문서는 검증의 출처입니다.** Haruka 문서군에 붙어 있는 `[검증 상태 · 2026-08-24]` 배너는
> 모두 이 문서를 근거로 삼습니다. 따라서 이 문서에는 배너를 붙이지 않습니다.
>
> 감사 기준일은 2026-08-24입니다. 대조 대상은 GitHub 조직 `github.com/carbonengine`의
> 공개 저장소 원본과 Haruka 저장소의 `E:\Haruka\src` 코드입니다. 이 문서에 적힌 구체값은
> 전부 원본 파일을 직접 읽어서 확인한 것이며, 확인하지 못한 항목은 "원본 미확인"으로
> 명시했습니다.

---

## 목차

1. [검증 방법과 범위](#1-검증-방법과-범위)
2. [저장소별 실체 요약](#2-저장소별-실체-요약)
3. [정정 대조표](#3-정정-대조표)
4. [라이선스 실측표](#4-라이선스-실측표)
5. [차용 판정표](#5-차용-판정표)
6. [CCP 상하이 Vulkan 백엔드](#6-ccp-상하이-vulkan-백엔드)
7. [재현 방법](#7-재현-방법)

---

## 1. 검증 방법과 범위

### 1-1. 대조한 것

| 구분 | 대상 | 확인 방법 |
|---|---|---|
| Carbon 원본 | `github.com/carbonengine` 조직의 공개 저장소 | 저장소 트리, README, LICENSE, CMakeLists.txt, vcpkg.json, 그리고 `src/`·`include/` 아래 실제 소스 파일을 직접 열람했습니다. |
| Carbon 부재 확인 | Dual Quaternion, SIMD 인트린식, ASTC, SDF, 옥타헤드럴, 레이마칭 성운, Rayleigh 산란, VR 관련 식별자 | 저장소 전역 문자열 검색으로 0건임을 확인했습니다. |
| Carbon 이슈·PR | `trinity` 이슈 트래커 전체, `trinity` PR #67, `ccpshanghai/trinity` 브랜치 목록 | 웹 페이지 직접 열람으로 확인했습니다. |
| 빌드 구성 | `vcpkg-registry`의 포트 목록, 메타 포트 `ports/carbon/vcpkg.json`, `toolchains/x64-windows-carbon.cmake`, 각 저장소의 CMakePresets | 원문 열람으로 확인했습니다. |
| Haruka 코드 | `E:\Haruka\src` 전체 | 파일과 행 번호 단위로 직접 열람했습니다. 결함 문서의 주장과 실제 코드를 1:1로 대조했습니다. |

### 1-2. 확인하지 못한 영역

다음 항목은 이번 감사에서 **확인하지 못했습니다.** 기존 문서에 이 영역의 구체값이 적혀 있다면
그것은 근거 없는 서술이며, 이 문서는 그 값을 지지하지도 반박하지도 않습니다.

| 미확인 영역 | 이유 |
|---|---|
| Trinity가 로드하는 94개 이펙트의 셰이더 내용 | `res:/Graphics/Effect/...` 형태의 런타임 리소스 경로만 공개되어 있고, 리소스 번들 자체는 공개되지 않았습니다. |
| MachoNet / GPCS 와이어 포맷 | `io` README에 "machoNet 패킷 송수신 지원 기능이 추가되었다"는 언급만 있고, 프레임 포맷도 패킷 타입 열거형도 저장소에 없습니다. |
| Destiny의 서버-클라이언트 스냅샷 직렬화 구조 | `OnAddBalls` 계열 메시지의 실제 바이너리 레이아웃을 저장소에서 찾지 못했습니다. |
| Dogma 룰 엔진 | 저장소도 vcpkg 포트도 존재하지 않습니다. 부재 자체는 확인했으나 내부 구조는 확인할 방법이 없습니다. |
| `carbonengine/db`의 백엔드 | README가 스텁이며 API도 백엔드 이름도 적혀 있지 않습니다. |
| `carbonengine/parser`의 내부 평가 방식 | 저장소 자기 설명이 "Math expression parser"라는 점과 trinity가 `>=1.0.2`로 의존한다는 점만 확인했습니다. 바이트코드 컴파일 여부는 확인하지 않았습니다. |
| 공개 저장소의 정확한 총 개수 | 조직 페이지의 페이지네이션에 따라 값이 달라지므로 "약 35개"로만 다룹니다. |

### 1-3. 조직 배경 사실

| 항목 | 내용 |
|---|---|
| 조직 | `github.com/carbonengine`, 표시명 "CARBON Engine", 아이슬란드, 블로그 `fenris.com/carbon` |
| 조직 생성일 | 2024-04-18. GitHub 인증(verified) 조직은 아닙니다. |
| 회사 배경 | Fenris Creations는 CCP Games의 새 사명입니다. CCP는 2026-05-01 Pearl Abyss가 경영진에게 매각(미화 1.2억 달러)하면서 독립했고, 2026-05-06 사명 변경을 발표했으며, Carbon 오픈소스 완료는 2026-07-01에 발표되었습니다. |
| 저작권 표기 | `trinity/LICENSE.md`의 표기는 여전히 "Copyright (c) 2026 CCP Games"입니다. |
| 실제 빌드 경로 | 실제 빌드는 Perforce와 TeamCity에서 이루어지고 GitHub은 컴포넌트 미러입니다. 저장소마다 `.teamcity/settings.kts`가 있고, trinity의 `.github/workflows`에는 `cpp_format.yml` 하나뿐입니다. |
| 아키텍처 문서 | `carbonengine/documentation`은 Jekyll 사이트이고 실제 페이지가 8개입니다. `index.md`는 문자 그대로 C++ 코딩 가이드라인이며, `getting_started.md`와 `contributing.md`는 미완성 스텁입니다. **조직에 아키텍처 문서가 존재하지 않습니다.** |

### 1-4. 가장 중요한 상위 판정

**Carbon은 Unity나 Unreal 의미의 게임 엔진이 아닙니다.** 애플리케이션 계층이 Python인
C++ 컴포넌트 플랫폼이며, 약 35개 저장소가 vcpkg 포트로 소비되고, 실제 배포 타깃은
CCP/Fenris 내부의 Perforce 모놀리스입니다. 메타 포트 `ports/carbon/vcpkg.json`
(name `carbon`, version 1.1.1, 의존성 26개)에는 `carbon-trinity`, `carbon-audio`,
`carbon-resources`, `carbon-videoplayer`, `carbon-grpc`가 **포함되지 않습니다.**
플랫폼과 렌더러가 의도적으로 별개 제품입니다.

---

## 2. 저장소별 실체 요약

### 2-1. 한눈에 보기

| 저장소 | 기존 문서가 부른 이름 | 실제 정체 | 실체 대비 기존 서술의 정확도 |
|---|---|---|---|
| `math` | "SIMD, Dual Quat, Geometry" | DirectXMath 상호변환용 기초 수학 헤더 27개 | 대부분 틀렸습니다 |
| `destiny` | "물리/항법 인터페이스 & Ballpark 뼈대" | 약 22,200줄의 완전히 동작하는 시뮬레이션 | 과소평가했습니다 |
| `trinity` | "Reverse-Z, SDF 볼류메트릭, GPU-Driven" | Forward+ DX11/DX12/Metal 렌더러, 셰이더 원천 비공개 | 절반은 존재하지 않는 기능입니다 |
| `io` | "IOCP/epoll 논블로킹 I/O 코어" | libuv 위에 올린 CPython 소켓·SSL 포크 | 전면적으로 틀렸습니다 |
| `blue` | "제로카피 글루 계층" | 임베디드 Python 인터프리터 + 리플렉션 + Marshal | 개념은 맞고 구체값은 전부 틀렸습니다 |
| `mesh` | "지오메트리 압축 & 듀얼 쿼터니언 스키닝" | CMF 직렬화 컨테이너 + 애니메이션 샘플링 런타임 | 압축 기법과 수치가 전부 틀렸습니다 |
| `imageio` | "ASTC / BCn 제로카피 GPU 로더" | 2014년경 CPU측 비트맵 라이브러리 | 전면적으로 틀렸습니다 |
| `scheduler` | "엔진 잡 시스템, CSP 락프리 채널" | Stackless 호환 tasklet/channel의 greenlet 재구현 | 전면적으로 틀렸습니다 |
| `core` | "OS 추상화, 스레딩, 파일 I/O" | 그대로 맞습니다. 단 Android/ARM64 경로가 없습니다 | 대체로 맞습니다 |
| `spatial-audio-clustering` | "에너지 가중 계층 클러스터링" | 평면 k-means Wwise Object Processor 플러그인 | 알고리즘 서술이 전부 틀렸습니다 |
| `audio` | "3D 사운드 코어 & 버퍼" | CCP의 Wwise SDK 래퍼 | 정체가 틀렸습니다 |

### 2-2. `math`

- `include/`에 헤더 27개가 평면 구조로 놓여 있습니다. 타입은 AxisAlignedBox,
  AxisAlignedEllipsoid, CcpMath, Color, Float16, Matrix, NumConst, Plane, Quaternion,
  Ray, Requirements, Sphere, Vector2/3/4입니다.
- **Dual Quaternion 타입, 파일, 함수가 존재하지 않습니다.** `include/`와 `src/` 모두 없습니다.
- Quaternion은 `float x, y, z, w` 4개 멤버로 16바이트이며 `alignas`가 없습니다. 함수는
  IdentityQuaternion, LengthSq, Length, Normalize, Inverse, Conjugate, Exp, Dot,
  RotationQuaternion(3종), Slerp, GetAxisAngle뿐입니다. NLERP는 없습니다.
- **SIMD 인트린식이 없습니다.** `Requirements.h`가 유일한 진입점이며 `DirectXMath.h`와
  `DirectXPackedVector.h`만 포함하고 `using namespace DirectX`를 선언합니다.
  `immintrin.h`, `arm_neon.h`, `xmmintrin.h`가 없고 아키텍처 분기 `#if`도 없습니다.
- Vector3는 `float x, y, z`로 12바이트라 XMVECTOR를 담을 수 없습니다. DirectXMath는
  연산용이 아니라 상호변환용입니다(`XMStoreFloat3` / `XMLoadFloat3` 왕복).
  저장소 전체에서 DirectXMath 연산 호출은 `src/Quaternion.cpp`의
  `XMQuaternionRotationRollPitchYaw` 단 하나이며 `#if __APPLE__` 대체 경로가 붙어 있습니다.
- vcpkg 의존성은 gtest와 directxmath 둘뿐이며 **glm이 없습니다.**
- CMakePresets 대상은 x64-windows, x64-osx, arm64-osx이고 **Android와 Linux 프리셋이 없습니다.**
- `PerspectiveFovMatrix`는 표준 Z(near가 0, far가 1)입니다:
  `m[2][2] = zf/(zn-zf)`, `m[2][3] = -1.0f`, `m[3][2] = (zf*zn)/(zn-zf)`. Reverse-Z가 아닙니다.
- Frustum 타입이 없고, Capsule이 없고, OBB가 없고, double 정밀도 벡터가 없습니다.

### 2-3. `destiny`

- **완전히 동작하는 시뮬레이션이며 뼈대가 아닙니다.** `src/`가 약 22,200줄의 C++이고
  Ballpark.cpp가 215KB, Ball.cpp가 70KB, Thunkers.cpp가 85KB입니다. Python 테스트 약 6,100줄이
  하드코딩된 골든 float 궤적으로 회귀 검증합니다.
- `include/`에는 파일이 2개(합계 854바이트, `IEveBallpark.h`와 `IEveReferencePoint.h`)뿐입니다.
  여기만 보면 뼈대로 오인하게 됩니다. 실물은 전부 `src/`에 있습니다.
- 1차 선형 항력 ODE를 **폐쇄형 해석해로** 풉니다. 오일러도 RK4도 아니며 틱은 1초입니다.
- 시정수는 `tau = mMass * mAgility / mFriction`입니다. `mFriction`은 상수가 아니라 Ballpark의
  변경 가능한 멤버이며 `Ballpark.cpp:100`에서 `1000000.0`으로 초기화되고 Python에
  "friction, 우주의 마찰 계수"로 노출됩니다. 엔진은 tau를 저장하지 않고
  `mTimeFactor = exp(-mFriction*dt/(mMass*mAgility))`를 저장합니다.
- 저작 방향이 일반적인 물리 엔진과 반대입니다. `mMaxVel`이 입력이고 추력을 역산합니다:
  `maxThrust = k * speedFraction * maxVel / (m * agility)` (`Ballpark.cpp:1408`).
- 좌표는 자체 타입 `Vector3d`(double x, y, z, `src/Vector3d.h`)이며 **glm이 아닙니다.**
  float에서 double로는 암시적으로 확장되고, double에서 float로는 명시적 `AsVector3()` 호출이
  필요합니다. 이 비대칭이 정밀도 손실 지점을 코드에서 눈에 보이게 만듭니다.
- Floating Origin이 **증분식**입니다. Destiny는 시뮬레이션을 재기준화하지 않습니다.
  위치는 한 Ballpark(= 한 성계) 안에서 절대 double로 유지됩니다. 노출하는 것은
  `IEveReferencePoint::GetReferencePoint`(플레이어 ego ball의 절대 위치)와
  `IEveBallpark::Delta`(ego ball의 틱당 이동량, `AsVector3()`로 float 축소)입니다.
  렌더러가 자기 float 원점을 유지하며 델타만큼 밀어 줍니다.
- Partition은 희소 8단계 **균일 그리드**(최소 셀 480m, int64 `(i,j,k,level)` 해시 키)입니다.
  계층적 바운딩 스피어 트리가 아닙니다.
- **Bubble은 물리가 아니라 AOI/네트워크 리플리케이션 범위입니다.** 아무것도 밀어내지 않습니다.
- Dual Quaternion이 없습니다. `src/Quaternion.h`에 double 쿼터니언 `QuaternionD`만 있습니다.
- vcpkg 의존성은 carbon-blue, carbon-core, carbon-math, carbon-exefile-interpreter, gtest입니다.
  **Havok, SpeedTree, FMOD가 없습니다.** destiny는 자체 충돌 검출과 응답을 갖고 있습니다
  (Collision.cpp, Capsule.cpp, OrientedBox.cpp, BoxShape.cpp, Triangle.cpp, AABB.cpp).
- 저장소 루트에 32비트 PE DLL `destinyI.dll`(438,272바이트)이 커밋되어 있으나 CMake가
  참조하지 않습니다. 실행하지 마십시오.

### 2-4. `trinity`

- `trinityal/` 백엔드 디렉터리는 정확히 dx11, dx12, metal, stub 넷이며 **vulkan이 없습니다.**
  CMake 옵션은 BUILD_DX11 / BUILD_DX12 / BUILD_METAL / BUILD_SHADER_COMPILER / WITH_GRANNY /
  BUILD_DOCUMENTATION입니다.
- `shadercompiler/`는 CCP 자체 HLSL 프론트엔드(`HLSLParser.y` + `HLSLScanner.re`, lemon과 re2c)이며
  **HLSL과 MSL만 방출합니다.** SPIR-V 경로도 GLSL 경로도 없습니다.
  `Platforms.h`는 PLATFORM_DX11=2, PLATFORM_DX12=6, PLATFORM_METAL=10입니다.
- **셰이더 파일이 저장소에 없습니다.** 렌더러가 로드하는 94개 이펙트는 전부
  `res:/Graphics/Effect/Managed/Space/PostProcess/ToneMapping.fx` 같은 런타임 리소스 경로이고
  그 번들은 공개되지 않았습니다. 공개된 것은 오케스트레이션뿐이며 셰이더 수학은 0바이트입니다.
- **Forward+(타일드 또는 클러스터드)입니다.** `Tr2LightManager`가 LightBuffer와
  LightIndexBuffer를 관리합니다. 저장소 전체에 G-buffer가 없습니다.
- **컬링은 CPU측입니다.** `EveInstancedMeshManager::PerformFrustumCulling`(line 480)이
  TBB(`Tr2ParallelDo`)로 계층적 구 판정을 수행하고 화면 픽셀 크기 추정으로 LOD를 정합니다.
  컬링 컴퓨트 셰이더가 없습니다. `Tr2IndirectDrawBuffer`는 있으나 CPU가 기록하고
  ExecuteIndirect가 재생합니다.
- **Reverse-Z는 실재하며, 켜는 방식이 핵심입니다.**
  `Tr2Renderer::GetReversedDepthProjectionTransform()`(`Tr2Renderer.cpp:477-483`)이
  `proj._33 = -proj._33 - 1.f; proj._43 = -proj._43;`를 수행하고,
  씬 전역 적용은 `EveSpaceSceneRenderDriver.cpp:446`의 `SetInvertedDepthTest(true)`입니다.
  즉 파이프라인마다 비교 함수를 손으로 뒤집는 것이 아니라
  `Tr2EffectStateManager::SetInvertedDepthTest`라는 **전역 렌더 상태 오버라이드 테이블**
  (각 비교 함수를 대응 함수로 매핑하는 배열)로 한 번에 뒤집습니다.
- **무한 원평면이 아닙니다.** `PerspectiveFovMatrix(fov, aspect, zn, zf)`와
  `PerspectiveOffCenterMatrix(l, r, b, t, zn, zf)` 모두 유한 far를 받습니다.
- **성운은 큐브맵 텍스처입니다.** `EveSpaceScene.cpp:2988`에 "the environment cubemap
  (aka nebula)", `:3206`에 "the environment cubemap aka the nebula"라는 주석이 있고,
  헤더(`EveSpaceScene.h:449-453`)에 `m_lowQualityNebulaResPath`, `m_lowQualityNebula`,
  `m_lowQualityNebulaMixResPath`, `m_lowQualityNebulaMix`가 있습니다. 레이마칭이 아닙니다.
- **SDF 쉴드가 없습니다.** "signed distance", "sdf", "hexagon", "hexgrid" 검색 결과가 없습니다.
  이름이 비슷한 `EveDistanceField`는 셰이더가 아니라 CPU측 근접·경계 추적기이며
  카메라 거리로 볼륨 안개 영역 크기를 정합니다(히스테리시스 in 0.25s / out 2.0s).
- Rayleigh는 저장소 전체에서 0건이고 행성 고리도 0건입니다. Mie는 있으나 대기가 아니라
  볼류메트릭 안개용입니다(`UpdateMieEnvironmentMap.fx`, `FroxelPerFrameData`의 `float EnvironmentG`).
- 왜곡은 실재합니다. `TRIBATCHTYPE_DISTORTION`이 13개 배치 타입 중 하나이고
  `GetDistortionMapIfNeeded`와 `ApplyDistortion`, `res:/.../PostProcess/Distortion.fx`가 있습니다.
  다만 톤매핑 직전이 아니라 **씬 렌더 내부에서 두 번** 적용됩니다(RenderBackgroundPass 후,
  RenderMainPass 후). 매번 뒤에 깊이 스텐실을 다시 바인딩합니다.
- **VR 지원이 전무합니다.** foveat, multiview, VK_KHR_multiview, stereo, OpenXR, OpenVR,
  HMD, per-eye, Oculus, Vive, Rift, headset, InstancedStereo, SV_ViewportArrayIndex, VRS,
  VariableRateShading이 전부 0건입니다.
- 기존 문서가 언급하지 않은 실제 기능이 많습니다: TAA(`Tr2PPTaaEffect`), ACES 기본 3종 톤매퍼,
  히스토그램 동적 노출, 시간적 업스케일링과 프레임 생성, froxel 볼류메트릭 안개,
  SH 2차 바운스 조명(`Tr2ShLightingManager`), 섀도맵 아틀라스 패커, 디노이징이 붙은
  레이트레이스드 섀도, GPU 파티클, 그리고 **`Tr2GpuResourcePool`(트랜지언트 리소스 풀)**입니다.
- `vcpkg.json`에 vulkan-headers와 vulkan-memory-allocator가 있지만 오해하면 안 됩니다.
  `trinity/Tr2VirtualAllocator.cpp`가 VMA를 **CPU측 가상 서브할당자로만** 씁니다:
  `VMA_STATIC_VULKAN_FUNCTIONS 0`, `VMA_DYNAMIC_VULKAN_FUNCTIONS 0`이고 호출은
  `vmaCreateVirtualBlock` / `vmaVirtualAllocate` / `vmaVirtualFree` / `vmaDestroyVirtualBlock`뿐입니다.
  Vulkan 디바이스도 Vulkan 메모리도 쓰지 않습니다.
- 툴체인은 `trinity/README.md`가 VS2017 **v141** 툴셋을 요구합니다
  (`cmake --preset x64-windows-internal -A x64 -T v141`). 이는 Python 2.7 레거시 클라이언트용
  trinity 전용 핀이며, 레지스트리에는 v145 트리플렛도 있습니다.

### 2-5. `io`와 `blue`

- **`io`는 자체 IOCP/epoll 코어가 아닙니다.** CPython 3.12의 C socket 모듈과 ssl 모듈을 포크해
  **libuv** 위에 올린 것이고, 목적은 소켓 호출이 OS 스레드가 아니라 greenlet/tasklet을
  블록하게 만드는 것입니다. 모듈명은 `_carbonsocket`과 `_carbonssl`이며 접두사를 떼고
  `sys.modules`에 주입됩니다. README에 "Tasklet-blocking sockets for use with
  carbon-scheduler... TCP and UDP connections only."라고 적혀 있습니다.
- vcpkg 의존성은 gtest, carbon-scheduler>=1.4.3, **libuv>=1.52.1**, python3>=3.12.9,
  carbon-core>=3.1.0, openssl 1.1.1k입니다.
- **워커 스레드 풀이 없습니다.** `TickUvLoop()`이 `uv_run(GetUvLoop(), UV_RUN_NOWAIT)`를
  코루틴 스케줄러에서 협조적으로 펌핑합니다. `CreateIoCompletionPort`,
  `GetQueuedCompletionStatusEx`, `epoll_create1`, `EPOLL_CLOEXEC`, `EPOLLET`, `recvmmsg`가
  전부 호출되지 않습니다.
- `cio_buffer_pool`, `RingBuffer::AllocatePacket`, 락프리 구조, 슬랩 할당자가 전부 없습니다.
  실제로 있는 것은 핸들별 수신 버퍼입니다: `constexpr size_t BUF_SIZE = 65536;`와
  `if( !handleBuf.base ) { handleBuf.base = new char[BUF_SIZE]; }`, 그리고
  `ssize_t bufReadPos` / `ssize_t bufWritePos` 커서입니다. 프레임이 안 맞으면 동적으로 realloc합니다.
- **실제 프레임 헤더는 4바이트 빅엔디언입니다.**
  `m_packetHeader = ntohl( *reinterpret_cast<uint32_t*>( src ) );`이고
  `payloadLen()`은 `m_packetHeader & ceHeaderSizeMask`(0x0FFFFFFF)입니다.
  비트 28이 서면 두 번째 빅엔디언 uint32 OOB 길이가 따라옵니다.
  와이어 레이아웃은 `[4B hdr][4B oobLen][oob][payload]`입니다.
- 압축은 마셜러가 아니라 이 헤더의 비트입니다: `ceHeaderBitZlibCompressed = 1 << 29`,
  `ceHeaderBitSnappyCompressed = 1 << 30`("was compressed with snappy, no longer supported").
  **LZ4는 없습니다.**
- `blue`의 Marshal 실제 opcode는 `blue/src/Marshal.h`의 `PYTYPES` 열거형입니다:

```
TY_INVALID=0, TY_NONE=1, TY_GLOBAL=2, TY_INT64=3, TY_INT32=4, TY_INT16=5, TY_INT8=6,
TY_INT_N1=7, TY_INT_0=8, TY_INT_1=9, TY_FLOAT=10, TY_FLOAT_0=11, TY_COMPLEX=12,
TY_STR=13, TY_STR_EMPTY=14, TY_STR_CHAR=15, TY_STR_SHORT=16, TY_STR_TABLE=17,
TY_UNICODE=18, TY_BUFFER=19, TY_TUPLE=20, TY_LIST=21, TY_DICT=22, TY_INSTANCE=23,
TY_CALLBACK=25, TY_PICKLE=26, TY_REFERENCE=27, TY_CRC_CHECK=28, TY_TRUE=31, TY_FALSE=32,
TY_PICKLER=33, TY_REDUCE=34, TY_NEWOBJ=35, TY_TUPLE0=36, TY_TUPLE1=37, TY_LIST0=38,
TY_LIST1=39, TY_UNICODE_0=40, TY_UNICODE_1=41, TY_DBROW=42, TY_WSTREAM=43, TY_TUPLE2=44,
TY_MARK=45, TY_UTF8_OBSOLETE=46, TY_LONG=47,
TY_SIGNATURE=126, TY_SIGNATURE2=125,
TY_SHAREDFLAG=0x40, TY_TYPEMASK=0x3F
```

- 구조적으로 가장 중요한 사실은 **태그 바이트의 상위 비트가 공유 객체 플래그이고
  타입은 하위 6비트뿐이라는 점**입니다(`TY_SHAREDFLAG=0x40`, `TY_TYPEMASK=0x3F`).
- **float32 태그가 존재하지 않습니다.** `TY_FLOAT=10`은 C double이고 `TY_FLOAT_0=11`은
  리터럴 0.0입니다. 즉 스칼라 실수에 항상 8바이트를 씁니다.
- `TY_SIGNATURE=126`(0x7E) 뒤에는 flags 바이트가 아니라 int32 공유객체 개수가 옵니다
  (헤더 5바이트). 버전이 붙는 변형은 다른 태그 `TY_SIGNATURE2=125`(0x7D)이며 헤더가 2바이트입니다.
- 스트링 테이블 인덱스는 **1바이트**입니다: `stream->Write((char)PyLong_AS_LONG(index));`
  테이블은 **C++에 컴파일 타임 하드코딩**되어 있습니다
  (`const char* MARSHAL_STRINGS[] = { "*corpid", "*locationid", "age", "Asteroid", ... };`,
  `Marshal::Init`에서 인터닝). 세션 시작 시 협상하거나 패킷 헤더로 전달하지 않으며,
  1바이트 인덱스라 255개가 상한입니다.
- 마셜러에 바이트 스왑이 없습니다. 네이티브 순서로 씁니다. 빅엔디언은 한 계층 아래
  carbon-io의 4바이트 프레임 헤더에만 있습니다.
- 객체 메모이제이션은 실재합니다: `isShared = (type & TY_SHAREDFLAG) == TY_SHAREDFLAG;`,
  객체를 `std::vector<BluePy> mShared`에 추가하고, 재등장 시 `TY_REFERENCE=27` 다음에
  `WriteInteger` 인덱스를 씁니다.
- **`BlueNetOOBHeader`가 존재하지 않습니다.** 실제 `BlueNetHeader`는 `StructPacker` 매크로로
  선언된 **비트팩 가변길이** 구조입니다: `int64 sourceID`, `int64 destinationType`,
  `int64 destinationID`, `int32 blueNetKey`, `uint32 forkedAddresses`, `uint32 serialNumber`,
  `uint32 latencyAccumulator`, `bool priority`, `uint64 timestamp`.
  매직 넘버가 없습니다. 아래 계층이 이미 프레임을 구분하기 때문입니다.
- 라우팅은 channelId 스위치가 아니라 destinationType 비교와 해시 조회입니다:
  `ADDRESS_TYPE_NODE=1`, `ADDRESS_TYPE_CLIENT=2`, `ADDRESS_TYPE_BROADCAST=4`,
  `ADDRESS_TYPE_ANY=8`. `blueNetKey`는 서비스명의 FNV-1a 해시이지 작은 enum이 아닙니다.
- **BlueNet은 Python을 우회하지 않습니다.** Python에 노출된 C++ 모듈이며 자신이
  `Marshal::New()`와 `SaveObject(...)`를 호출하고 `PyChannelObject` 큐로 전달합니다.
  다만 라우터가 페이로드에 마셜러를 돌리지 않고 OOB 블롭만으로 디스패치할 수 있다는
  **OOB 분리 아이디어 자체는 실재합니다.**
- `blue` README는 "Central library providing the embedded Python interpreter, game loop,
  resource-loading and other functions."입니다. BlueResMan, BackgroundReader,
  BlueObjectRecycler를 소유합니다. `blueexposure`는 완전한 C++ 리플렉션 프레임워크입니다
  (IBlueRtti.h, ICustomPersist.h, INotify.h, ICopier.h, BlueMemberIterator.h, BlueRegistration.h).
  `MAP_ATTRIBUTE("mass", mMass, "Mass of ball (kg)", Be::READWRITE | Be::NOTIFY | Be::PERSIST)`
  선언 하나로 Python 속성 접근과 변경 알림과 직렬화를 동시에 얻습니다.

### 2-6. `mesh`, `imageio`, `imagetools`

- `mesh`는 고성능 지오메트리 조작 라이브러리가 아니라 **CMF(Carbon Mesh Format) 직렬화
  컨테이너와 애니메이션 샘플링 런타임, 오프라인 CLI(`cmfprocessor`), Vulkan 디버그 뷰어**입니다.
- 임포트 시 정점 레이아웃(`src/processor/fbximport/mesh.cpp:341-402`)은
  Position Float32x3(12) + Normal Float32x3(12) + Tangent Float32x3(12) +
  Binormal Float32x3(12) + TexCoord Float32x2(8) = **56바이트**입니다.
- 패킹된 정적 정점은 **24바이트**입니다: Position Float32x3(12B) +
  PackedTangent Int16Normx4(8B) + TexCoord Float16x2(4B). 스킨드는 BoneIndices UInt8x4와
  BoneWeights UInt8Normx4가 붙어 32B입니다. 실제 절감은 56에서 24로 약 57%입니다.
- **옥타헤드럴 인코딩이 없습니다.** 실제로 구현된 것은 **QTangent**입니다
  (`include/cmf/tangents.h`, "Quaternion-based compression using 16-bit signed normalized
  integers"). TBN 정규직교 기저를 단위 쿼터니언으로 바꾸고, w는 버려서 언팩 때 복원하며
  그 자리에 handedness 부호를 넣습니다. Int16Norm x 4 = 8바이트로 **N, T, B 세 벡터 전부**를
  담습니다. 옥타헤드럴은 노멀 하나만 다루므로 범주가 다릅니다.
- **16비트 위치 양자화가 없습니다.** Position은 무조건 Float32x3입니다. 정점 선언을 만드는
  두 곳(`mesh.cpp:341`, `:531`)이 모두 하드코딩이고, `MeshImportOptions`는 UV, 색상,
  본 인덱스의 타입만 설정 가능하게 하며 위치는 제공하지 않습니다. 결정적으로 v1 포맷에
  scale, bias, origin, extent 필드가 없어 디코더가 복원할 방법이 없습니다.
- **스키닝은 선형 블렌드(LBS)입니다.** `geometryprepass.comp:475-478`이 4개 mat4x4 본 행렬의
  가중합을 계산합니다. dualquat 검색이 0건입니다.
- meshoptimizer의 정점/인덱스 스트림 코덱은 **디스크상 섹션 전체**에만 적용되며 상주 메모리를
  줄이지 않습니다.
- 저장 모델은 메가버퍼의 반대입니다: `BufferView{sectionIndex, offset, size, stride}`,
  버퍼당 섹션 하나, 각자 `gpuAlignment`를 가집니다. 명시적으로 "relocatable in memory,
  allowing selective loading of data as needed"를 지향합니다.
- `imageio`는 2014년경 **CPU측 비트맵 라이브러리**입니다. `HostBitmap`(malloc 픽셀 버퍼와
  치수, DXGI를 복제한 PixelFormat)과 포맷 핸들러 BMP, DDS, JPG, PNG, TGA, PSD,
  VTA(NanoVDB 기반 CCP 독자 애니메이션 볼륨 텍스처 컨테이너)를 갖습니다.
  **GPU 코드가 0바이트입니다.** 의존성은 libpng, libjpeg-turbo, zlib, carbon-core,
  openvdb, gtest입니다.
- **ASTC가 없습니다.** `include/PixelFormat.h`는 DXGI_FORMAT 0~99를 그대로 옮긴 것이고
  `PIXEL_FORMAT_BC7_UNORM_SRGB=99`에서 끝납니다. ASTC, ETC1, ETC2, EAC, PVRTC 항목이 없습니다.
  **Adreno가 하드웨어로 지원하는 포맷이 하나도 없다는 뜻입니다.**
- 밉맵 생성은 있으나 제한적입니다. 2D와 CUBE만 지원하고, 이미 밉이 있으면 거부하며,
  8비트 무압축 포맷만 허용하고, 필터는 저장 바이트에 대한 2x2 박스 평균에 정수 시프트이며,
  sRGB에서 선형으로의 변환이 없고 알파 프리멀티플라이도 없습니다.
  따라서 알베도 체인이 체계적으로 어두워집니다.
- **아틀라스 패커가 없습니다.**
- `imagetools`는 NVTT와 선택적으로 AMD Compressonator를 감싼 Python/Blue 확장이며
  BCn 전용이고 기본값이 BC1입니다.

### 2-7. `scheduler`와 `core`

- `scheduler`는 **엔진 잡 시스템이 아닙니다.** CPython 3.12 C 확장 모듈 `_scheduler`이며
  Stackless Python의 Tasklet/Channel API를 **greenlet 패키지 위에** 재구현한 것입니다.
  설계 문서 원문은 "Create a Python 3.12 Cextension to offer required functionality from
  Stackless Python utilising Greenlet."입니다. 배경은 EVE가 Stackless Python 2.7 기반이었고,
  CCP가 2023년 Python 3으로 옮기면서 Stackless 유지 대신 순정 CPython을 택했으며,
  잃어버린 tasklet/channel API를 복원하려고 이 확장을 만들었다는 것입니다.
- `src/Tasklet.h`의 private 멤버가 `PyGreenlet* m_greenlet;`이고 스택 전환은
  `PyGreenlet_Switch( m_greenlet, args, kwargs )`입니다.
- **GIL 아래 협조적 마이크로스레드이며 병렬 실행이 없습니다.** `ScheduleManager::Run`은
  한 번에 정확히 하나의 tasklet으로 전환합니다. tasklet은 스레드를 이동할 수 없고
  런타임 에러 문구가 "Cannot switch tasklet from another thread"입니다.
- Channel은 **버퍼 없는 랑데부**입니다. 수신자가 없으면 송신자가 블록합니다:
  `AddTaskletToWaitingToSend(current); current->Block(this); scheduleManager->Yield();`
- **잡 시스템이 없습니다.** 워커 스레드, 잡 그래프, 의존성 간선, 작업 훔치기, SIMD 디스패치가
  전부 없습니다. `src/`는 22개 파일이고 절반이 Py 바인딩 셰임입니다.
- 건질 만한 것은 `RunTaskletsForTime`이라는 **프레임 예산 기반 협조적 실행 패턴**입니다.
  새 스레드 없이 메인 스레드의 지연 작업 큐를 데드라인까지만 소진합니다.
- **Carbon이 Haruka의 `Threading.md`를 지지합니다.** Carbon도 독립적으로 같은 규칙에
  도달했습니다. 상태는 정확히 한 스레드가 소유하고, 작업은 스레드를 이동하지 않으며,
  교차 상호작용은 소유자 큐에 넣는 것뿐입니다. 설계 문서에 이 제약을 완화하는 안이
  검토되었다가 기각된 기록이 있습니다.
- `core` README는 "Provides generic low-level functionality and cross-platform abstractions
  for system calls."입니다. 헤더가 30개이며 thread, mutex, semaphore, atomic, time, file,
  process, hash, log, assert, memory, crash를 다룹니다.
  **플랫폼 분기가 `if(APPLE)`과 `if(WIN32)`뿐입니다.** `CcpThread.h`가 `mach_port_t`와
  `HANDLE`을 typedef하고 `CcpFileUtils.h`는 `std::wstring` API입니다.
  **Android와 ARM64 경로가 없습니다.**
- `CcpAtomic.h`의 전체 내용은 `#include <atomic>`과 `#define CcpAtomic std::atomic`뿐입니다.
- 참고할 만한 것은 `CachedAllocator<T>`(타입별 고정 크기 프리리스트 풀, 다만 타입별 싱글턴이라
  Zone 기반 힙과는 맞지 않습니다), `CCP_MALLOC(name, ...)` 귀속 할당 추적,
  그리고 Tracy 계측(`TELEMETRY_ZONE("Tasklet::SwitchImplementation")` 등, 뮤텍스마다 이름과
  소유자를 부여)입니다. Tracy는 Android와 ARM64를 지원하므로 Haruka에 적합합니다.

### 2-8. `spatial-audio-clustering`과 `audio`

- `spatial-audio-clustering`은 실재하며 빌드 가능한 **Wwise Object Processor 플러그인**입니다
  (`AK::IAkOutOfPlaceObjectPlugin`, `bCanProcessObjects=true`, `bIsInPlace=false`,
  CompanyID 64 / PluginID 6973, Wwise 2025.1 이상). 별이 65개입니다.
  최상위 구성은 FactoryAssets/, SoundEnginePlugin/, WwisePlugin/, ObjectClusterConfig.h,
  PremakePlugin.lua, bundle_template.json, LICENSE.txt, NOTICE입니다.
- 알고리즘 전체가 `SpatialClustering.cpp` 약 410줄입니다.
- **평면 분할 k-means(Lloyd 반복, 최대 20회)와 거리 게이트**입니다. `k = floor(sqrt(N))`이며
  계층적이지 않습니다. 덴드로그램도, 연결 기준도, 병합 트리도 없습니다.
- **에너지 가중이 아닙니다.** `calculateCentroid()`는 위치를 그냥 더해 개수로 나누는
  비가중 산술 평균입니다. `Utilities::CalculateMeanPosition()`도 비가중입니다.
  입력 구조체가 `ObjectPosition { AkVector position; AkAudioObjectID key; }`뿐이라
  **볼륨 값을 받지도 않습니다.**
- 유일한 가중치는 `initializeCentroids()`에서 원점 근접 시드 중심 하나를 구할 때 쓰는
  가우시안 공간 밀도 커널 `weight = exp(-d제곱/(2*r제곱))`입니다. 이마저 1회 반복에서
  비가중 평균으로 덮어써지고, 이 가중치는 음량이 아니라 거리 커널입니다.
- 클러스터 음량은 Wwise의 `MixNinNChannels`가 각 멤버를 자기 `cumulativeGain` 램프와 함께
  누적한 결과이며 이는 **선형 진폭 합**입니다.
- **LPF나 피치 조절이 없습니다.** `ObjectClusterFXParams.h`에
  `static const AkUInt32 NUM_PARAMS = 1;`이고 유일한 파라미터가 `DISTANCE_THRESHOLD = 0`입니다.
  `ObjectClusterNonRTPCParams`는 빈 구조체입니다. **플러그인은 DSP를 전혀 수행하지 않고
  라우팅과 믹싱만 합니다.**
- `ClusterState`는 `{AkUInt32 activeInputCount; AkUInt16 maxFrames;}`뿐입니다.
  반경도, 바운딩 볼륨도, 클러스터 간 거리 함수도 없습니다.
- 목적이 다릅니다. README에 따르면 고갈되는 자원은 Wwise 보이스가 아니라
  **시스템 공간 오디오 오브젝트 슬롯**입니다("Most spatial audio endpoints (Dolby, Sonic,
  DTS) offer up to 128 concurrent spatial audio objects"). 보이스 고갈은 다른 저장소의
  다른 시스템(`carbonengine/audio`의 SoundPrioritization, `maxAwakeGameObjects` 기본 75)이
  담당합니다.
- README 수치는 "up to 60% reduction in audio thread CPU usage"이고 20척 씬에서
  출력 오브젝트가 20~24개입니다.
- 성능상 주의할 점이 있습니다. `initializeCentroids`는 O(N제곱)이며 N제곱 회 `std::exp()`를
  호출하고 매 오디오 콜백마다 처음부터 다시 계산합니다. 실시간 오디오 스레드에서 힙 할당을
  수행합니다.
- `carbonengine/audio`는 3D 사운드 코어가 아니라 **CCP의 Wwise SDK 래퍼**입니다.
  자체 DSP도 믹서도 공간화 수학도 없습니다. 독자적인 부분은 SoundPrioritization
  (이미터 컬링과 보이스 예산)과 SpatialAudioSettings뿐입니다. 의존성이
  carbon-blue>=6.0.0, carbon-math>=1.2.1, carbon-trinityaudioapi>=2.0.3,
  wwise>=2025.1.5.9095, python3 3.12.9, tbb, carbon-core, carbon-exefile이라
  CCP 외부에서는 사실상 빌드가 불가능합니다.
- **두 저장소는 서로 독립입니다.** `carbonengine/audio`의 CMakeLists.txt와 vcpkg.json에
  클러스터링 플러그인 참조가 없습니다. 런타임에 Wwise 사운드 엔진 안에서 버스 이펙트로만
  만납니다.

---

## 3. 정정 대조표

이 절이 이 문서의 핵심입니다. 왼쪽이 기존 Haruka 문서의 주장이고 오른쪽이 원본 실체입니다.

### 3-1. `Carbon_Engine_Math_Analysis.md`

| 절 | 기존 주장 | 원본 실체 |
|---|---|---|
| 1. 도메인 지도 | math가 "SIMD 벡터/쿼터니언 연산"을 제공한다 | SIMD 인트린식이 0건입니다. DirectXMath는 상호변환용이며 연산 호출이 저장소 전체에 단 하나입니다. |
| 1. 도메인 지도 | math가 "Dual Quaternion 회전+이동"을 제공한다 | Dual Quaternion 타입, 파일, 함수가 존재하지 않습니다. |
| 1. 도메인 지도 | math가 "Ray-Sphere/OBB/Capsule 교차"를 제공한다 | Ray, Sphere, Plane, AxisAlignedBox, AxisAlignedEllipsoid는 있으나 OBB와 Capsule 타입이 없습니다. |
| 1. 도메인 지도 | trinity가 "Logarithmic Depth"를 쓴다 | 원본에서 확인되지 않았습니다. 실재하는 것은 Reverse-Z이며, 유한 far 평면과 함께 씁니다. |
| 1. 도메인 지도 | trinity가 "볼류메트릭 성운 & Procedural Noise"를 쓴다 | 성운은 환경 큐브맵 텍스처입니다. |
| 1. 도메인 지도 | trinity가 "SDF 쉴드 이펙트"를 쓴다 | SDF가 0건입니다. 이름이 비슷한 `EveDistanceField`는 CPU측 근접 추적기입니다. |
| 2-1. 1) | `τ = m·I / 10^6`이며 10^6은 정규화 상수다 | `tau = mMass * mAgility / mFriction`입니다. `mFriction`은 Ballpark의 변경 가능한 멤버이며 `Ballpark.cpp:100`에서 1000000.0으로 초기화되고 Python에 노출됩니다. 하드코딩 상수가 아니라 튜닝 가능한 항력 계수(kg/s)입니다. |
| 2-1. 3) | `t_align = τ · ln(4)` 폐쇄형으로 정렬 시간을 구한다 | 이 폐쇄형이 엔진 어디에도 없습니다. 엔진은 매 틱 2조건 술어(방향 내적 0.01 이내이고 속력제곱이 `0.5625 * maxVel제곱` 초과)를 폴링합니다. `MAX_ALIGN_TICKS = 180`(`Ball.h:43`) 강제 탈출구가 있습니다. 0.5625(`Ball.cpp:1912`)는 실재합니다. |
| 2-2. 1) | `v_t = |v_rel × r| / |r|` 외적으로 각속도를 구한다 | 외적 없이 구합니다. `radialSpeed = (v_rel · r)/|r|` 후 `omega제곱 = (|v_rel|제곱 - radialSpeed제곱) / |r|제곱`이며 sqrt도 없습니다. |
| 2-2. 2) | 명중률이 `0.5^(...)`이고 상수 40000이 엔진에 있다 | `Ballpark::GetAccuracy`(`Ballpark.cpp:5155`)는 `pow(2.0, exponent)`입니다. 40000은 엔진에 없으며 호출자가 넘기는 `optimalSignatureRadius` 인자입니다. 거리 항은 중심 거리가 아니라 표면 거리 `shortDist = |r| - radius_target - radius_shooter`(0에서 클램프)입니다. |
| 2-3. 1) | 월드 좌표가 `glm::dvec3`다 | destiny 자체 타입 `Vector3d`(`src/Vector3d.h`)입니다. glm 의존성이 없습니다. |
| 2-3. 2) | Floating Origin이 "모든 오브젝트에서 카메라를 뺀다" | 그 단계가 destiny에 없습니다. destiny는 재기준화하지 않고 절대 double을 유지하며, `GetReferencePoint`와 틱당 이동량 `Delta`만 노출합니다. 렌더러가 자기 float 원점을 델타만큼 미는 증분식입니다. |
| 2-4. 1) | Reverse-Z가 Carbon math에서 온다 | math의 `PerspectiveFovMatrix`는 표준 Z입니다. Reverse-Z는 trinity 런타임 쪽(`Tr2Renderer.cpp:477-483`)에 있습니다. |
| 2-5. 전체 | math의 Dual Quaternion이 Transform SSBO를 절반으로 줄인다 | Dual Quaternion이 원본에 존재하지 않습니다. 이 절 전체가 근거를 잃습니다. 또한 Haruka에는 `Renderer_render.cpp:234-237`이 모델 행렬 열 길이에서 스케일을 추출하므로, Dual Quaternion으로 바꾸면 스케일이 항상 1.0이 되어 컬링이 깨집니다. |
| 2-6. | "Hierarchical Bounding Sphere Clustering"이고 `I_cluster = sqrt(sum I_i제곱)`이다 | 평면 k-means(Lloyd 반복 최대 20회, `k = floor(sqrt(N))`)입니다. 계층이 없고, 에너지 가중이 아니며, 저 공식이 원본에 없습니다. 클러스터 음량은 Wwise `MixNinNChannels`의 선형 진폭 합입니다. |
| 3. 청사진 표 1행 | Dual Quaternion으로 SSBO 64B를 32B로 반감 | 근거가 사라졌습니다. 아래 [차용 판정표](#5-차용-판정표)의 폐기 항목을 보십시오. |
| 3. 청사진 표 4행 | "SIMD 기반 초고속 CPU/GPU Frustum Culling" | math에 SIMD가 없습니다. trinity의 컬링은 TBB 기반 CPU 계층 구 판정입니다. |
| 3. 청사진 표 6행 | "Reverse-Z & SDF Shaders" | Reverse-Z는 유효합니다. SDF는 원본에 존재하지 않습니다. |

### 3-2. `Full_Carbon_Ecosystem_Analysis.md`

| 절 | 기존 주장 | 원본 실체 |
|---|---|---|
| 1. 생태계 지도 | io가 "IOCP/epoll 논블로킹 I/O"다 | libuv 위에 올린 CPython socket·ssl 포크입니다. IOCP·epoll API가 한 번도 호출되지 않습니다. |
| 1. 생태계 지도 | math가 "SIMD, Dual Quat, Geometry"다 | SIMD도 Dual Quat도 없습니다. |
| 1. 생태계 지도 | audio가 "3D 사운드 코어 & 버퍼"다 | Wwise SDK 래퍼입니다. 자체 DSP도 믹서도 공간화 수학도 없습니다. |
| 2-1. | scheduler가 "수십만 엔티티를 병목 없이" 구동하고 채널이 "락프리"이며 스위칭이 "0.001ms"다 | GIL 아래 협조적 마이크로스레드이며 병렬 실행이 없습니다. Channel은 버퍼 없는 랑데부라 수신자가 없으면 송신자가 블록합니다. 0.001ms 수치가 원본에 없습니다. |
| 2-2. | blue.Marshal이 "메모리 덤프 + 포인터 테이블 리베이스 제로카피 로딩"이다 | 태그 스트림 직렬화기이며 포인터 리베이스가 원본에 없습니다. 실재하는 최적화는 `TY_SHAREDFLAG` 기반 객체 메모이제이션과 `TY_REFERENCE` 역참조입니다. |
| 2-3. 다이어그램 | 원시 정점이 48B이고 패킹 정점이 16B라서 67% 압축이다 | 원시 정점은 56B이고 패킹 정적 정점은 24B입니다. 16바이트 정점이 어디에도 없습니다. 실제 절감은 약 57%입니다. |
| 2-3. 1) | 옥타헤드럴 노멀/탄젠트 인코딩을 쓴다 | 옥타헤드럴이 0건입니다. 실제로 구현된 것은 QTangent(`include/cmf/tangents.h`)이며 Int16Norm x 4 = 8B로 N, T, B 세 벡터를 전부 담습니다. |
| 2-3. 2) | 16-bit 고정소수점 위치 양자화를 쓴다 | 없습니다. Position은 무조건 Float32x3이고, v1 포맷에 scale/bias/origin/extent 필드가 없어 복원이 불가능합니다. |
| 2-3. 제목 | "듀얼 쿼터니언 스키닝" | 선형 블렌드 스키닝(LBS)입니다. `geometryprepass.comp:475-478`이 4개 mat4x4의 가중합을 계산합니다. |
| 2-4. | 보이스 고갈을 해결하고, 에너지 가중 클러스터링을 하며, LPF와 피치를 자동 조절한다 | 고갈 자원은 보이스가 아니라 시스템 공간 오디오 오브젝트 슬롯입니다. 클러스터링은 비가중 평면 k-means입니다. 파라미터가 `DISTANCE_THRESHOLD` 하나뿐이며 DSP를 전혀 수행하지 않습니다. |
| 2-5. | imageio가 ASTC와 BC1~BC7을 GPU VRAM으로 직접 전송하고 아틀라스 패킹을 내장한다 | GPU 코드가 0바이트입니다. ASTC 항목이 없고, PixelFormat은 DXGI 0~99를 옮긴 것이며 BC7_UNORM_SRGB=99에서 끝납니다. 아틀라스 패커가 없습니다. 밉맵 생성은 있으나 sRGB 변환이 없어 알베도가 어두워집니다. |
| 2-6. | parser가 "게임 룰을 바이트코드로 JIT 컴파일하여 0.001ms에 계산"한다 | 저장소 자기 설명이 "Math expression parser"이며 trinity가 셰이더와 커브 수식 평가용으로 의존합니다. 게임 룰 엔진이 아닙니다. JIT 여부와 성능 수치는 원본 미확인입니다. |
| 3. 대응표 | "메가버퍼 16-byte 버텍스 패킹" | mesh의 저장 모델은 메가버퍼의 반대입니다. `BufferView{sectionIndex, offset, size, stride}`로 버퍼당 섹션 하나를 두고 선택적 로딩을 지향합니다. |
| 4. 결론 | Carbon이 "완벽하게 맞물린 초대규모 프레임워크"다 | vcpkg 포트 집합이며 메타 포트에 carbon-trinity와 carbon-audio가 포함되지 않습니다. 플랫폼과 렌더러가 의도적으로 별개 제품입니다. |

### 3-3. `Carbon_Proprietary_Exclusions.md`

| 절 | 기존 주장 | 원본 실체 |
|---|---|---|
| 1. 경계도 | destiny가 "물리/항법 인터페이스 & Ballpark 뼈대"다 | 약 22,200줄의 완전히 동작하는 시뮬레이션입니다. `include/`에 파일이 2개(합계 854바이트)뿐이라 뼈대로 오인하기 쉽습니다. |
| 1. 경계도 | math가 "기초 수학 / Dual Quat"다 | Dual Quat이 없습니다. |
| 1. 경계도 | io가 "CarbonIO 소켓 I/O 링 루프"다 | libuv 펌핑입니다. 자체 링 루프가 없습니다. |
| 2-1. | Dogma가 비공개다 | 부재는 맞습니다. 저장소도 vcpkg 포트도 없습니다. 다만 `carbonengine/parser`를 "Dogma의 축소판"으로 설명한 것은 틀렸습니다. parser는 셰이더와 커브 수식 평가용 수식 파서입니다. 참고로 `carbonengine/fsd` README에 "cFSD를 쓰기로 해서 FSD는 더 개발하지 않는다"고 적혀 있고 cFSD는 조직에 없습니다. |
| 2-2. | Tranquility 클러스터 백엔드가 비공개다 | 부재는 맞습니다. MachoNet/GPCS 와이어 포맷도 공개되지 않았습니다. |
| 2-3. | `CCP_EVE_PERFORCE_BRANCH_PATH`가 빌드를 차단한다 | **사실이 아닙니다.** 이 검사는 `trinity/CMakeLists.txt` 한 곳에만 있고 `if (BUILD_FOR_PYTHON_2)` 안에 있습니다. `BUILD_FOR_PYTHON_2`는 CMakePresets의 12개 프리셋 어디에서도 설정되지 않으므로 기본 빌드에서는 실행되지 않습니다. `destiny/CMakeLists.txt`에는 이 변수가 아예 없습니다. |
| 2-3. | 누락된 상용 미들웨어가 Havok, SpeedTree, FMOD다 | **FMOD, Havok, SpeedTree가 63개 포트와 약 35개 저장소 어디에도 없습니다.** 실제로 포함된 상용 미들웨어는 wwise, granny(RAD, `with-granny` 기능으로 게이트), nvidia-aftermath, nvidia-streamline입니다. destiny는 자체 충돌 검출과 응답을 갖고 있습니다. |
| 2-3. 커뮤니티 반응 | 빌드 실패 문의가 빗발쳤고 악성 스크립트 배포 시도가 있었다 | **사실이 아닙니다.** trinity의 전체 이슈는 12건이며 11건이 구체적인 렌더링 버그 리포트, 1건이 "Do Contributors get Paid?"(닫힘)입니다. 빌드 실패 이슈는 #45 "WITH_GRANNY build fails because Granny SDK download endpoint is unreachable" 하나뿐이고 Perforce와 무관합니다. 악성 스크립트 배포에 대한 근거가 없습니다. |
| 2-4. | 분산 인메모리 DB가 비공개다 | `carbonengine/db`는 존재하지만 README가 스텁입니다("Wrapper for game server database access"). 백엔드 이름도 API도 샤딩 논의도 없습니다. |
| 2-5. | 안티치트 계층이 완전 제외되었다 | 안티치트 저장소나 포트는 없습니다. 다만 인접 기계장치는 공개되어 있습니다: `carbonengine/pdm`("Platform Detection Module, an OS agnostic library for data collection")과 `carbonengine/pdm-proto-wrapper`이며 pdm은 blue와 trinity 양쪽의 의존성입니다. |
| 3. 차용 표 | math에서 "Dual Quat, 8D 패킹, SIMD"를 차용한다 | 셋 다 존재하지 않습니다. |
| 3. 차용 표 | trinity에서 "SDF 절차적 셰이더"를 차용한다 | 셰이더 원천이 저장소에 없습니다. trinity에서 차용 가능한 것은 셰이더가 아니라 상태 관리 방식입니다. |

### 3-4. `Deep_Carbon_Network_Analysis.md`

| 절 | 기존 주장 | 원본 실체 |
|---|---|---|
| 2-1. Opcode 표 전체 | `0x7E` Magic Header(2B flags), `0x01` String Table Ref(uint16 BE), `0x02`~`0x07` 정수/실수, `0x08` Raw String, `0x09`~`0x0C` 컨테이너, `0x2A` Compressed | **표 전체가 원본과 일치하지 않습니다.** 실제 opcode는 `blue/src/Marshal.h`의 `PYTYPES` 열거형이며 [2-5절](#2-5-io와-blue)에 전문을 옮겨 두었습니다. |
| 2-1. | `0x7E`가 2바이트 매직 헤더이고 뒤에 flags 바이트가 온다 | `TY_SIGNATURE=126`(0x7E) 뒤에는 int32 공유객체 개수가 옵니다(헤더 5바이트). 버전이 붙는 변형은 다른 태그 `TY_SIGNATURE2=125`(0x7D)이며 헤더가 2바이트입니다. |
| 2-1. | Float 32 opcode(`0x06`)가 있다 | **float32 태그가 존재하지 않습니다.** `TY_FLOAT=10`은 C double이고 `TY_FLOAT_0=11`은 리터럴 0.0입니다. 스칼라 실수에 항상 8바이트를 씁니다. |
| 2-1. | 태그 바이트가 곧 타입이다 | 상위 비트가 공유 객체 플래그이고 타입은 하위 6비트뿐입니다(`TY_SHAREDFLAG=0x40`, `TY_TYPEMASK=0x3F`). 이것이 이 포맷의 가장 중요한 구조적 특징입니다. |
| 2-1. | Compressed 래퍼가 zlib 또는 LZ4를 쓴다 | 압축은 마셜러가 아니라 io 프레임 헤더의 비트입니다: `ceHeaderBitZlibCompressed = 1 << 29`, `ceHeaderBitSnappyCompressed = 1 << 30`. **LZ4가 없습니다.** |
| 2-2. 1) | 스트링 테이블 인덱스가 2바이트이고 세션 초기나 패킷 헤더로 테이블을 협상한다 | 인덱스가 1바이트라 255개가 상한이며, 테이블은 C++에 컴파일 타임 하드코딩되어 있습니다(`MARSHAL_STRINGS[]`, `Marshal::Init`에서 인터닝). 협상 단계가 없습니다. |
| 2-2. 1) | 대역폭 절감 효과가 60~75%다 | 원본에 이 수치가 없습니다. |
| 2-2. 2) | 객체 메모이제이션이 있다 | **맞습니다.** `mShared` 벡터와 `TY_REFERENCE=27`로 구현되어 있습니다. |
| 3-1. | Windows는 `CreateIoCompletionPort`와 `GetQueuedCompletionStatusEx`, Linux는 `epoll_create1(EPOLL_CLOEXEC)`와 `EPOLLET`, `recvmmsg`를 쓴다 | 여섯 함수 모두 한 번도 호출되지 않습니다. `TickUvLoop()`이 `uv_run(GetUvLoop(), UV_RUN_NOWAIT)`를 코루틴 스케줄러에서 협조적으로 펌핑합니다. |
| 3-1. | `cio_buffer_pool`이 64KB 청크를 락프리 링버퍼로 관리한다 | `cio_buffer_pool`도 `RingBuffer::AllocatePacket`도 락프리 구조도 슬랩 할당자도 없습니다. 실제로는 핸들별 수신 버퍼(`BUF_SIZE = 65536`, `new char[BUF_SIZE]`)와 `bufReadPos`/`bufWritePos` 커서가 있으며 프레임이 안 맞으면 동적 realloc합니다. |
| 3-1. | 워커 스레드 풀이 있다 | 워커 스레드 풀이 없습니다. |
| 3-2. | `BlueNetOOBHeader`가 16바이트 고정이고 magic이 `0x424E4554`("BNET")이며 channelId로 라우팅한다 | **이 구조체가 존재하지 않습니다.** 실제 `BlueNetHeader`는 `StructPacker` 매크로로 선언된 비트팩 가변길이 구조이며 매직 넘버가 없습니다. 라우팅은 destinationType 비교와 해시 조회입니다(`ADDRESS_TYPE_NODE=1`, `CLIENT=2`, `BROADCAST=4`, `ANY=8`). `blueNetKey`는 서비스명의 FNV-1a 해시입니다. |
| 3-2. | Fast-Path가 Python을 완전히 건너뛰고 0.01ms 이하로 디스패치한다 | BlueNet은 Python에 노출된 C++ 모듈이며 자신이 `Marshal::New()`와 `SaveObject(...)`를 호출하고 `PyChannelObject` 큐로 전달합니다. Python을 우회하지 않으며 0.01ms 수치가 원본에 없습니다. 다만 라우터가 페이로드를 마셜링하지 않고 OOB 블롭만으로 디스패치한다는 **OOB 분리 아이디어 자체는 실재합니다.** |
| 4-1, 4-2. | MachoNet 4-튜플 주소와 GPCS 프레임 포맷, `CALL_REQ 0x01` 계열 패킷 타입 | **MachoNet/GPCS가 공개되지 않았습니다.** io README에 "machoNet 패킷 송수신 지원 기능이 추가되었다"는 언급만 있고, 프레임 포맷도 패킷 타입 열거형도 4-튜플 주소 구조체도 저장소에 없습니다. 이 절 전체가 원본 미확인입니다. |
| 5. 다이어그램 | Ballpark 틱이 1Hz~60Hz다 | 1초 틱입니다. |
| 5. 다이어그램 | `Mode=Drift` 모드가 있다 | **Drift 모드가 없습니다.** `DSTBALLMODE`는 13개 항목이고 그중 Drift에 해당하는 것이 없습니다. `mFriction`이 park 전역 스칼라라 바디별로 끌 수도 없습니다. `DSTBALL_STOP`은 댐프너가 아니라 추력을 0으로 두고 항상 켜진 항력이 감속시키는 것입니다. |
| 5-1. | `DestinyBallSnapshot` 와이어 구조체(smallest-three 압축 쿼터니언 포함) | 이 구조체를 저장소에서 확인하지 못했습니다. 원본 미확인이며 근거 없는 서술로 취급해야 합니다. |
| 5-2. | `OnAddBalls` / `OnUpdateBalls` / `OnRemoveBalls` 3대 델타 메시지 | 원본 미확인입니다. |

### 3-5. `Trinity_Rendering_Analysis.md`

| 절 | 기존 주장 | 원본 실체 |
|---|---|---|
| 1, 2-1. | GPU-Driven 파이프라인이며 컴퓨트 셰이더가 프러스텀·오클루전 컬링을 한다 | **컬링은 CPU측입니다.** `EveInstancedMeshManager::PerformFrustumCulling`(line 480)이 TBB(`Tr2ParallelDo`)로 계층적 구 판정을 하고 화면 픽셀 크기 추정으로 LOD를 정합니다. 컬링 컴퓨트 셰이더가 없습니다. `Tr2IndirectDrawBuffer`는 있으나 CPU가 기록하고 ExecuteIndirect가 재생합니다. |
| 1, 2-5. | Reverse-Z와 무한 원평면을 쓰며 범위가 0.1m ~ 1,000,000 km다 | Reverse-Z는 실재하지만 **무한 원평면이 아닙니다.** `PerspectiveFovMatrix`와 `PerspectiveOffCenterMatrix` 모두 유한 far를 받습니다. 0.1m ~ 1,000,000 km라는 수치가 저장소에 없습니다. |
| 2-5. | 이 방식으로 서브밀리미터 정밀도를 얻는다 | Haruka 쪽 실측으로도 성립하지 않습니다. 무한 원평면에서 `d = zNear/z`이므로 dz는 대략 z 곱하기 2의 -24승이고, z = 10^8 m에서 깊이 해상도가 약 6m입니다. |
| 1. | Rayleigh/Mie 대기 산란과 거대 행성 고리를 렌더링한다 | Rayleigh가 0건이고 행성 고리도 0건입니다. Mie는 있으나 대기가 아니라 볼류메트릭 안개용입니다(`UpdateMieEnvironmentMap.fx`, `FroxelPerFrameData`의 `float EnvironmentG`). |
| 2-3. | 성운이 3D 프랙탈 노이즈 레이마칭이며 텍스처 메모리가 0MB다 | **환경 큐브맵 텍스처입니다.** `EveSpaceScene.cpp:2988`과 `:3206` 주석, `EveSpaceScene.h:449-453`의 `m_lowQualityNebula*` 멤버가 근거입니다. |
| 2-3. | 제시된 `fbm()`과 `renderNebula()` GLSL 코드가 Trinity의 것이다 | **셰이더 파일이 저장소에 없습니다.** `.fx`/`.hlsl`/`.fxh`/`.glsl` 검색 결과는 trinityal 안의 Metal 파일 2개뿐입니다. 94개 이펙트는 전부 런타임 리소스 경로이고 번들이 공개되지 않았습니다. 인용된 GLSL은 원본이 아닙니다. |
| 2-2. | SDF 에너지 쉴드와 육각 격자 펄스 셰이더가 있다 | "signed distance", "sdf", "hexagon", "hexgrid" 검색이 전부 0건입니다. `EveDistanceField`는 셰이더가 아니라 CPU측 근접·경계 추적기이며 카메라 거리로 볼륨 안개 영역 크기를 정합니다(히스테리시스 in 0.25s / out 2.0s). |
| 2-4. 2) | 화면 왜곡을 PostPass 톤매핑 직전에 적용한다 | 왜곡 자체는 실재합니다(`TRIBATCHTYPE_DISTORTION`, `GetDistortionMapIfNeeded`, `ApplyDistortion`). 다만 **씬 렌더 내부에서 두 번** 적용됩니다(RenderBackgroundPass 후, RenderMainPass 후). 매번 뒤에 깊이 스텐실을 다시 바인딩합니다. |
| 2. | 렌더러가 "일반적 PBR"과 다른 특수 파이프라인이다 | 실제 구조는 **Forward+(타일드 또는 클러스터드)**입니다. `Tr2LightManager`가 LightBuffer와 LightIndexBuffer를 관리하고 G-buffer가 없습니다. |
| 문서 전체 | Trinity가 VR 렌더링의 참고가 된다 | **VR 지원이 전무합니다.** foveat, multiview, OpenXR, stereo, per-eye, VRS 등이 전부 0건입니다. |
| 문서 전체 | (누락) | 문서가 언급하지 않은 실제 기능이 많습니다: TAA, ACES 기본 3종 톤매퍼, 히스토그램 동적 노출, 시간적 업스케일링과 프레임 생성, froxel 볼류메트릭 안개, SH 2차 바운스 조명, 섀도맵 아틀라스 패커, 디노이징이 붙은 레이트레이스드 섀도, GPU 파티클, `Tr2GpuResourcePool`. |

### 3-6. `Deep_Space_VR_Specification.md`

이 문서는 Haruka 자체 설계 사양이므로 파라미터 표(질량 150~250kg 등)는 Carbon 원본 값이 아니라
Haruka의 자체 선택입니다. 그 자체는 문제가 아니지만, Carbon에서 유래했다고 서술한 부분은
정정이 필요합니다.

| 절 | 기존 주장 | 원본 실체 |
|---|---|---|
| 1-1. 1) | `τ = m·I / 10^6`이 Destiny의 공식이다 | `tau = mMass * mAgility / mFriction`이고 `mFriction`이 튜닝 가능한 멤버입니다. 저장소 골든 테스트로 검증된 예: mass 13e6, agility 0.9이면 tau = 11.6999996900558s. |
| 1-1. 2) | 해석적 적분 해를 쓴다 | **맞습니다.** Destiny도 폐쇄형으로 풉니다. 엔진은 tau가 아니라 `mTimeFactor = exp(-mFriction*dt/(mMass*mAgility))`를 저장합니다. 무저항 극한에서는 `Integrate()`의 `if (k < 1e-10 * m * t)` 테일러 분기가 `p += v*t + 0.5*a*t제곱`으로 퇴화합니다. |
| 1-2. 2) | Drift 모드가 Destiny의 모드다 | Destiny에 Drift 모드가 없습니다. 다만 위의 테일러 분기가 무저항 극한을 이미 처리하므로, **바디별 k를 두면 한 경로로 두 모드를 처리할 수 있다**는 것이 원본이 실제로 알려 주는 설계입니다. |
| 1-4. 1), 2) | 스프링 그랩과 뉴턴 제3법칙 반작용을 Destiny에서 가져온다 | Destiny의 충돌 응답은 그런 모델이 아닙니다. 관통 깊이에서 유도한 가속도를 접촉 법선 방향으로 주고 0.85로 감쇠시킨 뒤 항력 적분기에 넣습니다. 접선 미끄러짐도 반발 계수도 없습니다. **가져오면 안 됩니다.** |
| 2. | Floating Origin이 매 프레임 카메라를 빼는 방식이다 | Destiny 방식은 증분식입니다. 참조점과 틱당 델타만 노출합니다. Haruka가 어느 쪽을 택하든 무방하지만 Carbon에서 왔다고 쓰면 안 됩니다. |
| 3. 전체 | Dual Quaternion 트랜스폼 32B 사양 | Carbon math에 Dual Quaternion이 존재하지 않습니다. 이 절 전체가 Carbon 근거를 잃습니다. 추가로 Haruka `Renderer_render.cpp:234-237`이 모델 행렬 열 길이에서 스케일을 뽑으므로 도입 시 컬링이 깨집니다. |
| 4-2. | Reverse-Z 무한 원평면 행렬 | Haruka 자체 설계로는 유효합니다. 다만 Trinity가 무한 far를 쓴다는 근거는 없고, Trinity의 실제 기여는 [차용 판정표](#5-차용-판정표)에 적은 전역 상태 오버라이드 방식입니다. |
| 4-1. 다이어그램 | "84% 비트 밀집" 같은 수치 | 원본 미확인이며 출처가 Carbon이 아닙니다. |

### 3-7. `VR_Transition_Masterplan.md`

| 절 | 기존 주장 | 원본 실체 |
|---|---|---|
| 1. 차용 맵 | math의 "Dual Quat, SIMD, Fast Intersection" | Dual Quat과 SIMD가 없습니다. 교차 판정은 있으나 OBB와 Capsule 타입이 없습니다. |
| 1. 차용 맵 | destiny의 "Bubble 반발" | **Bubble은 물리가 아니라 AOI/네트워크 리플리케이션 범위입니다.** 아무것도 밀어내지 않습니다. |
| 1. 차용 맵 | trinity의 "Log Depth, SDF 셰이더" | Log Depth는 원본 미확인이고 SDF는 0건입니다. |
| 1. 차용 맵 | spatial-clustering이 파편 컬링에 쓰인다 | Wwise Object Processor 플러그인이며 오디오 오브젝트 슬롯 절약이 목적입니다. 렌더 컬링과 무관합니다. |
| 2-1. 표 | "SLERP / NLERP 회전 보간" | Slerp는 있으나 **NLERP가 없습니다.** |
| 2-1. 표 | "분리축 이론(SAT) 및 SIMD 벡터화 교차 검사" | SAT 구현도 SIMD도 math에 없습니다. |
| 2-2. 2) | "구면 반발 충돌 처리로 표면을 따라 미끄러진다" | Destiny의 충돌 응답에 접선 미끄러짐이 없습니다. `Ball.h` 설계 주석이 "The ball senses other balls as solids which cannot be traversed, so any movement of the balls will avoid each other or be stuck."이고, `Ballpark.cpp`에 "EEEEK! ball heading for infinite recursion into ball"이라는 에러 로깅이 있습니다. |
| 2-3. | Destiny Grid가 Floating Origin을 제공한다 | Partition은 희소 8단계 균일 그리드(최소 셀 480m, int64 해시 키)이며 Floating Origin 장치가 아닙니다. |
| 2-5. | 파편을 "공간 계층 트리(Hierarchical Bounding Sphere)"로 묶는다 | 평면 k-means입니다. 계층 트리가 없습니다. |
| 3. 로드맵 Phase 1 | Transform SSBO 동기화 버그 수정과 Dual Quaternion 도입 | 버그 수정은 유효한 항목이었습니다(2026-08-24 감사 시점 기준 미수정이었으며, **2026-08-25에 `DEF-01`로 해결되었습니다.** 아래 3-9절을 보십시오). Dual Quaternion 도입은 Carbon 근거가 없고 Haruka 컬링을 깨뜨립니다. |

### 3-8. `Engine_Strategy_Evaluation.md`와 `Vulkan_Single_RHI_Decision.md`

두 문서의 전략적 결론 자체는 Carbon 감사 결과로 흔들리지 않습니다. Carbon을 근거로 든
문장만 정정하면 됩니다.

| 문서 · 절 | 기존 주장 | 원본 실체 |
|---|---|---|
| `Engine_Strategy_Evaluation.md` 3-4) | "Carbon Engine의 점성 우주 물리와 Floating Origin, Dual Quat을 흡수하여" | 점성 항력 적분기는 실재하고 흡수할 가치가 있습니다. Floating Origin은 destiny 방식이 증분식이라는 점을 반영해야 합니다. Dual Quat은 존재하지 않으므로 문장에서 빼야 합니다. |
| `Vulkan_Single_RHI_Decision.md` 전체 | Vulkan 단일화가 옳다 | 결론이 유효하며 Carbon 감사가 오히려 이를 보강합니다. trinity의 백엔드는 dx11/dx12/metal/stub이고 `shadercompiler`가 HLSL과 MSL만 방출합니다. 즉 Carbon 본선에는 Haruka가 참고할 Vulkan 경로가 없습니다. `vcpkg.json`의 vulkan-headers와 vulkan-memory-allocator는 `Tr2VirtualAllocator.cpp`가 VMA를 CPU측 가상 서브할당자로만 쓰기 때문에 들어 있는 것이며 Vulkan 디바이스를 쓰지 않습니다. 실제로 참고할 Vulkan 작업물은 [6절](#6-ccp-상하이-vulkan-백엔드)의 미병합 브랜치입니다. |

### 3-9. `Core_Stabilization_Spec.md`와 `Core_Defects_Task_Spec.md` (Haruka 코드 대조)

> **[갱신 · 2026-08-25]** 아래 표의 실측 열은 **2026-08-24 시점의 기록**입니다. 이후 `DEF-01`과
> `DEF-23`(스피어 인덱스 도메인 불일치)이 실제로 수정되었으므로, 두 항목의 현재 상태는
> [Core_Stabilization_Spec.md](../Core_Stabilization_Spec.md)와
> [Core_Defects_Task_Spec.md](../Core_Defects_Task_Spec.md)를 정본으로 삼으십시오.
> "기존 표기" 열은 감사 당시의 문서 상태를 남긴 역사 기록이므로 갱신하지 않습니다.

| 결함 ID | 기존 표기 | `E:\Haruka\src` 실측 |
|---|---|---|
| DEF-01 | `Core_Stabilization_Spec.md:135`에 `[x] 0-1` 완료로 표기 | **[2026-08-25 갱신: 해결되었습니다.]** 아래는 2026-08-24 감사 시점의 기록입니다. 당시에는 수정되어 있지 않았습니다. `Renderer_render.cpp:215` `buildDrawCommands`는 visible/mesh/LOD/프러스텀을 통과한 메시에만, 그리고 opaque와 transparent가 카운터 하나를 공유하며 `ssboIdx`를 증가시켰습니다. `Renderer_render.cpp:296` `extractRenderView`의 `opaqueItems` 루프는 컬링을 하지 않고 별도 카운터를 썼으며, `:313-326`의 투명 머티리얼 역참조 루프도 같은 문제였습니다. **[정정]** 이 칸에 있던 "머티리얼이나 LOD가 없을 때도 증가시키는" 이라는 서술은 사실이 아니므로 삭제했습니다. `World::getVisibleMeshes()`가 `World.cpp:72`에서 `m->visible && m->mesh && m->material`을 이미 걸러내므로 머티리얼 없는 메시는 순회 목록에 들어오지 않고, LOD 실패 경로도 도달하지 않습니다. |
| DEF-02 | CRITICAL | **실재하나 심각도가 과장되었습니다.** `HarukaAllocator.cpp:177-199`의 `allocC`에 펜스 추적이 없는 것은 맞지만, `HarukaAllocator.hpp:9` 주석에 따르면 Zone C 실사용은 64MB 중 약 0.5MB(128배 여유)입니다. 랩어라운드가 약 128프레임마다 일어나고 덮어쓰는 영역이 128프레임 전 데이터이므로 실제 인플라이트 충돌 가능성은 사실상 없습니다. CRITICAL이 아니라 프레임당 Zone C 사용량이 32MB에 근접할 때의 잠재 리스크입니다. |
| DEF-03 | HIGH, 실재 | **맞습니다.** `GeometryPass.cpp:507`의 `vkCmdDrawIndexedIndirectCount`가 분기 없이 호출되고 `Renderer.cpp:586`이 관련 피처를 조건 없이 요구합니다. |
| DEF-04 | HIGH, 실재 | **맞습니다.** `vkCmdPipelineBarrier2`가 `RenderContext.cpp:167`과 `CullPass.cpp:166`, `:194`에서 무조건 호출되고 `Renderer.cpp:586`이 synchronization2를 조건 없이 요구합니다. |
| DEF-05 | `Renderer_render.cpp:L355-364`에 결함이 있다 | **이미 처리되어 있고 파일 위치도 다릅니다.** `Renderer_swap.cpp:59-60`의 `recreateSwapChain`이 진입 즉시 `vkDeviceWaitIdle(device)`를 호출한 뒤 `cleanupSwapChain()`을 실행합니다. |
| DEF-06 | HIGH, `LAZILY_ALLOCATED`와 TRANSIENT가 모두 빠졌다 | **절반만 맞습니다.** `GeometryPass.cpp:239`에 `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT`가 이미 있습니다. 빠진 것은 메모리 속성 쪽이며 `LAZILY_ALLOCATED`는 코드베이스 전체에 0건입니다. |
| DEF-07 | MEDIUM, 실재 | **맞습니다.** `onActorDestroyed` 이벤트는 존재하지만(`World.hpp:27` 선언, `World.cpp:27` 브로드캐스트) 구독자가 한 곳도 없습니다. |

### 3-10. Haruka 쪽 추가 실측 (문서에 반영되지 않은 사실)

이 항목들은 기존 문서의 오류는 아니지만, Carbon 차용을 계획할 때 반드시 함께 고려해야 합니다.

| 항목 | 실측 |
|---|---|
| Vulkan 버전 | `Renderer.cpp:502`의 `VK_API_VERSION_1_4`. 디바이스 피처 체인은 `VkPhysicalDeviceVulkan14Features{pushDescriptor}` 다음 `Vulkan13Features{synchronization2}` 다음 `Vulkan12Features{drawIndirectCount}`입니다. |
| 멀티뷰 | 절반만 되어 있습니다. 피처는 `Renderer.cpp:577-583`에서 켜지지만 `Renderer_render.cpp:169`가 `viewProj[1] = viewProj[0]`으로 두고, 어떤 렌더패스도 `viewMask`를 설정하지 않으며, 모든 셰이더가 `viewProj[0]`만 인덱싱합니다. `Renderer.hpp:167-170`의 FFR 멤버는 선언만 되고 쓰이지 않습니다. |
| 상한값 | `kMaxDraws = 1024`(`RenderTypes.hpp:12`), `kMaxBodies = 4096`(`PhysicsSystem.cpp:81`). |
| 인스턴싱 | `instanceCount = 1`이 다섯 곳에 리터럴로 박혀 있습니다(`Renderer_render.cpp:243`, `GeometryPass.cpp:530`, `:556`, `ShadowPass.cpp:181`). `GeometryPass::DrawItem`에는 `instanceCount` 멤버가 없습니다. |
| 패스 구조 | 패스 레지스트리가 없습니다. `HPass`는 의도적으로 `create()`와 `record()`를 제외한 수명주기 전용입니다(`HPass.hpp:8-15`). 여섯 패스가 Renderer의 구체 멤버이며 여섯 곳에서 손으로 열거됩니다. `EngineContext`(`IGameModule.hpp:9-14`)에 렌더러 참조가 없어 게임 모듈이 렌더러에 닿을 수 없습니다. |
| 셰이더 include | `shaders/include/noise.glsl`과 `sdf.glsl`이 존재하지만 어떤 셰이더도 include하지 않습니다. 게다가 CMake `add_custom_command`의 `DEPENDS`에 include 파일이 없어서, include를 쓰기 시작하면 include 수정이 재컴파일을 유발하지 않아 낡은 SPIR-V를 배포하게 됩니다. |
| GLSL 중복 | `GpuTransform`(`RenderTypes.hpp:27-29`)이 셰이더 4곳에 중복 선언되어 있습니다(`pbr.vert:25`, `depth.vert:18`, `shadow.vert:18`, `triangle.vert:19`). 공유 GLSL include가 없습니다. triangle 셰이더는 CMake 목록에 없는 고아 파일입니다. |
| Reverse-Z 영향 범위 | 전부 함께 바꿔야 합니다: `CameraComponent.cpp:17-18`, `GeometryPass.cpp:89`, `:331`, `:477`, `ShadowPass.cpp:121`, `:154`, `:26`, `:106-108`, `:24`, `linDepth.frag:17`, `pbr.frag:72`, `:74`, `cull.comp:32-33`, `Renderer_render.cpp:86-87`, `:181`, `EditorApp.cpp:1477-1478`, `ssao.frag:19`, `Renderer_swap.cpp:15`, `post.vert:7`, `ui.vert:19`. |
| 물리 | `PhysicsSystem.cpp:103`에서 중력이 `JPH::Vec3(0.f, -9.81f, 0.f)`로 하드코딩되어 있고 세터가 없습니다. 힘, 임펄스, 속도, 레이캐스트 API가 하나도 없습니다. 동기화 루프(`PhysicsSystem.cpp:232-238`)가 매 프레임 Dynamic 바디의 Actor 트랜스폼을 덮어씁니다. Jolt의 `JPH_DOUBLE_PRECISION`은 `CMakeLists.txt:56-68`에서 켜져 있지 않습니다. |
| 입력 | `HInputSystem`은 디지털 전용입니다(`HInputSystem.cpp:22-42`가 `win.isKeyDown`만 호출). `IWindow`(`IWindow.hpp:51-62`)에 아날로그 축이나 VR 포즈가 없습니다. |
| 안드로이드 | `main.cpp:38`이 `GlfwWindow`를 조건 없이 생성합니다. `AndroidWindow`는 `CMakeLists.txt:186-191`에서 컴파일되지만 어디서도 인스턴스화되지 않습니다. |
| TAA | `prevViewProj`가 셰이더 5곳에 선언되어 있으나 읽는 곳이 없습니다. 현재 TAA(`post.frag:48-51`)는 모션 벡터도, 깊이 샘플링도, 투영 지터도 없습니다. |
| 정점 | `Vertex.hpp`는 48바이트 float 레이아웃에 속성 4개입니다. `pbr.vert:36`이 정점마다 `transpose(inverse(mat3(model)))`를 계산합니다. |

---

## 4. 라이선스 실측표

### 4-1. 저장소별 실측 라이선스

| 저장소 | 라이선스 | 라이선스 파일 | 실무 의무 |
|---|---|---|---|
| `math` | MIT | LICENSE | 저작권 고지와 라이선스 전문을 배포물에 포함합니다. |
| `destiny` | MIT | LICENSE | 동일합니다. |
| `trinity` | MIT | **LICENSE.md** (파일명이 다릅니다) | 동일합니다. 저작권 표기는 "Copyright (c) 2026 CCP Games"입니다. |
| `blue` | MIT | LICENSE | 동일합니다. |
| `mesh` | MIT | LICENSE | 동일합니다. |
| `imageio` | MIT | LICENSE | 동일합니다. |
| `imagetools` | MIT | LICENSE | 동일합니다. |
| `scheduler` | MIT | LICENSE | 동일합니다. |
| `core` | MIT | LICENSE | 동일합니다. |
| `audio` | MIT | LICENSE | 동일합니다. 다만 Wwise SDK 자체는 별도 상용 라이선스입니다. |
| `io` | **PSF License Version 2** | LICENSE.txt | 아래 4-2절을 보십시오. |
| `spatial-audio-clustering` | **Apache-2.0** | LICENSE.txt + NOTICE | 아래 4-3절을 보십시오. |

### 4-2. `io`가 PSF-2.0인 이유와 그 결과

`io/LICENSE.txt`의 첫 줄이 "PYTHON SOFTWARE FOUNDATION LICENSE VERSION 2"입니다.
파일 안에 "Copyright (c) 2001 Python Software Foundation"과 "Copyright (c) 2026 CCP Games",
그리고 "This software is a derivative work incorporating Python as modified by CCP Games"가
함께 적혀 있습니다. `io`가 CPython 3.12의 C socket 모듈과 ssl 모듈을 포크한 것이기 때문입니다.

실무 의무는 다음과 같습니다.

| 조항 | 의무 |
|---|---|
| PSF-2.0 제2조 | PSF 저작권 고지를 유지해야 합니다. |
| PSF-2.0 제3조 | 파생물을 배포할 때 **Python에 가한 변경 사항의 요약(a brief summary of the changes made to Python)을 동봉**해야 합니다. MIT에는 없는 추가 의무입니다. |

**Haruka에 대한 판정**: `io`에서 코드를 가져오면 MIT 저장소와 라이선스 의무가 달라집니다.
그런데 [5절](#5-차용-판정표)에서 보듯 `io`에서 가져올 코드가 사실상 없습니다.
가져올 가치가 있는 것은 코드가 아니라 **4바이트 빅엔디언 프레임 헤더의 비트 배치 아이디어**
(길이 마스크 0x0FFFFFFF, OOB 비트 28, 압축 비트 29/30)라는 설계 개념입니다.
개념을 참고해 독립적으로 구현하면 PSF-2.0 의무가 발생하지 않습니다.
**코드를 복사하지 마십시오.**

### 4-3. `spatial-audio-clustering`이 Apache-2.0인 이유와 그 결과

`LICENSE.txt`에 Apache 2.0 전문이 들어 있고 별도의 `NOTICE` 파일이 존재합니다.
Apache-2.0은 MIT와 달리 다음을 요구합니다.

| 항목 | 의무 |
|---|---|
| 라이선스 사본 | 배포물에 Apache-2.0 전문을 첨부해야 합니다. |
| NOTICE 전파 | 원본에 NOTICE 파일이 있으므로 그 내용을 배포물에 전달해야 합니다. |
| 변경 고지 | 수정한 파일에 변경 사실을 표시해야 합니다. |
| 특허 조항 | 명시적 특허 라이선스 허여와 특허 소송 시 종료 조항이 붙습니다. |

**Haruka에 대한 판정**: 이 저장소는 알고리즘 자체가 평면 k-means라 새로 알려 주는 것이
거의 없고, 실시간 오디오 스레드에서 힙 할당과 O(N제곱) `std::exp` 호출을 수행하므로
코드를 그대로 가져올 이유가 없습니다. 가져갈 가치가 있는 것은
**프레임 간 클러스터 정체성 유지와 멤버 교체 시의 게인 행렬 보간**
(`GeneratedObject::volumeMatrix`에 이전 볼륨 행렬을 캐시하는 방식)이라는 개념뿐이고,
이것 없이는 멤버가 바뀔 때마다 파형 불연속이 생깁니다. 개념 참고에 그치면
Apache-2.0 의무를 지지 않습니다.

### 4-4. 라이선스 관점의 요약 판정

1. MIT 저장소(math, destiny, trinity, blue, mesh, imageio, scheduler, core, audio)에서는
   코드 차용이 자유롭습니다. 고지 유지만 지키면 됩니다.
2. `io`(PSF-2.0)와 `spatial-audio-clustering`(Apache-2.0)은 의무가 다릅니다.
   **두 저장소는 개념 참고로만 다루고 코드를 복사하지 않는 것이 원칙입니다.**
3. 상용 미들웨어 경계도 확인해야 합니다. Carbon에 실제로 포함된 상용 미들웨어는
   wwise, granny(RAD, `with-granny` 기능으로 게이트), nvidia-aftermath, nvidia-streamline입니다.
   FMOD, Havok, SpeedTree는 63개 포트와 약 35개 저장소 어디에도 없습니다.

---

## 5. 차용 판정표

### 5-1. 가져올 것 (우선순위 순)

| 순위 | 항목 | 출처 | 무엇을 가져오는가 | Haruka 적용 지점 |
|---|---|---|---|---|
| 1 | **폐쇄형 항력 적분기** | destiny (MIT) | 1차 선형 항력 ODE를 오일러나 RK4가 아니라 해석해로 풉니다. 엔진은 tau가 아니라 `mTimeFactor = exp(-mFriction*dt/(mMass*mAgility))`를 저장합니다. `Integrate()`의 `if (k < 1e-10 * m * t)` 테일러 분기가 무저항 극한을 `p += v*t + 0.5*a*t제곱`으로 퇴화시키므로, **바디별 k를 두면 한 경로로 감쇠 모드와 표류 모드를 모두 처리**할 수 있습니다. | 무중력 이동 컴포넌트. 가변 프레임레이트에서 발산하지 않습니다. |
| 2 | **QTangent** | mesh (MIT) | TBN 정규직교 기저를 단위 쿼터니언으로 바꾸고 w를 버려 언팩 때 복원하며 그 자리에 handedness 부호를 넣습니다. Int16Norm x 4 = 8바이트로 N, T, B 세 벡터 전부를 담습니다. | `Vertex.hpp`(현재 48바이트 float 레이아웃 4속성). 노멀 하나만 다루는 옥타헤드럴보다 범주가 넓습니다. |
| 3 | **SoundPrioritization** | audio (MIT) | 약 150줄이며 위치와 불리언만 필요합니다. 거리 제곱을 비용으로 삼고 특징을 감산 보너스로 처리해 정렬한 뒤 예산까지 살립니다. | 오디오 이미터 컬링. 단 크기가 EVE 기준으로 보정되어 있어(EVE는 거리 제곱이 1e10을 넘어 보너스에 1e7 배수가 필요합니다) **VR 스케일에서는 상수를 다시 잡아야 합니다.** |
| 4 | **전역 렌더 상태 오버라이드 방식 Reverse-Z** | trinity (MIT) | `Tr2EffectStateManager::SetInvertedDepthTest`가 각 비교 함수를 대응 함수로 매핑하는 배열로 깊이 테스트를 한 번에 뒤집습니다. 씬 전역 적용 지점이 `EveSpaceSceneRenderDriver.cpp:446` 한 곳입니다. | Haruka의 Reverse-Z 전환 지점이 20곳 이상 흩어져 있습니다([3-10절](#3-10-haruka-쪽-추가-실측-문서에-반영되지-않은-사실) 참조). 파이프라인마다 손으로 뒤집는 대신 이 방식을 쓰면 전환 비용과 회귀 위험이 크게 줄어듭니다. |
| 5 | **명시적 내로잉 규율** | destiny (MIT) | float에서 double로는 암시적으로 확장되고, double에서 float로는 명시적 `AsVector3()` 호출이 필요합니다. | 정밀도 손실 지점을 코드에서 눈에 보이게 만드는 규율입니다. 대규모 좌표 도입 시 그대로 채택할 가치가 있습니다. |
| 6 | **저작 방향 역전** | destiny (MIT) | `mMaxVel`이 입력이고 추력을 역산합니다: `maxThrust = k * speedFraction * maxVel / (m * agility)`(`Ballpark.cpp:1408`). | 디자이너가 "이 물체의 최고 속도"를 직접 지정하고 엔진이 추력을 맞추는 방식입니다. 튜닝 반복이 훨씬 빨라집니다. |
| 7 | **MiniBall / MiniBox / MiniCapsule 프록시** | destiny (MIT) | 복합 충돌 프록시 구조입니다. | 파편이나 구조물의 저비용 근사 충돌 표현. |
| 8 | **프레임 예산 실행 패턴** | scheduler (MIT) | `RunTaskletsForTime`이 새 스레드 없이 메인 스레드의 지연 작업 큐를 데드라인까지만 소진합니다. | 로딩, 스트리밍, 지연 초기화를 프레임 예산 안에서 처리합니다. Haruka `Threading.md`의 소유권 규칙과 충돌하지 않습니다. |
| 9 | **귀속 할당 추적과 Tracy 계측** | core (MIT) | `CCP_MALLOC(name, ...)` 귀속 할당 추적과 Tracy 계측(`TELEMETRY_ZONE("Tasklet::SwitchImplementation")` 등, 뮤텍스마다 이름과 소유자를 부여). | `HarukaAllocator`의 Zone별 사용량 추적. **Tracy는 Android와 ARM64를 지원하므로 Quest Pro 타깃에 적합합니다.** |

### 5-2. 개념만 참고할 것 (코드를 복사하지 않습니다)

| 항목 | 출처 | 참고할 개념 | 주의 |
|---|---|---|---|
| 프레임 헤더 비트 배치 | io (PSF-2.0) | 4바이트 빅엔디언 헤더 하나에 길이 마스크(0x0FFFFFFF), OOB 존재 비트(28), 압축 비트(29/30)를 함께 넣는 방식입니다. | **PSF-2.0이므로 코드를 복사하면 변경 요약 동봉 의무가 발생합니다.** |
| OOB 분리 디스패치 | blue (MIT) | 라우터가 페이로드에 마셜러를 돌리지 않고 OOB 블롭만으로 디스패치합니다. | Python 우회나 0.01ms 지연 같은 서술은 근거가 없습니다. |
| 공유 플래그 태그 | blue (MIT) | 태그 바이트의 상위 비트가 공유 객체 플래그이고 타입은 하위 6비트뿐입니다. 객체 메모이제이션과 역참조가 한 비트로 붙습니다. | Haruka 직렬화기 설계에 참고할 만합니다. |
| 선언 하나로 삼중 획득 | blue (MIT) | `MAP_ATTRIBUTE("mass", mMass, "Mass of ball (kg)", Be::READWRITE \| Be::NOTIFY \| Be::PERSIST)` 선언 하나로 속성 접근과 변경 알림과 직렬화를 동시에 얻습니다. | `SceneSerializer::ActorData`(`SceneSerializer.hpp:18-40`)가 컴포넌트 5종을 구조체 필드로 하드코딩해 새 컴포넌트가 왕복 불가능한 현 상태의 해법 방향입니다. |
| 트랜지언트 리소스 풀 | trinity (MIT) | `Tr2GpuResourcePool`. | DEF-06의 `LAZILY_ALLOCATED` 문제와 함께 검토할 가치가 있습니다. |
| 클러스터 정체성 유지 | spatial-audio-clustering (Apache-2.0) | 프레임 간 클러스터 정체성을 유지하고 멤버가 바뀔 때 게인 행렬을 보간합니다(`GeneratedObject::volumeMatrix`에 이전 볼륨 행렬 캐시). | **Apache-2.0이므로 개념 참고에 그쳐야 합니다.** 알고리즘 본체(k-means)는 가져올 가치가 없습니다. |
| Maple 유도 워크시트 | destiny (MIT) | `Equations.mws`가 `m * dv/dt = -k*v + m*g`에서 시작해 오일러 근사해와 엄밀해를 나란히 유도합니다. | 적분기 검증용 참고 자료입니다. |

### 5-3. 버릴 것

| 항목 | 이유 |
|---|---|
| **Python 계층 전부** | `scheduler`는 GIL 아래 협조적 마이크로스레드라 병렬 실행이 없고, tasklet이 스레드를 이동할 수 없습니다. `blue`는 임베디드 Python 인터프리터가 전제입니다. `io`는 CPython 소켓 포크입니다. Haruka는 C++20 단일 스택이므로 이 계층 전체가 무의미합니다. |
| **`imageio` 전부** | 2014년경 CPU측 비트맵 라이브러리이고 GPU 코드가 0바이트입니다. **ASTC/ETC2/EAC가 없어 Adreno가 하드웨어로 지원하는 포맷이 하나도 없습니다.** 아틀라스 패커가 없고, 밉맵 생성이 sRGB 변환도 알파 프리멀티플라이도 하지 않아 알베도 체인이 체계적으로 어두워집니다. Quest Pro 타깃에 정확히 반대입니다. |
| **destiny의 충돌 응답** | 관통 깊이에서 유도한 가속도를 접촉 법선 방향으로 주고 0.85로 감쇠시킨 뒤 항력 적분기에 넣습니다. 접선 미끄러짐이 없고 반발 계수가 없습니다. `Ball.h` 설계 주석이 "The ball senses other balls as solids which cannot be traversed, so any movement of the balls will avoid each other or be stuck."이고, `Ballpark.cpp`에 "EEEEK! ball heading for infinite recursion into ball" 에러 로깅이 있습니다. Haruka는 Jolt를 쓰므로 대체할 이유가 없습니다. |
| **destiny의 1Hz 틱** | 1초 틱은 서버 권위 MMO의 선택입니다. 90fps VR에 맞지 않습니다. 적분기 자체는 dt에 무관한 폐쇄형이므로 틱 주기만 버리고 적분기는 살리면 됩니다. |
| **destiny의 Ball 자료구조** | 안티패턴입니다. 약 50개 double/float 멤버에 unordered_set, vector, `bitset<16>`, `bool[27]`, BlueList 자식 3개가 붙어 바디마다 힙 할당이 여러 개 발생합니다. Haruka의 1GB Core Heap 철학과 정면으로 충돌합니다. |
| **셰이더 원천으로서의 trinity** | 셰이더 파일이 저장소에 없습니다. 94개 이펙트가 전부 런타임 리소스 경로이고 번들이 공개되지 않았습니다. 공개된 것은 오케스트레이션뿐이며 셰이더 수학은 0바이트입니다. SDF 쉴드, 레이마칭 성운, Rayleigh 산란은 애초에 존재하지 않습니다. |
| **Dual Quaternion 전환** | Carbon에 존재하지 않아 근거가 없을 뿐 아니라, Haruka `Renderer_render.cpp:234-237`이 모델 행렬 열 길이에서 스케일을 추출하므로 도입 시 스케일이 항상 1.0이 되어 컬링이 깨집니다. |
| **`core`의 플랫폼 추상화 계층** | 플랫폼 분기가 `if(APPLE)`과 `if(WIN32)`뿐입니다. `CcpThread.h`가 `mach_port_t`와 `HANDLE`을 typedef하고 `CcpFileUtils.h`가 `std::wstring` API입니다. **Android와 ARM64 경로가 없습니다.** `CcpAtomic.h`는 전체 내용이 `#include <atomic>`과 `#define CcpAtomic std::atomic`뿐이라 가져올 것이 없습니다. |
| **`CachedAllocator<T>`** | 타입별 고정 크기 프리리스트 풀인데 타입별 싱글턴이라 Zone 기반 힙과 맞지 않습니다. |
| **spatial-audio-clustering의 k-means 본체** | 평면 k-means이고 비가중이며 DSP를 전혀 하지 않습니다. `initializeCentroids`가 O(N제곱)이며 N제곱 회 `std::exp()`를 호출하고 매 오디오 콜백마다 처음부터 다시 계산하며 실시간 오디오 스레드에서 힙 할당을 합니다. |

---

## 6. CCP 상하이 Vulkan 백엔드

### 6-1. 실측 내용

| 항목 | 내용 |
|---|---|
| PR | `carbonengine/trinity` PR #67 "M4 vulkan trinity al" |
| 작성자 | wuyuntao |
| 브랜치 | `ccpshanghai/trinity:m4-vulkan-trinity-al` |
| 커밋 수 | 48 |
| 상태 | **closed(미병합)** |
| 포함 파일 | `trinityal/vulkan/Tr2RenderContextVulkan.cpp` / `.h`, `trinityal/vulkan/UtilitiesVulkan.h`, `TrinityAL_vulkan` CMake 타깃, `Shaders.vulkan` 디렉터리 |
| 셰이더 경로 | HLSL을 `dxc -spirv -fvk-b-shift ... -fvk-t-shift ...`로 SPIR-V 컴파일합니다. |

`ccpshanghai/trinity`의 브랜치 목록은 다음과 같습니다.

| 브랜치 | 성격 |
|---|---|
| `m1-foundation-2` | 기반 작업 |
| `m3-ios-metal-al` | iOS Metal 백엔드 |
| **`m4-vulkan-trinity-al`** | **Vulkan 백엔드 (PR #67의 원본)** |
| **`m6-android-vulkan-al`** | **안드로이드 Vulkan 백엔드** |
| `main` | 메인 |
| `mobile-exp` | 모바일 실험 |
| `s4-vulkan-revival` | Vulkan 재개 작업 |

### 6-2. Haruka에 참고 가치가 있는 이유

`m6-android-vulkan-al`은 **안드로이드에서 Vulkan을 돌리는 브랜치입니다. Quest Pro가 안드로이드
위의 Vulkan이므로 정확히 같은 타깃 조합입니다.** 본선 trinity에는 Vulkan 백엔드가 아예 없고
`shadercompiler`가 HLSL과 MSL만 방출하므로, Carbon 계열에서 Haruka와 타깃이 겹치는 작업물은
이 브랜치들이 유일합니다.

구체적으로 참고할 만한 지점은 다음과 같습니다.

1. DX11/DX12/Metal을 전제로 설계된 렌더러 추상화(`Tr2RenderContext` 계열)를 Vulkan에 얹을 때
   어디를 어떻게 갈랐는지 확인할 수 있습니다.
2. `dxc -spirv`의 `-fvk-b-shift` / `-fvk-t-shift` 바인딩 시프트 설정은 HLSL 레지스터 공간을
   Vulkan 디스크립터 바인딩으로 옮길 때의 실무 처리 방식입니다.
3. `Shaders.vulkan` 디렉터리 구성은 백엔드별 셰이더 산출물을 분리하는 방식의 예입니다.

### 6-3. 단서

**이것은 미병합 작업물입니다.** PR #67은 closed 상태이고 본선에 들어가지 않았습니다.
따라서 다음을 전제해야 합니다.

- 프로덕션에서 검증되었다는 근거가 없습니다.
- 본선과 API가 어긋나 있을 수 있으며, 그 상태로 유지될 것으로 보아야 합니다.
- 저장소 소유자가 브랜치를 삭제하면 접근할 수 없게 됩니다. 참고할 계획이라면
  내용을 지금 확보해 두는 것이 안전합니다.
- 라이선스는 trinity 본선과 같은 MIT입니다.

---

## 7. 재현 방법

다음 사람이 같은 확인을 다시 할 수 있도록, 무엇을 어디서 보면 되는지 적습니다.

### 7-1. 조직과 저장소

| 목적 | 위치 |
|---|---|
| 조직 전체 | `github.com/carbonengine` |
| 저장소 개별 | `github.com/carbonengine/<repo>` (math, destiny, trinity, io, blue, mesh, imageio, imagetools, scheduler, core, audio, spatial-audio-clustering, parser, fsd, db, pdm, pdm-proto-wrapper, documentation, vcpkg-registry, geo2) |
| 이슈 확인 | `github.com/carbonengine/trinity/issues` (전체 12건, 빌드 실패는 #45 하나) |
| Vulkan PR | `github.com/carbonengine/trinity/pull/67` |
| 상하이 브랜치 | `github.com/ccpshanghai/trinity` 의 브랜치 목록 |

### 7-2. 주장별 확인 파일

| 확인할 주장 | 보아야 할 파일 |
|---|---|
| Dual Quaternion 부재 | `math/include/` 전체 목록. dualquat, DualQuaternion 문자열 검색 |
| SIMD 부재 | `math/include/Requirements.h`. immintrin, arm_neon, xmmintrin 검색 |
| math가 표준 Z | `math` 의 `PerspectiveFovMatrix` 구현 |
| destiny 규모 | `destiny/src/Ballpark.cpp`, `Ball.cpp`, `Thunkers.cpp` 파일 크기. `destiny/include/` 는 파일 2개 |
| 시정수 정의 | `destiny/src/Ballpark.cpp:100` (mFriction 초기화 1000000.0), `Integrate()` 구현 |
| 워프 임계 | `destiny/src/Ball.cpp:1912` (`IsAlignedForWarp`, 0.5625), `Ball.h:43` (`MAX_ALIGN_TICKS = 180`) |
| 추력 역산 | `destiny/src/Ballpark.cpp:1408` |
| 명중률 | `destiny/src/Ballpark.cpp:5155` (`GetAccuracy`) |
| 좌표 타입 | `destiny/src/Vector3d.h`, `destiny/src/Quaternion.h` |
| Floating Origin 방식 | `destiny/include/IEveReferencePoint.h`, `IEveBallpark.h` (합계 854바이트) |
| 유일한 실제 파라미터 세트 | `destiny/python/destiny/test/helpers.py:207` |
| 항력 유도 | `destiny/Equations.mws` |
| Reverse-Z | `trinity/Tr2Renderer.cpp:477-483`, `EveSpaceSceneRenderDriver.cpp:446`, `EveSpaceScene.cpp:2571`, `:4129` |
| 성운이 큐브맵 | `trinity/EveSpaceScene.cpp:2988`, `:3206`, `EveSpaceScene.h:449-453` |
| CPU 컬링 | `trinity` 의 `EveInstancedMeshManager::PerformFrustumCulling` (line 480) |
| VMA 용도 | `trinity/Tr2VirtualAllocator.cpp` |
| Perforce 검사 위치 | `trinity/CMakeLists.txt` 의 `CCP_EVE_PERFORCE_BRANCH_PATH` (`if (BUILD_FOR_PYTHON_2)` 안), 그리고 `trinity/CMakePresets.json` 의 12개 프리셋 |
| 셰이더 컴파일러 대상 | `trinity/shadercompiler/Platforms.h`, `HLSLParser.y`, `HLSLScanner.re` |
| 툴셋 요구 | `trinity/README.md` |
| Marshal opcode | `blue/src/Marshal.h` 의 `PYTYPES` 열거형, `Marshal::Init` 의 `MARSHAL_STRINGS[]` |
| BlueNet 헤더 | `blue` 의 `BlueNetHeader` (`StructPacker` 매크로 선언부) |
| io 실체 | `io/README.md`, `io/vcpkg.json` (libuv), `TickUvLoop()` 구현, `BUF_SIZE = 65536` 선언부 |
| io 프레임 헤더 | `io` 의 `m_packetHeader` / `payloadLen()` / `ceHeaderSizeMask` / `ceHeaderBitZlibCompressed` / `ceHeaderBitSnappyCompressed` |
| io 라이선스 | `io/LICENSE.txt` 첫 줄 |
| 정점 레이아웃 | `mesh/src/processor/fbximport/mesh.cpp:341-402`, `:531` |
| QTangent | `mesh/include/cmf/tangents.h` |
| 스키닝 | `mesh` 의 `geometryprepass.comp:475-478` |
| 픽셀 포맷 | `imageio/include/PixelFormat.h` (BC7_UNORM_SRGB=99 에서 끝) |
| tasklet 구현 | `scheduler/src/Tasklet.h` (`PyGreenlet* m_greenlet`), `scheduler/README.md`, 설계 문서 |
| core 플랫폼 분기 | `core/CcpThread.h`, `core/CcpFileUtils.h`, `core/CcpAtomic.h` |
| 클러스터링 알고리즘 | `spatial-audio-clustering/SoundEnginePlugin/SpatialClustering.cpp` (약 410줄), `ObjectClusterFXParams.h` (`NUM_PARAMS = 1`), `ObjectClusterConfig.h`, `LICENSE.txt`, `NOTICE` |
| audio 실체 | `audio/vcpkg.json` (wwise 의존), `SoundPrioritization` 구현 |
| 포트 구성 | `vcpkg-registry/ports/carbon/vcpkg.json` (name carbon, version 1.1.1, 의존성 26개) |
| 툴체인 | `vcpkg-registry/toolchains/x64-windows-carbon.cmake` |
| 조직 문서 부재 | `carbonengine/documentation` 의 페이지 8개, `index.md`, `getting_started.md`, `contributing.md` |

### 7-3. Haruka 쪽 확인 파일

| 확인할 주장 | 보아야 할 파일 |
|---|---|
| DEF-01 (2026-08-24 당시 미수정 / **2026-08-25 해결됨**) | `E:\Haruka\src\renderer\Renderer.hpp`의 `RenderView::materialRows`, `E:\Haruka\src\renderer\Renderer_render.cpp`의 `buildDrawCommands`와 `extractRenderView` |
| DEF-02 심각도 | `E:\Haruka\src\memory\HarukaAllocator.cpp:177-199`, `HarukaAllocator.hpp:9` |
| DEF-03 | `E:\Haruka\src\renderer\passes\GeometryPass.cpp:507`, `Renderer.cpp:586` |
| DEF-04 | `E:\Haruka\src\renderer\RenderContext.cpp:167`, `passes\CullPass.cpp:166`, `:194` |
| DEF-05 이미 처리됨 | `E:\Haruka\src\renderer\Renderer_swap.cpp:59-60` |
| DEF-06 절반 | `E:\Haruka\src\renderer\passes\GeometryPass.cpp:239`, `LAZILY_ALLOCATED` 전역 검색 |
| DEF-07 | `E:\Haruka\src\core\World.hpp:27`, `World.cpp:27`, `onActorDestroyed` 구독자 검색 |
| Vulkan 버전과 피처 체인 | `E:\Haruka\src\renderer\Renderer.cpp:502`, `:577-583`, `:586` |
| 멀티뷰 미완 | `E:\Haruka\src\renderer\Renderer_render.cpp:169`, `Renderer.hpp:167-170` |
| Reverse-Z 영향 범위 | [3-10절](#3-10-haruka-쪽-추가-실측-문서에-반영되지-않은-사실)의 목록 전체 |
| 스케일 추출 | `E:\Haruka\src\renderer\Renderer_render.cpp:234-237` |
| 셰이더 include 미사용 | `E:\Haruka\shaders\include\noise.glsl`, `sdf.glsl`, `CMakeLists.txt:94-108` |
| 물리 API 부재 | `E:\Haruka\src\physics\PhysicsSystem.cpp:81`, `:103`, `:232-238`, `CMakeLists.txt:56-68` |
| 직렬화 하드코딩 | `E:\Haruka\src\core\SceneSerializer.hpp:18-40` |

---

## 8. 이 문서의 유지 관리

- 이 문서는 2026-08-24 기준의 원본 대조 결과입니다. Carbon 저장소는 계속 갱신되므로,
  이후 시점에 다시 확인하면 값이 달라질 수 있습니다. 다시 확인했다면 이 문서에
  확인일과 달라진 항목을 추가하십시오.
- Haruka 문서군의 다른 문서가 Carbon 관련 구체값을 추가할 때는, 먼저 이 문서의
  [2절](#2-저장소별-실체-요약)과 [3절](#3-정정-대조표)에 근거가 있는지 확인하십시오.
  근거가 없으면 "원본 미확인"으로 표기하십시오.
- [1-2절](#1-2-확인하지-못한-영역)의 미확인 영역에 대해 새로운 사실을 확보했다면,
  그 항목을 미확인 목록에서 빼고 [2절](#2-저장소별-실체-요약)에 실체를 기재하십시오.
