# Carbon Engine 네트워크 스택 초심층 기술 분석서 (Deep Network Architecture)

> **[검증 상태 · 2026-08-24]** 이 문서는 github.com/carbonengine 저장소 원본 및
> `src/` 코드와 대조하여 정정되었습니다. 정정 근거와 전체 내역은
> [Carbon_Source_Audit.md](Carbon_Source_Audit.md)를 참조하십시오.
> 계층 구분은 [Layer_Architecture.md](../Layer_Architecture.md)를 따릅니다. 이 문서가 다루는
> 네트워크 계층(`HNet`)은 그 문서 3절이 **범위 밖**으로 판정한 항목이며, 이 문서는 착수
> 계획이 아니라 사전 조사입니다(6절 참조).

> **[라이선스 경고]** 이 문서가 차용을 제안하는 `carbonengine/io`는 MIT가 아니라
> **PSF License Version 2**입니다. LICENSE.txt 첫 줄이 "PYTHON SOFTWARE FOUNDATION
> LICENSE VERSION 2"이며, 저작권 표기는 "Copyright (c) 2001 Python Software Foundation"과
> "Copyright (c) 2026 CCP Games"가 함께 있고, "This software is a derivative work
> incorporating Python as modified by CCP Games"라고 명시되어 있습니다.
> 실무상 의무는 두 가지입니다. 제2조는 PSF 저작권 고지를 유지할 것을 요구하고,
> 제3조는 파생물을 배포할 때 Python에 가한 변경 사항의 요약(a brief summary of the
> changes made to Python)을 동봉할 것을 요구합니다. 따라서 io의 코드를 Haruka에
> 복사해 오는 경우 Haruka의 배포물에 이 고지와 변경 요약을 포함해야 합니다.
> 같은 조직의 `carbonengine/blue`는 MIT이므로 조건이 다릅니다.

> **문서 대상**: `carbonengine/io` (`CarbonIO`), `BlueNet`, `blue.Marshal`, `MachoNet` (`GPCS`), `Destiny` (`Ballpark System`)  
> **분석 목적**: Carbon Engine의 **바이트 레벨 직렬화 포맷, 네이티브 I/O 계층, 라우팅
> 헤더 구조 및 공간 동기화 구조**를 저장소 원본에 근거하여 분석합니다.

**[정정]** 이전 판은 부제에서 "8,000명 동시 교전과 수십만 개 엔티티의 무결점 실시간 분산
동기화"와 "C++ Fast-Path 네이티브 I/O"를 분석 대상으로 내걸었으나, 공개된 저장소에는
클러스터 규모를 뒷받침하는 코드가 없고 I/O 계층도 자체 구현이 아니라 libuv 래퍼입니다.

---

## 1. 네트워크 계층별 전체 아키텍처 맵

```
+-------------------------------------------------------------------------------+
|                       Carbon Engine Game Logic Layer                          |
|  (Destiny Physics Ballpark / World Simulation / Entity-Component System)      |
+-------------------------------------------------------------------------------+
       ^                                           ^
       | (High-frequency state sync)               | (Low-frequency RPC)
       v                                           v
+-----------------------------+             +-----------------------------------+
|   BlueNet (C++ 모듈)        |             |   MachoNet / GPCS  [비공개]       |
| • OOB 블롭 분리             | <---------> | • 저장소에 소스가 없습니다        |
| • destinationType 라우팅    |             | • io README의 언급이 전부입니다   |
| • Python에 노출된 확장      |             |                                   |
+-----------------------------+             +-----------------------------------+
       ^                                           ^
       |                                           |
       +--------------------+----------------------+
                            |
                            v
+-------------------------------------------------------------------------------+
|             blue.Marshal (Compact Tagged-Binary Serializer)                   |
|  • 1바이트 태그 (상위 비트 = 공유 플래그, 하위 6비트 = 타입)                  |
|  • C++ 하드코딩 스트링 테이블 (1바이트 인덱스)  • 네이티브 바이트 순서        |
+-------------------------------------------------------------------------------+
                            |
                            v
+-------------------------------------------------------------------------------+
|                     CarbonIO (= CPython socket/ssl 포크 on libuv)             |
|  • 자체 IOCP/epoll 코어가 아닙니다. libuv가 플랫폼 차이를 흡수합니다.         |
|  • uv_run(UV_RUN_NOWAIT)를 코루틴 스케줄러가 협조적으로 펌핑합니다.           |
|  • 핸들별 64KB 수신 버퍼 + 읽기/쓰기 커서. 풀도 락프리 구조도 없습니다.       |
|  • 4바이트 빅엔디언 프레임 헤더 (하위 28비트 = 길이, 상위 비트 = 플래그)      |
+-------------------------------------------------------------------------------+
                            | (TCP / UDP)
                            v
                        [ Network ]
```

**[정정]** 이전 판의 이 그림은 CarbonIO를 IOCP와 epoll을 직접 다루는 자체 비동기 코어로,
버퍼 관리를 슬랩/락프리 풀로 그렸으나, 실제로는 libuv 위에 올린 CPython 소켓 모듈
포크이고 버퍼는 핸들마다 하나씩 있는 단순 `new char[65536]`입니다.
또한 전송은 TCP와 UDP뿐이며 io README가 "TCP and UDP connections only"라고 못박고
있으므로 "Reliable UDP"라는 계층은 존재하지 않습니다.

---

## 2. `blue.Marshal`: 바이트 레벨 바이너리 직렬화 포맷 정밀 명세

`blue.Marshal`은 범용 직렬화(JSON, Protocol Buffers, FlatBuffers)와 달리 Python 객체
그래프를 그대로 담는 것을 목표로 삼은 **1바이트 태그 기반 바이너리 직렬화 포맷**입니다.
정렬을 맞추는 포맷이 아니라 오히려 그 반대입니다. `#pragma pack` 없이 태그와 페이로드를
빈틈없이 이어 붙이는 스트림 포맷이며, 바이트 스왑도 하지 않고 네이티브 순서로 씁니다.

