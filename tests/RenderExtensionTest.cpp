// 패스 레지스트리와 렌더 확장 지점(DEF-17 · DEF-18)의 순서 검산입니다. Vulkan 을 호출하지 않고 창도 필요 없습니다.
//
// 이 테스트가 존재하는 이유: 확장이 기본 패스보다 먼저 크기 의존 자원을 놓지 않거나, 기본 패스가 확장보다 먼저
// 파괴되면 확장이 이미 사라진 HDR · 깊이 뷰를 쥐게 됩니다. 지점별 호출이 섞이면 다른 지점의 렌더 패스에서
// 파이프라인을 쓰게 됩니다. 둘 다 화면이 아니라 검증 레이어 오류나 장치 손실로 나타나므로 순서를 직접 봅니다.
//
// 실행: build/Release/HarukaRenderExtTest.exe

#include "../src/renderer/RenderExtensionRegistry.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

std::vector<std::string> g_log;

struct FakeBuiltin : HPass {
    std::string name;
    explicit FakeBuiltin(std::string n) : name(std::move(n)) {}
    void destroy() override              { g_log.push_back(name + ".destroy"); }
    void releaseSizeDependent() override { g_log.push_back(name + ".release"); }
    void acquireSizeDependent() override { g_log.push_back(name + ".acquire"); }
    void rebuildPipelines() override     { g_log.push_back(name + ".rebuild"); }
};

struct FakeExt : IRenderExtension {
    std::string name;
    uint32_t    mask;
    FakeExt(std::string n, uint32_t m) : name(std::move(n)), mask(m) {}
    uint32_t stages() const override { return mask; }
    void create(const HRenderDeviceInfo&) override {}
    void record(const HRenderStageContext& c) override {
        g_log.push_back(name + ".record" + std::to_string((uint32_t)c.stage) + "@" + std::to_string(c.viewIndex));
    }
    void destroy() override              { g_log.push_back(name + ".destroy"); }
    void releaseSizeDependent() override { g_log.push_back(name + ".release"); }
    void acquireSizeDependent() override { g_log.push_back(name + ".acquire"); }
    void rebuildPipelines() override     { g_log.push_back(name + ".rebuild"); }
};

std::string joined() {
    std::string s;
    for (const auto& l : g_log) { if (!s.empty()) s += ' '; s += l; }
    return s;
}

} // namespace

int main() {
    std::printf("Haruka 렌더 확장 지점 검산\n");

    FakeBuiltin shadow("shadow"), geometry("geometry"), post("post");
    RenderExtensionRegistry reg;
    reg.setBuiltins({ &shadow, &geometry, &post });

    auto sky  = std::make_shared<FakeExt>("sky",  stageBit(HRenderStage::OpaqueEnd));
    auto fog  = std::make_shared<FakeExt>("fog",  stageBit(HRenderStage::AfterScene) | stageBit(HRenderStage::OpaqueEnd));
    auto vign = std::make_shared<FakeExt>("vign", stageBit(HRenderStage::AfterTonemap));

    std::printf("\n== 1. 등록 ==\n");
    check(reg.add(sky) && reg.add(fog) && reg.add(vign), "세 확장 등록");
    check(!reg.add(sky), "같은 확장을 두 번 넣으면 거부");
    check(!reg.add(nullptr), "null 은 거부");
    check(reg.extensionCount() == 3, "확장 3개");

    std::printf("\n== 2. 지점별 호출 ==\n");
    check(reg.any(HRenderStage::OpaqueEnd) && reg.any(HRenderStage::AfterScene) &&
          reg.any(HRenderStage::AfterTonemap) && !reg.any(HRenderStage::TransparentEnd),
          "지점마다 등록 여부를 정확히 앎(반투명 끝은 없음)");
    {
        g_log.clear();
        HRenderStageContext c;
        c.stage = HRenderStage::OpaqueEnd; c.viewIndex = 1;
        const uint32_t n = reg.dispatch(c);
        check(n == 2 && joined() == "sky.record0@1 fog.record0@1", "불투명 끝은 sky → fog 순서로 그 눈 번호와 함께: " + joined());
        g_log.clear();
        c.stage = HRenderStage::TransparentEnd;
        check(reg.dispatch(c) == 0 && g_log.empty(), "등록되지 않은 지점은 아무도 부르지 않음");
        g_log.clear();
        c.stage = HRenderStage::AfterTonemap; c.viewIndex = 0;
        reg.dispatch(c);
        check(joined() == "vign.record3@0", "톤매핑 뒤는 vign 만: " + joined());
    }

    std::printf("\n== 3. 수명주기 순서 ==\n");
    {
        g_log.clear();
        reg.forEachForward([](HPass& p) { p.acquireSizeDependent(); });
        check(joined() == "shadow.acquire geometry.acquire post.acquire sky.acquire fog.acquire vign.acquire",
              "획득은 기본 패스(프레임 순서) → 확장(등록 순서): " + joined());
        g_log.clear();
        reg.forEachReverse([](HPass& p) { p.releaseSizeDependent(); });
        check(joined() == "vign.release fog.release sky.release post.release geometry.release shadow.release",
              "해제는 정확히 그 반대: " + joined());
        g_log.clear();
        reg.forEachForward([](HPass& p) { p.rebuildPipelines(); });
        check(g_log.size() == 6 && g_log.front() == "shadow.rebuild" && g_log.back() == "vign.rebuild",
              "핫 리로드는 기본 패스와 확장을 모두 다시 만듦");
    }

    std::printf("\n== 4. 해제 ==\n");
    {
        auto out = reg.remove(fog.get());
        check(out == fog && reg.extensionCount() == 2 && !reg.contains(fog.get()), "fog 를 빼서 돌려받음");
        check(reg.remove(fog.get()) == nullptr, "없는 것을 빼면 null");
        check(!reg.any(HRenderStage::AfterScene), "fog 만 쓰던 지점은 비게 됨");
        g_log.clear();
        HRenderStageContext c; c.stage = HRenderStage::OpaqueEnd;
        reg.dispatch(c);
        check(joined() == "sky.record0@0", "남은 확장만 부름: " + joined());
    }

    std::printf("\n== 5. 렌더러가 끝날 때 ==\n");
    {
        g_log.clear();
        for (auto& e : reg.takeExtensionsReversed()) e->destroy();
        reg.forEachReverse([](HPass& p) { p.destroy(); });
        check(joined() == "vign.destroy sky.destroy post.destroy geometry.destroy shadow.destroy",
              "확장(나중 것부터) → 기본 패스(뒤에서부터) 순서로 파괴: " + joined());
        check(reg.extensionCount() == 0, "확장 목록이 비어 다시 파괴되지 않음");
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
