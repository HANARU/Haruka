// Steam Audio 4.8.1 바이노럴 효과로 만든 HRTF 구현(2026-11-06, Docs/Audio_Design.md 9-6).
//
// 라이브러리(phonon.dll · libphonon.so)는 링크하지 않고 실행 중에 엽니다. 그래서 파일이 없어도 엔진은 시작하고 패닝으로
// 계속하며(AudioSystem 이 경고 한 줄), 실행 파일의 가져오기 목록도 바뀌지 않습니다(Vulkan 가져오기 시험). Windows 는 실행 파일
// 폴더와 System32 에서만 찾습니다(현재 폴더 · PATH 의 같은 이름 DLL 을 집지 않음). Android 는 APK 의 lib/arm64-v8a 에서 찾습니다.
// 라이브러리는 한 번 열면 프로세스가 끝날 때까지 닫지 않습니다(내부 스레드 · TLS 가 있는 라이브러리를 닫다가 생기는 문제를 피함).
//
// 쓰는 것은 컨텍스트 · 기본 HRTF · 바이노럴 효과뿐입니다. 시뮬레이터(차폐 · 반사 · 경로)는 쓰지 않습니다(차폐 · 잔향은 Haruka 가 함).
// 빌드 옵션 HARUKA_STEAM_AUDIO 를 끄면 phonon.h 없이 컴파일되고 loadSteamAudio 는 언제나 실패를 돌려줍니다.

#include "AudioHrtf.hpp"
#include "../core/HLog.hpp"

#if defined(HARUKA_HAS_STEAM_AUDIO)

#include <phonon.h>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

#include <mutex>
#include <string>

static constinit HLogCategory LogAudio{ "Audio", ELogVerbosity::Log };

namespace hhrtf {
namespace {

// 쓰는 함수만 모은 표입니다. 형은 phonon.h 의 선언에서 가져와 호출 규약(32비트 Windows 의 __stdcall)까지 맞춥니다.
struct PhononApi {
    decltype(&iplContextCreate)         contextCreate  = nullptr;
    decltype(&iplContextRelease)        contextRelease = nullptr;
    decltype(&iplHRTFCreate)            hrtfCreate     = nullptr;
    decltype(&iplHRTFRelease)           hrtfRelease    = nullptr;
    decltype(&iplBinauralEffectCreate)  effectCreate   = nullptr;
    decltype(&iplBinauralEffectRelease) effectRelease  = nullptr;
    decltype(&iplBinauralEffectReset)   effectReset    = nullptr;
    decltype(&iplBinauralEffectApply)   effectApply    = nullptr;
    decltype(&iplBinauralEffectGetTail) effectTail     = nullptr;
};

#if defined(_WIN32)
constexpr const char* kLibraryName = "phonon.dll";
#else
constexpr const char* kLibraryName = "libphonon.so";
#endif

void* openLibrary(std::string& error) {
#if defined(_WIN32)
    HMODULE m = LoadLibraryExW(L"phonon.dll", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m) error = std::string(kLibraryName) + " not found next to the executable (Windows error " +
                    std::to_string(GetLastError()) + ")";
    return reinterpret_cast<void*>(m);
#else
    void* m = dlopen(kLibraryName, RTLD_NOW | RTLD_LOCAL);
    if (!m) {
        const char* e = dlerror();
        error = std::string(kLibraryName) + " could not be loaded: " + (e ? e : "unknown error");
    }
    return m;
#endif
}

void* findSymbol(void* lib, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}

// 프로세스에 한 번만 엽니다(AudioSystem 을 여러 번 만드는 시험에서도 같은 표를 씀). 실패도 기억해 같은 경고를 되풀이하지 않습니다.
const PhononApi* phonon(std::string& error) {
    static std::once_flag once;
    static PhononApi      api;
    static bool           ok = false;
    static std::string    failure;
    std::call_once(once, [] {
        void* lib = openLibrary(failure);
        if (!lib) return;
        auto get = [&](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(findSymbol(lib, name));
            if (!fn && failure.empty()) failure = std::string(kLibraryName) + " has no " + name;
        };
        get(api.contextCreate,  "iplContextCreate");
        get(api.contextRelease, "iplContextRelease");
        get(api.hrtfCreate,     "iplHRTFCreate");
        get(api.hrtfRelease,    "iplHRTFRelease");
        get(api.effectCreate,   "iplBinauralEffectCreate");
        get(api.effectRelease,  "iplBinauralEffectRelease");
        get(api.effectReset,    "iplBinauralEffectReset");
        get(api.effectApply,    "iplBinauralEffectApply");
        get(api.effectTail,     "iplBinauralEffectGetTail");
        ok = failure.empty();
    });
    if (!ok) { error = failure; return nullptr; }
    return &api;
}

// Steam Audio 의 로그(주로 만들기 실패의 이유)를 엔진 로그로 넘깁니다. 정보 · 디버그는 버립니다. HLOG 는 스레드 안전합니다.
void IPLCALL phononLog(IPLLogLevel level, const char* message) {
    if (level != IPL_LOGLEVEL_WARNING && level != IPL_LOGLEVEL_ERROR) return;
    std::string m = message ? message : "";
    while (!m.empty() && (m.back() == '\n' || m.back() == '\r')) m.pop_back();
    if (level == IPL_LOGLEVEL_ERROR) { HLOG(LogAudio, Error)   << "Steam Audio: " << m; }
    else                             { HLOG(LogAudio, Warning) << "Steam Audio: " << m; }
}

class SteamRenderer final : public Renderer {
public:
    SteamRenderer(const PhononApi& api, IPLHRTF hrtf, IPLBinauralEffect effect) : api_(api), hrtf_(hrtf), effect_(effect) {}
    ~SteamRenderer() override { api_.effectRelease(&effect_); }

