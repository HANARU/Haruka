// miniaudio 구현부는 MiniaudioImpl.cpp 에 있습니다. 여기서는 선언만 씁니다.
#include "miniaudio.h"

#include "AudioSystem.hpp"
#include "../components/AudioComponent.hpp"
#include "../core/World.hpp"
#include "../vfs/Vfs.hpp"
#include "../core/Actor.hpp"
#include "../components/CameraComponent.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "../core/HLog.hpp"
#include "../core/TextUtf.hpp"

static constinit HLogCategory LogAudio{ "Audio", ELogVerbosity::Log };

// EAudioAttenuation 은 ma_attenuation_model 값을 그대로 씁니다(applySettings 가 캐스트만 합니다).
static_assert((int)EAudioAttenuation::Inverse     == (int)ma_attenuation_model_inverse);
static_assert((int)EAudioAttenuation::Linear      == (int)ma_attenuation_model_linear);
static_assert((int)EAudioAttenuation::Exponential == (int)ma_attenuation_model_exponential);

static ma_engine*      asEngine(void* p) { return static_cast<ma_engine*>(p); }
static ma_sound*       asSound (void* p) { return static_cast<ma_sound*>(p); }
static ma_sound_group* asGroup (void* p) { return static_cast<ma_sound_group*>(p); }

// ── 파일 읽기 (2026-10-24) ───────────────────────────────────────────────────────
// miniaudio 의 리소스 관리자가 소리 파일을 열 때 가상 파일 시스템을 거칩니다(에디터는 프로젝트 폴더, 게임은 묶음 파일).
// 소리는 통째로 디코딩하므로(STREAM 플래그 없음) 열 때 파일 전체를 메모리로 읽어 둡니다. 리소스 관리자의 작업 스레드가
// 부를 수 있고 hvfs 읽기는 여러 스레드에서 불러도 됩니다. 파일 하나는 한 스레드만 씁니다(miniaudio 규칙).
namespace {

struct VfsFile {
    std::vector<uint8_t> data;
    size_t               pos = 0;
};

// 이 콜백들은 miniaudio(C 코드)가 리소스 관리자의 잠금을 쥔 채 부릅니다. 예외가 이 경계를 넘으면 잠금이 풀리지 않은 채
// 프로그램이 끝나므로(DEF-123), 여기서 모두 받아 오류 코드로 바꿉니다. hvfs 읽기는 던지지 않지만 할당은 던질 수 있습니다.
ma_result vfsOpen(ma_vfs*, const char* path, ma_uint32 mode, ma_vfs_file* out) {
    if (!out || !path) return MA_INVALID_ARGS;
    *out = nullptr;
    if (mode & MA_OPEN_MODE_WRITE) return MA_ACCESS_DENIED;
    try {
        auto f = std::make_unique<VfsFile>();
        std::string err;
        if (!hvfs::readAll(path, f->data, &err)) {
            HLOG(LogAudio, Warning) << "Sound file not opened: " << err;
            return MA_DOES_NOT_EXIST;
        }
        *out = f.release();
        return MA_SUCCESS;
    } catch (...) {
        return MA_OUT_OF_MEMORY;
    }
}
ma_result vfsOpenW(ma_vfs* vfs, const wchar_t* path, ma_uint32 mode, ma_vfs_file* out) {
#ifdef _WIN32
    try {
        return vfsOpen(vfs, hutf::fromUtf16(path).c_str(), mode, out);   // Windows 의 wchar_t 는 UTF-16
    } catch (...) {
        return MA_OUT_OF_MEMORY;
    }
#else
    (void)vfs; (void)path; (void)mode; (void)out;
    return MA_NOT_IMPLEMENTED;
#endif
}
ma_result vfsClose(ma_vfs*, ma_vfs_file file) {
    delete static_cast<VfsFile*>(file);
    return MA_SUCCESS;
}
ma_result vfsRead(ma_vfs*, ma_vfs_file file, void* dst, size_t bytes, size_t* read) {
    auto* f = static_cast<VfsFile*>(file);
    const size_t n = std::min(bytes, f->data.size() - f->pos);
    if (n) std::memcpy(dst, f->data.data() + f->pos, n);
    f->pos += n;
    if (read) *read = n;
    return (n == 0 && bytes > 0) ? MA_AT_END : MA_SUCCESS;
}
ma_result vfsWrite(ma_vfs*, ma_vfs_file, const void*, size_t, size_t* written) {
    if (written) *written = 0;
    return MA_ACCESS_DENIED;
}
ma_result vfsSeek(ma_vfs*, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin) {
    auto* f = static_cast<VfsFile*>(file);
    const ma_int64 base = origin == ma_seek_origin_start ? 0
                        : origin == ma_seek_origin_current ? (ma_int64)f->pos : (ma_int64)f->data.size();
    const ma_int64 to = base + offset;
    if (to < 0 || to > (ma_int64)f->data.size()) return MA_BAD_SEEK;
    f->pos = (size_t)to;
    return MA_SUCCESS;
}
ma_result vfsTell(ma_vfs*, ma_vfs_file file, ma_int64* cursor) {
    *cursor = (ma_int64)static_cast<VfsFile*>(file)->pos;
    return MA_SUCCESS;
}
ma_result vfsInfo(ma_vfs*, ma_vfs_file file, ma_file_info* info) {
    info->sizeInBytes = static_cast<VfsFile*>(file)->data.size();
    return MA_SUCCESS;
}

ma_vfs_callbacks g_vfs = { vfsOpen, vfsOpenW, vfsClose, vfsRead, vfsWrite, vfsSeek, vfsTell, vfsInfo };

} // namespace

