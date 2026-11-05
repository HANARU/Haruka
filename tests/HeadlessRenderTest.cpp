// 창 없는 렌더 경로(패키징 2단계)의 검산입니다. 창도 GPU 도 장치도 필요 없습니다.
//
// 이 테스트가 존재하는 이유: Quest 와 --headless 는 화면 표면 없이 XR 이미지에만 그립니다. 빈 스왑체인 · 이미지별 세마포어
// · 표면 없는 창의 약속이 깨지면 실기에서 첫 프레임에 멈추는데(DEF-118), PC 의 창 있는 실행으로는 그 경로를 타지 않습니다.
// 자동 확인에 쓰는 명령줄 해석과 종료 코드도 여기서 고정합니다.
//
// 실행: build/Release/HarukaHeadlessTest.exe

#include "../src/core/EngineOptions.hpp"
#include "../src/platform/android/AndroidWindow.hpp"
#include "../src/platform/headless/HeadlessWindow.hpp"
#include "../src/renderer/FrameResources.hpp"
#include "../src/renderer/ISwapchain.hpp"
#include "../src/renderer/PipelineCacheFile.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

EngineOptions parse(std::vector<std::string> args, std::vector<std::string>& unknown) {
    unknown.clear();
    return hopt::parse(args, unknown);
}

// 표면이 없는 창의 공통 약속.
void checkNoSurfaceWindow(IWindow& w, const char* name) {
    const std::string n = name;
    check(!w.providesSurface(), n + ": 표면을 주지 않음");
    check(w.createSurface(VK_NULL_HANDLE) == VK_NULL_HANDLE, n + ": createSurface 가 예외 없이 VK_NULL_HANDLE");
    check(w.getRequiredInstanceExtensions().empty(), n + ": 인스턴스 확장을 요구하지 않음");
    int fw = -1, fh = -1, ww = -1, wh = -1;
    w.getFramebufferSize(fw, fh);
    w.getWindowSize(ww, wh);
    check(fw == 0 && fh == 0 && ww == 0 && wh == 0, n + ": 크기 0x0(엔진은 최소화와 같은 경로)");
    check(!w.shouldClose(), n + ": 처음에는 닫히지 않음");
    w.setShouldClose(true);
    check(w.shouldClose(), n + ": setShouldClose(true) 뒤 닫힘");
    w.setShouldClose(false);
    w.setClipboardText("하루카 clipboard");
    check(w.getClipboardText() == "하루카 clipboard", n + ": 클립보드 왕복");
}

} // namespace

// AndroidWindow 가 IWindow 를 모두 구현합니다(DEF-103: 예전에는 클립보드 두 함수가 없어 추상 클래스였음).
static_assert(!std::is_abstract_v<AndroidWindow>, "AndroidWindow must implement IWindow");
static_assert(!std::is_abstract_v<HeadlessWindow>, "HeadlessWindow must implement IWindow");

