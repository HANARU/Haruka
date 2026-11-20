#include "PrefabLink.hpp"

#include "SceneSerializer.hpp"
#include "PrefabRules.hpp"
#include "../scene/Actor.hpp"
#include "../scene/SceneLog.hpp"
#include "../scene/World.hpp"
#include "../vfs/LogicalPath.hpp"
#include "../vfs/Vfs.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <unordered_set>

namespace hprefablink {

// ── 캐시 ──────────────────────────────────────────────────────────────────────
bool PrefabSourceCache::get(const std::string& path, std::string& out) {
    const std::string key = hvfs::toCanonical(path);
    if (auto it = texts_.find(key); it != texts_.end()) { out = it->second; return true; }
    if (!hvfs::readText(path, out)) return false;
    texts_[key] = out;
    return true;
}
bool PrefabSourceCache::peek(const std::string& path, std::string* out) const {
    auto it = texts_.find(hvfs::toCanonical(path));
    if (it == texts_.end()) return false;
    if (out) *out = it->second;
    return true;
}
void PrefabSourceCache::set(const std::string& path, std::string text) { texts_[hvfs::toCanonical(path)] = std::move(text); }
void PrefabSourceCache::erase(const std::string& path) { texts_.erase(hvfs::toCanonical(path)); }

Loader loaderFor(const World* world) {
    if (world)
        if (PrefabSourceCache* cache = world->service<PrefabSourceCache>())
            return [cache](const std::string& p, std::string& t) { return cache->get(p, t); };
    return [](const std::string& p, std::string& t) { return hvfs::readText(p, t); };
}

// splitmix64 의 마무리 섞기.
static uint64_t mix(uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}
HGuid derive(HGuid root, HGuid source) {
    HGuid g;
    g.value = mix(root.value ^ mix(source.value ^ 0x5052454641424c4bull));   // "PREFABLK"
    if (!g.value) g.value = 1;
    return g;
}

static uint64_t hashText(std::string_view s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

static std::string hex(HGuid g) { return g.toString(); }

// ── 글 블록 ───────────────────────────────────────────────────────────────────
std::vector<TBlock> parseBlocks(const std::string& text) {
    std::vector<TBlock> out;
    bool open = false, compOpen = false;
    std::istringstream in(text);
    std::string line;
    auto close = [&] { open = false; compOpen = false; };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line == "END_ACTOR" || line == "END_PREFAB") { close(); continue; }
        if (line.rfind("ACTOR:", 0) == 0) {
            close();
            out.emplace_back();
            out.back().name = line.substr(6);
            open = true;
            continue;
        }
        if (line.rfind("PREFAB:", 0) == 0) {
            close();
            out.emplace_back();
            out.back().instance = true;
            out.back().source   = line.substr(7);
            open = true;
            continue;
        }
        if (!open) continue;
        TBlock& b = out.back();
        auto keyVal = [&](std::string& k, std::string& v) {
            const size_t c = line.find(':');
            if (c == std::string::npos) return false;
            k = line.substr(0, c);
            v = line.substr(c + 1);
            return true;
        };
        std::string k, v;
        if (b.instance) {
            if (!keyVal(k, v)) continue;
            if (k == "GUID")             HGuid::parse(v, b.guid);
            else if (k == "PARENT")      { std::istringstream ss(v); ss >> b.parent; }
            else if (k == "PARENT_GUID") HGuid::parse(v, b.parentGuid);
            else                         b.lines.push_back(line);
            continue;
        }
        if (line.rfind("COMPONENT:", 0) == 0) {
            b.comps.push_back({ line.substr(10), {} });
            compOpen = true;
            continue;
        }
        if (line == "END_COMPONENT") { compOpen = false; continue; }
        if (compOpen) {
            const size_t eq = line.find('=');
            if (eq != std::string::npos) b.comps.back().fields.emplace_back(line.substr(0, eq), line.substr(eq + 1));
            continue;
        }
        if (!keyVal(k, v)) continue;
        if (k == "GUID")               HGuid::parse(v, b.guid);
        else if (k == "PARENT")        { std::istringstream ss(v); ss >> b.parent; }
        else if (k == "PARENT_GUID")   HGuid::parse(v, b.parentGuid);
        else if (k == "PREFAB_SOURCE") b.linkSource = v;
        else if (k == "PREFAB_PAIR")   HGuid::parse(v, b.linkPair);
        else if (k == "PREFAB_RAW")    b.linkRaw.push_back(v);
        else                           b.props.emplace_back(k, v);
    }
    return out;
}

std::string writeBlocks(const std::vector<TBlock>& blocks) {
    std::ostringstream os;
    for (const TBlock& b : blocks) {
        if (b.instance) {
            os << "PREFAB:" << b.source << "\n";
            if (b.guid.valid()) os << "GUID:" << hex(b.guid) << "\n";
            if (b.parent >= 0) os << "PARENT:" << b.parent << "\n";
            if (b.parentGuid.valid()) os << "PARENT_GUID:" << hex(b.parentGuid) << "\n";
            for (const auto& l : b.lines) os << l << "\n";
            os << "END_PREFAB\n";
            continue;
        }
        os << "ACTOR:" << b.name << "\n";
        if (b.guid.valid()) os << "GUID:" << hex(b.guid) << "\n";
        for (const auto& [k, v] : b.props) os << k << ":" << v << "\n";
        if (b.parent >= 0) os << "PARENT:" << b.parent << "\n";
        if (b.parentGuid.valid()) os << "PARENT_GUID:" << hex(b.parentGuid) << "\n";
        if (!b.linkSource.empty()) os << "PREFAB_SOURCE:" << b.linkSource << "\n";
        if (b.linkPair.valid()) os << "PREFAB_PAIR:" << hex(b.linkPair) << "\n";
        for (const auto& r : b.linkRaw) os << "PREFAB_RAW:" << r << "\n";
        for (const TComp& c : b.comps) {
            os << "COMPONENT:" << c.type << "\n";
            for (const auto& [k, v] : c.fields) os << k << "=" << v << "\n";
            os << "END_COMPONENT\n";
        }
        os << "END_ACTOR\n";
    }
    return os.str();
}

