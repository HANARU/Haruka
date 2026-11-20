// 비동기 로딩(구간 할당자 · 백그라운드 모델 읽기 · 씬의 모델 참조)을 GPU 없이 검산합니다(2026-11-20, Docs/Async_Loading.md).
//
// 이 시험이 존재하는 이유: 메가 버퍼 구간이 한 칸만 겹쳐도 다른 메시의 정점을 덮어써 화면이 엉뚱하게 깨지고, 워커 스레드의
// 경합은 가끔만 드러납니다. 그래서 무작위 할당 · 해제를 비트맵과 맞대어 보고, 여러 스레드가 읽은 결과를 동기 결과와 바이트로 비교합니다.
//   1. RangeAllocator: 첫 맞춤 · 이웃 합치기 · 겹침 거절 · 넘침, 무작위 1만 번
//   2. AssetStreamer: 워커 결과 = 동기 decode, 중복 요청 하나, 없는 파일 실패, 동시 요청, waitFor, 멈춤
//   3. SceneTransition::modelRefs: 씬 글의 모델(.glb · .gltf)만 정식 경로로 모음
// 실행: build/Release/HarukaLoadingTest.exe

#include "../src/assets/AssetStreamer.hpp"
#include "../src/assets/GassetCache.hpp"
#include "../src/assets/Skin.hpp"
#include "../src/renderer/RangeAllocator.hpp"
#include "../src/runtime/SceneTransition.hpp"
#include "../src/vfs/Vfs.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failed = 0;
int g_checks = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) ++g_failed;
    std::printf("  [%s] %s\n", cond ? " OK " : "FAIL", what.c_str());
}

// ── 1. 구간 할당자 ────────────────────────────────────────────────────────────
void testRanges() {
    std::printf("== 1. RangeAllocator ==\n");
    RangeAllocator a(100);
    const uint32_t x = a.allocate(10), y = a.allocate(20), z = a.allocate(30);
    check(x == 0 && y == 10 && z == 30 && a.used() == 60 && a.highWater() == 60, "차례로 잡으면 범프와 같음");
    check(a.allocate(41) == RangeAllocator::kFail && a.used() == 60, "남은 40 보다 크면 거절(바뀌지 않음)");
    check(a.allocate(0) == RangeAllocator::kFail, "0 개는 거절");
    check(a.release(y, 20) && a.used() == 40 && a.freeRanges() == 2, "가운데를 놓으면 빈 구간 둘");
    check(a.allocate(15) == 10, "첫 맞춤: 앞 구멍에 들어감");
    check(!a.release(27, 5), "빈 구간 [25, 30) 과 겹치는 해제는 거절");
    check(!a.release(95, 10) && !a.release(200, 1), "범위 밖 해제는 거절");
    check(a.release(10, 15) && a.release(0, 10) && a.freeRanges() == 2, "앞 이웃과 합침");
    check(a.release(30, 30) && a.freeRanges() == 1 && a.used() == 0 && a.largestFree() == 100, "양쪽 이웃과 합쳐 하나로");
    check(!a.release(0, 10), "두 번 놓기는 거절");

    // 무작위: 비트맵과 맞대어 겹침이 없는지
    RangeAllocator r(5000);
    std::vector<uint8_t> bits(5000, 0);
    std::vector<std::pair<uint32_t, uint32_t>> live;
    std::mt19937 rng(11);
    bool overlap = false, accounting = true;
    uint64_t usedRef = 0;
    for (int i = 0; i < 10000; ++i) {
        if (live.empty() || rng() % 3 != 0) {
            const uint32_t n = 1 + rng() % 120;
            const uint32_t at = r.allocate(n);
            if (at == RangeAllocator::kFail) continue;
            for (uint32_t k = at; k < at + n; ++k) { overlap = overlap || bits[k]; bits[k] = 1; }
            live.push_back({ at, n });
            usedRef += n;
        } else {
            const size_t j = rng() % live.size();
            const auto [at, n] = live[j];
            if (!r.release(at, n)) accounting = false;
            for (uint32_t k = at; k < at + n; ++k) bits[k] = 0;
            live.erase(live.begin() + (std::ptrdiff_t)j);
            usedRef -= n;
        }
        accounting = accounting && r.used() == usedRef;
    }
    check(!overlap, "무작위 1만 번: 잡은 구간끼리 겹치지 않음");
    check(accounting, "사용량 = 잡은 구간 합, 해제는 모두 받아들여짐");
    for (const auto& [at, n] : live) r.release(at, n);
    check(r.used() == 0 && r.freeRanges() == 1 && r.largestFree() == 5000, "모두 놓으면 빈 구간 하나로 돌아옴");
}

