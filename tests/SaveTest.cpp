// 저장 게임 · 사용자 설정 시험(2026-11-20, 정본 Docs/Save_And_Settings.md 3절 S1 ~ S3).
// 저장 파일 형식(CRC · 버전 · 형이 있는 값), 슬롯(원자적 쓰기 · 이전 저장본 · 목록), 씬 GUID 참조 왕복, 사용자 설정 층(형 · 범위 ·
// 바꾼 값만 쓰기 · 모르는 이름 보존), 편안함 설정의 월드 서비스를 창 · GPU 없이 검산합니다. 파일은 임시 폴더에만 씁니다.

#include "../src/assets/AssetManager.hpp"
#include "../src/core/FileUtil.hpp"
#include "../src/core/Guid.hpp"
#include "../src/core/SaveGame.hpp"
#include "../src/core/UserSettings.hpp"
#include "../src/scene/ActorRef.hpp"
#include "../src/scene/World.hpp"
#include "../src/sceneio/SceneSerializer.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failed = 0;
int g_passed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (cond) ++g_passed;
    else ++g_failed;
}

std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
void writeFile(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}

SaveData sample() {
    SaveData d;
    d.dataVersion = 3;
    d.title       = "Chapter 2\nThe dock";
    d.setInt("player.coins", -1234567890123LL);
    d.setFloat("player.health", 0.1f + 0.2f);
    d.setFloat("player.speed", 1.2345678f);
    d.setBool("flags.door_open", true);
    d.setString("note", "a=b \\ c\r\nline two");
    d.setVec3("player.pos", glm::vec3(1.5f, -0.02f, 1e-7f));
    d.setGuid("player.target", HGuid{ 0x00ab00cd00ef0012ULL });
    return d;
}

// ── S1 저장 파일 ────────────────────────────────────────────────────────────────
void testFormat() {
    std::printf("\n== S1 format ==\n");
    const SaveData d = sample();
    const std::string text = hsave::serialize(d);
    check(text.rfind("# Haruka Save v1\n", 0) == 0, "머리 줄 v1");
    check(text.find("\n@crc=") != std::string::npos && text.back() == '\n', "마지막 줄 @crc");
    SaveData r;
    std::string why;
    check(hsave::parse(text, r, &why), "읽음 " + why);
    check(r.dataVersion == 3 && r.title == d.title, "메타(데이터 버전 · 줄바꿈이 든 제목)");
    check(r.getInt("player.coins") == -1234567890123LL, "int64");
    const float h = 0.1f + 0.2f, sp = 1.2345678f;
    check(r.getFloat("player.health") == h && r.getFloat("player.speed") == sp, "float 비트 같음");
    check(r.getBool("flags.door_open") == true, "bool");
    check(r.getString("note") == "a=b \\ c\r\nline two", "역슬래시 · 줄바꿈 · = 가 든 문자열");
    check(r.getVec3("player.pos") == glm::vec3(1.5f, -0.02f, 1e-7f), "vec3");
    check(r.getGuid("player.target").value == 0x00ab00cd00ef0012ULL, "GUID(actor:<hex>)");
    check(r.values() == d.values(), "값 전체가 같음");
    check(hsave::serialize(r) == text, "다시 쓰면 같은 글");
    check(text.find("player.target=actor:00ab00cd00ef0012\n") != std::string::npos, "참조는 HActorRef 와 같은 글");

    // S1-2 손상
    for (size_t at : { (size_t)20, text.size() / 2, text.size() - 4 }) {
        std::string bad = text;
        bad[at] = bad[at] == 'x' ? 'y' : 'x';
        check(!hsave::parse(bad, r, &why) && r.values().empty(), "바이트 하나 바꿈(" + std::to_string(at) + ") 거절: " + why);
    }
    check(!hsave::parse(text.substr(0, text.size() / 2), r, &why), "반만 쓴 파일 거절: " + why);
    check(!hsave::parse(text.substr(0, text.rfind("@crc=")), r, &why), "CRC 줄 없는 파일 거절: " + why);
    check(!hsave::parse("", r, &why) && !hsave::parse("hello", r, &why), "빈 글 · 다른 파일 거절");
    std::string noNl = text;
    noNl.pop_back();
    check(hsave::parse(noNl, r, &why), "끝 줄바꿈이 없어도 읽음");

    // S1-3 버전
    std::string newer = "# Haruka Save v2\n@dataVersion=1\n";
    char crc[16];
    std::snprintf(crc, sizeof(crc), "%08x", hsave::crc32(newer.data(), newer.size()));
    newer += std::string("@crc=") + crc + "\n";
    check(!hsave::parse(newer, r, &why) && why.find("newer") != std::string::npos, "더 새 형식 버전 거절: " + why);
    SaveData old;
    old.dataVersion = 1;
    old.setInt("player.coins", 5);
    check(hsave::parse(hsave::serialize(old), r, &why) && r.dataVersion == 1, "옛 데이터 버전을 읽고 dataVersion = 1");
    check(r.getInt("player.coins", 0) == 5 && r.getFloat("player.health", 1.f) == 1.f && !r.has("player.health"),
          "없는 필드는 기본값");
    std::string extraMeta = "# Haruka Save v1\n@future=1\nk=v\n";
    std::snprintf(crc, sizeof(crc), "%08x", hsave::crc32(extraMeta.data(), extraMeta.size()));
    extraMeta += std::string("@crc=") + crc + "\n";
    check(hsave::parse(extraMeta, r, &why) && r.getString("k") == "v", "모르는 메타 줄은 건너뜀");

    // S1-4 형이 틀린 값
    SaveData t;
    t.setString("n", "abc");
    t.setString("v", "1,2");
    t.setString("g", "00000000000000aa");
    check(t.getInt("n", 7) == 7 && t.getFloat("n", 2.f) == 2.f && t.getBool("n", true) == true, "형이 틀린 값은 기본값");
    check(t.getVec3("v", glm::vec3(9.f)) == glm::vec3(9.f) && !t.getGuid("g").valid(), "vec3 · GUID 도 기본값");
    check(!t.setInt("bad key", 1) && !t.setInt("", 1) && !t.setInt("a=b", 1) && !t.setInt("@x", 1) && !t.has("bad key"), "틀린 이름 거절");
    check(!t.setFloat("nan", std::nanf("")) && !t.has("nan"), "NaN 은 쓰지 않음");
}