bool needsExpand(const std::string& text) {
    if (text.rfind("PREFAB:", 0) == 0 || text.find("\nPREFAB:") != std::string::npos) return true;
    return text.find("PARENT_GUID:") != std::string::npos;
}

// actor:<16진수> 토큰을 표로 바꿉니다(GuidRemap 과 같은 규칙).
static void remapRefs(std::string& s, const std::unordered_map<HGuid, HGuid>& m) {
    constexpr std::string_view kRef = "actor:";
    for (size_t at = s.find(kRef); at != std::string::npos; at = s.find(kRef, at + 1)) {
        const size_t h = at + kRef.size();
        if (h + 16 > s.size()) break;
        HGuid old;
        if (!HGuid::parse(std::string_view(s).substr(h, 16), old)) continue;
        if (h + 16 < s.size() && std::isxdigit((unsigned char)s[h + 16])) continue;
        if (auto it = m.find(old); it != m.end()) s.replace(h, 16, it->second.toString());
    }
}
static void remapRefs(TBlock& b, const std::unordered_map<HGuid, HGuid>& m) {
    for (auto& kv : b.props) remapRefs(kv.second, m);
    for (auto& c : b.comps)
        for (auto& kv : c.fields) remapRefs(kv.second, m);
}

static std::string stemOf(const std::string& path) {
    std::string s = path;
    if (const size_t sl = s.find_last_of('/'); sl != std::string::npos) s = s.substr(sl + 1);
    if (const size_t dot = s.rfind('.'); dot != std::string::npos && dot > 0) s = s.substr(0, dot);
    return s.empty() ? std::string("Prefab") : s;
}

static void err(std::vector<std::string>* errors, std::string msg) {
    HLOG(LogScene, Error) << msg;
    if (errors) errors->push_back(std::move(msg));
}
static void warn(std::vector<std::string>* errors, std::string msg) {
    HLOG(LogScene, Warning) << msg;
    if (errors) errors->push_back(std::move(msg));
}

// 경로 "<형>[<순번>].<칸>" 을 나눕니다. 컴포넌트 경로가 아니면 false.
static bool splitCompPath(const std::string& path, std::string& type, int& k, std::string* field) {
    const size_t lb = path.find('[');
    if (lb == std::string::npos || lb == 0) return false;
    const size_t rb = path.find(']', lb);
    if (rb == std::string::npos) return false;
    try { k = std::stoi(path.substr(lb + 1, rb - lb - 1)); } catch (...) { return false; }
    if (k < 0) return false;
    type = path.substr(0, lb);
    if (field) {
        if (rb + 1 >= path.size() || path[rb + 1] != '.') return false;
        *field = path.substr(rb + 2);
    }
    return true;
}
static TComp* nthComp(TBlock& b, const std::string& type, int k, bool grow) {
    int seen = 0;
    for (TComp& c : b.comps)
        if (c.type == type && seen++ == k) return &c;
    if (!grow) return nullptr;
    for (; seen <= k; ++seen) b.comps.push_back({ type, {} });
    return &b.comps.back();
}
static void setKV(std::vector<std::pair<std::string, std::string>>& kvs, const std::string& k, const std::string& v) {
    for (auto& kv : kvs)
        if (kv.first == k) { kv.second = v; return; }
    kvs.emplace_back(k, v);
}