// ── 2. 백그라운드 읽기 ─────────────────────────────────────────────────────────
bool sameMeshes(const std::vector<MeshSourceData>& a, const std::vector<MeshSourceData>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].vertices.size() != b[i].vertices.size() || a[i].indices != b[i].indices) return false;
        if (!a[i].vertices.empty() &&
            std::memcmp(a[i].vertices.data(), b[i].vertices.data(), a[i].vertices.size() * sizeof(MeshSourceVertex)) != 0)
            return false;
        if ((a[i].skin == nullptr) != (b[i].skin == nullptr)) return false;
        if (a[i].skin && (a[i].skin->joints.size() != b[i].skin->joints.size() ||
                          a[i].skin->jointWeight != b[i].skin->jointWeight ||
                          a[i].skin->clips.size() != b[i].skin->clips.size()))
            return false;
    }
    return true;
}

std::vector<AssetStreamer::Result> drain(AssetStreamer& s, size_t want) {
    std::vector<AssetStreamer::Result> got;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (got.size() < want && std::chrono::steady_clock::now() < until) {
        for (auto& r : s.takeFinished()) got.push_back(std::move(r));
        if (got.size() < want) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return got;
}

void testStreamer() {
    std::printf("== 2. AssetStreamer ==\n");
    const std::vector<std::string> paths = {
        "/Game/models/anim/TestArm.glb", "/Game/models/hands/TestGlove_R.glb",
        "/Game/models/hands/TestGlove_L.glb", "/Game/models/model.glb", "/Game/models/anim/TestMorph.glb" };
    std::vector<std::vector<MeshSourceData>> ref(paths.size());
    bool syncOk = true;
    for (size_t i = 0; i < paths.size(); ++i) {
        std::string err;
        syncOk = AssetStreamer::decode(paths[i], ref[i], err) && syncOk;
    }
    check(syncOk, "동기 decode 로 기준 결과 다섯(캐시를 만듦)");
    {
        AssetStreamer s(2);
        check(s.workers() == 2, "워커 둘");
        size_t accepted = 0;
        for (const auto& p : paths) accepted += s.request(p) ? 1 : 0;
        check(accepted == paths.size() && !s.request(paths[0]), "다섯 요청, 같은 경로 다시 요청은 새 작업 없음");
        check(s.request("/Game/models/nope.glb"), "없는 파일도 요청은 받음");
        auto got = drain(s, paths.size() + 1);
        check(got.size() == paths.size() + 1 && s.pending() == 0, "모두 끝나 꺼냄(대기 0)");
        bool same = true, failOk = false;
        for (const auto& r : got) {
            if (r.path == "/Game/models/nope.glb") { failOk = !r.ok && !r.error.empty() && r.meshes.empty(); continue; }
            for (size_t i = 0; i < paths.size(); ++i)
                if (paths[i] == r.path) same = same && r.ok && sameMeshes(r.meshes, ref[i]);
        }
        check(same, "워커 결과 = 동기 결과(정점 바이트 · 인덱스 · 스킨 · 클립)");
        check(failOk, "없는 파일은 실패 결과(이유 있음, 메시 없음)");
        check(s.state(paths[0]) == AssetStreamer::State::None, "꺼낸 뒤 상태는 None(다시 요청 가능)");
    }
    {
        // 캐시를 지우고 여러 워커가 동시에 glTF 를 디코드 · 캐시를 씀
        GassetCache::setDirectory((fs::temp_directory_path() / "haruka_loading_test" / "cache2").string());
        AssetStreamer s(4);
        for (int round = 0; round < 3; ++round)
            for (const auto& p : paths) s.request(p);
        auto got = drain(s, paths.size());
        bool same = got.size() == paths.size();
        for (const auto& r : got)
            for (size_t i = 0; i < paths.size(); ++i)
                if (paths[i] == r.path) same = same && r.ok && sameMeshes(r.meshes, ref[i]);
        check(same, "워커 넷 · 빈 캐시에서 동시 디코드해도 결과가 같음");
        std::string err;
        std::vector<MeshSourceData> again;
        check(AssetStreamer::decode(paths[0], again, err) && sameMeshes(again, ref[0]), "워커가 쓴 캐시를 다시 읽어도 같음");
    }
    {
        AssetStreamer s(1);
        AssetStreamer::Result r;
        check(!s.waitFor("/Game/models/model.glb", r), "요청한 적 없는 경로를 기다리면 false");
        s.request("/Game/models/model.glb");
        check(s.waitFor("/Game/models/model.glb", r) && r.ok && sameMeshes(r.meshes, ref[3]), "waitFor 가 그 작업을 기다려 결과를 줌");
        for (int i = 0; i < 20; ++i) s.request("/Game/models/missing_" + std::to_string(i) + ".glb");
    }   // 대기 중 작업이 남은 채 지움
    check(true, "대기 작업이 남은 채 지워도 멈추지 않고 끝남");
}

// ── 3. 씬의 모델 참조 ─────────────────────────────────────────────────────────
void testModelRefs() {
    std::printf("== 3. SceneTransition::modelRefs ==\n");
    std::string text;
    check(hvfs::readText("/Game/scenes/AnimProbe.hscene", text), "AnimProbe 읽음");
    const auto refs = SceneTransition::modelRefs("/Game/scenes/AnimProbe.hscene", text);
    check(refs.size() == 2 && refs[0] == "/Game/models/anim/TestArm.glb" && refs[1] == "/Game/models/anim/TestMorph.glb",
          "모델 둘(같은 모델 세 번은 하나로), 정식 경로");
    std::string sp;
    hvfs::readText("/Game/scenes/SpatialAudioProbe.hscene", sp);
    bool noAudio = true;
    for (const auto& r : SceneTransition::modelRefs("/Game/scenes/SpatialAudioProbe.hscene", sp))
        noAudio = noAudio && r.find(".wav") == std::string::npos && r.find(".ogg") == std::string::npos;
    check(noAudio, "소리 파일은 모으지 않음");
    const std::string handMade = "ACTOR:A\nCOMPONENT:StaticMeshComponent\nmodel=assets/models/model.glb\nEND_COMPONENT\nEND_ACTOR\n";
    const auto old = SceneTransition::modelRefs("/Game/scenes/x.hscene", handMade);
    check(old.size() == 1 && old[0] == "/Game/models/model.glb", "옛 assets/ 경로도 정식 경로로");
}

} // namespace

int main() {
    std::printf("HarukaLoadingTest\n");
    const fs::path root = HARUKA_SOURCE_DIR;
    hvfs::reset();
    hvfs::mountDirectoryAs(root / "Projects/Samples/Content", "/Game/", 0);
    const fs::path tmp = fs::temp_directory_path() / "haruka_loading_test";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    GassetCache::setDirectory((tmp / "cache").string());
    testRanges();
    testStreamer();
    testModelRefs();
    fs::remove_all(tmp, ec);
    std::printf("\n결과: %s (검사 %d, 실패 %d)\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
