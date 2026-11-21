#pragma once
// Add Component 팝업의 목록 계산(2026-11-21). Unity 의 Add Component 창과 같은 동작입니다.
//  - 검색어가 없고 분류도 고르지 않았으면 분류 목록(Rendering · Audio ...)을 보입니다.
//  - 분류를 고르면 그 분류의 컴포넌트를 이름 순으로 보입니다.
//  - 검색어가 있으면 분류를 가리지 않고 맞는 컴포넌트를 한 목록으로 보입니다. 대소문자 · 공백 · 기호는 무시하고,
//    표시 이름 · 타입 이름(RigidBodyComponent) · 분류 이름에서 찾습니다. 앞에서부터 맞는 것이 먼저 옵니다.
//  - 이미 붙어 있는 타입은 목록에 남기되 고를 수 없습니다(한 액터에 같은 타입은 하나).
// 위젯 · 렌더러와 무관해 화면 없이 검산합니다(scene_components 테스트).

#include "../scene/ComponentRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace hcpick {

inline constexpr const char* kOtherCategory = "Other";

// 분류를 보이는 순서. 여기 없는 분류(Layer 2 가 붙인 이름)는 이 뒤에 이름 순으로, "Other" 는 맨 끝입니다.
inline const std::vector<std::string>& categoryOrder() {
    static const std::vector<std::string> k = { "Rendering", "Audio", "Physics", "Animation", "Effects",
                                                "Navigation & AI", "XR", "UI" };
    return k;
}

inline std::string categoryOf(const ComponentRegistry::Entry& e) {
    return e.category.empty() ? std::string(kOtherCategory) : e.category;
}

// 비교용: ASCII 영문자 · 숫자만 남기고 소문자로("Nav Agent" → "navagent"). 다른 글자(한글 등)는 그대로 둡니다.
inline std::string foldKey(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        const unsigned char c = (unsigned char)ch;
        if (c >= 0x80) out.push_back(ch);
        else if (std::isalnum(c)) out.push_back((char)std::tolower(c));
    }
    return out;
}

// 단어 머리글자들("Exponential Height Fog" → "ehf").
inline std::string initials(std::string_view s) {
    std::string out;
    bool start = true;
    for (char ch : s) {
        const unsigned char c = (unsigned char)ch;
        if (std::isalnum(c)) {
            const bool upperInCamel = !start && std::isupper(c) && !out.empty();
            if (start || upperInCamel) out.push_back((char)std::tolower(c));
            start = false;
        } else {
            start = true;
        }
    }
    return out;
}

// 맞는 정도(작을수록 앞). 맞지 않으면 -1.
inline int matchScore(const ComponentRegistry::Entry& e, std::string_view query) {
    const std::string q = foldKey(query);
    if (q.empty()) return 0;
    const std::string label = foldKey(e.editorLabel);
    if (label.rfind(q, 0) == 0) return 0;                       // 이름 앞부분
    if (initials(e.editorLabel).rfind(q, 0) == 0) return 1;     // 머리글자(ehf)
    // 이름 속 단어의 앞부분("fog" → Exponential Height Fog)
    {
        auto words = [&]() {
            std::vector<std::string> w;
            std::string cur;
            for (char ch : e.editorLabel) {
                if (std::isalnum((unsigned char)ch)) cur.push_back((char)std::tolower((unsigned char)ch));
                else if (!cur.empty()) { w.push_back(cur); cur.clear(); }
            }
            if (!cur.empty()) w.push_back(cur);
            return w;
        }();
        for (size_t i = 0; i < words.size(); ++i) {
            std::string tail;
            for (size_t j = i; j < words.size(); ++j) tail += words[j];
            if (tail.rfind(q, 0) == 0) return 1;
        }
    }
    if (label.find(q) != std::string::npos) return 2;
    if (foldKey(e.name).find(q) != std::string::npos) return 3;  // 타입 이름(RigidBodyComponent)
    if (foldKey(categoryOf(e)).rfind(q, 0) == 0) return 4;       // 분류 이름("xr", "audio")
    return -1;
}