// 펼친 소속 액터 목록에 인스턴스 블록의 줄을 넣습니다(D5).
static void applyLines(std::vector<TBlock>& E, const std::vector<std::string>& lines, const std::string& source,
                       std::vector<std::string>* errors) {
    std::unordered_map<HGuid, int> byPair;
    for (int i = 0; i < (int)E.size(); ++i) byPair.emplace(E[i].linkPair, i);
    auto find = [&](std::string_view hexv, int& idx) {
        HGuid p;
        if (!HGuid::parse(hexv, p)) return false;
        auto it = byPair.find(p);
        if (it == byPair.end()) return false;
        idx = it->second;
        return true;
    };

    struct CompRemove { int idx; std::string type; int k; };
    std::vector<CompRemove> compRemoves;
    std::vector<int>        actorRemoves;
    std::vector<std::pair<int, std::string>> overrides;   // (idx, "path=value")
    std::vector<std::pair<int, HGuid>>       guids;
    for (const std::string& l : lines) {
        const size_t c = l.find(':');
        if (c == std::string::npos) continue;
        const std::string key = l.substr(0, c);
        const std::string rest = l.substr(c + 1);
        const size_t sp = rest.find(' ');
        const std::string pairHex = rest.substr(0, sp);
        const std::string tail = sp == std::string::npos ? std::string() : rest.substr(sp + 1);
        int idx = -1;
        if (key != "OVERRIDE" && key != "REMOVE" && key != "MEMBER_GUID") {
            warn(errors, "prefab instance of " + source + ": unknown line '" + l + "' skipped");
            continue;
        }
        if (!find(pairHex, idx)) {
            warn(errors, "prefab instance of " + source + ": '" + l + "' names an actor the prefab no longer has; skipped");
            continue;
        }
        if (key == "OVERRIDE") {
            if (tail.find('=') == std::string::npos) { warn(errors, "prefab override without '=': " + l); continue; }
            overrides.emplace_back(idx, tail);
        } else if (key == "REMOVE") {
            if (tail.empty()) { actorRemoves.push_back(idx); continue; }
            CompRemove r{ idx, {}, 0 };
            if (!splitCompPath(tail, r.type, r.k, nullptr)) { warn(errors, "prefab remove line not understood: " + l); continue; }
            compRemoves.push_back(r);
        } else {
            HGuid g;
            if (HGuid::parse(tail, g) && g.valid()) guids.emplace_back(idx, g);
        }
    }

    // 1) 지운 컴포넌트(같은 형 안 순번이 큰 것부터, 앞 순번이 밀리지 않게).
    std::sort(compRemoves.begin(), compRemoves.end(), [](const CompRemove& a, const CompRemove& b) { return a.k > b.k; });
    for (const CompRemove& r : compRemoves) {
        TBlock& b = E[(size_t)r.idx];
        int seen = 0;
        for (auto it = b.comps.begin(); it != b.comps.end(); ++it)
            if (it->type == r.type && seen++ == r.k) { b.comps.erase(it); break; }
    }
    // 2) 덮어쓰기.
    for (const auto& [idx, pv] : overrides) {
        const size_t eq = pv.find('=');
        const std::string path = pv.substr(0, eq), value = pv.substr(eq + 1);
        TBlock& b = E[(size_t)idx];
        std::string type, field;
        int k = 0;
        if (path == "NAME") b.name = value;
        else if (path == "@PARENT") {
            int p = -1;
            if (idx == 0 || !find(value, p) || p == idx) { warn(errors, "prefab instance of " + source + ": bad @PARENT " + value); continue; }
            bool cycle = false;
            for (int q = p; q >= 0; q = E[(size_t)q].parent) if (q == idx) { cycle = true; break; }
            if (cycle) { warn(errors, "prefab instance of " + source + ": @PARENT would make a cycle; skipped"); continue; }
            b.parent = p;
        } else if (splitCompPath(path, type, k, &field)) {
            setKV(nthComp(b, type, k, true)->fields, field, value);
        } else {
            setKV(b.props, path, value);
        }
    }
    // 3) GUID.
    for (const auto& [idx, g] : guids) E[(size_t)idx].guid = g;
    // 4) 지운 액터(서브트리째). 루트는 지울 수 없습니다.
    if (!actorRemoves.empty()) {
        std::vector<char> gone(E.size(), 0);
        for (int i : actorRemoves) {
            if (i == 0) { warn(errors, "prefab instance of " + source + ": the root cannot be removed; skipped"); continue; }
            gone[(size_t)i] = 1;
        }
        // 부모가 지워졌으면 자식도(부모가 뒤에 올 수도 있어 바뀌지 않을 때까지).
        for (bool changed = true; changed;) {
            changed = false;
            for (size_t i = 0; i < E.size(); ++i)
                if (!gone[i] && E[i].parent >= 0 && gone[(size_t)E[i].parent]) { gone[i] = 1; changed = true; }
        }
        std::vector<int> newIdx(E.size(), -1);
        std::vector<TBlock> kept;
        for (size_t i = 0; i < E.size(); ++i)
            if (!gone[i]) { newIdx[i] = (int)kept.size(); kept.push_back(std::move(E[i])); }
        for (TBlock& b : kept) b.parent = b.parent >= 0 ? newIdx[(size_t)b.parent] : -1;
        E = std::move(kept);
    }
}

static bool expandSourceRec(const std::string& path, HGuid root, const Loader& load, std::vector<std::string>& stack,
                            std::vector<TBlock>& out, std::vector<std::string>* errors, bool& cycle);

// 블록 목록(보통 블록 + 인스턴스 블록)을 보통 블록으로 펼칩니다. 블록 순번 부모 · PARENT_GUID 를 펼친 순번으로 바꿉니다.
// 안쪽 어디서든 순환을 만나면 cycle 이 참이 됩니다(그 원본은 통째로 거부, D8).
static std::vector<TBlock> expandBlocks(const std::vector<TBlock>& blocks, const Loader& load, std::vector<std::string>& stack,
                                        std::vector<std::string>* errors, bool& cycle) {
    std::vector<TBlock> out;
    std::vector<int> first(blocks.size(), -1);
    std::vector<int> topParent;   // 펼친 액터마다: 블록 순번 부모(-1 = 없음), -2 = 인스턴스 안 부모(이미 펼친 순번)
    for (size_t i = 0; i < blocks.size(); ++i) {
        const TBlock& b = blocks[i];
        first[i] = (int)out.size();
        if (!b.instance) {
            TBlock c = b;
            topParent.push_back(c.parent);
            c.parent = -1;
            out.push_back(std::move(c));
            continue;
        }
        HGuid R = b.guid;
        if (!R.valid()) R = HGuid::make();
        std::vector<TBlock> E;
        bool innerCycle = false;
        const bool expanded = expandSourceRec(b.source, R, load, stack, E, errors, innerCycle);
        if (innerCycle) cycle = true;
        if (expanded) {
            applyLines(E, b.lines, b.source, errors);
            const int offset = (int)out.size();
            for (size_t j = 0; j < E.size(); ++j) {
                TBlock& e = E[j];
                if (j == 0) { topParent.push_back(b.parent); e.parent = -1; e.parentGuid = b.parentGuid; }
                else        { topParent.push_back(-2); if (e.parent >= 0) e.parent += offset; }
                out.push_back(std::move(e));
            }
        } else {
            // 펼치지 못한 인스턴스: 빈 루트만 두고 줄을 그대로 기억합니다(D8).
            TBlock p;
            p.name       = stemOf(b.source);
            p.guid       = R;
            p.parentGuid = b.parentGuid;
            p.linkSource = b.source;
            p.linkRaw    = b.lines;
            topParent.push_back(b.parent);
            out.push_back(std::move(p));
        }
    }
    for (size_t k = 0; k < out.size(); ++k) {
        const int tp = topParent[k];
        if (tp == -2) continue;
        out[k].parent = (tp >= 0 && tp < (int)blocks.size() && first[(size_t)tp] != (int)k) ? first[(size_t)tp] : -1;
    }
    std::unordered_map<HGuid, int> byGuid;
    for (size_t k = 0; k < out.size(); ++k)
        if (out[k].guid.valid()) byGuid.emplace(out[k].guid, (int)k);
    for (size_t k = 0; k < out.size(); ++k) {
        if (!out[k].parentGuid.valid()) continue;
        auto it = byGuid.find(out[k].parentGuid);
        if (it != byGuid.end() && it->second != (int)k) out[k].parent = it->second;
        else warn(errors, "actor '" + out[k].name + "': parent " + hex(out[k].parentGuid)
                              + " is not in the scene (removed from its prefab?); placed at the root");
        out[k].parentGuid = HGuid{};
    }
    return out;
}