### 2-1. 실제 타입 태그 열거형 (`blue/src/Marshal.h`, `PYTYPES`)

**[정정]** 이전 판의 Opcode 표는 14행 중 12행이 사실과 달랐습니다. `0x01`을 스트링 테이블
참조로, `0x02`~`0x05`를 정수 폭별 태그로, `0x06`을 float32로, `0x2A`를 압축 래퍼로 적었으나
그런 배정은 존재하지 않습니다. 아래가 저장소 원문의 열거형 전체입니다.

```cpp
// blue/src/Marshal.h
TY_INVALID=0,  TY_NONE=1,      TY_GLOBAL=2,    TY_INT64=3,     TY_INT32=4,
TY_INT16=5,    TY_INT8=6,      TY_INT_N1=7,    TY_INT_0=8,     TY_INT_1=9,
TY_FLOAT=10,   TY_FLOAT_0=11,  TY_COMPLEX=12,  TY_STR=13,      TY_STR_EMPTY=14,
TY_STR_CHAR=15, TY_STR_SHORT=16, TY_STR_TABLE=17, TY_UNICODE=18, TY_BUFFER=19,
TY_TUPLE=20,   TY_LIST=21,     TY_DICT=22,     TY_INSTANCE=23,
TY_CALLBACK=25, TY_PICKLE=26,  TY_REFERENCE=27, TY_CRC_CHECK=28,
TY_TRUE=31,    TY_FALSE=32,    TY_PICKLER=33,  TY_REDUCE=34,   TY_NEWOBJ=35,
TY_TUPLE0=36,  TY_TUPLE1=37,   TY_LIST0=38,    TY_LIST1=39,
TY_UNICODE_0=40, TY_UNICODE_1=41, TY_DBROW=42, TY_WSTREAM=43,  TY_TUPLE2=44,
TY_MARK=45,    TY_UTF8_OBSOLETE=46, TY_LONG=47,

TY_SIGNATURE=126,   // 0x7E
TY_SIGNATURE2=125,  // 0x7D

TY_SHAREDFLAG=0x40, // 공유 객체 플래그
TY_TYPEMASK=0x3F    // 타입 비트
```

#### 이 포맷에서 구조적으로 가장 중요한 사실

태그 바이트는 통짜 opcode가 아닙니다. **상위 비트(`TY_SHAREDFLAG=0x40`)가 공유 객체
플래그이고 타입은 하위 6비트(`TY_TYPEMASK=0x3F`)뿐입니다.** 디코더는 태그 하나를 읽어
`isShared = (type & TY_SHAREDFLAG) == TY_SHAREDFLAG;`로 공유 여부를 판정한 뒤 나머지
6비트로 타입을 분기합니다. 따라서 타입 공간의 상한은 256이 아니라 64이며, 위 열거형이
0부터 47까지를 이미 채우고 있다는 점을 감안하면 확장 여지가 거의 없습니다.

**[정정]** **float32 태그가 존재하지 않습니다.** `TY_FLOAT=10`은 C `double`이고
`TY_FLOAT_0=11`은 리터럴 `0.0`을 나타내는 값 없는 태그입니다. 즉 이 마셜러는 스칼라
실수에 언제나 8바이트를 씁니다. 이전 판이 `0x06`을 "32비트 단정밀도"로 적은 것은
근거가 없으며, 이는 VR 트래킹처럼 float32로 충분한 데이터에 이 포맷을 그대로 쓰면
실수 필드마다 4바이트를 낭비한다는 뜻이므로 Haruka에 직접 영향을 줍니다.

#### 크기 특화 태그가 이 포맷의 실제 압축 전략입니다

정수는 폭별 태그(`TY_INT64` / `TY_INT32` / `TY_INT16` / `TY_INT8`)로 줄이고, 그 위에
**값 자체를 태그에 접어 넣은 태그**를 따로 둡니다. `TY_INT_N1`(-1), `TY_INT_0`(0),
`TY_INT_1`(1)은 페이로드가 0바이트입니다. 같은 방식이 문자열과 컨테이너에도 적용됩니다.
`TY_STR_EMPTY`, `TY_STR_CHAR`(1글자), `TY_UNICODE_0`, `TY_UNICODE_1`, `TY_TUPLE0`,
`TY_TUPLE1`, `TY_TUPLE2`, `TY_LIST0`, `TY_LIST1`이 모두 개수 필드를 태그로 대체합니다.
게임 패킷에 0, 1, 빈 문자열, 원소 한두 개짜리 튜플이 압도적으로 많다는 관찰을 포맷
차원에서 이용한 것이며, 이 아이디어 자체는 Haruka가 그대로 가져갈 만합니다.

#### 서명 헤더

**[정정]** 이전 판은 `0x7E`를 "Magic Header, 2바이트, 버전 및 플래그 `uint8_t flags`"로
적었으나 뒤따르는 것은 플래그 바이트가 아닙니다. `TY_SIGNATURE=126`(`0x7E`) 뒤에는
**int32 공유 객체 개수**가 오므로 헤더는 5바이트입니다. 버전이 붙는 변형은 같은 태그가
아니라 별개의 태그 `TY_SIGNATURE2=125`(`0x7D`)이며 헤더가 2바이트입니다.

#### 바이트 순서

마셜러에는 바이트 스왑이 없습니다. `htons`, `ntohl`, byteswap 계열 호출이 한 곳도 없고
네이티브 순서로 씁니다. **[정정]** 이전 판은 정수 태그에 "Little-Endian", 스트링 테이블
인덱스에 "Big-Endian"을 병기했으나 마셜러는 엔디언을 명시하지 않습니다. 빅엔디언은 한
계층 아래인 carbon-io의 4바이트 프레임 헤더에만 존재합니다(3-1절 참조).

