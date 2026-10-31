// 묶음 파일(.hpak v1)과 가상 파일 시스템을 화면 없이 검산합니다(2026-10-24, Docs/Packaging_Design.md 5절).
//
// 이 시험이 존재하는 이유: 묶음 형식은 한 번 배포하면 오래 쓰는 규약입니다. 쓰는 쪽과 읽는 쪽이 같은 바이트 배치를
// 전제하는지(왕복), 손상 · 잘림 · 모르는 버전을 크래시 없이 거부하는지, 같은 입력이 같은 묶음을 만드는지(결정적 출력),
// 여러 묶음 · 낱개 폴더의 우선순위가 마운트 순서와 상관없이 같은지를 숫자로 확인합니다.
// 실행: build/Release/HarukaPakTest.exe

#include "../src/tools/PakGlob.hpp"
#include "../src/vfs/PakFormat.hpp"
#include "../src/vfs/PakReader.hpp"
#include "../src/vfs/PakWriter.hpp"
#include "../src/vfs/PathUtf8.hpp"
#include "../src/vfs/ShaderManifest.hpp"
#include "../src/vfs/Vfs.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

using Bytes = std::vector<uint8_t>;

Bytes bytesOf(const std::string& s) { return Bytes(s.begin(), s.end()); }

Bytes randomBytes(size_t n, uint32_t seed) {
    std::mt19937 rng(seed);
    Bytes b(n);
    for (auto& x : b) x = (uint8_t)rng();
    return b;
}

// 압축이 잘 되는 내용(반복 글 + 조금의 변화).
Bytes textBytes(size_t n) {
    Bytes b(n);
    const char* line = "ACTOR:Floor\nCOMPONENT:StaticMeshComponent\nmesh=assets/models/model.glb\n";
    const size_t len = std::strlen(line);
    for (size_t i = 0; i < n; ++i) b[i] = (uint8_t)(line[i % len] + ((i / 4096) % 3));
    return b;
}

bool build(std::vector<hpak::PakInput> inputs, Bytes& out, std::string& err, hpak::WriteStats* st = nullptr) {
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    if (!hpak::writePak(std::move(inputs), ss, err, st)) return false;
    const std::string s = ss.str();
    out.assign(s.begin(), s.end());
    return true;
}

hpak::PakInput input(const std::string& path, Bytes data, hpak::Codec codec = hpak::Codec::Lz4) {
    hpak::PakInput in;
    in.path  = path;
    in.data  = std::move(data);
    in.codec = codec;
    return in;
}

bool openMem(hpak::PakReader& r, const Bytes& pack, std::string& err) {
    return r.open(hpak::memorySource(pack), err);
}

hpak::Header headerOf(const Bytes& pack) {
    hpak::Header h{};
    std::memcpy(&h, pack.data(), sizeof(h));
    return h;
}

// 머리 · 목차를 고친 뒤 해시를 다시 맞춥니다(버전 · 플래그 규칙만 시험하려고).
void resealToc(Bytes& pack) {
    hpak::Header h = headerOf(pack);
    h.tocHash = hpak::hash64(pack.data() + h.tocOffset, (size_t)h.tocSize);
    std::memcpy(pack.data(), &h, sizeof(h));
}
void resealHeader(Bytes& pack) {
    hpak::Header h = headerOf(pack);
    h.headerHash = (uint32_t)hpak::hash64(&h, offsetof(hpak::Header, headerHash));
    std::memcpy(pack.data(), &h, sizeof(h));
}

void writeFile(const fs::path& p, const Bytes& b) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), (std::streamsize)b.size());
}

std::string readVfsText(const std::string& path) {
    std::string s;
    return hvfs::readText(path, s) ? s : std::string("<missing>");
}

} // namespace