struct Item {
    bool        isCategory = false;   // 분류 행(누르면 그 분류로 들어감)
    int         entry      = -1;      // ComponentRegistry::entries() 의 자리(컴포넌트 행)
    std::string label;                // 보이는 이름(분류 행은 분류 이름)
    std::string category;             // 컴포넌트 행의 분류(검색 결과 오른쪽에 흐리게)
    int         count      = 0;       // 분류 행: 그 안의 컴포넌트 수
    bool        present    = false;   // 이미 붙어 있어 고를 수 없음
    bool selectable() const { return isCategory || !present; }
};

// 목록을 만듭니다. actor 가 null 이면 붙은 것이 없다고 봅니다.
inline std::vector<Item> buildItems(const ComponentRegistry& reg, const Actor* actor, std::string_view query,
                                    std::string_view category) {
    const auto& entries = reg.entries();
    std::vector<Item> out;
    const bool searching = !foldKey(query).empty();

    if (!searching && category.empty()) {
        std::vector<std::string> cats;
        std::vector<int> counts;
        for (const auto& e : entries) {
            if (e.editorLabel.empty()) continue;
            const std::string c = categoryOf(e);
            auto it = std::find(cats.begin(), cats.end(), c);
            if (it == cats.end()) { cats.push_back(c); counts.push_back(1); }
            else ++counts[(size_t)(it - cats.begin())];
        }
        auto rank = [](const std::string& c) {
            const auto& order = categoryOrder();
            auto it = std::find(order.begin(), order.end(), c);
            if (it != order.end()) return (int)(it - order.begin());
            return c == kOtherCategory ? 1 << 20 : (int)order.size();
        };
        std::vector<size_t> idx(cats.size());
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            const int ra = rank(cats[a]), rb = rank(cats[b]);
            return ra != rb ? ra < rb : cats[a] < cats[b];
        });
        for (size_t i : idx) {
            Item it;
            it.isCategory = true;
            it.label      = cats[i];
            it.count      = counts[i];
            out.push_back(std::move(it));
        }
        return out;
    }

    struct Scored { Item item; int score; };
    std::vector<Scored> rows;
    for (int i = 0; i < (int)entries.size(); ++i) {
        const auto& e = entries[(size_t)i];
        if (e.editorLabel.empty()) continue;
        int score = 0;
        if (searching) {
            score = matchScore(e, query);
            if (score < 0) continue;
        } else if (categoryOf(e) != category) {
            continue;
        }
        Item it;
        it.entry    = i;
        it.label    = e.editorLabel;
        it.category = categoryOf(e);
        it.present  = actor && e.find && e.find(*actor) != nullptr;
        rows.push_back({ std::move(it), score });
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Scored& a, const Scored& b) {
        if (a.score != b.score) return a.score < b.score;
        return a.item.label < b.item.label;
    });
    for (auto& r : rows) out.push_back(std::move(r.item));
    return out;
}

// 처음 고를 수 있는 행(없으면 -1).
inline int firstSelectable(const std::vector<Item>& items) {
    for (int i = 0; i < (int)items.size(); ++i)
        if (items[(size_t)i].selectable()) return i;
    return -1;
}

// cur 에서 dir(-1 위 · +1 아래) 쪽의 다음 고를 수 있는 행. 끝에서 멈춥니다(돌지 않음). 없으면 cur.
inline int stepSelection(const std::vector<Item>& items, int cur, int dir) {
    if (items.empty()) return -1;
    if (cur < 0) return firstSelectable(items);
    for (int i = cur + dir; i >= 0 && i < (int)items.size(); i += dir)
        if (items[(size_t)i].selectable()) return i;
    return cur;
}

} // namespace hcpick