#### 압축

**[정정]** 압축은 마셜러의 태그가 아닙니다. 이전 판이 적은 `0x2A` 압축 래퍼는 존재하지
않으며, 압축 여부는 io 프레임 헤더의 비트로 표시됩니다.
`ceHeaderBitZlibCompressed = 1 << 29`, `ceHeaderBitSnappyCompressed = 1 << 30`이며
후자에는 "was compressed with snappy, no longer supported"라는 주석이 붙어 있습니다.
**LZ4는 저장소 어디에도 없습니다.**

---

### 2-2. String Table Interning & Object Memoization 메커니즘

수천 대의 함선이 패킷을 주고받을 때 `"position"`, `"velocity"`, `"shield"`, `"targetID"` 같은 딕셔너리 키나 서비스 이름이 반복적으로 발생합니다.

**1. String Interning Table (실재하지만 기구가 다릅니다)**

인터닝 자체는 실재합니다. 해당 태그는 `TY_STR_TABLE=17`입니다. 그러나 이전 판이 서술한
동적 협상 방식은 존재하지 않습니다.

**[정정]** 이전 판은 테이블이 "세션 연결 초기 또는 패킷 헤더에" 빌드된다고 서술했으나,
테이블은 **C++에 컴파일 타임 하드코딩**되어 있습니다. 원문은
`const char* MARSHAL_STRINGS[] = { "*corpid", "*locationid", "age", "Asteroid", ... };`
형태의 정적 배열이고 `Marshal::Init`에서 인터닝됩니다. **세션 시작 시 협상하지 않고
패킷 헤더로 전달하지도 않습니다.** 따라서 송신자와 수신자는 같은 빌드여야 하며,
테이블을 바꾸면 와이어 포맷이 깨집니다.

**[정정]** 인덱스는 2바이트가 아니라 **1바이트**입니다. 기록 코드가
`stream->Write((char)PyLong_AS_LONG(index));`이므로 **테이블의 상한이 255개**입니다.
이것은 이 설계의 실질적 한계이며, 어휘가 그보다 많은 게임은 나머지를 원시 문자열로
보내야 합니다.

**[정정]** "패킷 크기 60~75% 축소"라는 수치는 저장소 어디에도 근거가 없으므로
삭제합니다. 절감 효과는 그 게임의 키 어휘 분포에 전적으로 의존하며, 공개된 자료에는
측정치가 없습니다.

Haruka에 대한 함의는 분명합니다. 협상 없는 하드코딩 테이블은 단일 빌드 배포에서는
가장 단순하고 빠른 선택이지만, 클라이언트와 서버의 버전이 갈릴 수 있는 환경에서는
테이블 해시를 핸드셰이크에 포함시키는 정도의 보강이 필요합니다.

**2. Object Memoization (Back-reference) — 개념이 정확합니다**

이 항목은 이전 판의 서술이 옳으며, 실제 기구의 이름을 넣어 보강합니다.
공유 여부는 별도 필드가 아니라 **태그 바이트의 상위 비트 `TY_SHAREDFLAG`(0x40)**로
표시됩니다. 인코더는 `isShared = (type & TY_SHAREDFLAG) == TY_SHAREDFLAG;`로 판정하고
해당 객체를 `std::vector<BluePy> mShared`에 추가합니다. 같은 객체가 다시 나타나면
전체를 다시 쓰지 않고 **`TY_REFERENCE=27` 태그와 `WriteInteger`로 기록한 인덱스**만
보냅니다. 이 구조가 중복 역직렬화를 막고 순환 참조를 해결합니다.
공유 객체의 총 개수는 스트림 맨 앞 `TY_SIGNATURE` 헤더의 int32 필드로 미리 알려주므로,
디코더가 `mShared` 벡터를 한 번에 예약할 수 있습니다.

---

## 3. `CarbonIO` & `BlueNet`: 실제 I/O 계층 구조

**[정정]** 이 장 전체의 전제가 틀렸습니다. CarbonIO는 IOCP와 epoll을 직접 다루는
C++ Fast-Path 네이티브 I/O 코어가 아닙니다.

```
[OS Socket Stream (TCP / UDP)]
               |
               v
+--------------------------------------------------------------------+
| libuv (플랫폼 차이를 흡수하는 외부 의존성, vcpkg libuv>=1.52.1)    |
+--------------------------------------------------------------------+
               |
               v
+--------------------------------------------------------------------+
| _carbonsocket / _carbonssl (CPython 3.12 C 모듈 포크)              |
|  • 워커 스레드 없음. TickUvLoop()이                                |
|    uv_run(GetUvLoop(), UV_RUN_NOWAIT)를 협조적으로 펌핑            |
|  • 핸들별 수신 버퍼 1개: new char[65536] + bufReadPos/bufWritePos  |
|  • 4바이트 빅엔디언 프레임 헤더 파싱                               |
+--------------------------------------------------------------------+
               |
      [헤더 비트 28 = OOB 존재 여부]
        /                 \
   (OOB 블롭)          (마셜된 페이로드)
      /                     \
     v                       v
+------------------------+  +----------------------------------------+
| BlueNet 라우팅         |  | MachoNet 계층  [소스 비공개]           |
| • destinationType 비교 |  | • io README에 언급만 있습니다          |
| • 해시 조회            |  |                                        |
| • Python C++ 확장이며  |  |                                        |
|   Python을 우회하지    |  |                                        |
|   않습니다             |  |                                        |
+------------------------+  +----------------------------------------+
```

---

### 3-1. `CarbonIO`의 실제 구현