// ── S2 슬롯 ─────────────────────────────────────────────────────────────────────
void testSlots(const fs::path& root) {
    std::printf("\n== S2 slots ==\n");
    check(SaveStore::validSlotName("slot_1") && SaveStore::validSlotName("auto-save") && SaveStore::validSlotName(std::string(64, 'a')),
          "맞는 슬롯 이름");
    check(!SaveStore::validSlotName("") && !SaveStore::validSlotName("Slot1") && !SaveStore::validSlotName("a.b")
              && !SaveStore::validSlotName("../x") && !SaveStore::validSlotName("a/b") && !SaveStore::validSlotName(std::string(65, 'a')),
          "대문자 · 점 · 경로 · 빈 이름 · 65자 거절");

    const fs::path folder = root / "Saves";
    std::string why;
    {
        SaveStore store(folder);
        SaveData d = sample();
        check(store.save("slot1", d, &why), "저장 " + why);
        check(d.savedAt > 1700000000, "savedAt 을 채움");
        SaveData bad;
        check(!store.save("Bad", bad, &why) && !fs::exists(folder / "Bad.hsave"), "틀린 슬롯 이름은 쓰지 않음");
    }
    {
        SaveStore store(folder);   // 새 저장소 = 다시 시작
        SaveData r;
        const auto res = store.load("slot1", r);
        check(res.ok && !res.fromBackup && r.values() == sample().values() && r.title == sample().title, "다시 시작 뒤 불러오기 왕복");
        check(!fs::exists(folder / "slot1.hsave.bak") && !fs::exists(folder / "slot1.hsave.tmp"), "첫 저장 뒤 .bak · .tmp 없음");
    }
    SaveStore store(folder);
    const std::string first = readFile(store.pathOf("slot1"));
    SaveData d2 = sample();
    d2.setInt("player.coins", 99);
    check(store.save("slot1", d2, &why), "두 번째 저장");
    const fs::path main = store.pathOf("slot1");
    fs::path bak = main;
    bak += ".bak";
    check(readFile(bak) == first, ".bak 은 첫 저장 내용");
    SaveData r;
    check(store.load("slot1", r).ok && r.getInt("player.coins") == 99, "본 파일은 새 값");

    // 쓰는 도중 끊긴 파일(본 파일이 잘림) → 이전 저장본.
    const std::string second = readFile(main);
    writeFile(main, second.substr(0, second.size() / 3));
    auto res = store.load("slot1", r);
    check(res.ok && res.fromBackup && r.getInt("player.coins") == -1234567890123LL, "잘린 본 파일 → .bak 을 읽음: " + res.why);
    // 손상된 본 파일 위에 저장해도 좋은 .bak 을 덮지 않음.
    SaveData d3 = sample();
    d3.setInt("player.coins", 3);
    check(store.save("slot1", d3, &why) && readFile(bak) == first, "손상된 본 파일은 .bak 으로 옮기지 않음");
    check(store.load("slot1", r).ok && r.getInt("player.coins") == 3, "그 뒤 본 파일은 새 값");
    const std::string third = readFile(main);
    SaveData d4 = sample();
    d4.setInt("player.coins", 4);
    check(store.save("slot1", d4, &why) && readFile(bak) == third, ".bak 이 이미 있어도 온전한 이전 본 파일로 바꿈");
    check(store.load("slot1", r).ok && r.getInt("player.coins") == 4, "본 파일은 네 번째 값");
    // 두 이름 바꾸기 사이에서 끊김 = 본 파일 없음, .bak 만.
    fs::remove(main);
    res = store.load("slot1", r);
    check(res.ok && res.fromBackup && store.exists("slot1"), "본 파일이 없고 .bak 만 있어도 읽음: " + res.why);
    // 둘 다 손상.
    writeFile(main, "garbage");
    writeFile(bak, "garbage");
    res = store.load("slot1", r);
    check(!res.ok && r.values().empty() && !res.why.empty(), "둘 다 손상이면 실패와 이유: " + res.why);
    check(!store.load("nothing", r).ok, "없는 슬롯");

    // S2-4 남은 .tmp · 목록 · 지우기
    SaveData a;
    a.title = "Auto";
    a.dataVersion = 2;
    check(store.save("auto", a, &why), "auto 저장");
    fs::path tmp = store.pathOf("auto");
    tmp += ".tmp";
    writeFile(tmp, "half written");
    check(store.load("auto", r).ok && r.title == "Auto", "남은 .tmp 는 읽지 않음");
    check(store.save("auto", a, &why) && !fs::exists(tmp), "다음 저장이 .tmp 를 덮고 남기지 않음");
    writeFile(folder / "notes.txt", "x");
    writeFile(folder / "UPPER.hsave", "x");
    SaveData s3;
    s3.title = "Third";
    store.save("slot3", s3, &why);
    const auto list = store.list();
    std::string names;
    for (const auto& i : list) names += i.name + ";";
    check(names == "auto;slot3;", "list 는 읽을 수 있는 .hsave 만(이름순): " + names);
    check(list.size() == 2 && list[0].title == "Auto" && list[0].dataVersion == 2 && list[0].savedAt > 0, "목록의 제목 · 데이터 버전 · 시각");
    check(store.remove("auto", &why) && !store.exists("auto") && !fs::exists(store.pathOf("auto")), "remove");
}