int main() {
    std::printf("Haruka 창 없는 렌더 경로 검산\n");

    std::printf("\n== 1. 빈 스왑체인 ==\n");
    {
        NullSwapchain s(VK_FORMAT_R8G8B8A8_SRGB, { 64, 64 });
        auto same = [&]() {
            return s.imageCount() == 0 && s.images().empty() && s.imageViews().empty() && !s.hasArea() &&
                   !s.needsRecreation() && s.format() == VK_FORMAT_R8G8B8A8_SRGB && s.extent().width == 64 &&
                   s.extent().height == 64;
        };
        check(same(), "이미지 0장, 넓이 없음, 재생성 필요 없음, 명목 포맷 · 크기");
        s.create(); s.recreate(); s.destroy(); s.destroy(); s.create();
        check(same(), "create · recreate · destroy 를 여러 번 불러도 같은 상태");
        uint32_t idx = 77;
        check(s.acquireNextImage(VK_NULL_HANDLE, idx) == VK_ERROR_SURFACE_LOST_KHR && idx == 0,
              "acquireNextImage 는 VK_ERROR_SURFACE_LOST_KHR(불리면 렌더러가 예외로 알림)");
        check(s.present(VK_NULL_HANDLE, 0, VK_NULL_HANDLE) == VK_ERROR_SURFACE_LOST_KHR, "present 도 VK_ERROR_SURFACE_LOST_KHR");
    }

    std::printf("\n== 2. 이미지별 세마포어(DEF-118) ==\n");
    {
        FrameResources fr;   // 이미지 0장(창 없음)과 같은 상태
        check(fr.renderDoneSem(0) == VK_NULL_HANDLE && fr.renderDoneSem(5) == VK_NULL_HANDLE,
              "이미지가 없으면 renderDoneSem(0) · (5) 가 범위 밖 읽기 없이 VK_NULL_HANDLE");
    }

    std::printf("\n== 3. 표면 없는 창 ==\n");
    {
        HeadlessWindow h;
        h.init();
        checkNoSurfaceWindow(h, "HeadlessWindow");
        float dx = 1.f, dy = 1.f;
        h.getMouseDelta(dx, dy);
        check(dx == 0.f && dy == 0.f && !h.isKeyDown('W') && !h.isMouseButtonDown(0), "HeadlessWindow: 입력은 모두 비어 있음");
        AndroidWindow a;
        a.init();
        checkNoSurfaceWindow(a, "AndroidWindow");
    }

    std::printf("\n== 4. 명령줄 ==\n");
    {
        std::vector<std::string> u;
        EngineOptions o = parse({}, u);
        check(!o.headless && !o.syntheticEyes && o.frames == 0 && o.timeoutSec == 0.f && o.capturePath.empty() && u.empty(),
              "기본값: 창 있음, 합성 눈 없음, 끝없이 돎");

        o = parse({ "--headless" }, u);
        check(o.headless && u.empty(), "--headless");
        o = parse({ "--frames=120" }, u);
        check(o.frames == 120 && u.empty(), "--frames=120 → 120");
        for (const char* bad : { "--frames=0", "--frames=abc", "--frames=", "--frames=-3", "--frames=12x" }) {
            o = parse({ bad }, u);
            check(o.frames == 0 && u.size() == 1 && u[0] == bad, std::string(bad) + " → 모르는 인자로 경고, 값 0");
        }
        o = parse({ "--synthetic-eyes" }, u);
        check(o.syntheticEyes && o.syntheticEyeW == 512 && o.syntheticEyeH == 512 && u.empty(), "--synthetic-eyes → 512x512");
        o = parse({ "--synthetic-eyes=640x480" }, u);
        check(o.syntheticEyes && o.syntheticEyeW == 640 && o.syntheticEyeH == 480 && u.empty(),
              "--synthetic-eyes=640x480 → 640x480");
        for (const char* bad : { "--synthetic-eyes=0x10", "--synthetic-eyes=640", "--synthetic-eyes=9000x512",
                                 "--synthetic-eyes=x" }) {
            o = parse({ bad }, u);
            check(!o.syntheticEyes && u.size() == 1, std::string(bad) + " → 거부(합성 눈을 켜지 않음)");
        }
        o = parse({ "--timeout=5" }, u);
        check(o.timeoutSec == 5.f && u.empty(), "--timeout=5");
        o = parse({ "--timeout=2.5" }, u);
        check(o.timeoutSec == 2.5f && u.empty(), "--timeout=2.5");
        for (const char* bad : { "--timeout=0", "--timeout=-1", "--timeout=abc", "--timeout=" }) {
            o = parse({ bad }, u);
            check(o.timeoutSec == 0.f && u.size() == 1, std::string(bad) + " → 거부");
        }
        o = parse({ "--capture=a.png" }, u);
        check(o.capturePath == "a.png" && u.empty(), "--capture=a.png");
        o = parse({ "--capture=" }, u);
        check(o.capturePath.empty() && u.size() == 1, "--capture= → 거부");

        o = parse({ "--xr", "--emulate-stereo", "--loose" }, u);
        check(o.xr == true && o.emulateStereo && o.loose && u.empty(), "기존 --xr · --emulate-stereo · --loose 는 그대로");
        o = parse({ "--no-xr" }, u);
        check(o.xr == false && u.empty(), "기존 --no-xr 은 그대로");
        o = parse({ "--vk-no-sync2" }, u);
        check(o.vkNoSync2 && u.empty(), "--vk-no-sync2(1.0 배리어 경로)");
        o = parse({}, u);
        check(!o.vkNoSync2 && !o.vkDedicatedMsaa, "기본은 sync2 를 쓰고 MSAA 컬러는 보통 경로");
        o = parse({ "--vk-dedicated-msaa" }, u);
        check(o.vkDedicatedMsaa && u.empty(), "--vk-dedicated-msaa(별도 할당 경로)");
        o = parse({}, u);
        check(!o.ssao.has_value(), "SSAO 옵션이 없으면 프로젝트 설정을 따름");
        o = parse({ "--no-ssao" }, u);
        check(o.ssao == false && u.empty(), "--no-ssao(AO 끔 경로)");
        o = parse({ "--ssao" }, u);
        check(o.ssao == true && u.empty(), "--ssao(AO 켬)");
        o = parse({ "--headless", "--no-xr", "--synthetic-eyes", "--frames=120", "--capture=out/x.png", "--bogus" }, u);
        check(o.headless && o.xr == false && o.syntheticEyes && o.frames == 120 && o.capturePath == "out/x.png" &&
              u.size() == 1 && u[0] == "--bogus",
              "점검 명령 전체를 한 번에, 모르는 인자만 남김");
        o = parse({ "--headless=1", "--frames" }, u);
        check(!o.headless && o.frames == 0 && u.size() == 2, "값을 받지 않는 옵션에 값, 값이 필요한 옵션에 값 없음 → 모두 경고");
    }

    std::printf("\n== 5. 종료 코드 · 빈 화면 ==\n");
    {
        std::vector<std::string> u;
        check(!hopt::isAutomated(parse({}, u)) && !hopt::isAutomated(parse({ "--headless", "--synthetic-eyes" }, u)),
              "프레임 수 · 시간 · 캡처가 없으면 자동 실행이 아님(종료 코드 0)");
        check(hopt::isAutomated(parse({ "--frames=1" }, u)) && hopt::isAutomated(parse({ "--timeout=3" }, u)) &&
              hopt::isAutomated(parse({ "--capture=a.png" }, u)),
              "--frames · --timeout · --capture 중 하나면 자동 실행");
        check(hopt::headlessExitCode(120, 120, false, 0, false) == hopt::kExitOk, "다 그리고 오류 없음 → 0");
        check(hopt::headlessExitCode(0, 0, false, 0, false) == hopt::kExitOk, "프레임 수를 정하지 않음 → 0");
        check(hopt::headlessExitCode(119, 120, false, 0, false) == hopt::kExitTimeout, "덜 그림 → 2");
        check(hopt::headlessExitCode(120, 120, true, 0, false) == hopt::kExitTimeout, "시간 초과 → 2");
        check(hopt::headlessExitCode(120, 120, false, 3, false) == hopt::kExitValidation, "검증 오류 → 3");
        check(hopt::headlessExitCode(120, 120, false, 0, true) == hopt::kExitBlank, "빈 화면 → 4");
        check(hopt::headlessExitCode(10, 120, true, 3, true) == hopt::kExitTimeout, "여럿이면 시간 초과가 먼저");
        check(hopt::headlessExitCode(120, 120, false, 3, true) == hopt::kExitValidation, "검증 오류가 빈 화면보다 먼저");

        std::vector<uint8_t> img(100 * 100 * 4, 0);
        check(hopt::looksBlank(img), "모두 0 인 RGBA → 빈 화면");
        for (int i = 0; i < 100; ++i) img[(size_t)i * 4 + 1] = 200;   // 1% = 100 화소
        check(hopt::looksBlank(img), "밝은 화소가 정확히 1% 면 아직 빈 화면");
        img[100 * 4] = 200;
        check(!hopt::looksBlank(img), "1% 를 넘으면 빈 화면이 아님");
        std::vector<uint8_t> dim(100 * 100 * 4, 16);
        check(hopt::looksBlank(dim), "아주 어두운 화면(16 이하)도 빈 화면");
        check(hopt::looksBlank({}), "화소가 없으면 빈 화면");
    }

    // Android 는 프로세스가 언제든 끝날 수 있어, 저장 도중 잘린 캐시 파일이 다음 실행의 드라이버로 가면 안 됩니다.
    std::printf("\n== 6. 파이프라인 캐시 파일 (DEF-153) ==\n");
    {
        VkPhysicalDeviceProperties props{};
        props.vendorID = 0x5143;   // Qualcomm
        props.deviceID = 0x6060001;
        for (uint32_t i = 0; i < VK_UUID_SIZE; ++i) props.pipelineCacheUUID[i] = (uint8_t)(i * 7 + 1);
        // vkGetPipelineCacheData 가 주는 모양: VkPipelineCacheHeaderVersionOne + 드라이버 데이터.
        std::vector<uint8_t> vk(hpcache::kVkHeaderOneSize + 100, 0xAB);
        const uint32_t hs = (uint32_t)hpcache::kVkHeaderOneSize, hv = VK_PIPELINE_CACHE_HEADER_VERSION_ONE;
        std::memcpy(vk.data() + 0, &hs, 4);
        std::memcpy(vk.data() + 4, &hv, 4);
        std::memcpy(vk.data() + 8, &props.vendorID, 4);
        std::memcpy(vk.data() + 12, &props.deviceID, 4);
        std::memcpy(vk.data() + 16, props.pipelineCacheUUID, VK_UUID_SIZE);

        const std::vector<uint8_t> file = hpcache::wrap(vk.data(), vk.size());
        size_t off = 0, size = 0;
        std::string why;
        check(hpcache::unwrap(file, props, off, size, why) && size == vk.size() &&
              std::memcmp(file.data() + off, vk.data(), size) == 0, "감싼 파일을 풀면 같은 Vulkan 데이터");

        auto rejected = [&](std::vector<uint8_t> f, const VkPhysicalDeviceProperties& p) {
            std::string w;
            size_t o = 0, s = 0;
            return !hpcache::unwrap(f, p, o, s, w) && !w.empty();
        };
        std::vector<uint8_t> cut(file.begin(), file.end() - 1);
        check(rejected(cut, props), "끝이 잘린 파일(저장 도중 종료) → 버림");
        check(rejected(std::vector<uint8_t>(file.begin(), file.begin() + 10), props), "머리보다 짧은 파일 → 버림");
        std::vector<uint8_t> flip = file;
        flip[hpcache::kHeaderSize + 50] ^= 1;
        check(rejected(flip, props), "데이터 1비트가 바뀜 → 버림(해시)");
        check(rejected(vk, props), "자체 머리가 없는 옛 형식(예전 빌드가 쓴 파일) → 버림");
        VkPhysicalDeviceProperties other = props;
        other.pipelineCacheUUID[3] ^= 1;
        check(rejected(file, other), "드라이버가 바뀜(pipelineCacheUUID) → 버림");
        other = props;
        other.deviceID += 1;
        check(rejected(file, other), "다른 GPU(deviceID) → 버림");
        std::vector<uint8_t> badVer = vk;
        const uint32_t two = 2;
        std::memcpy(badVer.data() + 4, &two, 4);
        check(rejected(hpcache::wrap(badVer.data(), badVer.size()), props), "Vulkan 캐시 머리 버전이 1 이 아님 → 버림");
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