`carbonengine/io`는 **CPython 3.12의 C `socket` 모듈과 `ssl` 모듈을 포크해 libuv 위에
올린 것**입니다. 모듈명은 `_carbonsocket`과 `_carbonssl`이며, 접두사를 떼고
`sys.modules`에 주입되므로 Python 코드에서는 표준 `socket`과 `ssl`처럼 보입니다.

목적은 성능 그 자체가 아니라 **블로킹 대상의 교체**입니다. 소켓 호출이 OS 스레드를
블록하는 대신 carbon-scheduler의 greenlet/tasklet만 블록하게 만드는 것이 전부입니다.
README의 표현은 "Tasklet-blocking sockets for use with carbon-scheduler... TCP and UDP
connections only."입니다.

vcpkg 의존성은 gtest, carbon-scheduler>=1.4.3, **libuv>=1.52.1**, python3>=3.12.9,
carbon-core>=3.1.0, openssl 1.1.1k입니다.

* **[정정] 워커 스레드 풀이 없습니다.**
  이전 판은 "CarbonIO Worker Threads"가 IOCP 완료 포트를 코어 수만큼 바인딩한다고
  서술했으나, 스레드 풀이 존재하지 않습니다. 실제로는 `TickUvLoop()`이
  `uv_run( GetUvLoop(), UV_RUN_NOWAIT )`를 호출하며, 이것을 코루틴 스케줄러가
  매 루프마다 협조적으로 펌핑합니다. `UV_RUN_NOWAIT`이므로 블록하지 않고 준비된
  이벤트만 처리한 뒤 즉시 스케줄러로 제어를 돌려줍니다.
* **[정정] IOCP와 epoll API 호출이 하나도 없습니다.**
  `CreateIoCompletionPort`, `GetQueuedCompletionStatusEx`, `epoll_create1`,
  `EPOLL_CLOEXEC`, `EPOLLET`, `recvmmsg`가 전부 호출되지 않습니다. 이전 판이 적은
  Edge-Triggered 모드와 `recvmmsg` 일괄 수신은 저장소에 없습니다. 플랫폼별 완료
  통지 기구는 libuv 내부의 것이며 CCP가 작성한 코드가 아닙니다.
* **[정정] `cio_buffer_pool`도, `RingBuffer::AllocatePacket`도, 락프리 구조도,
  슬랩 할당자도 존재하지 않습니다.**
  실제 버퍼 관리는 **핸들마다 수신 버퍼 하나**를 두는 방식입니다.

  ```cpp
  constexpr size_t BUF_SIZE = 65536;
  if( !handleBuf.base ) { handleBuf.base = new char[BUF_SIZE]; }
  ```

  여기에 `ssize_t bufReadPos`와 `ssize_t bufWritePos` 두 커서가 붙고, 한 프레임이
  버퍼에 다 들어가지 않으면 동적으로 재할당합니다. 64KB라는 숫자는 맞았지만
  그것이 풀의 청크 크기가 아니라 핸들당 버퍼 크기라는 점에서 의미가 다릅니다.
  "메모리 파편화 및 OS 할당 오버헤드 0"이라는 결론도 성립하지 않습니다.

#### 실제 프레임 헤더 (4바이트 빅엔디언)

이 계층에서 확인되는 유일한 와이어 포맷입니다.

```cpp
m_packetHeader = ntohl( *reinterpret_cast<uint32_t*>( src ) );
// payloadLen() == m_packetHeader & ceHeaderSizeMask;   // 0x0FFFFFFF
```

* 하위 28비트가 페이로드 길이이며 상한은 약 256MB입니다.
* **비트 28**이 서면 곧바로 두 번째 빅엔디언 `uint32`가 따라오고 그것이 OOB 블롭의
  길이입니다.
* **비트 29** `ceHeaderBitZlibCompressed`, **비트 30** `ceHeaderBitSnappyCompressed`가
  압축 여부를 나타냅니다.

즉 헤더 하나에 길이와 플래그를 함께 담았고, 매직 넘버에 바이트를 쓰지 않습니다.

---

### 3-2. `BlueNet` Out-of-band (OOB) 라우팅 헤더 스펙

**[정정]** 이전 판이 제시한 `BlueNetOOBHeader` 구조체는 **존재하지 않습니다.**
매직 넘버 `0x424E4554`("BNET")도, `channelId`도, `flags`도, `sequence`도,
16바이트 고정 크기도, `static_assert`도 저장소에 없습니다. 아래가 실제 구조입니다.

#### 실제 `BlueNetHeader` (StructPacker 비트팩 가변길이)

`BlueNetHeader`는 `#pragma pack`으로 고정 배치한 POD가 아니라, **StructPacker 매크로로
선언된 비트팩 가변길이 구조**입니다. 필드는 다음과 같습니다.

| 필드 | 타입 | 역할 |
|---|---|---|
| `sourceID` | `int64` | 송신 주체 식별자 |
| `destinationType` | `int64` | 목적지 종류 (`ADDRESS_TYPE_*`) |
| `destinationID` | `int64` | 목적지 식별자 |
| `blueNetKey` | `int32` | 서비스명의 FNV-1a 해시 |
| `forkedAddresses` | `uint32` | 분기된 주소 수 |
| `serialNumber` | `uint32` | 일련번호 |
| `latencyAccumulator` | `uint32` | 누적 지연 |
| `priority` | `bool` | 우선순위 플래그 |
| `timestamp` | `uint64` | 타임스탬프 |

**매직 넘버가 없습니다.** 아래 계층인 carbon-io의 4바이트 헤더가 이미 프레임 경계와
OOB 유무를 결정하므로, 이 계층에서 다시 4바이트를 매직에 쓸 이유가 없습니다.
이전 판이 제안한 `magic` 필드는 패킷마다 4바이트를 순수 손실로 만드는 설계이며,
**실제 설계 쪽이 더 낫습니다.** 프로토콜을 새로 만들 때 매직 넘버는 파일 포맷에는
쓸모가 있어도, 이미 프레이밍이 된 스트림 안에서는 대개 낭비입니다.