// ── S2-5 씬 GUID 참조 ───────────────────────────────────────────────────────────
void testSceneRefs(const fs::path& root, AssetManager& assets) {
    std::printf("\n== S2-5 scene refs ==\n");
    World w;
    Actor* door = w.spawnActor("Door");
    w.spawnActor("Key");
    const std::string sceneText = SceneSerializer::writeScene(w);

    SaveStore store(root / "Saves");
    SaveData d;
    d.setGuid("quest.opened", door->guid());
    d.setString("quest.ref_text", HActorRef(door).toText());
    std::string why;
    check(store.save("refs", d, &why), "참조 저장");

    // 다시 시작: 씬을 파일에서 GUID 를 지키며 다시 읽고, 저장을 불러와 찾습니다.
    World w2;
    const auto actors = SceneSerializer::readScene(sceneText, w2, assets, SceneSerializer::GuidPolicy::Keep);
    SaveData r;
    check(store.load("refs", r).ok, "참조 불러오기");
    const HActorRef ref(r.getGuid("quest.opened"));
    Actor* found = ref.resolve(w2);
    check(found && found->name == "Door" && found != door, "다시 읽은 씬에서 같은 액터를 찾음");
    HActorRef fromText;
    check(HActorRef::fromText(r.getString("quest.ref_text"), fromText) && fromText.resolve(w2) == found, "HActorRef 글로도 같은 대상");
    World w3;
    SceneSerializer::readScene(sceneText, w3, assets, SceneSerializer::GuidPolicy::Fresh);
    check(ref.resolve(w3) == nullptr, "Fresh 로 읽은 씬(GUID 바뀜)에서는 찾지 못함(nullptr, 멈추지 않음)");
}

