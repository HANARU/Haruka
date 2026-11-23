// B2 품질 시험: (1) 등록된 모든 컴포넌트의 속성 기본값(withDefault)과 생성자 기본값 자동 대조,
// (2) 샘플 씬 전체(.hscene) 2차 저장 왕복(읽기 -> 쓰기 -> 읽기 -> 쓰기) 바이트 일치 검산.
//
// 실행: build/Release/HarukaSceneQualityTest.exe

#include "../src/assets/AssetManager.hpp"
#include "../src/components/CoreComponents.hpp"
#include "../src/components/XrComponents.hpp"
#include "../src/core/ProjectPaths.hpp"
#include "../src/scene/Actor.hpp"
#include "../src/scene/ComponentRegistry.hpp"
#include "../src/scene/Reflect.hpp"
#include "../src/scene/World.hpp"
#include "../src/sceneio/SceneSerializer.hpp"
#include "../src/vfs/Vfs.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (cond) ++g_passed; else ++g_failed;
}

std::string readBytes(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

std::string defaultValueSummary(const PropDesc& pd) {
    switch (pd.kind) {
        case PropDesc::Float:    return std::to_string(pd.defF);
        case PropDesc::Toggle:   return pd.defB ? "True" : "False";
        case PropDesc::Enum:
            if (pd.defI >= 0 && pd.defI < (int)pd.options.size() && pd.options[pd.defI])
                return std::string(pd.options[pd.defI]) + " (" + std::to_string(pd.defI) + ")";
            return std::to_string(pd.defI);
        case PropDesc::ActorRef: return std::to_string(pd.defG);
        case PropDesc::String:
        case PropDesc::AssetPath: return "'" + pd.defS + "'";
        case PropDesc::Vec3:
            return "(" + std::to_string(pd.defV.x) + "," + std::to_string(pd.defV.y) + "," + std::to_string(pd.defV.z) + ")";
        case PropDesc::Color:
            return "(" + std::to_string(pd.defV.x) + "," + std::to_string(pd.defV.y) + "," + std::to_string(pd.defV.z) +
                   (pd.components >= 4 ? ("," + std::to_string(pd.defV.w)) : "") + ")";
    }
    return "?";
}

void testPropDefaults() {
    std::printf("\n== 1. 컴포넌트 속성 기본값 자동 대조 ==\n");
    const auto& entries = ComponentRegistry::get().entries();
    check(!entries.empty(), "ComponentRegistry 가 비어 있지 않음 (" + std::to_string(entries.size()) + "종)");

    int compChecked = 0;
    int propChecked = 0;
    for (const auto& e : entries) {
        World w;
        Actor* a = w.spawnActor(e.name);
        Component* comp = e.add ? e.add(*a) : nullptr;
        check(comp != nullptr, e.name + " 생성 · 부착");
        if (!comp) continue;
        ++compChecked;

        const std::vector<PropDesc> props = comp->buildProps();
        int hasDefCount = 0;
        for (const PropDesc& pd : props) {
            if (!pd.hasDefault) continue;
            ++hasDefCount;
            ++propChecked;
            const bool ok = hprop::isDefault(pd);
            std::string msg = e.name + "." + (pd.label ? pd.label : "");
            if (!ok) {
                msg += " (actual=" + hprop::valueText(pd) + ", withDefault=" + defaultValueSummary(pd) + ")";
            }
            check(ok, msg);
        }
        std::printf("    (%s: %zu props, %d withDefault)\n", e.name.c_str(), props.size(), hasDefCount);
    }
    std::printf("  요약: 컴포넌트 %d종, 기본값 속성 %d개 검사\n", compChecked, propChecked);
}

void describeFirstDiff(const std::string& a, const std::string& b, std::string& out) {
    std::istringstream ia(a), ib(b);
    std::string la, lb;
    int line = 1;
    while (true) {
        const bool ga = static_cast<bool>(std::getline(ia, la));
        const bool gb = static_cast<bool>(std::getline(ib, lb));
        if (!ga && !gb) break;
        if (ga != gb || la != lb) {
            out = "line " + std::to_string(line) + ":\n      1st: " + (ga ? la : "<EOF>") +
                  "\n      2nd: " + (gb ? lb : "<EOF>");
            return;
        }
        ++line;
    }
    out = "byte length diff (" + std::to_string(a.size()) + " vs " + std::to_string(b.size()) + ")";
}

void testSampleSceneRoundTrip() {
    std::printf("\n== 2. 샘플 씬 저장 2차 왕복 ==\n");
    const fs::path savedRoot = hproj::root();
    hproj::setRoot(HARUKA_SAMPLES_DIR);
    hvfs::mountDirectoryAs(fs::path(HARUKA_SAMPLES_DIR) / "Content", "/Game/", 0);

    AssetManager assets(nullptr);
    const fs::path scenesDir = fs::path(HARUKA_SAMPLES_DIR) / "Content" / "scenes";
    std::vector<fs::path> scenes;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(scenesDir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".hscene")
            scenes.push_back(entry.path());
    }
    std::sort(scenes.begin(), scenes.end());
    check(!scenes.empty(), "샘플 씬 목록 확보 (" + std::to_string(scenes.size()) + "개)");

    for (const fs::path& p : scenes) {
        const std::string name = p.filename().string();
        const std::string orig = readBytes(p);
        check(!orig.empty(), name + " 읽기");
        if (orig.empty()) continue;

        World w1;
        const auto spawned1 = SceneSerializer::readScene(orig, w1, assets);
        const std::string pass1 = SceneSerializer::writeScene(w1);

        World w2;
        const auto spawned2 = SceneSerializer::readScene(pass1, w2, assets);
        const std::string pass2 = SceneSerializer::writeScene(w2);

        const bool same12 = (pass1 == pass2);
        std::string msg = name + " 1차==2차 (" + std::to_string(spawned1.size()) + " actors, " +
                          std::to_string(pass1.size()) + " bytes, orig==1차: " +
                          (orig == pass1 ? "yes" : "no") + ")";
        if (!same12) {
            std::string diff;
            describeFirstDiff(pass1, pass2, diff);
            msg += " -> " + diff;
        }
        check(same12, msg);
        (void)spawned2;
    }

    hproj::setRoot(savedRoot);
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("Haruka B2 품질 검산 (속성 기본값 · 샘플 씬 2차 왕복)\n");
    registerCoreComponents();
    registerXrComponents();

    testPropDefaults();
    testSampleSceneRoundTrip();

    std::printf("\n%s (%d passed, %d failed)\n", g_failed ? "FAILED" : "ALL PASSED", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