    void reset() override { api_.effectReset(effect_); }

    bool process(const float* mono, float* outL, float* outR, const glm::vec3& dir, float blend) override {
        IPLBinauralEffectParams p{};
        p.direction     = IPLVector3{ dir.x, dir.y, dir.z };   // 청취자 → 음원(Steam Audio 규약). miniaudio 예제는 반대 방향을 넘김
        // 쌍선형: 가장 가까운 측정 방향 4개를 섞습니다. 최근접은 싸지만 움직이는 소리 · 고개 돌림에서 필터가 갑자기 바뀌는 소리가
        // 들립니다(Steam Audio 문서). VR 은 고개가 늘 움직이므로 쌍선형을 씁니다. Quest 비용은 실기에서 잽니다.
        p.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
        p.spatialBlend  = blend;
        p.hrtf          = hrtf_;
        p.peakDelays    = nullptr;
        float* inData[1]  = { const_cast<float*>(mono) };   // Steam Audio 는 입력을 읽기만 합니다
        float* outData[2] = { outL, outR };
        IPLAudioBuffer in { 1, (IPLint32)kFrameSize, inData };
        IPLAudioBuffer out{ 2, (IPLint32)kFrameSize, outData };
        return api_.effectApply(effect_, &p, &in, &out) == IPL_AUDIOEFFECTSTATE_TAILREMAINING;   // 실제로는 언제나 남음
    }

    bool tail(float* outL, float* outR) override {
        float* outData[2] = { outL, outR };
        IPLAudioBuffer out{ 2, (IPLint32)kFrameSize, outData };
        return api_.effectTail(effect_, &out) == IPL_AUDIOEFFECTSTATE_TAILREMAINING;
    }

private:
    const PhononApi&  api_;
    IPLHRTF           hrtf_;
    IPLBinauralEffect effect_;
};

class SteamBackend final : public Backend {
public:
    SteamBackend(const PhononApi& api, IPLContext ctx, IPLHRTF hrtf, IPLAudioSettings settings)
        : api_(api), ctx_(ctx), hrtf_(hrtf), settings_(settings) {}
    ~SteamBackend() override {
        api_.hrtfRelease(&hrtf_);
        api_.contextRelease(&ctx_);
    }