// ── S3 사용자 설정 ──────────────────────────────────────────────────────────────
void testSettings(const fs::path& root) {
    std::printf("\n== S3 user settings ==\n");
    UserSettings s;
    std::string why;
    const uint64_t r0 = s.revision();
    check(s.set("audio.music", "0.25", &why) && why.empty() && s.getFloat("audio.music", 1.f) == 0.25f, "audio.music");
    check(s.revision() == r0 + 1, "변경 번호가 오름");
    check(s.set("audio.music", "0.25") && s.revision() == r0 + 1, "같은 값은 변경 번호를 올리지 않음");
    why.clear();
    check(s.set("audio.master", "5", &why) && s.getFloat("audio.master", 0.f) == 2.f && !why.empty(), "범위 밖은 잘라 알림: " + why);
    check(!s.set("audio.music", "loud", &why) && s.getFloat("audio.music", 0.f) == 0.25f, "형이 틀리면 거절, 값 그대로: " + why);
    check(s.set("render.ao", "0") && s.getString("render.ao") == "false" && s.getBool("render.ao", true) == false, "bool 은 true/false 로");
    check(s.setInt("render.csmCascades", 3, &why) && s.getInt("render.csmCascades", 0) == 2, "int 범위");
    check(s.set("comfort.height", "FIXED") && s.getString("comfort.height") == "fixed", "선택지는 소문자로");
    check(!s.set("comfort.height", "sitting", &why), "없는 선택지 거절: " + why);
    check(s.set("input.action.Teleport", "key:T") && s.set("game.difficulty", "hard"), "입력 · game.* 글");
    check(!s.set("graphics.ultra", "1", &why) && !s.has("graphics.ultra"), "모르는 이름 거절: " + why);
    check(!s.set("game.", "x") && !s.set("game.a b", "x") && !s.set("game.x", "a\nb"), "틀린 game 이름 · 줄바꿈 거절");
    check(UserSettings::find("render.csmCascades")->apply == UserSettings::Apply::Restart
              && UserSettings::find("render.ao")->apply == UserSettings::Apply::Live
              && UserSettings::find("input.axis.MoveX")->prefix,
          "적용 시점 · prefix 표");

    // S3-2 바꾼 값만 씀 · reset
    const std::string text = s.serialize();
    check(text.find("audio.music=0.25\n") != std::string::npos && text.find("audio.sfx") == std::string::npos, "바꾼 값만 씀");
    check(s.reset("audio.master") && !s.has("audio.master") && s.serialize().find("audio.master") == std::string::npos,
          "reset 하면 파일에서 빠짐");
    check(!s.reset("audio.master"), "없는 값 reset 은 false");

    // 파일 왕복(다시 시작과 같음) + 모르는 이름 보존
    const fs::path path = root / "Config" / "user_settings.ini";
    check(s.save(path, &why), "저장 " + why);
    std::string disk = readFile(path);
    writeFile(path, disk + "future.key=7\n# comment\nbroken line\naudio.sfx=abc\naudio.voice=-1\n");
    UserSettings s2;
    std::vector<std::string> msgs;
    check(s2.load(path, msgs), "다시 읽음");
    check(s2.getFloat("audio.music", 1.f) == 0.25f && s2.getString("input.action.Teleport") == "key:T"
              && s2.getString("game.difficulty") == "hard" && s2.getString("comfort.height") == "fixed",
          "다시 시작 뒤 같은 값");
    std::string all;
    for (const auto& m : msgs) all += m + " | ";
    check(msgs.size() == 4, "알림 4개(모르는 이름 · = 없는 줄 · 틀린 형 · 범위): " + all);
    check(!s2.has("audio.sfx") && s2.getFloat("audio.voice", 1.f) == 0.f, "틀린 값은 건너뛰고 범위 밖은 잘라 씀");
    check(s2.serialize().find("future.key=7\n") != std::string::npos, "모르는 이름은 다시 쓸 때 남김");

    // BOM · UTF-16 파일
    const std::string u8bom = "\xEF\xBB\xBF" "audio.ui=0.5\n";
    writeFile(path, u8bom);
    UserSettings s3;
    msgs.clear();
    check(s3.load(path, msgs) && s3.getFloat("audio.ui", 1.f) == 0.5f && msgs.empty(), "UTF-8 BOM 파일");
    std::string u16 = "\xFF\xFE";
    for (char c : std::string("audio.ui=0.75\n")) { u16 += c; u16 += '\0'; }
    writeFile(path, u16);
    msgs.clear();
    check(s3.load(path, msgs) && s3.getFloat("audio.ui", 1.f) == 0.75f, "UTF-16LE 파일(PowerShell 5.1 의 >)");
    UserSettings s4;
    msgs.clear();
    check(!s4.load(root / "Config" / "none.ini", msgs) && s4.values().empty() && msgs.empty(), "없는 파일은 false, 알림 없음");
}