또한 고정 크기가 아니라 비트팩 가변길이이므로 `sizeof`로 크기를 논할 수 없습니다.
작은 ID를 담은 헤더는 실제로 더 짧게 나갑니다.

#### 실제 와이어 레이아웃

```
[ 4B 프레임 헤더 (빅엔디언) ][ 4B OOB 길이 (빅엔디언) ][ OOB 블롭 ][ 페이로드 ]
        ^ 하위 28비트 = 길이            ^ 비트 28이 설 때만 존재
          비트 28 = OOB 있음
          비트 29 = zlib
          비트 30 = snappy(폐기)
```

#### 라우팅 방식

**[정정]** `channelId` 개념이 존재하지 않습니다. `0=DestinySync, 1=CombatEvents,
2=RPC, 3=Voice`라는 채널 배정은 저장소에 없으며, 라우팅은 채널 번호에 대한 스위치가
아니라 **`destinationType` 비교와 해시 조회**로 이루어집니다.

```
ADDRESS_TYPE_NODE      = 1
ADDRESS_TYPE_CLIENT    = 2
ADDRESS_TYPE_BROADCAST = 4
ADDRESS_TYPE_ANY       = 8
```

값이 1, 2, 4, 8인 것에서 알 수 있듯 비트 플래그로 설계되어 있습니다.
`blueNetKey`는 작은 enum이 아니라 **서비스명의 FNV-1a 해시**이므로, 서비스 종류를
미리 열거하지 않고도 이름만으로 주소를 만들 수 있습니다. 이 점은 Haruka가 참고할
만합니다. 라우팅 테이블을 컴파일 타임 enum으로 고정하면 모듈을 추가할 때마다
enum을 고쳐야 하지만, 이름 해시를 쓰면 등록만으로 끝납니다.

#### Fast-Path 디스패치에 대하여

**[정정]** **BlueNet은 Python을 우회하지 않습니다.** BlueNet은 Python에 노출된 C++
모듈이며, 스스로 `Marshal::New()`와 `SaveObject(...)`를 호출하고 결과를
`PyChannelObject` 큐로 전달합니다. 따라서 "Python 런타임을 완전히 건너뛰고
`Destiny::Ballpark`의 C++ 수신 큐로 포인터만 전달한다"는 서술은 성립하지 않습니다.
**[정정]** "0.01ms 이하 지연"이라는 수치도 소스에 근거가 없으므로 삭제합니다.

다만 이전 판이 붙잡으려 한 **아이디어 자체는 실재합니다.** 방어할 수 있는 형태로
다시 쓰면 다음과 같습니다. 라우팅에 필요한 정보를 마셜된 페이로드 바깥의 OOB 블롭에
따로 두면, **라우터가 페이로드에 마셜러를 돌리지 않고 OOB 블롭만 읽어 디스패치할 수
있습니다.** 중계 노드가 본문을 역직렬화하지 않아도 된다는 것이 이 구조의 실질적
이득이며, 이것을 **OOB 분리**라고 부르는 편이 정확합니다. Haruka에도 그대로 유효한
설계 원칙입니다.

---

## 4. `MachoNet` (`GPCS`): 공개되지 않은 계층

> **[정정 · 이 장 전체]** **MachoNet과 GPCS는 오픈소스로 공개되지 않았습니다.**
> `carbonengine` 조직의 어떤 저장소에도 MachoNet의 소스, GPCS 프레임 포맷, 패킷 타입
> 열거형, 주소 구조체가 없습니다. 존재하는 근거는 `carbonengine/io`의 README에 있는
> 한 문장뿐입니다: "Some functionality to support sending / receiving of machoNet
> packets has been added." 즉 io가 machoNet 패킷을 **송수신할 수 있다**는 사실만
> 확인되며, 그 패킷의 내부 구조는 확인되지 않습니다.
>
> 아래 두 절은 절 제목을 남겨 두되, 이전 판의 근거 없는 구체값을 삭제하고 무엇이
> 확인되지 않았는지를 명시합니다. 같은 오해가 반복되지 않도록 하기 위함입니다.

### 4-1. MachoNet 주소 체계 (원본 미확인)

**[정정]** 이전 판은 통신 대상이 `NodeID`, `ClientID`, `ServiceID`, `SessionID`의
**4-튜플 주소**로 식별된다고 서술하고 각 필드의 타입까지 적었으나, 이 구조체는
저장소에 존재하지 않으므로 삭제합니다. `uint32`, `uint64` 같은 폭 지정과
`"station"`, `"dogmaIM"`, `"space"` 같은 예시 서비스명 역시 근거가 없습니다.

원본에서 확인되는 주소 체계는 MachoNet의 것이 아니라 **BlueNet의 것**이며, 3-2절에
적은 대로 `sourceID` / `destinationType` / `destinationID` / `blueNetKey` 구성입니다.
MachoNet의 주소 체계가 이와 같은지 다른지는 **미확인**입니다.

---

### 4-2. GPCS (Generic Protocol Client-Server) 패킷 프레임 포맷 (원본 미확인)

**[정정]** 이전 판은 `Length(4B) | PacketType(1B) | Source Addr(16B) | Dest Addr(16B)`
프레임 다이어그램과 `CALL_REQ (0x01)`, `CALL_RSP (0x02)`, `CALL_ERR (0x03)`,
`NOTIFICATION (0x04)`, `PING / PONG (0x05 / 0x06)`의 패킷 타입 표를 제시했으나,
**이 프레임 포맷도 이 열거형도 저장소에 없습니다.** 근거가 없으므로 표를 삭제합니다.