// ── 출력 장치 (2026-10-16) ───────────────────────────────────────────────────────
// 엔진이 장치를 직접 만들게 두지 않고(ma_engine_config::pDevice) 여기서 만든 장치에 연결합니다. 장치를 바꿀 때 엔진과
// 사운드는 그대로 두고 장치만 닫았다 같은 자리에 다시 열면 되기 때문입니다. 장치 형식은 언제나 f32 · 2채널 · 48kHz 로
// 열어(실제 장치 형식과 다르면 miniaudio 가 변환) 엔진이 처음 본 형식이 바뀌지 않게 합니다.
namespace {
struct OutputState {
    ma_context  ctx{};
    ma_device   dev{};
    bool        devOk = false;
    std::string activeId;     // 연 장치(빈 문자열 = 시스템 기본)
    std::string activeName;
};
OutputState* asOut(void* p) { return static_cast<OutputState*>(p); }

// WASAPI 는 끝점 ID(예: "{0.0.0.00000000}.{GUID}")가 장치를 정확히 가리키고, OpenXR 런타임이 알려 주는 헤드셋 장치 ID 와
// 같은 형식입니다. 다른 백엔드는 장치 이름을 씁니다.
std::string deviceIdString(const ma_context& ctx, const ma_device_info& info) {
    if (ctx.backend == ma_backend_wasapi) return hutf::fromUtf16(info.id.wasapi);
    return info.name;
}

void deviceData(ma_device* d, void* out, const void* /*in*/, ma_uint32 frames) {
    ma_engine_read_pcm_frames(static_cast<ma_engine*>(d->pUserData), out, frames, nullptr);
}

// want 가 가리키는 장치 하나를 엽니다(시작은 하지 않음). 비었으면 시스템 기본 장치입니다. 목록에 없으면 열지 않고
// missing 을 참으로, 열기가 실패하면(다른 앱의 독점 모드 등) error 에 이유를 남기고 false 를 돌려줍니다.
bool openDevice(OutputState& o, ma_engine* eng, const std::string& want, bool& missing, std::string& error) {
    missing = false;
    error.clear();
    ma_device_id id{};
    bool haveId = false;
    if (!want.empty()) {
        ma_device_info* infos = nullptr;
        ma_uint32       count = 0;
        if (ma_context_get_devices(&o.ctx, &infos, &count, nullptr, nullptr) == MA_SUCCESS)
            for (ma_uint32 i = 0; i < count && !haveId; ++i)
                if (hutf::equalsIgnoreCase(deviceIdString(o.ctx, infos[i]), want)) { id = infos[i].id; haveId = true; }
        if (!haveId) { missing = true; return false; }
    }
    ma_device_config dc = ma_device_config_init(ma_device_type_playback);
    dc.playback.pDeviceID        = haveId ? &id : nullptr;
    dc.playback.format           = ma_format_f32;
    dc.playback.channels         = AudioSystem::kChannels;
    dc.sampleRate                = AudioSystem::kSampleRate;
    dc.dataCallback              = deviceData;
    dc.pUserData                 = eng;
    dc.noPreSilencedOutputBuffer = MA_TRUE;   // 콜백이 언제나 모든 프레임을 채웁니다(엔진 기본 장치와 같은 설정)
    dc.noClip                    = MA_TRUE;   // 엔진이 직접 자릅니다
    if (const ma_result r = ma_device_init(&o.ctx, &dc, &o.dev); r != MA_SUCCESS) {
        error = ma_result_description(r);
        return false;
    }
    o.devOk      = true;
    o.activeId   = want;
    o.activeName = o.dev.playback.name;
    return true;
}

// 후보를 앞에서부터 열어 처음 열린 것을 씁니다(AudioSystem::outputCandidates 순서). 목록에 없거나 열리지 않은 후보는
// 경고하고 다음 후보로 갑니다. 예전에는 고른 장치가 열리지 않으면 시스템 기본으로 돌아가지 않았고(DEF-146), 헤드셋 장치가
// 목록에 없으면 고른 장치를 건너뛰고 시스템 기본으로 열었습니다(DEF-147). quiet 면 경고를 남기지 않습니다(다시 열기 재시도).
bool openPreferred(OutputState& o, ma_engine* eng, const std::vector<std::string>& candidates, bool quiet) {
    for (size_t k = 0; k < candidates.size(); ++k) {
        bool        missing = false;
        std::string error;
        if (openDevice(o, eng, candidates[k], missing, error)) return true;
        if (quiet) continue;
        const std::string what = candidates[k].empty() ? std::string("system default") : candidates[k];
        if (missing) {
            HLOG(LogAudio, Warning) << "Audio output device not found: " << what;
        } else {
            HLOG(LogAudio, Warning) << "Audio output device could not be opened: " << what << " (" << error << ")";
        }
        if (k + 1 < candidates.size()) {
            HLOG(LogAudio, Warning) << "  trying " << (candidates[k + 1].empty() ? std::string("the system default") : candidates[k + 1]);
        }
    }
    return false;
}
} // namespace