// ── S3-4 편안함 ─────────────────────────────────────────────────────────────────
void testComfort() {
    std::printf("\n== S3-4 comfort ==\n");
    UserSettings s;
    ComfortSettings c = ComfortSettings::from(s);
    check(c == ComfortSettings{}, "아무것도 안 바꾸면 모두 씬 값");
    check(c.snapTurn(30.f) == 30.f && c.teleportOn(true) && !c.teleportOn(false) && c.trackedHeight(true) && c.eye(1.6f) == 1.6f,
          "씬 값 그대로");
    s.set("comfort.snapTurn", "2");
    s.set("comfort.teleport", "off");
    s.set("comfort.height", "fixed");
    s.set("comfort.eyeHeight", "1.2");
    c = ComfortSettings::from(s);
    check(c.snapTurn(45.f) == 5.f, "스냅 각 0 < x < 5 는 5");
    check(!c.teleportOn(true) && !c.trackedHeight(true) && c.eye(1.6f) == 1.2f, "텔레포트 끔 · 고정 높이 · 눈높이 1.2 m 가 씬 값보다 앞섬");
    s.set("comfort.snapTurn", "90");
    s.set("comfort.teleport", "on");
    s.set("comfort.height", "tracked");
    s.set("comfort.eyeHeight", "0.1");
    c = ComfortSettings::from(s);
    check(c.snapTurn(45.f) == 90.f && c.teleportOn(false) && c.trackedHeight(false) && c.eye(1.6f) == 0.5f, "반대 방향 · 눈높이 아래 한도 0.5");
    World w;
    check(w.service<ComfortSettings>() == nullptr, "서비스가 없으면 nullptr(컴포넌트는 씬 값)");
    w.setService<ComfortSettings>(&c);
    Actor* rig = w.spawnActor("Rig");
    const ComfortSettings* got = rig->world() ? rig->world()->service<ComfortSettings>() : nullptr;
    check(got == &c && got->snapTurn(45.f) == 90.f, "액터의 월드에서 서비스로 찾음(이동 컴포넌트와 같은 길)");
    w.setService<ComfortSettings>(nullptr);
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / ("haruka_save_test_" + HGuid::make().toString());
    std::error_code ec;
    fs::create_directories(root, ec);
    AssetManager assets(nullptr);   // 메시 없는 씬만 읽으므로 렌더러를 부르지 않습니다
    testFormat();
    testSlots(root);
    testSceneRefs(root, assets);
    testSettings(root);
    testComfort();
    fs::remove_all(root, ec);
    std::printf("\n%s (%d passed, %d failed)\n", g_failed ? "FAILED" : "ALL PASSED", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