저장소에서 확인되는 프레이밍은 3-1절의 **4바이트 빅엔디언 헤더**가 전부이며,
그 안에 패킷 타입 필드는 없고 길이 28비트와 플래그 비트(OOB 유무, zlib, snappy)만
있습니다.

RPC 계층을 Haruka에 설계할 때 이 장을 참조 규격으로 삼아서는 안 됩니다.
요청과 응답과 오류와 알림을 구분하는 태그가 필요하다는 **일반적 요구**는 물론
타당하지만, 그것을 Carbon이 어떻게 했는지는 공개 자료로 알 수 없습니다.

---

## 5. `Destiny` 물리 공간 동기화: `Ballpark` 프로토콜

EVE Online과 Carbon Engine에서 하나의 성계(Solar System) 또는 격리된 3D 우주 공간 인스턴스를 **`Ballpark`**라고 부르며, 내부의 모든 움직이는 오브젝트를 **`Ball`**이라고 합니다.

```
[Server Ballpark Simulation — 1Hz 고정 틱]
  - Ball #1001 (Player Ship): Mode=Goto,   Target=(X,Y,Z)
  - Ball #1002 (Missile):     Mode=Follow, TargetBall=1001
  - Ball #1003 (Debris):      Mode=Stop  (추력 0, 항상 켜진 항력이 감속)
           |
           v  상태가 아니라 ACTION을 틱 스탬프와 함께 전송
           |
           v (Network Stream)
[Client Destiny Engine — 같은 코드로 결정론적 재시뮬레이션]
  - 1차 선형 항력 ODE를 폐쇄형(해석해)으로 풉니다. 오일러도 RK4도 아닙니다.
  - mTimeFactor = exp(-mFriction * dt / (mMass * mAgility))
  - 임의의 dt에 대해 오차 누적 없이 정확한 위치를 바로 얻습니다.
```

**[정정]** 이전 판은 틱 주기를 "1Hz ~ 60Hz"로, 파편의 모드를 `Drift`로, 클라이언트
보간을 "Hermite Spline / Dead Reckoning"으로 서술했으나 셋 다 사실과 다릅니다.
틱은 **1초 고정**이고, `DSTBALLMODE`는 13개 항목이며 그중 `Drift`에 해당하는 것이
없습니다. `DSTBALL_STOP`은 댐프너가 아니라 추력을 0으로 두고 언제나 켜져 있는 항력이
감속시키는 상태입니다. `mFriction`은 park 전역 스칼라라서 바디별로 끌 수 없습니다.

#### 왜 1Hz로 충분한가

이 구조의 핵심은 여기에 있습니다. **적분기가 폐쇄형이기 때문에 1Hz 틱이 가능합니다.**
1차 선형 항력 방정식 `m * dv/dt = -k*v + m*a`는 해석해가 존재하므로, 엔진은 매 프레임
적분을 반복하지 않고 시정수 하나로 임의 시각의 상태를 직접 계산합니다.

* 시정수: **`tau = mMass * mAgility / mFriction`**
* 엔진은 tau를 저장하지 않고 `mTimeFactor = exp(-mFriction*dt/(mMass*mAgility))`를 저장합니다.
* `mFriction`은 하드코딩 상수가 아니라 Ballpark의 변경 가능한 멤버입니다.
  `Ballpark.cpp:100`에서 1000000.0으로 초기화되고 Python에 "friction, 우주의 마찰
  계수"로 노출됩니다. 단위는 kg/s입니다.
* 저장소의 골든 테스트가 이를 검증합니다. mass 13e6, agility 0.9이면
  tau = 11.6999996900558s입니다.

수치 적분이라면 틱 간격이 곧 오차이므로 1초 틱에서 궤적이 어긋납니다. 폐쇄형은 그렇지
않으므로 서버와 클라이언트가 서로 다른 프레임률로 돌면서도 같은 값을 얻습니다.
클라이언트가 60fps나 90fps로 부드럽게 보이는 것은 추측 항법으로 오차를 감수한 결과가
아니라, **같은 해석해를 더 촘촘한 시각에 평가한 결과**입니다.

---

### 5-1. `BallState` 스냅샷 구조 (원본 미확인) 및 실제 리플리케이션 방식

**[정정]** 이전 판이 제시한 `DestinyBallSnapshot` 구조체는 **이 저장소들에서 확인되지
않았습니다.** `#pragma pack(push, 1)` 배치도, Smallest-Three 압축 쿼터니언
(`uint16_t quat[3]` + `quatMaxIndex`)도, `mode` 값 배정 `0=Stop, 1=Goto, 2=Follow,
3=Orbit, 4=Warp, 5=Drift`도 근거가 없습니다. 특히 `5=Drift`는 앞서 적은 대로
`DSTBALLMODE`에 존재하지 않는 모드입니다.

더 중요한 것은 **이 스냅샷 발상 자체가 destiny의 구조와 어긋난다**는 점입니다.

#### 실제 구조는 명령 리플리케이션입니다

destiny는 매 틱 물리 상태를 스냅샷으로 떠서 보내지 않습니다. **상태가 아니라 ACTION을
틱 스탬프와 함께 전송하고, 서버와 클라이언트가 같은 코드로 결정론적으로 재시뮬레이션**
합니다. 이 방식이 성립하는 이유는 앞 절의 폐쇄형 적분기입니다. 적분 오차가 누적되지
않으므로 양쪽이 같은 명령 열에서 같은 궤적을 얻습니다.

이 차이는 대역폭 특성을 근본적으로 바꿉니다. 스냅샷 방식은 움직이는 엔티티 수에
비례해 매 틱 트래픽이 발생하지만, 명령 리플리케이션은 **플레이어가 실제로 명령을
내릴 때만** 트래픽이 발생합니다. 수백 대가 같은 방향으로 항행하는 동안 네트워크는
조용합니다. 1Hz 틱과 이 방식이 결합해 EVE 규모의 교전을 가능하게 한 것입니다.