std::vector<std::string> AudioSystem::outputCandidates(const std::string& overrideId, const std::string& requestedId) {
    std::vector<std::string> c;
    if (!overrideId.empty()) c.push_back(overrideId);
    if (!requestedId.empty() && !hutf::equalsIgnoreCase(requestedId, overrideId)) c.push_back(requestedId);
    c.push_back(std::string());   // 시스템 기본
    return c;
}

void AudioSystem::startup() {
    auto* eng = new ma_engine();
    ma_engine_config cfg = ma_engine_config_init();
    cfg.channels   = kChannels;
    cfg.sampleRate = kSampleRate;
    cfg.noDevice   = headless_ ? MA_TRUE : MA_FALSE;
    cfg.pResourceManagerVFS = &g_vfs;   // 소리 파일을 가상 파일 시스템으로 엶(2026-10-24)

    OutputState* out = nullptr;
    if (!headless_) {
        out = new OutputState();
        if (const ma_result r = ma_context_init(nullptr, 0, nullptr, &out->ctx); r != MA_SUCCESS) {
            HLOG(LogAudio, Error) << "ma_context_init failed: " << ma_result_description(r);
            delete out;
            delete eng;
            return;
        }
        if (!openPreferred(*out, eng, outputCandidates(overrideId_, requestedId_), false)) {
            HLOG(LogAudio, Error) << "No audio output (no device could be opened)";
            ma_context_uninit(&out->ctx);
            delete out;
            delete eng;
            return;
        }
        cfg.pDevice = &out->dev;   // 엔진이 이 장치를 시작합니다(noAutoStart 꺼짐)
    }

    if (const ma_result r = ma_engine_init(&cfg, eng); r != MA_SUCCESS) {
        HLOG(LogAudio, Error) << "ma_engine_init failed: " << ma_result_description(r);
        if (out) {
            ma_device_uninit(&out->dev);
            ma_context_uninit(&out->ctx);
            delete out;
        }
        delete eng;
        return;
    }
    engine_ = eng;
    out_    = out;

    // 그룹은 기본으로 재생 상태이고 공간화가 꺼져 있어, 섞인 소리를 다시 감쇠하지 않습니다.
    // 피치도 끕니다(NO_PITCH). 켜 두면 그룹이 섞인 소리 전체를 배율 1 로 리샘플링하느라
    // 연산을 쓰고, 그 리샘플러가 한 프레임 늦게 내보내서 멈춘 소리의 마지막 샘플이 다음
    // 블록 첫머리에 남습니다(HarukaAudioTest 5절이 잡았습니다).
    auto* grp = new ma_sound_group();
    if (const ma_result r = ma_sound_group_init(eng, MA_SOUND_FLAG_NO_PITCH, nullptr, grp); r != MA_SUCCESS) {
        // 그룹이 없으면 사운드는 엔진 출력에 바로 붙습니다. 재생은 되고 일시정지만 듣지 않습니다.
        HLOG(LogAudio, Error) << "world sound group init failed: " << ma_result_description(r)
                              << " (pause will not silence sounds)";
        delete grp;
    } else {
        group_ = grp;
    }
    HLOG(LogAudio, Log) << "miniaudio initialized (48 kHz stereo" << (headless_ ? ", no device" : "") << ")";
    if (out) HLOG(LogAudio, Log) << "Audio output: " << out->activeName << (out->activeId.empty() ? " (system default)" : "");
}