static bool expandSourceRec(const std::string& path, HGuid root, const Loader& load, std::vector<std::string>& stack,
                            std::vector<TBlock>& out, std::vector<std::string>* errors, bool& cycle) {
    const std::string canon = hvfs::toCanonical(path);
    if (std::find(stack.begin(), stack.end(), canon) != stack.end()) {
        cycle = true;
        std::string chain;
        for (const auto& s : stack) chain += s + " -> ";
        err(errors, "prefab cycle refused: " + chain + canon);
        return false;
    }
    std::string text;
    if (!load || !load(path, text)) { err(errors, "prefab not found: " + path); return false; }
    stack.push_back(canon);
    bool innerCycle = false;
    std::vector<TBlock> E = expandBlocks(parseBlocks(text), load, stack, errors, innerCycle);
    stack.pop_back();
    if (innerCycle) { cycle = true; return false; }
    if (E.empty()) { err(errors, "prefab has no actor: " + path); return false; }

    // GUID 가 없는 옛 프리팹(v4 이하)은 경로 + 순번으로 짝을 만듭니다(다시 읽어도 같은 값).
    const HGuid seed{ hashText(canon) };
    std::unordered_set<HGuid> seen;
    for (size_t i = 0; i < E.size(); ++i) {
        if (!E[i].guid.valid() || !seen.insert(E[i].guid).second) {
            E[i].guid = derive(seed, HGuid{ (uint64_t)i + 1 });
            seen.insert(E[i].guid);
        }
    }
    std::unordered_map<HGuid, HGuid> m;
    for (size_t i = 0; i < E.size(); ++i) m[E[i].guid] = i == 0 ? root : derive(root, E[i].guid);
    for (size_t i = 0; i < E.size(); ++i) {
        TBlock& b = E[i];
        b.linkPair   = b.guid;
        b.linkSource = i == 0 ? path : std::string();
        b.linkRaw.clear();
        b.guid = m[b.guid];
        remapRefs(b, m);
    }
    E[0].parent = -1;
    out = std::move(E);
    return true;
}

bool expandSource(const std::string& path, HGuid root, const Loader& load, std::vector<TBlock>& out,
                  std::vector<std::string>* errors) {
    std::vector<std::string> stack;
    bool cycle = false;
    return expandSourceRec(path, root, load, stack, out, errors, cycle);
}

std::string expandText(const std::string& text, const Loader& load, std::vector<std::string>* errors) {
    std::vector<std::string> stack;
    bool cycle = false;   // 씬 단계에서는 그 인스턴스 블록만 빈 루트가 됩니다
    return writeBlocks(expandBlocks(parseBlocks(text), load, stack, errors, cycle));
}

static bool dependsRec(const std::string& source, const std::string& target, const Loader& load, std::set<std::string>& seen) {
    const std::string canon = hvfs::toCanonical(source);
    if (canon == target) return true;
    if (!seen.insert(canon).second) return false;
    std::string text;
    if (!load || !load(source, text)) return false;
    for (const TBlock& b : parseBlocks(text))
        if (b.instance && dependsRec(b.source, target, load, seen)) return true;
    return false;
}
bool dependsOn(const std::string& source, const std::string& target, const Loader& load) {
    std::set<std::string> seen;
    return dependsRec(source, hvfs::toCanonical(target), load, seen);
}

// ── 소속 ─────────────────────────────────────────────────────────────────────
const Actor* instanceRootOf(const Actor* a) {
    if (!a) return nullptr;
    if (!a->prefabLink.source.empty()) return a;
    if (!a->prefabLink.pair.valid()) return nullptr;
    // 소속이면 부모도 같은 인스턴스의 소속(또는 루트)이어야 합니다(D5: 더한 액터 아래로 옮기면 빠짐).
    const Actor* p = a->getParent();
    if (!p) return nullptr;
    const Actor* r = instanceRootOf(p);
    return (r && r->prefabLink.raw.empty()) ? r : nullptr;
}
Actor* instanceRootOf(Actor* a) { return const_cast<Actor*>(instanceRootOf(static_cast<const Actor*>(a))); }

