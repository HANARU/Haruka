// 씬 문서(2026-10-15)를 화면 없이 검산합니다.
//
// 이 테스트가 존재하는 이유: 예전 에디터는 실행 폴더의 scene.hscene 하나에만 저장했고, 지금 열린 파일도, 저장하지
// 않은 변경이 있는지도 몰랐습니다. 이제 저장 여부를 "저장한 순간의 Undo 상태 표지"로 판정하므로, 되돌려서 저장
// 시점으로 돌아오면 깨끗해지고, 되돌린 뒤 다른 편집을 쌓으면 같은 개수라도 바뀐 것으로 봐야 합니다. Play 동안의
// 편집은 씬을 바꾸지 않습니다. 저장은 임시 파일에 쓴 뒤 바꿔치기해 실패해도 원본이 남아야 합니다.
// 실행: build/Release/HarukaSceneDocTest.exe
#include "../src/editor/HCommandStack.hpp"
#include "../src/editor/SceneDocument.hpp"
#include "../src/core/ProjectSettings.hpp"
#include "../src/core/FileUtil.hpp"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

struct NopCmd : ICommand {
    int* counter;
    explicit NopCmd(int* c) : counter(c) {}
    void undo() override { --*counter; }
    void redo() override { ++*counter; }
};

void push(HCommandStack& st, int& value) { ++value; st.push(std::make_unique<NopCmd>(&value)); }

} // namespace