대신 대가가 있습니다. 결정론이 깨지면 양쪽이 조용히 갈라지므로, 부동소수점 연산
순서까지 동일해야 합니다. destiny가 Python 테스트 약 6,100줄로 **하드코딩된 골든
float 궤적**에 대해 회귀 검증을 하는 이유가 여기에 있습니다. Haruka가 이 방식을
채택한다면 같은 수준의 골든 테스트가 선택이 아니라 필수입니다.

#### 확인되는 실제 자료형

* 좌표는 `double`입니다. 자체 타입 `Vector3d`(`double x, y, z`, `src/Vector3d.h`)이며
  glm이 아닙니다. float에서 double로는 암시적으로 확장되지만, double에서 float로는
  명시적인 `AsVector3()` 호출이 필요합니다.
* 회전에는 압축 쿼터니언이 아니라 `QuaternionD`(double 쿼터니언, `src/Quaternion.h`)가
  있습니다. Dual Quaternion은 없습니다.
* 저장소 안의 유일한 실제 파라미터 세트는 `python/destiny/test/helpers.py:207`입니다.
  mass 13,000,000 kg, Agility 0.9, maxVelocity 10.0 m/s, radius 2.0 m,
  speedFraction 0.95이며 tau는 11.7초입니다.
* 저작 방향이 일반적인 물리 엔진과 반대입니다. `mMaxVel`이 입력이고 추력을 역산합니다:
  `maxThrust = k * speedFraction * maxVel / (m * agility)` (`Ballpark.cpp:1408`).

#### 워프 정렬 판정

워프 75% 임계는 실재합니다. `Ball::IsAlignedForWarp`(`Ball.cpp:1912`)의 0.5625가
0.75의 제곱입니다. 다만 엔진은 폐쇄형으로 정렬 시각을 구하지 않고, 매 틱
**2조건 술어**를 폴링합니다. 방향 내적이 0.01 이내이고 속력의 제곱이
`0.5625 * maxVel의 제곱`을 넘는지 확인하는 방식이며, `MAX_ALIGN_TICKS = 180`
(`Ball.h:43`)이라는 강제 탈출구가 붙어 있습니다.

---

### 5-2. `Ballpark` 델타 동기화 메시지 (메시지 이름 미확인)

**[정정]** 이전 판이 나열한 `OnAddBalls`, `OnUpdateBalls`, `OnRemoveBalls`라는 세
메시지 이름과 각각의 페이로드 명세는 공개 저장소에서 확인되지 않았습니다.
이 계층은 MachoNet 쪽에 속하며 4장에 적은 대로 공개되지 않았습니다.
`std::vector<uint64_t>` 같은 구체적 페이로드 타입도 근거가 없으므로 삭제합니다.

확인되는 사실만 남기면 다음과 같습니다.

1. **AOI 범위는 Bubble이 정합니다.**
   **[정정]** Bubble은 물리 구조물이 아닙니다. 아무것도 밀어내지 않으며,
   **AOI(관심 영역) 및 네트워크 리플리케이션 범위**입니다. 어떤 클라이언트가 어떤
   Ball의 갱신을 받는지를 결정하는 것이 전부입니다.
2. **공간 질의는 희소 8단계 균일 그리드가 담당합니다.**
   최소 셀 480m, `int64 (i, j, k, level)` 해시 키입니다. 계층적 바운딩 스피어 트리가
   아닙니다.
3. **등속 비행 중 무전송은 결과적으로 옳습니다.**
   다만 이유가 다릅니다. 추측 항법으로 오차를 감수하기 때문이 아니라, 명령이 바뀌지
   않는 한 양쪽이 같은 해석해를 평가하므로 보낼 것이 없기 때문입니다.

#### Floating Origin에 대한 보충

**[정정]** destiny의 Floating Origin은 흔히 알려진 방식과 다릅니다. destiny는
시뮬레이션을 재기준화하지 않습니다. 위치는 한 Ballpark(하나의 성계) 안에서 절대
double로 유지됩니다. "모든 오브젝트에서 카메라 위치를 뺀다"는 단계가 없습니다.

엔진이 노출하는 것은 두 가지뿐입니다.
`IEveReferencePoint::GetReferencePoint`(플레이어 ego ball의 절대 위치)와
`IEveBallpark::Delta`(ego ball의 틱당 이동량, `AsVector3()`로 float 축소)입니다.
렌더러가 자기 float 원점을 유지하면서 델타만큼 밀어 주는 **증분식 floating origin**
입니다. Haruka가 대규모 좌표계를 도입한다면 이 쪽이 참고할 형태입니다.

---

## 6. Haruka VR 엔진용 네트워크 시스템 아키텍처 결론

> **[계층 · 범위 밖]** 아래 표의 "Haruka 적용 판단" 열은 **멀티플레이어를 도입하기로 결정했을 때를 위한 사전 조사**이지 착수 계획이 아닙니다. 네트워크 계층(`HNet`)은 [Layer_Architecture.md](../Layer_Architecture.md) 3절이 **범위 밖**으로 판정했습니다. 현재 [Roadmap.md](../Roadmap.md)에 해당 Phase가 없고, 아래 6-1이 정리하듯 [Threading.md](../Threading.md)의 단일 스레드 전제와 충돌하기 때문입니다. Haruka는 Vulkan 기반 VR 엔진이고 코어(Layer 1) 완성이 우선이므로, 이 문서의 결론을 근거로 네트워크 작업을 앞당기지 마십시오.