std::vector<Actor*> membersOf(Actor* root) {
    std::vector<Actor*> out;
    if (!root || root->prefabLink.source.empty()) return out;
    std::unordered_set<HGuid> pairs;
    std::function<void(Actor*)> walk = [&](Actor* a) {
        out.push_back(a);
        pairs.insert(a->prefabLink.pair);
        for (Actor* c : a->getChildren())
            if (c->prefabLink.source.empty() && c->prefabLink.pair.valid() && !pairs.count(c->prefabLink.pair)) walk(c);
    };
    walk(root);
    return out;
}

// ── 비교 ─────────────────────────────────────────────────────────────────────
using Flat = std::vector<std::pair<std::string, std::string>>;

// 액터 블록 하나를 (경로 → 값)으로 폅니다(D4). parentPair 가 있으면 @PARENT.
static Flat flatten(const TBlock& b, const std::string& parentPair) {
    Flat f;
    f.emplace_back("NAME", b.name);
    for (const auto& kv : b.props) f.push_back(kv);
    if (!parentPair.empty()) f.emplace_back("@PARENT", parentPair);
    std::map<std::string, int> count;
    for (const TComp& c : b.comps) {
        const int k = count[c.type]++;
        const std::string pre = c.type + "[" + std::to_string(k) + "].";
        for (const auto& kv : c.fields) f.emplace_back(pre + kv.first, kv.second);
    }
    return f;
}
static std::map<std::string, int> compCounts(const TBlock& b) {
    std::map<std::string, int> n;
    for (const TComp& c : b.comps) ++n[c.type];
    return n;
}

// 짝 하나의 덮어쓰기 줄을 더합니다.
static void diffInto(const std::string& pairHex, const TBlock& cur, const std::string& curParent, const TBlock& base,
                     const std::string& baseParent, std::vector<std::string>& lines, std::vector<std::string>* paths) {
    const Flat fc = flatten(cur, curParent), fb = flatten(base, baseParent);
    std::unordered_map<std::string, const std::string*> mb, mc;
    for (const auto& kv : fb) mb.emplace(kv.first, &kv.second);
    for (const auto& kv : fc) mc.emplace(kv.first, &kv.second);
    // 지운 컴포넌트.
    const auto nc = compCounts(cur), nb = compCounts(base);
    for (const auto& [type, n] : nb) {
        const auto it = nc.find(type);
        for (int k = it == nc.end() ? 0 : it->second; k < n; ++k) {
            const std::string p = type + "[" + std::to_string(k) + "]";
            lines.push_back("REMOVE:" + pairHex + " " + p);
            if (paths) paths->push_back(p);
        }
    }
    for (const auto& [path, value] : fc) {
        auto it = mb.find(path);
        if (it != mb.end() && *it->second == value) continue;
        lines.push_back("OVERRIDE:" + pairHex + " " + path + "=" + value);
        if (paths) paths->push_back(path);
    }
    // 원본에만 있는 액터 수준 줄(태그를 모두 지움 등)은 빈 값으로.
    for (const auto& [path, value] : fb) {
        if (mc.count(path) || path.find('[') != std::string::npos || path == "@PARENT") continue;
        lines.push_back("OVERRIDE:" + pairHex + " " + path + "=");
        if (paths) paths->push_back(path);
    }
}

static TBlock blockOf(const Actor& a) {
    std::ostringstream os;
    os << "ACTOR:" << a.name << "\n";
    SceneSerializer::writeActor(os, &a, -1, false);
    os << "END_ACTOR\n";
    std::vector<TBlock> v = parseBlocks(os.str());
    return v.empty() ? TBlock{} : std::move(v[0]);
}

struct InstanceInfo {
    bool                           ok = false;
    std::vector<TBlock>            E;
    std::unordered_map<HGuid, int> byPair;
};
static InstanceInfo expandFor(const Actor& root, const Loader& load, std::vector<std::string>* errors) {
    InstanceInfo info;
    if (!root.prefabLink.raw.empty()) return info;
    info.ok = expandSource(root.prefabLink.source, root.guid(), load, info.E, errors);
    for (int i = 0; i < (int)info.E.size(); ++i) info.byPair.emplace(info.E[(size_t)i].linkPair, i);
    return info;
}

// 인스턴스 하나의 소속 액터(세트 안 순번)로 덮어쓰기 줄을 만듭니다.
static std::vector<std::string> linesFor(const InstanceInfo& info, const std::vector<const Actor*>& members,
                                         const std::function<const Actor*(const Actor*)>& parentOf,
                                         const Actor* only = nullptr, std::vector<std::string>* paths = nullptr) {
    std::vector<std::string> lines;
    std::unordered_set<HGuid> present;
    for (const Actor* a : members) present.insert(a->prefabLink.pair);
    for (size_t i = 0; i < members.size(); ++i) {
        const Actor* a = members[i];
        const HGuid pair = a->prefabLink.pair;
        auto it = info.byPair.find(pair);
        if (it == info.byPair.end()) continue;
        if (only && a != only) continue;
        const TBlock& base = info.E[(size_t)it->second];
        const std::string baseParent = base.parent >= 0 ? hex(info.E[(size_t)base.parent].linkPair) : std::string();
        const Actor* p = i == 0 ? nullptr : parentOf(a);
        const std::string curParent = p ? hex(p->prefabLink.pair) : std::string();
        diffInto(hex(pair), blockOf(*a), curParent, base, baseParent, lines, paths);
        if (a->guid() != base.guid) lines.push_back("MEMBER_GUID:" + hex(pair) + " " + hex(a->guid()));
    }
    if (only) return lines;
    for (size_t j = 1; j < info.E.size(); ++j) {
        const TBlock& b = info.E[j];
        if (present.count(b.linkPair)) continue;
        if (b.parent >= 0 && !present.count(info.E[(size_t)b.parent].linkPair)) continue;   // 부모가 지워졌으면 함께 빠짐
        lines.push_back("REMOVE:" + hex(b.linkPair));
    }
    return lines;
}