void AudioSystem::shutdown() {
    if (!engine_) return;
    // 장치 콜백(오디오 스레드)이 엔진을 읽지 않도록 먼저 멈춥니다.
    OutputState* out = asOut(out_);
    if (out && out->devOk) ma_device_stop(&out->dev);
    // 엔진보다 사운드를 먼저 정리합니다. World 는 Engine 이 소멸할 때 컴포넌트 onDestroy 없이
    // 해제되므로, 여기서 정리하지 않으면 해제된 엔진에 붙은 사운드와 사라질 이 객체를 가리키는
    // audioSys_ 가 컴포넌트에 남습니다.
    releaseWorldSounds();
    if (group_) {
        ma_sound_group_uninit(asGroup(group_));
        delete asGroup(group_);
        group_ = nullptr;
    }
    ma_engine_uninit(asEngine(engine_));   // 장치는 엔진 것이 아니므로 아래에서 직접 닫습니다
    delete asEngine(engine_);
    engine_ = nullptr;
    if (out) {
        if (out->devOk) ma_device_uninit(&out->dev);
        ma_context_uninit(&out->ctx);
        delete out;
        out_ = nullptr;
    }
    paused_ = false;
}

std::vector<AudioOutputDevice> AudioSystem::outputDevices() const {
    std::vector<AudioOutputDevice> list;
    OutputState* out = asOut(out_);
    if (!out) return list;
    ma_device_info* infos = nullptr;
    ma_uint32       count = 0;
    if (ma_context_get_devices(&out->ctx, &infos, &count, nullptr, nullptr) != MA_SUCCESS) return list;
    for (ma_uint32 i = 0; i < count; ++i)
        list.push_back({ deviceIdString(out->ctx, infos[i]), infos[i].name, infos[i].isDefault != 0 });
    return list;
}

void AudioSystem::setOutputDevice(const std::string& id) {
    requestedId_ = id;
    reopenDevice();
}

