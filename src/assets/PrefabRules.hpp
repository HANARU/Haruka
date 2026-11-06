#pragma once
// 프리팹 파일의 이름 검사 · 머리 줄 판별 · 꺼내다 실패한 액터 정리(2026-11-06, DEF-140 · 141 · 142).
// PrefabManager 와 에디터가 쓰고, 화면 없이 검산합니다(editor_commands 테스트).
#include "../core/Actor.hpp"
#include "../core/World.hpp"

#include <cctype>
#include <string>
#include <vector>

namespace hprefab {

// 액터 이름을 그대로 프리팹 파일 이름으로 쓸 수 있는지. 프리팹 목록(PrefabManager::scan)은 assets/prefabs 한 단계만 훑고,
// UI 글꼴은 ASCII 만 그리므로 폴더 구분자 · Windows 금지 글자 · 제어 문자 · ASCII 밖 글자, '.' · '..', 끝의 점 · 공백,
// 장치 이름(CON · NUL · COM1 등)을 거부합니다. 예전에는 이름을 그대로 써서 '/' 가 있으면 하위 폴더에 저장되고 목록에
// 나오지 않았습니다(DEF-140). "Cube (1)" 같은 복사본 이름은 그대로 됩니다.
inline bool isValidName(const std::string& name, std::string* why = nullptr) {
    auto fail = [&](const char* reason) { if (why) *why = reason; return false; };
    if (name.empty())      return fail("the name is empty");
    if (name.size() > 100) return fail("the name is longer than 100 characters");
    for (char ch : name) {
        const unsigned char c = (unsigned char)ch;
        if (c < 32 || c >= 127) return fail("only ASCII letters, digits, spaces and punctuation are allowed");
        if (std::string("/\\:*?\"<>|").find(ch) != std::string::npos)
            return fail("the name has a character a file name cannot have (/ \\ : * ? \" < > |)");
    }
    if (name == "." || name == "..")             return fail("'.' and '..' are not file names");
    if (name.front() == ' ')                     return fail("the name starts with a space");
    if (name.back() == ' ' || name.back() == '.') return fail("the name ends with a space or a dot");
    // Windows 장치 이름은 확장자가 붙어도 파일로 만들 수 없습니다(CON.hprefab 등).
    std::string stem = name.substr(0, name.find('.'));
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    for (char& ch : stem) ch = (char)std::toupper((unsigned char)ch);
    static const char* const kReserved[] = { "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
    for (const char* r : kReserved)
        if (stem == r) return fail("the name is a Windows device name");
    return true;
}

// 텍스트 앞의 UTF-8 BOM 을 떼고, v3 머리 줄("# Haruka Prefab v3")이 있는지 봅니다. 머리 줄 앞의 빈 줄 · 다른 주석은
// 건너뜁니다. 예전에는 첫 바이트부터 머리 줄이어야 해서, BOM 이나 앞의 빈 줄 · 주석이 있으면 v1 경로로 빠져
// "Prefab missing NAME" 오류만 남았습니다(DEF-142).
inline bool stripBomAndDetectV3(std::string& text) {
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        text.erase(0, 3);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        size_t b = pos, e = eol;
        while (b < e && (text[b] == ' ' || text[b] == '\t')) ++b;
        while (e > b && (text[e - 1] == '\r' || text[e - 1] == ' ' || text[e - 1] == '\t')) --e;
        if (b < e) {
            if (text.compare(b, 18, "# Haruka Prefab v3") == 0) return true;
            if (text[b] != '#') return false;   // 액터 줄(v1 · v2 의 NAME: 등)이 먼저 나옴
        }
        pos = eol + 1;
    }
    return false;
}

// v3 프리팹을 꺼낸 슬롯(readScene 결과, 실패한 블록은 null) 중 루트(첫 슬롯)의 서브트리 밖에 남은 액터를 지웁니다.
// 루트가 실패했으면 꺼낸 액터를 모두 지웁니다. 중간 블록이 실패하면 그 자식들은 부모 없는 루트로 월드에 남아, 선택 ·
// 생성 Undo 어디에도 잡히지 않았습니다(DEF-141). 지운 액터 수(서브트리 루트 기준)를 돌려줍니다.
inline int discardOrphans(World& world, const std::vector<Actor*>& slots) {
    Actor* const root = slots.empty() ? nullptr : slots[0];
    // 먼저 고아 루트(부모 없는 슬롯)를 모두 모으고 나서 지웁니다. 지우는 도중에 훑으면, 앞서 지운 고아의 자식 슬롯이
    // 해제된 액터를 가리킵니다. 부모가 있는 슬롯은 그 부모(루트 쪽이든 고아 쪽이든)와 함께 정리됩니다.
    std::vector<Actor*> orphans;
    for (Actor* a : slots)
        if (a && a != root && !a->getParent()) orphans.push_back(a);
    for (Actor* a : orphans) world.destroyActor(a);   // 자식(같은 고아 서브트리)도 함께 지워집니다
    return (int)orphans.size();
}

} // namespace hprefab