std::string writeCompact(const std::vector<const Actor*>& set, const std::vector<int>& parentIndex, const Loader& load,
                         const std::string& flattenRoot, std::vector<std::string>* errors) {
    const size_t n = set.size();
    auto par = [&](size_t i) { return i < parentIndex.size() ? parentIndex[i] : -1; };
    std::vector<int> root(n, -1);
    std::unordered_map<int, InstanceInfo> infos;
    std::unordered_map<int, std::unordered_set<HGuid>> used;
    const std::string flattenCanon = flattenRoot.empty() ? std::string() : hvfs::toCanonical(flattenRoot);
    for (size_t i = 0; i < n; ++i) {
        const Actor* a = set[i];
        const int p = par(i);
        if (!a->prefabLink.source.empty()) {
            if (i == 0 && !flattenCanon.empty() && hvfs::toCanonical(a->prefabLink.source) == flattenCanon) continue;
            InstanceInfo info = expandFor(*a, load, errors);
            if (!info.ok && a->prefabLink.raw.empty()) {
                warn(errors, "prefab instance '" + a->name + "': " + a->prefabLink.source
                                 + " cannot be expanded; saved as plain actors (link lost)");
                continue;
            }
            root[i] = (int)i;
            used[(int)i].insert(a->prefabLink.pair);
            infos.emplace((int)i, std::move(info));
            continue;
        }
        if (!a->prefabLink.pair.valid() || p < 0 || root[(size_t)p] < 0) continue;
        const int r = root[(size_t)p];
        const InstanceInfo& info = infos[r];
        if (!info.ok || !info.byPair.count(a->prefabLink.pair)) continue;   // 원본에 없는 짝 = 더한 액터
        if (!used[r].insert(a->prefabLink.pair).second) continue;          // 같은 짝이 둘이면 뒤엣것은 더한 액터
        root[i] = r;
    }

    std::vector<int> blockIndex(n, -1);
    int next = 0;
    for (size_t i = 0; i < n; ++i)
        if (root[i] < 0 || root[i] == (int)i) blockIndex[i] = next++;

    std::unordered_map<const Actor*, size_t> indexOf;
    for (size_t i = 0; i < n; ++i) indexOf[set[i]] = i;
    auto parentOf = [&](const Actor* a) -> const Actor* {
        const int p = par(indexOf[a]);
        return p >= 0 ? set[(size_t)p] : nullptr;
    };

    std::ostringstream os;
    for (size_t i = 0; i < n; ++i) {
        if (blockIndex[i] < 0) continue;
        const Actor* a = set[i];
        const int p = par(i);
        int   parentBlock = -1;
        HGuid parentGuid;
        if (p >= 0) {
            if (blockIndex[(size_t)p] >= 0) parentBlock = blockIndex[(size_t)p];
            else                            parentGuid  = set[(size_t)p]->guid();
        }
        if (root[i] == (int)i) {
            os << "PREFAB:" << a->prefabLink.source << "\n";
            os << "GUID:" << hex(a->guid()) << "\n";
            if (parentBlock >= 0) os << "PARENT:" << parentBlock << "\n";
            if (parentGuid.valid()) os << "PARENT_GUID:" << hex(parentGuid) << "\n";
            const InstanceInfo& info = infos[(int)i];
            if (!info.ok) {
                for (const auto& l : a->prefabLink.raw) os << l << "\n";
            } else {
                std::vector<const Actor*> members;
                for (size_t j = i; j < n; ++j)
                    if (root[j] == (int)i) members.push_back(set[j]);
                for (const auto& l : linesFor(info, members, parentOf)) os << l << "\n";
            }
            os << "END_PREFAB\n";
            continue;
        }
        os << "ACTOR:" << a->name << "\n";
        SceneSerializer::writeActor(os, a, parentBlock, false, parentGuid);
        os << "END_ACTOR\n";
    }
    return os.str();
}

std::vector<std::string> overridesOf(const Actor& root, const Loader& load, bool* ok) {
    InstanceInfo info = expandFor(root, load, nullptr);
    if (ok) *ok = info.ok;
    if (!info.ok) return root.prefabLink.raw;
    std::vector<Actor*> m = membersOf(const_cast<Actor*>(&root));
    std::vector<const Actor*> members;
    std::unordered_set<HGuid> taken;
    for (Actor* a : m)
        if (info.byPair.count(a->prefabLink.pair)) members.push_back(a);
    return linesFor(info, members, [](const Actor* a) { return a->getParent(); });
}

std::vector<std::string> overrideLinesOf(const Actor& member, const Loader& load) {
    std::vector<std::string> lines;
    const Actor* root = instanceRootOf(&member);
    if (!root) return lines;
    InstanceInfo info = expandFor(*root, load, nullptr);
    if (!info.ok) return lines;
    std::vector<const Actor*> members;
    for (Actor* a : membersOf(const_cast<Actor*>(root))) members.push_back(a);
    return linesFor(info, members, [](const Actor* a) { return a->getParent(); }, &member);
}

std::vector<std::string> overridePathsOf(const Actor& member, const Loader& load) {
    std::vector<std::string> paths;
    const Actor* root = instanceRootOf(&member);
    if (!root) return paths;
    InstanceInfo info = expandFor(*root, load, nullptr);
    if (!info.ok) return paths;
    std::vector<const Actor*> members;
    for (Actor* a : membersOf(const_cast<Actor*>(root))) members.push_back(a);
    // 0 번이 루트여야 @PARENT 를 비교하지 않습니다.
    linesFor(info, members, [](const Actor* a) { return a->getParent(); }, &member, &paths);
    return paths;
}

