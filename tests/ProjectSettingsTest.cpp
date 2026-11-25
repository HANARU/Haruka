// 프로젝트 설정 정본 표(ProjectSettingsSchema) · ini 한 줄 쓰기/지우기 단위 시험 (PS1a, 2026-11-24).
#include "../src/core/ProjectSettings.hpp"
#include "../src/core/ProjectSettingsSchema.hpp"
#include "../src/core/EditorPreferencesSchema.hpp"
#include "../src/core/ProjectDescriptor.hpp"
#include "../src/core/HText.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  [PASS] %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  [FAIL] %s\n", what.c_str()); }
}

static void testSchemaCoverage() {
    std::printf("schema coverage\n");
    for (const char* k : hps_schema::parseKnownScalarKeys()) {
        check(hps_schema::find(hps_schema::SettingsFile::ProjectIni, k) != nullptr,
              std::string("parse key in schema: ") + k);
    }
    for (const char* k : { "name", "company", "packageId", "engineVersion", "engine" }) {
        const auto* d = hps_schema::find(hps_schema::SettingsFile::HProject, k);
        check(d != nullptr, std::string(".hproject key in schema: ") + k);
        if (d) {
            const bool expectedRo = (std::string(k) == "engineVersion" || std::string(k) == "engine");
            check(d->readOnly == expectedRo, std::string(".hproject readOnly flag: ") + k);
        }
    }
}

static void testSchemaMatchesParse() {
    std::printf("schema matches ProjectSettings::parse\n");
    std::vector<std::string> msgs;
    const ProjectSettings def = project_settings::parse("", msgs, "");
    check(msgs.empty(), "empty project.ini parses with no messages");

    auto cmpFloat = [](double a, double b) { return std::fabs(a - b) < 1e-4; };

    check(hps_schema::find(hps_schema::SettingsFile::ProjectIni, "startScene")->defaultString == def.startScene,
          "default startScene matches");
    check(cmpFloat(hps_schema::find(hps_schema::SettingsFile::ProjectIni, "sim.tickRate")->defaultNumber, def.simTickRate),
          "default sim.tickRate matches");
    check(hps_schema::find(hps_schema::SettingsFile::ProjectIni, "render.ao")->defaultBool == def.renderAo,
          "default render.ao matches");
    check(hps_schema::find(hps_schema::SettingsFile::ProjectIni, "render.bloom")->defaultBool == def.renderBloom,
          "default render.bloom matches");
    check((int)hps_schema::find(hps_schema::SettingsFile::ProjectIni, "render.csmCascades")->defaultNumber == def.renderCsmCascades,
          "default render.csmCascades matches");
    check(cmpFloat(hps_schema::find(hps_schema::SettingsFile::ProjectIni, "render.shadowDistance")->defaultNumber, def.renderShadowDistance),
          "default render.shadowDistance matches");
    check(hps_schema::find(hps_schema::SettingsFile::ProjectIni, "render.contactShadows")->defaultBool == def.renderContactShadows,
          "default render.contactShadows matches");
    check(hps_schema::find(hps_schema::SettingsFile::ProjectIni, "render.heightFog")->defaultBool == def.renderHeightFog,
          "default render.heightFog matches");
    check(hps_schema::find(hps_schema::SettingsFile::ProjectIni, "render.reflectionCapture")->defaultBool == def.renderReflectionCapture,
          "default render.reflectionCapture matches");
    check((uint32_t)hps_schema::find(hps_schema::SettingsFile::ProjectIni, "render.lightLimit")->defaultNumber == def.renderLightLimit,
          "default render.lightLimit matches");
    check(hps_schema::find(hps_schema::SettingsFile::ProjectIni, "xr.startInVr")->defaultBool == def.xrStartInVr,
          "default xr.startInVr matches");
    check(std::string(hps_schema::find(hps_schema::SettingsFile::ProjectIni, "xr.height")->defaultString) ==
          (def.xrTrackedHeight ? "tracked" : "fixed"), "default xr.height matches");
    check(cmpFloat(hps_schema::find(hps_schema::SettingsFile::ProjectIni, "xr.eyeHeight")->defaultNumber, def.xrEyeHeight),
          "default xr.eyeHeight matches");

    // 범위 안 · 밖 값 검증
    auto checkRange = [&](const char* key, const char* inRange, const char* outLow, const char* outHigh) {
        std::vector<std::string> mIn, mLo, mHi;
        project_settings::parse(std::string(key) + " = " + inRange + "\n", mIn, "");
        project_settings::parse(std::string(key) + " = " + outLow + "\n", mLo, "");
        project_settings::parse(std::string(key) + " = " + outHigh + "\n", mHi, "");
        check(mIn.empty(), std::string("in-range accepted by parse: ") + key);
        check(!mLo.empty(), std::string("below-min rejected/clamped by parse: ") + key);
        check(!mHi.empty(), std::string("above-max rejected by parse: ") + key);
    };
    checkRange("sim.tickRate", "90", "30", "300");
    checkRange("render.csmCascades", "1", "0", "3");
    checkRange("render.shadowDistance", "50", "1", "500");
    checkRange("render.lightLimit", "128", "-1", "9999");
    checkRange("xr.eyeHeight", "1.6", "0.1", "5.0");

    // 플랫폼 덮어쓰기 허용: render.* 만 true
    for (const auto& d : hps_schema::entries()) {
        const bool isRender = std::string(d.key).rfind("render.", 0) == 0;
        check(d.allowPlatformOverride == isRender,
              std::string("allowPlatformOverride only on render.*: ") + d.key);
    }
}