void AudioSystem::setOutputOverride(const std::string& id) {
    overrideId_ = id;
    reopenDevice();
}

std::string AudioSystem::activeOutputName() const {
    const OutputState* out = asOut(out_);
    return (out && out->devOk) ? out->activeName : std::string();
}

bool AudioSystem::overrideActive() const {
    const OutputState* out = asOut(out_);
    return out && out->devOk && !overrideId_.empty() && hutf::equalsIgnoreCase(out->activeId, overrideId_);
}

bool AudioSystem::reopenDevice(bool quiet) {
    OutputState* out = asOut(out_);
    if (!out || !engine_) return false;   // headless 이거나 시작 전: 값만 기억했다가 시작할 때 엽니다
    const std::string& want = overrideId_.empty() ? requestedId_ : overrideId_;
    // 원하는 장치가 이미 열려 돌고 있으면 그대로 둡니다. 멈춘 장치(실행 중에 뽑힘 등)는 같은 장치라도 다시 엽니다. 예전에는
    // 장치 ID 만 비교해서, 잃은 장치를 대화상자에서 다시 골라도 열지 않았습니다(DEF-145).
    if (out->devOk && hutf::equalsIgnoreCase(out->activeId, want) && ma_device_is_started(&out->dev)) return true;
    // 같은 ma_device 자리를 닫고 다시 엽니다. 엔진은 이 주소를 들고 있고, 형식이 같으므로 엔진 · 사운드는 그대로입니다.
    if (out->devOk) {
        ma_device_uninit(&out->dev);
        out->devOk = false;
    }
    if (!openPreferred(*out, asEngine(engine_), outputCandidates(overrideId_, requestedId_), quiet)) {
        if (!quiet) { HLOG(LogAudio, Error) << "No audio output (no device could be opened)"; }
        return false;
    }
    if (const ma_result r = ma_device_start(&out->dev); r != MA_SUCCESS) {
        if (!quiet) { HLOG(LogAudio, Error) << "Audio output device could not start: " << ma_result_description(r); }
        return false;   // 열린 채 멈춘 장치는 Play 중 tick 이 간격을 두고 다시 엽니다
    }
    HLOG(LogAudio, Log) << "Audio output: " << out->activeName
                        << (out->activeId.empty() ? " (system default)" : "")
                        << (overrideActive() ? " (temporary)" : "");
    return true;
}

// 실행 중에 장치를 잃으면(USB · 블루투스 장치를 뽑음 등) miniaudio 는 장치를 멈춥니다. 시스템 기본 장치는 miniaudio 가
// 새 기본 장치로 옮겨 주지만, 직접 고른 장치는 멈춘 채 남습니다. 간격을 두고 다시 열어, 그 장치가 없으면 다음 후보(고른
// 장치 → 시스템 기본)로 소리를 잇습니다(DEF-145). 장치가 돌고 있으면 목록을 훑지 않으므로 평소 비용은 상태 읽기 하나입니다.
void AudioSystem::checkOutput(float dt) {
    OutputState* out = asOut(out_);
    if (!out || !engine_) return;
    if (out->devOk && ma_device_is_started(&out->dev)) {
        if (outputLost_) {
            outputLost_ = false;
            const std::string& want = overrideId_.empty() ? requestedId_ : overrideId_;
            HLOG(LogAudio, Log) << "Audio output restored: " << out->activeName
                                << (hutf::equalsIgnoreCase(out->activeId, want) ? "" : " (the chosen device is unavailable)");
        }
        outputRetry_ = 0.f;
        return;
    }
    if (!outputLost_) {
        outputLost_  = true;
        outputRetry_ = 0.f;   // 처음 알아챈 프레임에 바로 한 번
        HLOG(LogAudio, Warning) << "Audio output stopped" << (out->devOk ? " (device lost?): " + out->activeName : std::string())
                                << "; reopening every " << kOutputRetrySec << " s";
    }
    outputRetry_ -= dt;
    if (outputRetry_ > 0.f) return;
    outputRetry_ = kOutputRetrySec;
    reopenDevice(/*quiet*/ true);   // 성공하면 다음 프레임에 "restored" 를 남깁니다
}