    std::string name() const override {
        return "Steam Audio " + std::to_string(STEAMAUDIO_VERSION_MAJOR) + "." + std::to_string(STEAMAUDIO_VERSION_MINOR) + "." +
               std::to_string(STEAMAUDIO_VERSION_PATCH);
    }

    std::unique_ptr<Renderer> createRenderer() override {
        IPLBinauralEffectSettings es{};
        es.hrtf = hrtf_;
        IPLBinauralEffect effect = nullptr;
        if (api_.effectCreate(ctx_, &settings_, &es, &effect) != IPL_STATUS_SUCCESS || !effect) return nullptr;
        return std::make_unique<SteamRenderer>(api_, hrtf_, effect);
    }

    // 기본 HRTF 로 정면의 백색 잡음을 내면 귀별 RMS 가 입력보다 +3.5 dB(왼쪽) · +5.5 dB(오른쪽) 큽니다(데이터가 좌우 대칭이
    // 아님). 패닝일 때 정면 음원은 채널마다 0.5(-6 dB)라서, 두 귀의 거듭제곱 평균이 그 크기가 되는 값(잰 값 0.586)을 곱합니다.
    // 그래서 HRTF 를 켜고 꺼도 정면 소리의 크기가 같습니다(오디오 테스트 17-6: 평균 ±0.5 dB, 귀마다 ±1.5 dB).
    float outputGain() const override { return kOutputGain; }

    static constexpr float kOutputGain = 0.59f;

private:
    const PhononApi& api_;
    IPLContext       ctx_;
    IPLHRTF          hrtf_;
    IPLAudioSettings settings_;
};

} // namespace

std::unique_ptr<Backend> loadSteamAudio(std::string& error) {
    const PhononApi* api = phonon(error);
    if (!api) return nullptr;

    IPLContextSettings cs{};
    cs.version     = STEAMAUDIO_VERSION;   // 라이브러리가 이 API 와 맞지 않으면 만들기가 실패합니다
    cs.logCallback = phononLog;
    // AVX-512 는 일부 CPU 에서 클럭을 낮춰 오히려 느려질 수 있어 AVX2 까지만 씁니다(Steam Audio 문서의 권고). ARM 은 NEON.
    cs.simdLevel   = IPL_SIMDLEVEL_AVX2;
    IPLContext ctx = nullptr;
    if (api->contextCreate(&cs, &ctx) != IPL_STATUS_SUCCESS || !ctx) {
        error = "Steam Audio context could not be created (library version does not match " +
                std::to_string(STEAMAUDIO_VERSION_MAJOR) + "." + std::to_string(STEAMAUDIO_VERSION_MINOR) + "?)";
        return nullptr;
    }
    IPLAudioSettings as{};
    as.samplingRate = 48000;
    as.frameSize    = (IPLint32)kFrameSize;
    IPLHRTFSettings hs{};
    hs.type     = IPL_HRTFTYPE_DEFAULT;   // 내장 HRTF(48 kHz 로 맞춰 줌)
    hs.volume   = 1.f;
    hs.normType = IPL_HRTFNORMTYPE_NONE;
    IPLHRTF hrtf = nullptr;
    if (api->hrtfCreate(ctx, &as, &hs, &hrtf) != IPL_STATUS_SUCCESS || !hrtf) {
        api->contextRelease(&ctx);
        error = "Steam Audio default HRTF could not be created";
        return nullptr;
    }
    return std::make_unique<SteamBackend>(*api, ctx, hrtf, as);
}

} // namespace hhrtf

#else   // HARUKA_HAS_STEAM_AUDIO

namespace hhrtf {
std::unique_ptr<Backend> loadSteamAudio(std::string& error) {
    error = "built without Steam Audio (CMake option HARUKA_STEAM_AUDIO is OFF)";
    return nullptr;
}
} // namespace hhrtf

#endif