bool baselineBlock(const Actor& member, const Loader& load, std::string& out) {
    const Actor* root = instanceRootOf(&member);
    if (!root) return false;
    InstanceInfo info = expandFor(*root, load, nullptr);
    if (!info.ok) return false;
    auto it = info.byPair.find(member.prefabLink.pair);
    if (it == info.byPair.end()) return false;
    TBlock b = info.E[(size_t)it->second];
    b.parent = -1;
    b.guid   = member.guid();
    out = writeBlocks({ b });
    return true;
}

// ── 원본에 적용 ───────────────────────────────────────────────────────────────
int applyToSourceText(std::string& text, const std::string& sourcePath, const std::vector<std::string>& lines,
                      const Loader& load, std::vector<std::string>* errors) {
    std::vector<TBlock> blocks = parseBlocks(text);
    if (blocks.empty()) { err(errors, "apply: " + sourcePath + " has no actor"); return 0; }
    // 인스턴스 쪽 짝 = 원본 글을 펼친 GUID. 보통 블록은 GUID 그대로(루트는 펼칠 때 루트 GUID 로 바뀌므로 첫 블록 GUID).
    // 안쪽 인스턴스 블록은 그 블록을 펼친 GUID 들 → (블록, 안쪽 짝).
    struct Where { int block; HGuid inner; bool innerRoot; };
    std::unordered_map<HGuid, Where> where;
    const std::string canon = hvfs::toCanonical(sourcePath);
    const HGuid seed{ hashText(canon) };
    for (int i = 0; i < (int)blocks.size(); ++i) {
        TBlock& b = blocks[(size_t)i];
        if (!b.instance) {
            if (!b.guid.valid()) b.guid = derive(seed, HGuid{ (uint64_t)i + 1 });   // 옛 프리팹(expandSourceRec 과 같은 규칙)
            where[b.guid] = { i, HGuid{}, false };
            continue;
        }
        std::vector<TBlock> E;
        if (!b.guid.valid()) continue;
        if (!expandSource(b.source, b.guid, load, E, nullptr)) continue;
        // 펼친 소속 액터의 GUID = 바깥 원본에서의 GUID(= 바깥 인스턴스 쪽 짝). 덮어쓰기를 먼저 넣어 MEMBER_GUID 까지 맞춥니다.
        applyLines(E, b.lines, b.source, nullptr);
        for (size_t j = 0; j < E.size(); ++j) where[E[j].guid] = { i, E[j].linkPair, j == 0 };
    }
    int applied = 0;
    std::vector<char> gone(blocks.size(), 0);
    for (const std::string& l : lines) {
        const size_t c = l.find(':');
        if (c == std::string::npos) continue;
        const std::string key = l.substr(0, c), rest = l.substr(c + 1);
        const size_t sp = rest.find(' ');
        const std::string pairHex = rest.substr(0, sp);
        const std::string tail = sp == std::string::npos ? std::string() : rest.substr(sp + 1);
        HGuid pair;
        if (!HGuid::parse(pairHex, pair)) continue;
        auto it = where.find(pair);
        if (it == where.end()) { warn(errors, "apply: " + sourcePath + " has no actor for '" + l + "'"); continue; }
        TBlock& b = blocks[(size_t)it->second.block];
        if (key == "MEMBER_GUID") continue;   // 인스턴스 쪽 값이라 원본에 넣지 않음
        if (b.instance && key == "REMOVE" && tail.empty() && it->second.innerRoot) {
            gone[(size_t)it->second.block] = 1;   // 안쪽 인스턴스 통째로
            ++applied;
            continue;
        }
        if (b.instance) {
            // 안쪽 인스턴스의 덮어쓰기로: 같은 (짝, 경로)의 옛 줄을 지우고 새 줄을 넣습니다.
            const std::string innerHex = hex(it->second.inner);
            std::string newLine = key + ":" + innerHex + (tail.empty() ? std::string() : " " + tail);
            const std::string pathKey = key == "OVERRIDE" ? tail.substr(0, tail.find('=')) : tail;
            b.lines.erase(std::remove_if(b.lines.begin(), b.lines.end(), [&](const std::string& o) {
                              if (o.rfind(key + ":" + innerHex, 0) != 0) return false;
                              const std::string ot = o.size() > key.size() + 1 + 16 ? o.substr(key.size() + 2 + 16) : std::string();
                              const std::string op = key == "OVERRIDE" ? ot.substr(0, ot.find('=')) : ot;
                              return op == pathKey;
                          }),
                          b.lines.end());
            b.lines.push_back(std::move(newLine));
            ++applied;
            continue;
        }
        if (key == "REMOVE") {
            if (tail.empty()) {
                // 액터를 지움: 그 블록과 자손 블록을 지웁니다(순번이 바뀌므로 끝에서 한꺼번에).
                gone[(size_t)it->second.block] = 1;
                ++applied;
                continue;
            }
            std::string type;
            int k = 0;
            if (!splitCompPath(tail, type, k, nullptr)) continue;
            int seen = 0;
            for (auto ci = b.comps.begin(); ci != b.comps.end(); ++ci)
                if (ci->type == type && seen++ == k) { b.comps.erase(ci); ++applied; break; }
            continue;
        }
        if (key != "OVERRIDE") continue;
        const size_t eq = tail.find('=');
        if (eq == std::string::npos) continue;
        const std::string path = tail.substr(0, eq), value = tail.substr(eq + 1);
        std::string type, field;
        int k = 0;
        if (path == "NAME") b.name = value;
        else if (path == "@PARENT") {
            HGuid pg;
            auto pw = HGuid::parse(value, pg) ? where.find(pg) : where.end();
            if (pw == where.end()) { warn(errors, "apply: unknown @PARENT " + value); continue; }
            const TBlock& pb = blocks[(size_t)pw->second.block];
            b.parent = -1;
            b.parentGuid = HGuid{};
            // 안쪽 인스턴스의 루트가 아닌 액터 아래면 PARENT_GUID(바깥 원본에서의 GUID), 그 밖은 블록 순번.
            if (pb.instance && !pw->second.innerRoot) b.parentGuid = pg;
            else                                      b.parent = pw->second.block;
        } else if (splitCompPath(path, type, k, &field)) {
            setKV(nthComp(b, type, k, true)->fields, field, value);
        } else {
            bool erased = false;
            if (value.empty() && path == "TAGS") {
                b.props.erase(std::remove_if(b.props.begin(), b.props.end(), [&](const auto& kv) { return kv.first == path; }),
                              b.props.end());
                erased = true;
            }
            if (!erased) setKV(b.props, path, value);
        }
        ++applied;
    }
    // 지운 액터 블록(자손 포함)을 빼고 부모 순번을 다시 맞춥니다.
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t i = 0; i < blocks.size(); ++i)
            if (!gone[i] && blocks[i].parent >= 0 && blocks[i].parent < (int)blocks.size() && gone[(size_t)blocks[i].parent]) {
                gone[i] = 1;
                changed = true;
            }
    }
    std::vector<int> newIdx(blocks.size(), -1);
    std::vector<TBlock> kept;
    for (size_t i = 0; i < blocks.size(); ++i)
        if (!gone[i]) { newIdx[i] = (int)kept.size(); kept.push_back(std::move(blocks[i])); }
    for (TBlock& b : kept)
        if (b.parent >= 0) b.parent = b.parent < (int)newIdx.size() ? newIdx[(size_t)b.parent] : -1;

    // 안에 인스턴스 블록이 생길 수 있으므로 언제나 지금 판 머리 줄로 씁니다.
    text = std::string(hprefab::kPrefabHeaderV6) + "\n" + writeBlocks(kept);
    return applied;
}