void AudioSystem::releaseWorldSounds() {
    if (!world_) return;
    for (auto& ap : world_->getActors())
        if (auto* ac = ap->getComponent<AudioComponent>(); ac && ac->sound_) {
            destroySound(ac->sound_);
            ac->sound_    = nullptr;
            ac->audioSys_ = nullptr;
        }
}

void AudioSystem::updateListener() {
    glm::vec3 pos, fwd, up;
    if (hasListenerPose_) {
        // XR 세션 동안의 머리 자세. 카메라와 같은 규약(-Z 정면, +Y 위)입니다.
        pos = glm::vec3(listenerPose_[3]);
        fwd = -glm::normalize(glm::vec3(listenerPose_[2]));
        up  =  glm::normalize(glm::vec3(listenerPose_[1]));
    } else {
        CameraComponent* cam = world_->getActiveCamera();
        if (!cam || !cam->owner) return;
        pos = cam->owner->worldPosition();
        fwd = cam->forward();
        up  = cam->up();
    }
    ma_engine_listener_set_position(asEngine(engine_), 0, pos.x, pos.y, pos.z);
    // 예전에는 위치만 넘겨서 청취자가 늘 -Z 를 보고 있었고, 카메라를 돌려도 소리의 좌우가
    // 바뀌지 않았습니다. miniaudio 는 방향 × 위 벡터로 오른쪽을 정합니다. 카메라 자세가 액터
    // 회전이 된 뒤로(DEF-21) 롤이 있을 수 있으므로 월드 +Y 가 아니라 카메라의 위 벡터를 넘깁니다.
    ma_engine_listener_set_direction(asEngine(engine_), 0, fwd.x, fwd.y, fwd.z);
    ma_engine_listener_set_world_up(asEngine(engine_), 0, up.x, up.y, up.z);
}