static void testIniWriteAndRemove() {
    std::printf("setIniValue / removeIniKey\n");
    {
        const std::string in = "# startScene = /Game/Other.hscene\nstartScene = /Game/A.hscene\nrender.ao = true";
        const std::string out = project_settings::setIniValue(in, "startScene", "/Game/B.hscene");
        check(out == "# startScene = /Game/Other.hscene\nstartScene = /Game/B.hscene\nrender.ao = true",
              "setIniValue preserves comment with same key and last line without newline");
    }
    {
        const std::string in = "render.ao = true";
        const std::string out = project_settings::setIniValue(in, "render.ao.android", "false");
        check(out == "render.ao = true\nrender.ao.android = false\n",
              "setIniValue appends missing .android key when last line lacks newline");
        const std::string removed = project_settings::removeIniKey(out, "render.ao.android");
        check(removed == "render.ao = true\n",
              "removeIniKey removes .android key while preserving base key");
    }
    {
        const std::string in = "# comment\r\nrender.bloom = true\r\nrender.bloom = false\r\n";
        const std::string out = project_settings::setIniValue(in, "render.bloom", "true");
        check(out == "# comment\r\nrender.bloom = true\r\nrender.bloom = true\r\n",
              "setIniValue updates duplicate keys and preserves CRLF");
        const std::string removed = project_settings::removeIniKey(out, "render.bloom");
        check(removed == "# comment\r\n",
              "removeIniKey removes all matching active lines and preserves comments");
    }
    {
        // .hproject 형식 호환 확인
        const std::string hp = "name = OldApp\ncompany = Haruka\npackageId = com.haruka.app\nengineVersion = 0.1.0\n";
        const std::string hp2 = project_settings::setIniValue(hp, "name", "NewApp");
        std::vector<std::string> w, e;
        const ProjectDescriptor d = project_descriptor::parse(hp2, w, e);
        check(w.empty() && e.empty() && d.name == "NewApp" && d.company == "Haruka",
              "setIniValue works on .hproject format");
    }
    {
        ProjectSettings a, b;
        ProjectDescriptor da, db;
        check(!hps_schema::hasRestartChanges(a, b, da, db), "hasRestartChanges false when unchanged");
        a.renderAo = false;   // Live 항목
        check(!hps_schema::hasRestartChanges(a, b, da, db), "hasRestartChanges false for Live setting render.ao");
        a.renderShadowDistance = 35.f;   // Restart 항목
        check(hps_schema::hasRestartChanges(a, b, da, db), "hasRestartChanges true for Restart setting render.shadowDistance");
        check(hps_schema::androidKey("render.bloom") == "render.bloom.android", "androidKey appends .android suffix");
    }
}

static void testEditorPreferencesSchema() {
    std::printf("editor preferences schema\n");
    const auto& defs = heprefs::entries();
    check(defs.size() == 5, "EditorPreferencesSchema has 5 entries");
    for (const heprefs::SettingDef& d : defs) {
        check(d.file == heprefs::PrefFile::EditorState,
              std::string("storage file is EditorState (not project.ini) for ") + d.key);
        check(std::string(heprefs::storageFile(d.file)) == "Saved/Config/editor_state.txt",
              std::string("storagePath is Saved/Config/editor_state.txt for ") + d.key);
    }

    // 기본값이 parseEditorState("") 및 EditorStateData 기본값과 일치하는지 확인
    std::vector<std::string> w;
    const heprefs::EditorStateData defState = heprefs::parseEditorState("", w);
    check(w.empty(), "empty editor_state.txt parses with no warnings");
    for (const heprefs::SettingDef& d : defs) {
        const std::string k = d.key;
        if (k == "culture")
            check(defState.culture == d.defaultString, "culture schema default matches EditorStateData");
        else if (k == "gizmoSize")
            check(defState.gizmoSize == d.defaultString &&
                  std::string(heprefs::gizmoNameFromFactor(heprefs::gizmoFactorFromName(d.defaultString))) == d.defaultString,
                  "gizmoSize schema default matches EditorStateData and factor round-trip");
        else if (k == "audioOutput")
            check(defState.audioOutput == d.defaultString, "audioOutput schema default matches EditorStateData");
        else if (k == "audioHeadsetInVr")
            check(defState.audioHeadsetInVr == d.defaultBool, "audioHeadsetInVr schema default matches EditorStateData");
        else if (k == "audioHrtf")
            check(defState.audioHrtf == d.defaultString, "audioHrtf schema default matches EditorStateData");
    }

    // 열거형 범위 · 왕복 확인
    heprefs::EditorStateData s;
    s.lastScene        = "/Game/scenes/Test.hscene";
    s.culture          = "ko";
    s.gizmoSize        = "large";
    s.audioOutput      = "dev-1";
    s.audioHeadsetInVr = false;
    s.audioHrtf        = "on";
    const std::string txt = heprefs::formatEditorState(s);
    const heprefs::EditorStateData rt = heprefs::parseEditorState(txt, w);
    check(w.empty() && rt.lastScene == s.lastScene && rt.culture == s.culture &&
          rt.gizmoSize == s.gizmoSize && rt.audioOutput == s.audioOutput &&
          rt.audioHeadsetInVr == s.audioHeadsetInVr && rt.audioHrtf == s.audioHrtf,
          "editor_state.txt format/parse round-trip preserves all keys");

    // 잘못된 값 경고 및 기본값 복귀
    std::vector<std::string> badW;
    const heprefs::EditorStateData bad = heprefs::parseEditorState("gizmoSize=huge\naudioHrtf=bogus\n", badW);
    check(badW.size() == 2 && bad.gizmoSize == "medium" && bad.audioHrtf == "auto",
          "invalid gizmoSize/audioHrtf warn and fall back to defaults");
}

int main() {
    testSchemaCoverage();
    testSchemaMatchesParse();
    testIniWriteAndRemove();
    testEditorPreferencesSchema();
    std::printf("ProjectSettingsTest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