| Carbon 항목 | 검증된 실제 구현 | Haruka 적용 판단 (멀티플레이어 결정 시) |
|---|---|---|
| **CarbonIO** | libuv 위에 올린 CPython socket/ssl 포크. 워커 스레드 없음. `uv_run(UV_RUN_NOWAIT)`를 코루틴 루프가 펌핑 | 자체 IOCP 코어를 쓸 이유가 없습니다. libuv를 직접 쓰거나 그에 준하는 단일 스레드 이벤트 루프를 채택합니다. **라이선스가 PSF-2.0이므로 코드 복사 시 변경 요약 동봉 의무가 발생합니다.** |
| **4바이트 프레임 헤더** | 빅엔디언 uint32. 하위 28비트 길이, 비트 28 OOB, 29 zlib, 30 snappy(폐기) | 그대로 채택할 만합니다. 매직 넘버 없이 헤더 4바이트로 프레이밍과 플래그를 모두 해결합니다. |
| **BlueNet** | 비트팩 가변길이 `BlueNetHeader`. `destinationType` 비교와 FNV-1a 해시 조회로 라우팅. Python을 우회하지 않음 | 채택할 것은 채널 번호 스위치가 아니라 **OOB 분리**와 **서비스명 해시 주소**입니다. 중계 계층이 본문을 역직렬화하지 않아도 됩니다. |
| **blue.Marshal** | 1바이트 태그. 상위 비트 `TY_SHAREDFLAG`, 하위 6비트 타입. 크기 특화 태그. `TY_REFERENCE` 백레퍼런스. 하드코딩 스트링 테이블(1바이트 인덱스, 255개 상한) | 크기 특화 태그와 공유 객체 백레퍼런스는 좋습니다. 그러나 **float32 태그가 없어 실수마다 8바이트를 씁니다.** VR 트래킹 데이터에는 부적합하므로 Haruka는 float32 태그를 반드시 추가해야 합니다. |
| **MachoNet / GPCS** | **공개되지 않았습니다.** io README의 한 문장이 전부입니다 | 참조 규격으로 삼을 수 없습니다. RPC 프레임은 처음부터 직접 설계해야 합니다. |
| **Destiny Ballpark** | 1Hz 고정 틱. 폐쇄형 항력 해석해. 상태가 아니라 ACTION을 틱 스탬프와 함께 보내고 양쪽이 결정론적으로 재시뮬레이션 | 가장 값어치 있는 항목입니다. 다만 결정론이 전제 조건이므로 골든 궤적 회귀 테스트가 함께 와야 합니다. |

**[정정]** 이전 판의 이 표는 다섯 행 전부가 잘못된 전제 위에 세워져 있었습니다.
특히 `HNetSerializer`에 붙인 "28-byte VR 상태 스냅샷"이라는 수치는 근거가 없고,
`HNetFastDispatcher`가 전제한 16바이트 OOB 헤더와 `channelId`는 존재하지 않는
구조이며, `HNetRPC`가 참조하려던 4-튜플 GPCS 규격은 공개되지 않았습니다.
`ZeroGBallpark`가 전제한 `BallState` 스냅샷 동기화 역시 destiny의 실제 방식이 아닙니다.

### 6-1. 선행 해결 과제: HNet 계획과 Threading.md의 충돌

이 문서가 그려 온 `HNetSocket`, `HNetFastDispatcher` 계열의 설계는 **워커 스레드에서
소켓을 수거해 게임 스레드로 넘기는 구조**를 전제합니다. 이것은 Haruka의
`Threading.md`가 정한 단일 스레드 전제와 정면으로 충돌하므로, 네트워크 코드를 쓰기
전에 먼저 해소해야 합니다.

주목할 점은 **Carbon이 독립적으로 Haruka와 같은 결론에 도달했다**는 사실입니다.
carbon-scheduler는 엔진 잡 시스템이 아니라 CPython 3.12 C 확장 모듈이며, GIL 아래에서
동작하는 협조적 마이크로스레드라 병렬 실행이 없습니다. `ScheduleManager::Run`은 한 번에
정확히 하나의 tasklet으로 전환하고, tasklet은 스레드를 이동할 수 없으며 시도하면
"Cannot switch tasklet from another thread"라는 런타임 에러가 납니다. CarbonIO에
워커 스레드 풀이 없는 것도 같은 이유입니다. 상태는 정확히 한 스레드가 소유하고,
작업은 스레드를 이동하지 않으며, 교차 상호작용은 소유자 큐에 넣는 것뿐입니다.
Carbon의 설계 문서에는 이 제약을 완화하는 안이 검토되었다가 기각된 기록이 있습니다.

따라서 권장하는 방향은 IOCP 워커 풀이 아니라 다음과 같습니다.

1. 이벤트 루프를 **게임 스레드에서 논블로킹으로 펌핑**합니다.
   Carbon의 `uv_run(UV_RUN_NOWAIT)`가 정확히 이 형태입니다.
2. 수신 처리량을 **프레임 예산으로 제한**합니다.
   carbon-scheduler의 `RunTaskletsForTime`이 같은 패턴이며, 새 스레드 없이 메인
   스레드의 지연 작업 큐를 데드라인까지만 소진합니다. VR의 프레임 데드라인이
   엄격하다는 점을 고려하면 이 패턴이 특히 잘 맞습니다.
3. 소켓 버퍼는 **연결마다 하나**를 두고 읽기와 쓰기 커서로 프레임을 잘라냅니다.
   락프리 풀은 단일 스레드에서 아무 이득이 없습니다.

본 분석서는 공개된 Carbon 저장소에서 실제로 확인 가능한 범위만을 기술합니다.
MachoNet과 GPCS를 포함한 상위 분산 계층은 공개되지 않았으므로, Haruka의 멀티플레이어
설계에서 그 부분은 참조 대상이 아니라 자체 설계 영역으로 두어야 합니다.
