#pragma once
// 액터 세트의 GUID 를 새로 주고, 세트 안을 가리키던 참조를 함께 바꿉니다(참조 체계 R-5 · D4, Docs/Object_References.md).
// 프리팹 꺼내기 · 붙여넣기 · 복제(Fresh 정책)가 씁니다. 씬 형식 글(ACTOR: 블록들)을 받아 글을 돌려줍니다.
//
// 글 단계에서 바꾸므로 컴포넌트가 무엇을 참조하는지 몰라도 되고, 등록되지 않은 컴포넌트(UnknownComponent)의 값도 함께 바뀝니다.
//   - "GUID:<16진수>" 줄: 새 값으로.
//   - 다른 줄의 "actor:<16진수>" 토큰: 그 값이 세트 안 GUID 면 새 값으로, 아니면(세트 밖 액터) 그대로.
//   - "ACTOR:" · "NAME:" 줄(액터 이름)은 건드리지 않습니다. 이름에 같은 글자가 있어도 바뀌지 않습니다.

#include <cctype>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "../core/Guid.hpp"

namespace hguid {

// inUse 가 참이면 그 값은 새 GUID 로 쓰지 않습니다(월드에 이미 있는 값). 바꾼 수를 *count 에.
inline std::string remapFresh(const std::string& text, const std::function<bool(HGuid)>& inUse, int* count = nullptr) {
    constexpr std::string_view kGuidKey = "GUID:";
    constexpr std::string_view kRef     = "actor:";
    auto lineView = [](const std::string& t, size_t b, size_t e) {
        std::string_view v(t.data() + b, e - b);
        if (!v.empty() && v.back() == '\r') v.remove_suffix(1);
        return v;
    };

    // 1) 세트 안 GUID 모으기 → 새 값.
    std::unordered_map<HGuid, HGuid> remap;
    std::unordered_set<HGuid>        taken;
    for (size_t pos = 0; pos < text.size();) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        const std::string_view line = lineView(text, pos, eol);
        HGuid old;
        if (line.substr(0, kGuidKey.size()) == kGuidKey && HGuid::parse(line.substr(kGuidKey.size()), old) && !remap.count(old)) {
            HGuid fresh;
            do { fresh = HGuid::make(); } while ((inUse && inUse(fresh)) || taken.count(fresh));
            taken.insert(fresh);
            remap[old] = fresh;
        }
        pos = eol + 1;
    }
    if (count) *count = (int)remap.size();
    if (remap.empty()) return text;

    // 2) 다시 쓰기.
    std::string out;
    out.reserve(text.size());
    for (size_t pos = 0; pos < text.size();) {
        size_t eol = text.find('\n', pos);
        const bool hasEol = eol != std::string::npos;
        if (!hasEol) eol = text.size();
        std::string line(text.data() + pos, eol - pos);
        const std::string_view lv(line);
        HGuid old;
        if (lv.substr(0, 6) == "ACTOR:" || lv.substr(0, 5) == "NAME:") {
            // 이름 줄은 그대로
        } else if (lv.substr(0, kGuidKey.size()) == kGuidKey) {
            std::string_view rest = lv.substr(kGuidKey.size());
            const bool cr = !rest.empty() && rest.back() == '\r';
            if (cr) rest.remove_suffix(1);
            if (HGuid::parse(rest, old)) {
                auto it = remap.find(old);
                if (it != remap.end()) line = std::string(kGuidKey) + it->second.toString() + (cr ? "\r" : "");
            }
        } else {
            for (size_t at = line.find(kRef); at != std::string::npos; at = line.find(kRef, at + 1)) {
                const size_t hexAt = at + kRef.size();
                if (hexAt + 16 > line.size()) break;
                if (HGuid::parse(std::string_view(line).substr(hexAt, 16), old)) {
                    // 토큰 뒤가 16진수로 이어지면(17자리 이상) 다른 값이므로 건드리지 않습니다.
                    const bool more = hexAt + 16 < line.size() && std::isxdigit((unsigned char)line[hexAt + 16]);
                    auto it = remap.find(old);
                    if (!more && it != remap.end()) line.replace(hexAt, 16, it->second.toString());
                }
            }
        }
        out += line;
        if (hasEol) out += '\n';
        pos = eol + 1;
    }
    return out;
}

} // namespace hguid