int main() {
    std::printf("Haruka 씬 문서 검산\n");

    std::printf("\n== 1. Undo 상태 표지 ==\n");
    {
        HCommandStack st;
        int v = 0;
        const uint64_t t0 = st.stateToken();
        push(st, v);
        const uint64_t tA = st.stateToken();
        check(tA != t0, "쌓으면 표지가 바뀜");
        st.undo();
        check(st.stateToken() == t0, "되돌리면 처음 표지로 돌아옴");
        st.redo();
        check(st.stateToken() == tA, "다시 하면 같은 표지");
        st.undo();
        push(st, v);
        check(st.stateToken() != tA && st.stateToken() != t0, "되돌린 뒤 다른 편집을 쌓으면 개수가 같아도 다른 표지");
        const uint64_t before = st.stateToken();
        st.clear();
        check(st.stateToken() != before && st.stateToken() != t0 && st.stateToken() != tA, "이력을 비우면 새 표지");
    }
    {
        HCommandStack st;
        int v = 0;
        const uint64_t t0 = st.stateToken();
        for (int i = 0; i < 65; ++i) push(st, v);
        for (int i = 0; i < 70; ++i) st.undo();
        check(v == 1, "65개를 쌓으면 가장 오래된 하나는 되돌릴 수 없음(상한 64)");
        check(st.stateToken() != t0, "밀려난 편집이 남아 있으므로 다 되돌려도 처음 표지가 아님");
    }

    std::printf("\n== 2. 씬 문서의 변경 판정 ==\n");
    {
        HCommandStack st;
        SceneDocument doc;
        int v = 0;
        doc.reset("assets/scenes/A.hscene", st.stateToken());
        check(!doc.dirty(st.stateToken()), "연 직후에는 깨끗함");
        push(st, v);
        check(doc.dirty(st.stateToken()), "편집하면 바뀜");
        st.undo();
        check(!doc.dirty(st.stateToken()), "되돌려 연 시점으로 오면 다시 깨끗함");
        push(st, v);
        push(st, v);
        doc.markSaved(doc.path(), st.stateToken());
        check(!doc.dirty(st.stateToken()), "저장하면 깨끗함");
        st.undo();
        check(doc.dirty(st.stateToken()), "저장 뒤 되돌리면 저장한 내용과 달라 바뀜");
        st.redo();
        check(!doc.dirty(st.stateToken()), "다시 하면 저장한 상태라 깨끗함");
        doc.markDirty();
        check(doc.dirty(st.stateToken()), "이력에 남지 않는 편집은 표시로 바뀜");
        doc.markSaved(doc.path(), st.stateToken());
        check(!doc.dirty(st.stateToken()), "저장하면 그 표시도 지워짐");
        doc.markSaved("assets/scenes/sub/B.hscene", st.stateToken());
        check(doc.path() == "assets/scenes/sub/B.hscene" && doc.displayName() == "B.hscene",
              "다른 이름으로 저장하면 경로와 표시 이름이 바뀜(폴더 제외)");
    }
    {
        HCommandStack st;
        SceneDocument doc;
        int v = 0;
        doc.reset("", st.stateToken());
        check(doc.untitled() && doc.displayName() == "Untitled", "새 씬은 이름 없음");

        // 깨끗한 상태로 Play → 편집 → Stop(월드 복원 + 이력 비움)
        doc.beginPlay(st.stateToken());
        push(st, v);
        doc.markDirty();
        check(!doc.dirty(st.stateToken()), "Play 중의 편집과 표시는 씬을 바꾸지 않음");
        st.clear();
        doc.endPlay(st.stateToken());
        check(!doc.dirty(st.stateToken()), "Stop 뒤에도 Play 전처럼 깨끗함");

        // 바뀐 상태로 Play → Stop
        push(st, v);
        check(doc.dirty(st.stateToken()), "편집하면 바뀜");
        doc.beginPlay(st.stateToken());
        st.clear();
        doc.endPlay(st.stateToken());
        check(doc.dirty(st.stateToken()), "Stop 이 이력을 비워도 Play 전의 변경은 남음");
    }

    std::printf("\n== 3. 프로젝트 설정(assets/project.ini) ==\n");
    {
        std::vector<std::string> msg;
        ProjectSettings d = project_settings::parse("", msg);
        check(d.startScene == "assets/scenes/LightingProbe.hscene" && msg.empty(), "빈 파일이면 기본 시작 씬");

        msg.clear();
        ProjectSettings p = project_settings::parse(
            "# comment\r\n\r\nstartScene = assets/scenes/Levels/Base.hscene\r\n"
            "input.action.Grab = xr:right.squeeze\r\ninput.axis.MoveY = key:W/S\r\n", msg);
        check(p.startScene == "assets/scenes/Levels/Base.hscene" && msg.empty(), "시작 씬 · 주석 · 빈 줄 · CRLF");
        check(p.input.size() == 2 && p.input[0].first == "input.action.Grab" && p.input[1].second == "key:W/S",
              "input. 줄은 순서대로 입력 시스템에 넘김");

        const char* bad[] = { "startScene = scenes/A.hscene", "startScene = assets/scenes/A.txt",
                              "startScene = assets/../x.hscene", "startScene = assets\\scenes\\A.hscene" };
        bool allRejected = true;
        for (const char* b : bad) {
            msg.clear();
            ProjectSettings r = project_settings::parse(b, msg);
            allRejected = allRejected && r.startScene == "assets/scenes/LightingProbe.hscene" && msg.size() == 1;
        }
        check(allRejected, "assets/ 밖 · 다른 확장자 · .. · 백슬래시 경로는 거부하고 기본값");

        msg.clear();
        project_settings::parse("nonsense\nfoo = 1\n", msg);
        check(msg.size() == 2, "= 없는 줄과 모르는 키는 경고");

        // XR 리그 높이(2026-10-16): 보이지 않는 리그가 쓰는 값
        check(d.xrTrackedHeight && d.xrEyeHeight == 1.7f, "XR 리그 높이 기본값: 실제 키, 눈높이 1.7m");
        msg.clear();
        ProjectSettings x = project_settings::parse("xr.height = fixed\nxr.eyeHeight = 1.2\n", msg);
        check(!x.xrTrackedHeight && x.xrEyeHeight == 1.2f && msg.empty(), "xr.height = fixed, xr.eyeHeight = 1.2");
        const char* badXr[] = { "xr.height = standing", "xr.eyeHeight = 3", "xr.eyeHeight = 0.2", "xr.eyeHeight = 1.7m",
                                "xr.eyeHeight =" };
        bool xrRejected = true;
        for (const char* b : badXr) {
            msg.clear();
            ProjectSettings r = project_settings::parse(b, msg);
            xrRejected = xrRejected && r.xrTrackedHeight && r.xrEyeHeight == 1.7f && msg.size() == 1;
        }
        check(xrRejected, "모르는 높이 모드 · 범위 밖(0.5 ~ 2.5) · 단위 붙은 값 · 빈 값은 경고하고 기본값");

        // 게임 실행 파일의 VR 시작(2026-10-19): 기본은 끔, true · false 만 받음
        check(!d.xrStartInVr, "xr.startInVr 기본값: 끔");
        msg.clear();
        ProjectSettings v = project_settings::parse("xr.startInVr = true\n", msg);
        check(v.xrStartInVr && msg.empty(), "xr.startInVr = true");
        msg.clear();
        ProjectSettings vb = project_settings::parse("xr.startInVr = yes\n", msg);
        check(!vb.xrStartInVr && msg.size() == 1, "xr.startInVr 의 true · false 밖 값은 경고하고 끔");

        // 후처리 패스 켬/끔(2026-11-01): 기본은 켬, true · false 만 받음
        check(d.renderAo && d.renderBloom, "render.ao · render.bloom 기본값: 켬");
        msg.clear();
        ProjectSettings pf = project_settings::parse("render.ao = false\nrender.bloom = false\n", msg);
        check(!pf.renderAo && !pf.renderBloom && msg.empty(), "render.ao = false · render.bloom = false");
        msg.clear();
        ProjectSettings pb = project_settings::parse("render.ao = off\n", msg);
        check(pb.renderAo && pb.renderBloom && msg.size() == 1, "render.ao 의 true · false 밖 값은 경고하고 켬");
    }

    std::printf("\n== 4. 원자적 저장 ==\n");
    {
        const fs::path dir = fs::temp_directory_path() / "haruka_scene_doc_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        const fs::path f = dir / "scenes" / "A.hscene";

        std::string err, back;
        check(hfile::writeAtomic(f, "first\n", &err) && hfile::readAll(f, back) && back == "first\n",
              "새 파일(폴더도 만듦)");
        check(hfile::writeAtomic(f, "second\n", &err) && hfile::readAll(f, back) && back == "second\n",
              "있는 파일을 바꿈");
        fs::path tmp = f;
        tmp += ".tmp";
        check(!fs::exists(tmp, ec), "임시 파일이 남지 않음");

        // 실패: 같은 이름의 폴더가 있어 바꿀 수 없는 경우. 원래 것은 그대로, 임시 파일은 지움.
        const fs::path blocked = dir / "blocked.hscene";
        fs::create_directories(blocked / "keep");
        const bool ok = hfile::writeAtomic(blocked, "x", &err);
        fs::path btmp = blocked;
        btmp += ".tmp";
        check(!ok && !err.empty(), "바꿀 수 없으면 실패를 알림");
        check(fs::is_directory(blocked / "keep", ec) && !fs::exists(btmp, ec), "실패해도 원래 것은 그대로, 임시 파일 없음");

        // 바이너리판(DEF-153): 0x0A 가 CRLF 로 바뀌지 않음.
        const fs::path bin = dir / "cache.bin";
        const std::string raw("\x01\n\x02\r\n\x00\xFF", 7);
        std::string rb;
        bool binOk = hfile::writeAtomicBinary(bin, raw.data(), raw.size(), &err);
        {
            std::ifstream bf(bin, std::ios::binary);
            rb.assign(std::istreambuf_iterator<char>(bf), std::istreambuf_iterator<char>());
        }
        fs::path bintmp = bin;
        bintmp += ".tmp";
        check(binOk && rb == raw && !fs::exists(bintmp, ec), "바이너리 원자적 저장은 바이트를 그대로 씀(0x0A · NUL 포함)");

        fs::remove_all(dir, ec);
    }

    std::printf("\n== 5. 손으로 만든 설정 파일의 인코딩 (DEF-152) ==\n");
    {
        auto dec = [](std::string s, bool& ok, std::string* why = nullptr) { ok = hfile::decodeTextBytes(s, why); return s; };
        bool ok = false;
        check(dec("--frames=600\n", ok) == "--frames=600\n" && ok, "BOM 없는 UTF-8 은 그대로");
        check(dec("\xEF\xBB\xBF--frames=600", ok) == "--frames=600" && ok, "UTF-8 BOM 을 지움(PowerShell -Encoding utf8)");
        const std::string le("\xFF\xFE-\0-\0f\0=\0" "6\0\r\0\n\0", 16);
        check(dec(le, ok) == "--f=6\r\n" && ok, "BOM 이 있는 UTF-16LE 를 UTF-8 로(PowerShell 5.1 의 >)");
        const std::string be("\xFE\xFF\0a\0b", 6);
        check(dec(be, ok) == "ab" && ok, "BOM 이 있는 UTF-16BE");
        const std::string ko("\xFF\xFE\x5C\xD5\x00\xAE", 6);   // U+D55C U+AE00 "한글"
        check(dec(ko, ok) == "\xED\x95\x9C\xEA\xB8\x80" && ok, "UTF-16 한글 → UTF-8 3바이트씩");
        const std::string pair("\xFF\xFE\x3D\xD8\x00\xDE", 6);  // U+1F600 서로게이트 쌍
        check(dec(pair, ok) == "\xF0\x9F\x98\x80" && ok, "서로게이트 쌍 → UTF-8 4바이트");
        const std::string lone("\xFF\xFE\x3D\xD8" "a\0", 6);    // 짝 없는 상위 서로게이트 뒤 'a'
        check(dec(lone, ok) == "\xEF\xBF\xBD" "a" && ok, "짝 없는 서로게이트는 U+FFFD");
        std::string why;
        dec(std::string("\xFF\xFE" "a", 3), ok, &why);
        check(!ok && !why.empty(), "홀수 바이트 UTF-16 은 거부하고 이유를 알림");
        why.clear();
        dec(std::string("-\0-\0", 4), ok, &why);
        check(!ok && !why.empty(), "BOM 없이 NUL 이 있으면 거부하고 이유를 알림(BOM 없는 UTF-16 등)");
        why.clear();
        dec(std::string("\xFF\xFE\0\0-\0\0\0", 8), ok, &why);
        check(!ok && why.find("UTF-32") != std::string::npos, "UTF-32LE(BOM FF FE 00 00)는 UTF-16 으로 읽지 않고 거부");
        why.clear();
        dec(std::string("\xFF\xFE" "a\0\0\0b\0", 8), ok, &why);
        check(!ok && !why.empty(), "UTF-16 으로 바꾼 글에 U+0000 이 있으면 거부");

        const fs::path dir = fs::temp_directory_path() / "haruka_config_text_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        std::string text;
        why.clear();
        check(!hfile::readConfigText(dir / "missing.txt", text, &why) && why.empty(), "없는 파일은 false, 이유 없음");
        {
            std::ofstream f(dir / "args.txt", std::ios::binary);
            f.write(le.data(), (std::streamsize)le.size());
        }
        check(hfile::readConfigText(dir / "args.txt", text, &why) && text == "--f=6\r\n", "UTF-16LE 파일을 읽어 UTF-8 로");
        fs::remove_all(dir, ec);
    }

    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