void AudioSystem::applySettings(void* maSound, const AudioComponent& ac) {
    ma_sound* s = asSound(maSound);
    const AudioAttenuationSettings& at = ac.attenuation;
    ma_sound_set_volume(s, ac.volume);
    ma_sound_set_looping(s, ac.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_spatialization_enabled(s, at.spatialize ? MA_TRUE : MA_FALSE);
    ma_sound_set_attenuation_model(s, (ma_attenuation_model)at.model);
    ma_sound_set_min_distance(s, at.minDistance);
    ma_sound_set_max_distance(s, at.maxDistance);
    ma_sound_set_rolloff(s, at.rolloff);
}

void AudioSystem::tick(float dt) {
    if (!engine_) return;
    checkOutput(dt);
    if (!world_) return;
    updateListener();

    for (auto& actorPtr : world_->getActors()) {
        Actor* a = actorPtr.get();
        auto* ac = a->getComponent<AudioComponent>();
        // 실효 활성(컴포넌트 enabled && 액터 activeInHierarchy)일 때만 다룹니다. 렌더·틱과
        // 같은 기준이며, 꺼질 때 나오던 소리는 AudioComponent::onDisable 이 멈춥니다.
        if (!ac || !ac->isLive()) continue;

        bool created = false;
        if (!ac->sound_ && !ac->filePath.empty() && !ac->loadFailed_) {
            void* snd = nullptr;
            if (initSound(ac->filePath.c_str(), ac->loop, &snd)) {
                ac->sound_    = snd;
                ac->audioSys_ = this;
                created       = true;
            } else {
                ac->loadFailed_ = true;   // 다음 플레이 시작(onBeginPlay)까지 다시 시도하지 않습니다
            }
        }
        if (!ac->sound_) continue;

        const glm::vec3 pos = a->worldPosition();
        ma_sound_set_position(asSound(ac->sound_), pos.x, pos.y, pos.z);
        applySettings(ac->sound_, *ac);
        // 위치와 설정을 넣은 뒤에 시작해야 첫 소리가 원점에서 나지 않습니다.
        if (created && ac->autoPlay) ma_sound_start(asSound(ac->sound_));
    }
}

void AudioSystem::onBeginPlay() {
    // 지난 플레이에서 불러오기에 실패한 파일을 이번 플레이에서 다시 시도합니다.
    if (world_)
        for (auto& ap : world_->getActors()) {
            auto* ac = ap->getComponent<AudioComponent>();
            if (ac && ac->sound_ == nullptr) ac->loadFailed_ = false;
        }
    onPauseChanged(false);   // 어떤 경로로든 멈춘 그룹이 남아 있으면 풉니다
}

void AudioSystem::onEndPlay() {
    // 플레이가 끝나면 남은 소리를 멈춥니다. 에디터는 Stop 에서 월드를 되돌리며 사운드를 이미
    // 정리하므로 보통은 할 일이 없지만, 다른 경로로 끝나도 소리가 남지 않게 합니다.
    if (world_)
        for (auto& ap : world_->getActors())
            if (auto* ac = ap->getComponent<AudioComponent>(); ac && ac->sound_)
                stopSound(ac->sound_);
    // Pause 상태에서 Stop 하면 그룹이 멈춘 채 남아 다음 플레이가 무음이 됩니다. 여기서 풉니다.
    onPauseChanged(false);
}

void AudioSystem::onPauseChanged(bool paused) {
    if (!group_ || paused == paused_) return;
    if (paused) ma_sound_group_stop(asGroup(group_));
    else        ma_sound_group_start(asGroup(group_));
    paused_ = paused;
}

bool AudioSystem::initSound(const char* filePath, bool loop, void** outSound) {
    if (!engine_) return false;
    auto* snd = new ma_sound();
    // ASYNC 여도 miniaudio 는 디코더를 초기화할 때까지 기다렸다가 돌아옵니다(WAIT_INIT 강제).
    // 그래서 없는 파일과 모르는 형식은 여기서 바로 실패하고, 디코딩은 믹싱 중에 합니다.
    // 논리 경로 그대로 넘깁니다. 리소스 관리자가 가상 파일 시스템으로 엽니다(위 g_vfs, 2026-10-24).
    const ma_result r = ma_sound_init_from_file(asEngine(engine_), filePath, MA_SOUND_FLAG_ASYNC,
                                                asGroup(group_), nullptr, snd);
    if (r != MA_SUCCESS) {
        HLOG(LogAudio, Error) << "failed to load '" << filePath << "': " << ma_result_description(r);
        delete snd;
        return false;
    }
    ma_sound_set_looping(snd, loop ? MA_TRUE : MA_FALSE);
    *outSound = snd;
    return true;
}

void AudioSystem::destroySound(void* maSound) {
    if (!maSound) return;
    auto* snd = asSound(maSound);
    ma_sound_uninit(snd);
    delete snd;
}

void AudioSystem::playSound(void* maSound) {
    if (maSound) ma_sound_start(asSound(maSound));
}

void AudioSystem::stopSound(void* maSound) {
    if (!maSound) return;
    // ma_sound_stop 은 재생 위치를 그대로 두는 일시정지입니다. 엔진의 stop 은 처음으로
    // 되감는 것이 규칙이라(Unity·UE 와 같음) 되감기를 함께 겁니다. 되감기는 다음 믹싱에서
    // 적용됩니다.
    ma_sound_stop(asSound(maSound));
    ma_sound_seek_to_pcm_frame(asSound(maSound), 0);
}

bool AudioSystem::isSoundPlaying(void* maSound) const {
    if (!maSound) return false;
    return ma_sound_is_playing(asSound(maSound)) == MA_TRUE;
}

bool AudioSystem::renderHeadless(float* outStereo, uint32_t frameCount) {
    if (!engine_ || !headless_) return false;
    ma_uint64 read = 0;
    const ma_result r = ma_engine_read_pcm_frames(asEngine(engine_), outStereo, frameCount, &read);
    return r == MA_SUCCESS && read == frameCount;
}