int main() {
    std::printf("Haruka 묶음 파일(.hpak) 검산\n");
    const fs::path tmp = fs::temp_directory_path() / "haruka_pak_test";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp);

    // ── 1. 왕복 ───────────────────────────────────────────────────────────────
    std::printf("\n== 1. 왕복 ==\n");
    const Bytes zero, one = { 0x5A }, blockM1 = textBytes(hpak::kBlockSize - 1), block = textBytes(hpak::kBlockSize),
                blockP1 = textBytes(hpak::kBlockSize + 1), rnd = randomBytes(1 << 20, 7), zeros(1 << 20, 0),
                crlf = bytesOf("line one\r\nline two\r\n");
    std::vector<hpak::PakInput> inputs;
    inputs.push_back(input("data/zero.bin", zero));
    inputs.push_back(input("data/one.bin", one));
    inputs.push_back(input("data/blockM1.txt", blockM1));
    inputs.push_back(input("data/block.txt", block));
    inputs.push_back(input("data/blockP1.txt", blockP1));
    inputs.push_back(input("data/random.bin", rnd));
    inputs.push_back(input("data/zeros.bin", zeros));
    inputs.push_back(input("data/crlf.txt", crlf));
    inputs.push_back(input("audio/tone.ogg", randomBytes(5000, 3), hpak::Codec::Stored));
    Bytes pack;
    std::string err;
    hpak::WriteStats st;
    check(build(inputs, pack, err, &st), "묶음을 씀 " + err);
    hpak::PakReader r;
    check(openMem(r, pack, err), "묶음을 엶 " + err);
    check(r.count() == 9, "항목 9개");
    const std::pair<const char*, const Bytes*> expect[] = {
        { "data/zero.bin", &zero }, { "data/one.bin", &one }, { "data/blockM1.txt", &blockM1 }, { "data/block.txt", &block },
        { "data/blockP1.txt", &blockP1 }, { "data/random.bin", &rnd }, { "data/zeros.bin", &zeros }, { "data/crlf.txt", &crlf } };
    bool same = true;
    for (const auto& [path, data] : expect) {
        Bytes got;
        const int i = r.find(path);
        same = same && i >= 0 && r.readAll(i, got, err) && got == *data;
    }
    check(same, "0B · 1B · 64KiB±1 · 1MiB 난수 · 1MiB 0 · CRLF 글이 바이트 그대로 돌아옴(쓰는 쪽은 줄 끝을 바꾸지 않음)");
    const auto codecOf = [&](const char* p) { return (hpak::Codec)r.entry(r.find(p)).codec; };
    check(codecOf("data/zeros.bin") == hpak::Codec::Lz4 && codecOf("data/blockP1.txt") == hpak::Codec::Lz4,
          "압축되는 내용은 LZ4");
    check(codecOf("data/random.bin") == hpak::Codec::Stored, "압축 이득이 없는 난수는 그대로 저장(90% 규칙)");
    check(codecOf("audio/tone.ogg") == hpak::Codec::Stored, "Stored 로 요청한 파일은 그대로 저장");
    check(r.entry(r.find("data/zeros.bin")).storedSize < 64 * 1024, "1MiB 0 은 64KiB 미만으로 줄어듦");
    check(st.files == 9 && st.packBytes == pack.size(), "쓰기 통계가 묶음과 맞음");

    // ── 2. 블록 경계를 걸친 범위 읽기 ──────────────────────────────────────────────
    std::printf("\n== 2. 범위 읽기 ==\n");
    {
        const int i = r.find("data/blockP1.txt");
        // 파일은 64KiB + 1B(블록 둘). 두 번째 경우는 경계를 걸쳐 파일 끝까지 읽습니다.
        const uint64_t cases[][2] = { { 0, 10 }, { hpak::kBlockSize - 5, 6 }, { hpak::kBlockSize, 1 }, { 3, hpak::kBlockSize - 2 } };
        for (const auto& c : cases) {
            Bytes part((size_t)c[1]);
            err.clear();
            const bool read = r.readRange(i, c[0], part.data(), part.size(), err);
            check(read && std::memcmp(part.data(), blockP1.data() + c[0], part.size()) == 0,
                  "LZ4 항목의 [" + std::to_string(c[0]) + ", +" + std::to_string(c[1]) + ") 조각이 원본과 같음 " + err);
        }
        Bytes part(4);
        check(!r.readRange(i, blockP1.size() - 2, part.data(), 4, err), "파일 끝을 넘는 범위는 실패");
        const int s = r.find("data/random.bin");
        Bytes rp(100);
        check(r.readRange(s, 123456, rp.data(), rp.size(), err) && std::memcmp(rp.data(), rnd.data() + 123456, 100) == 0,
              "그대로 저장한 항목의 범위 읽기");
    }

    // ── 3. 손상 ────────────────────────────────────────────────────────────────
    std::printf("\n== 3. 손상 ==\n");
    {
        const hpak::Header h = headerOf(pack);
        auto opens = [&](const Bytes& b) { hpak::PakReader rr; std::string e; return openMem(rr, b, e); };
        Bytes b = pack; b[30] ^= 0x40;
        check(!opens(b), "머리 1바이트 손상 → 마운트 거부");
        b = pack; b[80] ^= 0x01;   // 예약 영역: 범위 검사로는 잡히지 않고 머리 해시만 잡음
        check(!opens(b), "머리 예약 영역 1바이트 손상 → 마운트 거부(머리 해시)");
        b = pack; b[(size_t)h.tocOffset + 40] ^= 0x01;
        check(!opens(b), "목차 1바이트 손상 → 마운트 거부");
        b = pack; b[(size_t)(h.tocOffset + h.tocSize) - 2] ^= 0x01;   // 문자열 표의 경로 글자: 목차 해시만 잡음
        check(!opens(b), "목차 문자열 1바이트 손상 → 마운트 거부(목차 해시)");
        b = pack; b.pop_back();
        check(!opens(b), "끝 1바이트 잘림 → 마운트 거부");
        check(!opens(randomBytes(300, 9)), "묶음이 아닌 파일 → 거부");
        check(!opens(Bytes(10, 0)), "머리보다 작은 파일 → 거부");

        b = pack;
        {
            hpak::PakReader tmpR; openMem(tmpR, pack, err);
            const hpak::Entry e = tmpR.entry(tmpR.find("data/random.bin"));
            b[(size_t)e.dataOffset + 500] ^= 0xFF;
        }
        hpak::PakReader rs;
        Bytes got;
        check(openMem(rs, b, err), "저장 데이터만 손상되면 마운트는 됨");
        check(!rs.readAll(rs.find("data/random.bin"), got, err), "손상된 그대로 저장 항목은 내용 해시로 실패");
        check(rs.readAll(rs.find("data/one.bin"), got, err) && got == one, "다른 파일은 그대로 읽힘");

        b = pack;
        {
            hpak::PakReader tmpR; openMem(tmpR, pack, err);
            const hpak::Entry e = tmpR.entry(tmpR.find("data/blockP1.txt"));
            for (int k = 0; k < 8; ++k) b[(size_t)e.dataOffset + 7 + (size_t)k * 13] ^= 0x5A;
        }
        hpak::PakReader rc;
        openMem(rc, b, err);
        check(!rc.readAll(rc.find("data/blockP1.txt"), got, err), "손상된 압축 블록은 크래시 없이 실패");
    }

    // ── 4. 정렬 ────────────────────────────────────────────────────────────────
    std::printf("\n== 4. 정렬 ==\n");
    {
        bool ok = true;
        for (int i = 0; i < (int)r.count(); ++i) {
            const hpak::Entry& e = r.entry(i);
            ok = ok && e.dataOffset % 16 == 0 && (e.size < hpak::kBigEntryBytes || e.dataOffset % hpak::kBigAlign == 0);
        }
        check(ok, "모든 항목이 16B, 64KiB 이상 항목은 4KiB 에 맞춤");
        check(headerOf(pack).tocOffset % 16 == 0, "목차가 16B 에 맞춤");
    }

    // ── 5. 결정적 출력 ──────────────────────────────────────────────────────────
    std::printf("\n== 5. 결정적 출력 ==\n");
    {
        std::vector<hpak::PakInput> rev(inputs.rbegin(), inputs.rend());
        Bytes a, b2;
        check(build(inputs, a, err) && build(rev, b2, err) && a == pack && b2 == pack,
              "입력 순서를 바꾸거나 다시 만들어도 같은 바이트");
    }

    // ── 6. 경로 정규화와 찾기 ────────────────────────────────────────────────────
    std::printf("\n== 6. 경로 ==\n");
    {
        std::string n;
        check(hpak::normalizePath("assets\\models\\A.glb", n) && n == "assets/models/A.glb", "'\\' 는 '/' 로");
        check(hpak::normalizePath("./././assets/x", n) && n == "assets/x", "앞의 './' 는 지움");
        const char* bad[] = { "", "/abs", "a/", "a//b", "a/./b", "a/../b", "C:/x", "a\x01" "b", "a*b", "a?b", "a|b" };
        bool rejected = true;
        for (const char* p : bad) rejected = rejected && !hpak::normalizePath(p, n);
        check(rejected && !hpak::normalizePath(std::string(1025, 'a'), n), "빈 · 절대 · 끝 '/' · 빈 구성요소 · . · .. · ':' · 제어 문자 · 금지 문자 · 1024B 초과 거부");
        check(r.find("DATA/One.BIN") >= 0 && r.find("data\\one.bin") >= 0, "찾기는 대소문자 · 구분자를 무시");
        check(std::string(r.path(r.find("data/blockP1.txt"))) == "data/blockP1.txt", "저장한 표기는 그대로");
        Bytes p;
        std::vector<hpak::PakInput> dup;
        dup.push_back(input("Dir/File.txt", one));
        dup.push_back(input("dir/file.txt", one));
        check(!build(dup, p, err) && err.find("letter case") != std::string::npos, "대소문자만 다른 두 경로는 쓰기 오류");
        dup[1].path = "Dir/File.txt";
        check(!build(dup, p, err) && err.find("twice") != std::string::npos, "같은 경로 두 번은 쓰기 오류");
        std::vector<hpak::PakInput> badPath;
        badPath.push_back(input("../escape.txt", one));
        check(!build(badPath, p, err), "정규화할 수 없는 경로는 쓰기 오류");
    }

    // ── 7. 버전과 플래그 ─────────────────────────────────────────────────────────
    std::printf("\n== 7. 버전 ==\n");
    {
        auto opens = [&](const Bytes& b, std::string& e) { hpak::PakReader rr; return openMem(rr, b, e); };
        Bytes b = pack;
        { hpak::Header h = headerOf(b); h.major = 2; std::memcpy(b.data(), &h, sizeof(h)); resealHeader(b); }
        check(!opens(b, err) && err.find("version") != std::string::npos, "major 2 는 거부");
        b = pack;
        { hpak::Header h = headerOf(b); h.minor = 7; std::memcpy(b.data(), &h, sizeof(h)); resealHeader(b); }
        check(opens(b, err), "minor 가 커도 열림(하위 호환 추가만)");
        b = pack;
        { hpak::Header h = headerOf(b); h.incompatFlags = 1; std::memcpy(b.data(), &h, sizeof(h)); resealHeader(b); }
        check(!opens(b, err), "모르는 incompat 비트는 거부");
        b = pack;
        { hpak::Header h = headerOf(b); h.compatFlags = 0x80000001u; std::memcpy(b.data(), &h, sizeof(h)); resealHeader(b); }
        check(opens(b, err), "모르는 compat 비트는 무시");

        // 한 항목의 코덱을 모르는 번호로(목차를 고치고 해시를 다시 맞춤).
        b = pack;
        const hpak::Header h = headerOf(b);
        hpak::PakReader tmpR; openMem(tmpR, pack, err);
        const int target = tmpR.find("data/one.bin");
        const size_t codecAt = (size_t)h.tocOffset + hpak::kTocHeaderSize + (size_t)target * hpak::kEntrySize + 38;
        b[codecAt] = 7;
        resealToc(b);
        resealHeader(b);
        hpak::PakReader ru;
        Bytes got;
        check(openMem(ru, b, err), "모르는 코덱 항목이 있어도 마운트는 됨");
        check(!ru.readAll(ru.find("data/one.bin"), got, err) && err.find("unknown compression") != std::string::npos,
              "그 파일만 열지 못함");
        check(ru.readAll(ru.find("data/zeros.bin"), got, err) && got == zeros, "다른 파일은 읽힘");
    }

    // ── 8. 여러 묶음과 낱개 폴더의 우선순위 ─────────────────────────────────────────
    std::printf("\n== 8. 우선순위 ==\n");
    {
        Bytes base, patch;
        std::vector<hpak::PakInput> bi, pi;
        bi.push_back(input("a.txt", bytesOf("base")));
        bi.push_back(input("b.txt", bytesOf("base")));
        bi.push_back(input("c.txt", bytesOf("base")));
        pi.push_back(input("A.TXT", bytesOf("patch")));
        build(bi, base, err);
        build(pi, patch, err);
        writeFile(tmp / "loose" / "b.txt", bytesOf("loose"));
        auto mountAll = [&](bool patchFirst) {
            hvfs::reset();
            auto mount = [&](const Bytes& p, const char* name, int prio) {
                auto rr = std::make_unique<hpak::PakReader>();
                rr->open(hpak::memorySource(p), err);
                hvfs::mountPack(std::move(rr), name, prio);
            };
            if (patchFirst) { mount(patch, "patch", 5); hvfs::mountDirectory(tmp / "loose", "", 1); mount(base, "base", 0); }
            else            { mount(base, "base", 0); hvfs::mountDirectory(tmp / "loose", "", 1); mount(patch, "patch", 5); }
            return readVfsText("a.txt") + "|" + readVfsText("b.txt") + "|" + readVfsText("c.txt");
        };
        const std::string x = mountAll(false), y = mountAll(true);
        check(x == "patch|loose|base", "우선순위: 패치(5) > 낱개(1) > 기본(0), 대소문자 무시 덮어쓰기 (" + x + ")");
        check(x == y, "마운트 순서를 바꿔도 같은 결과");
        hvfs::reset();
        auto mountSame = [&](const Bytes& p, const char* name) {
            auto rr = std::make_unique<hpak::PakReader>();
            rr->open(hpak::memorySource(p), err);
            hvfs::mountPack(std::move(rr), name, 3);
        };
        mountSame(base, "first");
        mountSame(patch, "second");
        check(readVfsText("a.txt") == "patch", "같은 우선순위는 나중에 마운트한 쪽이 이김");

        // 파일 이름 규칙: game.hpak < dlc_*.hpak < game_p<N>.hpak(N 은 숫자 크기로)
        const fs::path dir = tmp / "packs";
        auto packFile = [&](const char* name, const char* v) {
            Bytes p;
            std::vector<hpak::PakInput> in;
            in.push_back(input("v.txt", bytesOf(v)));
            in.push_back(input(std::string("only_") + v + ".txt", bytesOf(v)));
            build(in, p, err);
            writeFile(dir / name, p);
        };
        packFile("game.hpak", "base");
        packFile("dlc_extra.hpak", "dlc");
        packFile("game_p2.hpak", "p2");
        packFile("game_p10.hpak", "p10");
        writeFile(dir / "weird.hpak", bytesOf("x"));
        writeFile(dir / "notes.txt", bytesOf("ignored"));
        hvfs::reset();
        const hvfs::MountReport rep = hvfs::mountGamePacks(dir, 0);
        check(rep.mounted.size() == 4 && rep.failed.size() == 1, "규칙에 맞는 묶음 4개 마운트, 이름이 맞지 않는 1개는 알림");
        check(readVfsText("v.txt") == "p10", "패치 번호는 숫자 크기로(10 > 2)");
        check(readVfsText("only_dlc.txt") == "dlc" && readVfsText("only_base.txt") == "base", "아래 묶음에만 있는 파일도 찾음");
        hvfs::Stat s;
        check(hvfs::stat("v.txt", s) && s.inPack && s.size == 3, "stat 는 목차의 크기 · 묶음 여부");
        check(hvfs::diskPath("v.txt").empty(), "묶음 안 파일은 실제 경로가 없음");
        hvfs::reset();
    }

    // ── 9. 여러 스레드가 함께 읽기 ───────────────────────────────────────────────
    std::printf("\n== 9. 동시 읽기 ==\n");
    {
        const fs::path file = tmp / "concurrent.hpak";
        writeFile(file, pack);
        hvfs::reset();
        check(hvfs::mountPack(file, 0, err), "파일 묶음 마운트 " + err);
        std::atomic<int> bad{ 0 };
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t)
            threads.emplace_back([&, t] {
                for (int k = 0; k < 40; ++k) {
                    const auto& [path, data] = expect[(size_t)(t + k) % (sizeof(expect) / sizeof(expect[0]))];
                    Bytes got;
                    if (!hvfs::readAll(path, got) || got != *data) ++bad;
                }
            });
        for (auto& th : threads) th.join();
        check(bad == 0, "8 스레드 × 40 회 읽기가 모두 원본과 같음(위치 지정 읽기)");
        hvfs::reset();
    }

    // ── 10. 절대 경로는 마운트를 거치지 않음 ─────────────────────────────────────────
    std::printf("\n== 10. 절대 경로 ==\n");
    {
        const fs::path f = tmp / "direct.txt";
        writeFile(f, bytesOf("direct"));
        hvfs::reset();
        check(readVfsText(f.string()) == "direct", "마운트가 없어도 절대 경로 파일은 직접 읽음(시스템 글꼴 등)");
        check(!hvfs::exists("direct.txt"), "같은 이름의 논리 경로는 마운트가 없으면 없음");
    }

    // ── 11. 조작한 목차 (DEF-122) ─────────────────────────────────────────────────
    // 해시를 다시 맞춘 목차는 손상 검사를 지나므로, 개수 · 크기 필드 자체를 넘침 없이 검사하는지 봅니다. 예전에는 아래
    // 묶음들이 마운트를 지나, 거대한 할당(bad_alloc · length_error)으로 프로그램을 끝내거나 실제 바이트 없이 거대한 크기를
    // 선언할 수 있었습니다.
    std::printf("\n== 11. 조작한 목차 ==\n");
    {
        const hpak::Header h0 = headerOf(pack);
        auto tocAt   = [&](Bytes& b) { return b.data() + h0.tocOffset; };
        auto entryAt = [&](Bytes& b, int i) { return tocAt(b) + hpak::kTocHeaderSize + (size_t)i * hpak::kEntrySize; };
        auto blockAt = [&](Bytes& b, uint32_t i) {
            return tocAt(b) + hpak::kTocHeaderSize + (size_t)h0.entryCount * hpak::kEntrySize + (size_t)i * 4;
        };
        auto reseal = [&](Bytes& b) { resealToc(b); resealHeader(b); };
        // 열리거나 예외가 나면 "막지 못함"입니다.
        auto notRejected = [&](const Bytes& b) {
            try { hpak::PakReader rr; std::string e; return openMem(rr, b, e); } catch (...) { return true; }
        };
        hpak::PakReader orig;
        openMem(orig, pack, err);

        Bytes b = pack;
        {
            hpak::Header h = headerOf(b);
            h.entryCount = 0xFFFFFFFFu;
            std::memcpy(b.data(), &h, sizeof(h));
            hpak::TocHeader th{};
            std::memcpy(&th, tocAt(b), sizeof(th));
            th.entryRecordSize = 0xFFFFFFFFu;   // (2^32-1)^2 + 2^31 * 4 + 32 이 64비트에서 33 으로 되감김
            th.blockCount      = 0x80000000u;
            th.stringBytes     = 0;
            std::memcpy(tocAt(b), &th, sizeof(th));
            reseal(b);
        }
        check(!notRejected(b), "개수 × 크기의 합이 64비트를 넘쳐 작아지는 목차 → 거부");

        b = pack;
        {
            const int z = orig.find("data/zeros.bin");
            hpak::Entry e = orig.entry(z);
            e.size = ~0ull;   // 블록 수 (size + 65535) / 65536 이 넘쳐 0 이 됨
            e.storedSize = 0;
            std::memcpy(entryAt(b, z), &e, sizeof(e));
            reseal(b);
        }
        check(!notRejected(b), "LZ4 항목의 크기가 2^64-1(블록 수 계산이 넘침) → 거부");

        const int k = orig.find("data/block.txt");
        const hpak::Entry ek = orig.entry(k);
        check(ek.codec == (uint8_t)hpak::Codec::Lz4 && ek.size == hpak::kBlockSize, "시험 준비: data/block.txt 는 LZ4 블록 하나");
        auto forgeBlock = [&](uint32_t word) {
            Bytes f = pack;
            std::memcpy(blockAt(f, ek.firstBlock), &word, 4);
            hpak::Entry e = ek;
            e.storedSize = word & ~hpak::kBlockRawBit;   // 합계 검사는 지나게 맞춤
            std::memcpy(entryAt(f, k), &e, sizeof(e));
            reseal(f);
            return f;
        };
        check(!notRejected(forgeBlock(0)), "압축 블록의 크기가 0 → 거부");
        check(!notRejected(forgeBlock(1)), "압축 블록 1바이트가 64KiB 로 풀린다고 선언(LZ4 최대 배율 초과) → 거부");
        check(!notRejected(forgeBlock(100 | hpak::kBlockRawBit)), "원문 블록인데 크기가 블록 길이와 다름 → 거부");
    }

    // ── 12. UTF-8 경로 (DEF-121 · DEF-123) ──────────────────────────────────────
    std::printf("\n== 12. UTF-8 경로 ==\n");
    {
        const std::string ko = "assets/한글 에셋.txt", emoji = "assets/🎵.txt";
        fs::path p;
        check(hpak::pathFromUtf8(ko, p) && hpak::utf8Of(p) == ko && hpak::pathFromUtf8(emoji, p) && hpak::utf8Of(p) == emoji,
              "한글 · 4바이트 글자가 경로와 UTF-8 사이를 그대로 오감");
        check(!hpak::pathFromUtf8("assets/\xff\xfe.txt", p), "올바르지 않은 UTF-8 은 false(예외 없음)");
#ifdef _WIN32
        check(hpak::utf8Of(fs::path(std::wstring(L"a\xD800"))).empty(), "짝이 맞지 않는 서로게이트 경로는 빈 문자열(예외 없음)");
#endif
        const fs::path dir = tmp / "utf8";
        fs::path koFile;
        hpak::pathFromUtf8("한글 에셋.txt", koFile);
        writeFile(dir / "assets" / koFile, bytesOf("ko"));
        hvfs::reset();
        hvfs::mountDirectory(dir, "assets/", 0);
        check(readVfsText(ko) == "ko", "한글 이름의 낱개 파일을 UTF-8 논리 경로로 찾음");
        bool threw = false, found = true;
        try {
            Bytes got;
            found = hvfs::readAll("assets/\xff\xfe.txt", got) || hvfs::exists("C:/\xff.txt");
        } catch (...) {
            threw = true;
        }
        check(!threw && !found, "올바르지 않은 UTF-8 의 논리 · 절대 경로는 예외 없이 '없음'(오디오 콜백이 C 경계를 넘지 않음)");
        hvfs::reset();
    }

    // ── 13. 묶음 목록의 제외 패턴 (DEF-127) ──────────────────────────────────────────
    // 저장소의 Packaging/game.pack.txt 를 그대로 읽어, 배포 묶음에서 빠질 것과 남을 것을 확인합니다.
    std::printf("\n== 13. 목록 패턴 ==\n");
    {
        std::vector<std::string> excludes;
        std::ifstream mf(HARUKA_PACK_MANIFEST);
        std::string line;
        while (std::getline(mf, line)) {
            std::istringstream ls(line);
            std::string cmd, pat;
            ls >> cmd;
            if (cmd != "exclude") continue;
            while (ls >> pat) excludes.push_back(pat);
        }
        check(!excludes.empty(), "Packaging/game.pack.txt 의 exclude 줄을 읽음");
        auto out = [&](const char* path) { return hpak::anyMatch(excludes, path); };
        check(out("assets/.svn/entries") && out("assets/models/.vs/x/y.bin"), "숨김 폴더 아래 파일은 빠짐");
        check(out("assets/.gitkeep") && out("assets/cache/0123.gasset") && out("assets/scenes/a.hscene.tmp") &&
              out("assets/textures/Thumbs.db") && out("assets/DESKTOP.INI"), "숨김 파일 · 캐시 · 임시 파일 · 탐색기 파일은 빠짐");
        check(!out("assets/project.ini") && !out("assets/a.b/c.txt") && !out("assets/models/model.glb") &&
              !out("shaders/pbr.frag.spv"), "일반 파일과 이름에 점이 든 폴더는 남음");
    }

    // ── 14. --loose 는 모든 묶음보다 위 (DEF-126) ────────────────────────────────────
    std::printf("\n== 14. --loose ==\n");
    {
        const fs::path dir = tmp / "loosepacks";
        Bytes p;
        std::vector<hpak::PakInput> in;
        in.push_back(input("v.txt", bytesOf("patch")));
        build(in, p, err);
        writeFile(dir / "game_p999999999.hpak", p);
        writeFile(dir / "v.txt", bytesOf("loose"));
        hvfs::reset();
        const hvfs::MountReport rep = hvfs::mountGamePacks(dir, 20);
        check(rep.mounted.size() == 1 && rep.failed.empty(), "가장 큰 번호(9자리)의 패치 묶음을 마운트");
        hvfs::mountDirectory(dir, "", hvfs::kLoosePriority);
        check(readVfsText("v.txt") == "loose", "패치 번호가 커도 --loose 의 낱개 파일이 이김");
        check(hvfs::kLoosePriority > 20 + 2 + hvfs::kMaxPatchNumber, "--loose 우선순위가 묶음 우선순위의 상한보다 큼");
        hvfs::reset();
    }

    // ── 15. 에디터 배치: 실행 폴더는 shaders/ 만 (DEF-124) ────────────────────────────
    std::printf("\n== 15. 에디터 배치 ==\n");
    {
        const fs::path exe = tmp / "exe", project = tmp / "project";
        writeFile(exe / "shaders/a.spv", bytesOf("spv"));
        writeFile(exe / "assets/models/old.glb", bytesOf("stale copy"));   // 프로젝트에서 지운 에셋의 옛 복사본
        writeFile(project / "assets/models/new.glb", bytesOf("new"));
        hvfs::reset();
        hvfs::mountDirectory(exe, "shaders/", 0);
        hvfs::mountDirectory(project, "assets/", 10);
        check(readVfsText("shaders/a.spv") == "spv" && readVfsText("assets/models/new.glb") == "new",
              "셰이더는 실행 폴더, 에셋은 프로젝트 폴더에서 찾음");
        check(!hvfs::exists("assets/models/old.glb"), "프로젝트에서 지운 에셋은 실행 폴더의 옛 복사본으로 넘어가지 않음");
        hvfs::reset();
    }

    // ── 16. 묶음과 빌드의 짝: 셰이더 목록 (DEF-149) ───────────────────────────────────
    // 엔진은 빌드가 넣은 셰이더 목록(경로 · 내용 해시)을, 마운트 뒤 실제로 읽힐 파일과 비교합니다. 다른 빌드의 묶음 · 빠진 셰이더 ·
    // 그 위의 낱개 파일을 구별하는지 봅니다.
    std::printf("\n== 16. 셰이더 목록 ==\n");
    {
        const fs::path dir = tmp / "shaderpack";
        Bytes p;
        std::vector<hpak::PakInput> in;
        in.push_back(input("shaders/a.spv", bytesOf("spv-a")));
        in.push_back(input("shaders/b.spv", bytesOf("spv-b")));
        build(in, p, err);
        writeFile(dir / "game.hpak", p);
        const uint64_t ha = hpak::hash64("spv-a", 5), hb = hpak::hash64("spv-b", 5);
        auto viaVfs = [](const char* path, uint64_t& h) { return hvfs::contentHash(path, h); };

        hvfs::reset();
        check(hvfs::mountGamePacks(dir, 20).mounted.size() == 1, "시험 준비: 묶음 마운트");
        const hpak::ShaderManifestEntry same[] = { { "shaders/a.spv", ha }, { "shaders/b.spv", hb } };
        check(hpak::compareShaderManifest(same, 2, viaVfs).empty(), "같은 빌드의 묶음 → 어긋남 없음");

        const hpak::ShaderManifestEntry other[] = { { "shaders/a.spv", ha }, { "shaders/b.spv", hb ^ 1 }, { "shaders/c.spv", 7 } };
        const auto bad = hpak::compareShaderManifest(other, 3, viaVfs);
        check(bad.size() == 2 && bad[0].path == "shaders/b.spv" && !bad[0].missing && bad[1].path == "shaders/c.spv" &&
              bad[1].missing, "내용이 다른 셰이더와 묶음에 없는 셰이더를 구별해 알림");

        writeFile(dir / "loose" / "shaders" / "a.spv", bytesOf("spv-a2"));
        hvfs::mountDirectory(dir / "loose", "", hvfs::kLoosePriority);
        const auto loose = hpak::compareShaderManifest(same, 2, viaVfs);
        check(loose.size() == 1 && loose[0].path == "shaders/a.spv", "--loose 의 낱개 파일이 이기면 그 파일로 비교");

        // 묶음이 있는 폴더 확인과 마운트 이름(전체 경로).
        writeFile(dir / "other" / "GAME.HPAK", p);
        writeFile(dir / "none" / "notes.txt", bytesOf("x"));
        check(hvfs::hasPackFiles(dir / "other") && !hvfs::hasPackFiles(dir / "none") && !hvfs::hasPackFiles(dir / "absent"),
              "묶음 파일이 있는 폴더만 참(대소문자 무시, 없는 폴더는 거짓)");
        hvfs::reset();
        check(hvfs::mountPack(dir / "game.hpak", 0, err), "시험 준비: 파일 묶음 마운트");
        const std::vector<std::string> d = hvfs::describe();
        check(d.size() == 1 && d[0].find(hpak::utf8Of(dir / "game.hpak")) != std::string::npos,
              "마운트 이름은 묶음의 전체 경로(어느 폴더의 묶음인지 로그로 보임)");
        hvfs::reset();
    }

    fs::remove_all(tmp, ec);
    std::printf("\n결과: %s (실패 %d)\n", g_failed ? "FAIL" : "PASS", g_failed);
    return g_failed ? 1 : 0;
}
