#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ── 프로젝트 설명 파일 (*.hproject, P3a 2026-11-19) ────────────────────────────
// project.ini 와 같은 "이름 = 값" 형식입니다. 필수 키:
//   name           프로젝트 이름
//   company        회사/제작사 이름 (쓰기 루트 · 패키징용)
//   packageId      Android 패키지 ID (예: com.haruka.game)
//   engineVersion  최소 엔진 버전 (예: 0.1.0)
struct ProjectDescriptor {
    std::string name;
    std::string company;
    std::string packageId;
    std::string engineVersion;
};

namespace project_descriptor {

inline std::string trim(std::string_view s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return std::string(s.substr(a, b - a));
}

inline ProjectDescriptor parse(const std::string& text,
                               std::vector<std::string>& warnings,
                               std::vector<std::string>& errors) {
    ProjectDescriptor d;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) {
            warnings.push_back("line " + std::to_string(lineNo) + ": expected 'name = value'");
            continue;
        }
        const std::string key = trim(std::string_view(t).substr(0, eq));
        const std::string val = trim(std::string_view(t).substr(eq + 1));
        if      (key == "name")          d.name          = val;
        else if (key == "company")       d.company       = val;
        else if (key == "packageId")     d.packageId     = val;
        else if (key == "engineVersion") d.engineVersion = val;
        else warnings.push_back("unknown key '" + key + "'");
    }
    if (d.name.empty())          errors.push_back("missing required key 'name'");
    if (d.company.empty())       errors.push_back("missing required key 'company'");
    if (d.packageId.empty())     errors.push_back("missing required key 'packageId'");
    if (d.engineVersion.empty()) errors.push_back("missing required key 'engineVersion'");
    return d;
}

// projectDir 바로 아래의 *.hproject 파일을 찾습니다(이름순 첫 번째). 없으면 빈 경로.
inline std::filesystem::path findInDirectory(const std::filesystem::path& projectDir) {
    std::error_code ec;
    std::vector<std::filesystem::path> found;
    for (const auto& e : std::filesystem::directory_iterator(projectDir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        if (e.path().extension() == ".hproject") found.push_back(e.path());
    }
    if (found.empty()) return {};
    std::sort(found.begin(), found.end());
    return found.front();
}

inline bool loadFromFile(const std::filesystem::path& file,
                         ProjectDescriptor& out,
                         std::vector<std::string>& warnings,
                         std::vector<std::string>& errors) {
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        errors.push_back("cannot open " + file.generic_string());
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = parse(ss.str(), warnings, errors);
    return errors.empty();
}

} // namespace project_descriptor
