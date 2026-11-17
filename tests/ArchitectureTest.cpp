// 모듈 의존 방향 시험(엔진 구조 안정화 S1, 2026-11-18). 정본 Docs/Engine_Structure_Design.md, 규칙 tests/ArchitectureRules.txt.
//
// 이 시험이 존재하는 이유: 모듈 사이의 include 방향은 컴파일러가 막지 않아, 한 줄씩 늘어난 거꾸로 된 의존이 쌓여 순환이
// 됩니다(2026-11-18 진단: core 가 바닥이자 꼭대기). src 아래 모든 소스의 `#include "..."` 를 실제 파일로 풀어, 모듈이 다르면
// 규칙 표가 허용하는 방향인지 봅니다. 아직 고치지 않은 위반은 허용 목록에 파일 쌍으로 적고, 고쳐서 필요 없어진 항목이 남아
// 있으면 실패합니다(목록은 줄기만 함).
//
// 실행: build/Release/HarukaArchTest.exe [--list]
//   --list : 지금 위반을 허용 목록 형식으로 출력합니다(목록을 다시 만들 때).

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef HARUKA_SOURCE_DIR
#error "HARUKA_SOURCE_DIR must point to the repository root"
#endif

namespace fs = std::filesystem;

namespace {

int g_failed = 0;
int g_checks = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++g_failed;
}

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string w; in >> w;) {
        if (!w.empty() && w.back() == ',') w.pop_back();
        if (!w.empty()) out.push_back(w);
    }
    return out;
}

// src 기준 경로(예: "scene/World.cpp")의 모듈. src 바로 아래 파일(main.cpp)은 "main".
std::string moduleOf(const std::string& rel) {
    const size_t s = rel.find('/');
    return s == std::string::npos ? std::string("main") : rel.substr(0, s);
}

struct Rules {
    std::map<std::string, std::set<std::string>> deps;   // 모듈 → 허용하는 대상 모듈
    std::set<std::pair<std::string, std::string>> allow;  // (포함하는 파일, 포함되는 파일), src 기준
    std::vector<std::string> errors;
};

Rules loadRules(const fs::path& file) {
    Rules r;
    std::ifstream in(file);
    if (!in) { r.errors.push_back("cannot open " + file.string()); return r; }
    int lineNo = 0;
    for (std::string line; std::getline(in, line);) {
        ++lineNo;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        line = trim(line);
        if (line.empty()) continue;
        if (line.rfind("module ", 0) == 0) {
            const size_t colon = line.find(':');
            if (colon == std::string::npos) { r.errors.push_back("line " + std::to_string(lineNo) + ": module without ':'"); continue; }
            const std::string name = trim(line.substr(7, colon - 7));
            if (r.deps.count(name)) r.errors.push_back("line " + std::to_string(lineNo) + ": module " + name + " twice");
            auto& set = r.deps[name];
            for (const std::string& w : words(line.substr(colon + 1))) set.insert(w);
        } else if (line.rfind("allow ", 0) == 0) {
            const size_t arrow = line.find("->");
            if (arrow == std::string::npos) { r.errors.push_back("line " + std::to_string(lineNo) + ": allow without '->'"); continue; }
            const std::string from = trim(line.substr(6, arrow - 6)), to = trim(line.substr(arrow + 2));
            if (!r.allow.insert({ from, to }).second) r.errors.push_back("line " + std::to_string(lineNo) + ": duplicate allow");
        } else {
            r.errors.push_back("line " + std::to_string(lineNo) + ": unknown line '" + line + "'");
        }
    }
    return r;
}

struct Edge { std::string from, to; };   // src 기준 파일 경로

// src 아래 모든 소스와, 모듈이 다른 include 를 모읍니다.
void scan(const fs::path& src, std::set<std::string>& files, std::vector<Edge>& edges) {
    for (const auto& e : fs::recursive_directory_iterator(src)) {
        if (!e.is_regular_file()) continue;
        const std::string ext = e.path().extension().string();
        if (ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".inl") continue;
        files.insert(fs::relative(e.path(), src).generic_string());
    }
    for (const std::string& rel : files) {
        std::ifstream in(src / rel);
        const fs::path dir = fs::path(rel).parent_path();
        for (std::string line; std::getline(in, line);) {
            const std::string t = trim(line);
            if (t.rfind("#", 0) != 0) continue;
            const std::string d = trim(t.substr(1));
            if (d.rfind("include", 0) != 0) continue;
            const size_t q0 = d.find('"');
            if (q0 == std::string::npos) continue;   // <...> 는 외부 라이브러리
            const size_t q1 = d.find('"', q0 + 1);
            if (q1 == std::string::npos) continue;
            const std::string spec = d.substr(q0 + 1, q1 - q0 - 1);
            std::string target;
            for (const fs::path& base : { dir, fs::path() }) {   // 파일 기준, 그다음 src 기준
                const std::string cand = (base / spec).lexically_normal().generic_string();
                if (files.count(cand)) { target = cand; break; }
            }
            if (target.empty()) continue;   // 생성 파일 · 외부 헤더
            if (moduleOf(target) != moduleOf(rel)) edges.push_back({ rel, target });
        }
    }
}