// ── Fresh ────────────────────────────────────────────────────────────────────
std::unordered_map<HGuid, HGuid> freshMapping(const std::string& text, const std::function<bool(HGuid)>& inUse) {
    std::unordered_map<HGuid, HGuid> m;
    const std::vector<TBlock> blocks = parseBlocks(text);
    std::unordered_set<HGuid> taken;
    auto fresh = [&] {
        HGuid g;
        do { g = HGuid::make(); } while ((inUse && inUse(g)) || taken.count(g));
        taken.insert(g);
        return g;
    };
    // 루트 블록 순번(전체 형식 글에서 소속 액터의 인스턴스 루트). 부모가 앞에 오므로 한 번 훑으면 됩니다.
    std::vector<int> rootOf(blocks.size(), -1);
    for (size_t i = 0; i < blocks.size(); ++i) {
        const TBlock& b = blocks[i];
        if (!b.linkSource.empty() && b.linkRaw.empty()) rootOf[i] = (int)i;
        else if (b.linkSource.empty() && b.linkPair.valid() && b.parent >= 0 && b.parent < (int)i) rootOf[i] = rootOf[(size_t)b.parent];
    }
    for (size_t i = 0; i < blocks.size(); ++i) {
        const TBlock& b = blocks[i];
        if (!b.guid.valid() || m.count(b.guid)) continue;
        if (rootOf[i] >= 0 && rootOf[i] != (int)i) {
            const HGuid r = m[blocks[(size_t)rootOf[i]].guid];
            HGuid g = derive(r, b.linkPair);
            if ((inUse && inUse(g)) || taken.count(g)) g = fresh();
            taken.insert(g);
            m[b.guid] = g;
        } else {
            m[b.guid] = fresh();
        }
    }
    return m;
}

} // namespace hprefablink

#include "ComponentSerializer.hpp"
#include "TransformText.hpp"

namespace hprefablink {

std::string actorBlock(const Actor& a) {
    std::ostringstream os;
    os << "ACTOR:" << a.name << "\n";
    SceneSerializer::writeActor(os, &a, -1, false);
    os << "END_ACTOR\n";
    return os.str();
}

bool applyActorBlock(Actor& a, const std::string& text, const ComponentLoadContext& ctx) {
    SceneSerializer::ActorData d;
    bool in = false;
    std::istringstream is(text);
    std::string line;
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line == "END_ACTOR") break;
        if (line.rfind("ACTOR:", 0) == 0) { d.name = line.substr(6); in = true; continue; }
        if (in) SceneSerializer::parseActorLine(line, d);
    }
    if (!in) return false;
    a.name = d.name;
    a.setActive(d.active != 0);
    a.clearTags();
    for (const std::string& t : d.tags) a.addTag(t);
    a.setLocalPosition(d.pos);
    a.setLocalRotation(transform_text::resolve(d.euler, d.hasQuat, d.quat));
    a.setLocalScale(d.scale);
    // 컴포넌트는 모두 떼고 글의 블록으로 다시 붙입니다(씬을 읽을 때와 같은 길).
    while (!a.getComponents().empty()) a.removeComponent(a.getComponents().back().get());
    ComponentSerializer::spawn(a, d.components.blocks(), ctx);
    a.notifyTransformCommitted();
    return true;
}

} // namespace hprefablink