bool acyclic(const std::map<std::string, std::set<std::string>>& g, std::string& cycleAt) {
    std::map<std::string, int> state;   // 0 미방문, 1 방문 중, 2 끝
    std::vector<std::string> stack;
    std::function<bool(const std::string&)> visit = [&](const std::string& m) {
        state[m] = 1;
        stack.push_back(m);
        auto it = g.find(m);
        if (it != g.end())
            for (const std::string& d : it->second) {
                if (state[d] == 1) {
                    cycleAt.clear();
                    for (auto s = std::find(stack.begin(), stack.end(), d); s != stack.end(); ++s) cycleAt += *s + " -> ";
                    cycleAt += d;
                    return false;
                }
                if (state[d] == 0 && !visit(d)) return false;
            }
        stack.pop_back();
        state[m] = 2;
        return true;
    };
    for (const auto& [m, _] : g)
        if (state[m] == 0 && !visit(m)) return false;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const bool list = argc > 1 && std::string(argv[1]) == "--list";
    const fs::path root = HARUKA_SOURCE_DIR;
    const fs::path src  = root / "src";

    const Rules rules = loadRules(root / "tests" / "ArchitectureRules.txt");
    std::set<std::string> files;
    std::vector<Edge> edges;
    scan(src, files, edges);

    if (list) {
        std::map<std::string, std::vector<std::string>> byPair;
        for (const Edge& e : edges) {
            const std::string a = moduleOf(e.from), b = moduleOf(e.to);
            auto it = rules.deps.find(a);
            if (it != rules.deps.end() && it->second.count(b)) continue;
            byPair[a + " -> " + b].push_back("allow " + e.from + " -> " + e.to);
        }
        for (const auto& [pair, lines] : byPair) {
            std::printf("# %s\n", pair.c_str());
            for (const std::string& l : lines) std::printf("%s\n", l.c_str());
        }
        return 0;
    }

    std::printf("Haruka 모듈 의존 방향 시험\n");
    std::printf("\n[규칙 파일]\n");
    for (const std::string& e : rules.errors) std::printf("    %s\n", e.c_str());
    check(rules.errors.empty(), "규칙 파일을 읽음(모르는 줄 · 중복 없음)");

    std::set<std::string> modules;   // 실제 모듈: src 아래 폴더 + main
    for (const auto& e : fs::directory_iterator(src))
        if (e.is_directory()) modules.insert(e.path().filename().string());
    modules.insert("main");
    std::vector<std::string> undeclared, unknownDecl, unknownDep;
    for (const std::string& m : modules) if (!rules.deps.count(m)) undeclared.push_back(m);
    for (const auto& [m, ds] : rules.deps) {
        if (!modules.count(m)) unknownDecl.push_back(m);
        for (const std::string& d : ds) if (!rules.deps.count(d)) unknownDep.push_back(m + " -> " + d);
    }
    for (const auto& m : undeclared) std::printf("    규칙이 없는 모듈: %s\n", m.c_str());
    for (const auto& m : unknownDecl) std::printf("    폴더가 없는 모듈: %s\n", m.c_str());
    for (const auto& m : unknownDep) std::printf("    규칙에 없는 대상: %s\n", m.c_str());
    check(undeclared.empty(), "src 아래 모든 모듈에 규칙이 있음(새 폴더는 규칙부터)");
    check(unknownDecl.empty() && unknownDep.empty(), "규칙의 모듈 이름이 모두 실제 모듈");
    std::string cycle;
    const bool dag = acyclic(rules.deps, cycle);
    if (!dag) std::printf("    순환: %s\n", cycle.c_str());
    check(dag, "허용 방향 표에 순환이 없음");

    std::printf("\n[include 방향] 소스 %zu 개, 모듈을 건너는 include %zu 개\n", files.size(), edges.size());
    std::map<std::string, std::vector<std::string>> violations;   // 모듈 → 위반 설명
    std::set<std::pair<std::string, std::string>> used;
    size_t allowed = 0;
    for (const Edge& e : edges) {
        const std::string a = moduleOf(e.from), b = moduleOf(e.to);
        auto it = rules.deps.find(a);
        if (it != rules.deps.end() && it->second.count(b)) continue;
        if (rules.allow.count({ e.from, e.to })) { used.insert({ e.from, e.to }); ++allowed; continue; }
        violations[a].push_back(e.from + " -> " + e.to + "  (" + a + " -> " + b + ")");
    }
    for (const std::string& m : modules) {
        const auto it = violations.find(m);
        if (it != violations.end())
            for (const std::string& v : it->second) std::printf("    규칙 밖: %s\n", v.c_str());
        check(it == violations.end(), "모듈 " + m + " 의 include 가 허용 방향 안(허용 목록 포함)");
    }

    std::printf("\n[허용 목록] 항목 %zu 개, 지금 쓰이는 것 %zu 개(include %zu 줄)\n", rules.allow.size(), used.size(), allowed);
    std::vector<std::string> stale;
    for (const auto& p : rules.allow) if (!used.count(p)) stale.push_back(p.first + " -> " + p.second);
    for (const std::string& s : stale) std::printf("    필요 없어진 항목(지우십시오): %s\n", s.c_str());
    check(stale.empty(), "허용 목록의 항목이 모두 아직 남은 위반(고친 것은 목록에서 뺌)");

    std::printf("\n결과: %s (검사 %d, 실패 %d)\n", g_failed ? "FAIL" : "PASS", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
