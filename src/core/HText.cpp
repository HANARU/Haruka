#include "HText.hpp"
#include "Utf8.hpp"
#include "../vfs/Vfs.hpp"

#include <cmath>
#include <cstdio>
#include <map>
#include <unordered_map>

// ── 공유 데이터 ────────────────────────────────────────────────────────────────────────────────
struct HText::Data {
    enum class Kind : uint8_t { Literal, Localized, Format, Number, Integer } kind = Kind::Literal;
    std::string ns, key, source;          // Literal: source 만, Localized: 셋 다, Format: 틀은 pattern
    std::shared_ptr<const HText> pattern;
    std::vector<std::pair<std::string, HTextArg>> args;   // 차례 인자는 이름 "0" "1" ...
    double  number  = 0.0;
    int64_t integer = 0;
    int     maxFrac = 2;
    // 풀린 글 캐시(문화권 판 번호마다).
    mutable uint32_t    cachedRevision = 0xFFFFFFFFu;
    mutable std::string cached;
};

namespace {

const std::string kEmpty;

struct TableEntry {
    std::string source;
    std::string translation;
};
// culture → 층 → "ns\x1Fkey" → 항목
struct CultureTable {
    std::unordered_map<std::string, TableEntry> layers[2];
};

struct State {
    std::string culture = "en";
    uint32_t    revision = 1;
    std::map<std::string, CultureTable> tables;
    std::map<std::string, bool>         filesLoaded;   // useCulture 가 파일을 읽은 문화권
};

State& state() {
    static State s;
    return s;
}

std::string tableKey(std::string_view ns, std::string_view key) {
    std::string k;
    k.reserve(ns.size() + key.size() + 1);
    k.append(ns);
    k.push_back('\x1F');
    k.append(key);
    return k;
}

// 틀의 {이름} 을 채웁니다. {{ · }} 는 중괄호 하나. 모르는 이름은 그대로 둡니다(UE 와 같음).
std::string applyFormat(const std::string& pat, const std::vector<std::pair<std::string, HTextArg>>& args) {
    std::string out;
    out.reserve(pat.size() + 16);
    for (size_t i = 0; i < pat.size();) {
        const char c = pat[i];
        if (c == '{' && i + 1 < pat.size() && pat[i + 1] == '{') { out.push_back('{'); i += 2; continue; }
        if (c == '}' && i + 1 < pat.size() && pat[i + 1] == '}') { out.push_back('}'); i += 2; continue; }
        if (c == '{') {
            const size_t close = pat.find('}', i + 1);
            if (close != std::string::npos) {
                const std::string name = pat.substr(i + 1, close - i - 1);
                const HTextArg* hit = nullptr;
                for (const auto& a : args) if (a.first == name) { hit = &a.second; break; }
                if (hit) { out += hit->toString(); i = close + 1; continue; }
            }
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

} // namespace

// ── HTextArg ───────────────────────────────────────────────────────────────────────────────────
HTextArg::HTextArg(const HText& t) : kind(Kind::Text), text(std::make_shared<const HText>(t)) {}

std::string HTextArg::toString() const {
    switch (kind) {
        case Kind::Text:   return text ? text->toString() : std::string();
        case Kind::Int:    return htext::formatInteger(i, htext::culture());
        case Kind::Double: return htext::formatNumber(d, 2, htext::culture());
        case Kind::String: default: return s;
    }
}

// ── HText ──────────────────────────────────────────────────────────────────────────────────────
HText HText::fromString(std::string s) {
    auto d = std::make_shared<Data>();
    d->kind   = Data::Kind::Literal;
    d->source = std::move(s);
    return HText(std::move(d));
}

HText HText::localized(std::string_view ns, std::string_view key, std::string_view source) {
    auto d = std::make_shared<Data>();
    d->kind   = Data::Kind::Localized;
    d->ns     = ns;
    d->key    = key;
    d->source = source;
    return HText(std::move(d));
}

HText HText::asNumber(double v, int maxFractionDigits) {
    auto d = std::make_shared<Data>();
    d->kind    = Data::Kind::Number;
    d->number  = v;
    d->maxFrac = maxFractionDigits;
    return HText(std::move(d));
}

HText HText::asInteger(int64_t v) {
    auto d = std::make_shared<Data>();
    d->kind    = Data::Kind::Integer;
    d->integer = v;
    return HText(std::move(d));
}

HText HText::format(const HText& pattern, std::initializer_list<HTextArg> args) {
    auto d = std::make_shared<Data>();
    d->kind    = Data::Kind::Format;
    d->pattern = std::make_shared<const HText>(pattern);
    int n = 0;
    for (const HTextArg& a : args) d->args.emplace_back(std::to_string(n++), a);
    return HText(std::move(d));
}

HText HText::formatNamed(const HText& pattern, std::vector<std::pair<std::string, HTextArg>> args) {
    auto d = std::make_shared<Data>();
    d->kind    = Data::Kind::Format;
    d->pattern = std::make_shared<const HText>(pattern);
    d->args    = std::move(args);
    return HText(std::move(d));
}

const std::string& HText::toString() const {
    if (!d_) return kEmpty;
    const Data& d = *d_;
    if (d.kind == Data::Kind::Literal) return d.source;
    const uint32_t rev = htext::revision();
    if (d.cachedRevision == rev) return d.cached;
    switch (d.kind) {
        case Data::Kind::Localized: {
            const std::string* t = htext::lookup(d.ns, d.key, d.source);
            d.cached = t ? *t : d.source;
            break;
        }
        case Data::Kind::Format:
            d.cached = applyFormat(d.pattern ? d.pattern->toString() : kEmpty, d.args);
            break;
        case Data::Kind::Number:
            d.cached = htext::formatNumber(d.number, d.maxFrac, htext::culture());
            break;
        case Data::Kind::Integer:
            d.cached = htext::formatInteger(d.integer, htext::culture());
            break;
        default:
            d.cached = d.source;
            break;
    }
    d.cachedRevision = rev;
    return d.cached;
}

bool HText::isLocalized() const { return d_ && d_->kind == Data::Kind::Localized; }
const std::string& HText::ns() const     { return d_ ? d_->ns : kEmpty; }
const std::string& HText::key() const    { return d_ ? d_->key : kEmpty; }
const std::string& HText::source() const {
    if (!d_) return kEmpty;
    if (d_->kind == Data::Kind::Format && d_->pattern) return d_->pattern->source();
    return d_->source;
}

// ── 문화권 · 표 ────────────────────────────────────────────────────────────────────────────────
namespace htext {

const std::string& culture() { return state().culture; }

void setCulture(std::string c) {
    if (c.empty()) c = "en";
    State& s = state();
    if (s.culture == c) return;
    s.culture = std::move(c);
    ++s.revision;
}

uint32_t revision() { return state().revision; }

void clearTables() {
    state().tables.clear();
    state().filesLoaded.clear();
    ++state().revision;
}

size_t useCulture(const std::string& c, std::vector<std::string>* errors) {
    const std::string name = c.empty() ? std::string("en") : c;
    size_t n = 0;
    if (!state().filesLoaded[name]) {
        state().filesLoaded[name] = true;
        n = loadCultureFiles(name, errors);
    }
    setCulture(name);
    return n;
}

const std::vector<CultureInfo>& knownCultures() {
    static const std::vector<CultureInfo> k = {
        { "en", "English" },
        { "ko", "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4" },   // 한국어
    };
    return k;
}

const std::string* lookup(std::string_view ns, std::string_view key, std::string_view source) {
    State& s = state();
    auto it = s.tables.find(s.culture);
    if (it == s.tables.end()) return nullptr;
    const std::string k = tableKey(ns, key);
    for (int layer = 1; layer >= 0; --layer) {
        const auto& m = it->second.layers[layer];
        auto e = m.find(k);
        if (e == m.end()) continue;
        if (e->second.source != source || e->second.translation.empty()) return nullptr;   // 원문이 바뀐 낡은 번역 · 빈 번역
        return &e->second.translation;
    }
    return nullptr;
}

std::string poUnescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { out.push_back(s[i]); continue; }
        const char n = s[++i];
        switch (n) {
            case 'n':  out.push_back('\n'); break;
            case 't':  out.push_back('\t'); break;
            case 'r':  out.push_back('\r'); break;
            case '"':  out.push_back('"');  break;
            case '\\': out.push_back('\\'); break;
            default:   out.push_back('\\'); out.push_back(n); break;
        }
    }
    return out;
}

std::string poEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

size_t loadPo(std::string_view text, const std::string& cultureName, Layer layer, std::vector<std::string>* errors) {
    // 줄 단위 읽기. 항목은 msgctxt · msgid · msgstr 이고, 각 값은 이어지는 "..." 줄을 이어 붙입니다. # 줄은 주석.
    struct Entry { std::string ctxt, id, str; bool hasCtxt = false, hasId = false, hasStr = false; int line = 0; };
    std::vector<Entry> entries;
    Entry cur;
    std::string* target = nullptr;
    int lineNo = 0;
    auto flush = [&] {
        if (cur.hasId || cur.hasCtxt || cur.hasStr) entries.push_back(cur);
        cur    = Entry{};
        target = nullptr;
    };
    auto quoted = [&](std::string_view rest, std::string& out) -> bool {
        const size_t a = rest.find('"');
        const size_t b = rest.rfind('"');
        if (a == std::string_view::npos || b == a) return false;
        out += poUnescape(rest.substr(a + 1, b - a - 1));
        return true;
    };
    size_t pos = 0;
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        pos = 3;   // UTF-8 BOM
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        std::string_view ln = text.substr(pos, nl - pos);
        pos = nl + 1;
        ++lineNo;
        if (!ln.empty() && ln.back() == '\r') ln.remove_suffix(1);
        size_t f = 0;
        while (f < ln.size() && (ln[f] == ' ' || ln[f] == '\t')) ++f;
        ln.remove_prefix(f);
        if (ln.empty()) { flush(); if (pos > text.size()) break; continue; }
        if (ln[0] == '#') { if (pos > text.size()) break; continue; }
        auto starts = [&](std::string_view p) { return ln.substr(0, p.size()) == p; };
        bool ok = true;
        if (starts("msgctxt")) {
            if (cur.hasCtxt || cur.hasId) flush();
            cur.hasCtxt = true; cur.line = lineNo; target = &cur.ctxt; ok = quoted(ln.substr(7), cur.ctxt);
        } else if (starts("msgid_plural")) {
            target = nullptr;   // 복수형은 쓰지 않음(첫 형태만)
        } else if (starts("msgid")) {
            if (cur.hasId) flush();
            if (!cur.hasCtxt) cur.line = lineNo;
            cur.hasId = true; target = &cur.id; ok = quoted(ln.substr(5), cur.id);
        } else if (starts("msgstr")) {
            cur.hasStr = true;
            const size_t sp = ln.find(' ');
            target = &cur.str;
            ok = sp != std::string_view::npos && quoted(ln.substr(sp), cur.str);
            if (ln.substr(0, 7) == "msgstr[" && ln.substr(0, 9) != "msgstr[0]") { cur.str.clear(); target = nullptr; ok = true; }
        } else if (ln[0] == '"') {
            ok = target && quoted(ln, *target);
        } else {
            ok = false;
        }
        if (!ok && errors)
            errors->push_back("line " + std::to_string(lineNo) + ": cannot read '" + std::string(ln.substr(0, 60)) + "'");
        if (pos > text.size()) break;
    }
    flush();

    auto& table = state().tables[cultureName].layers[(int)layer];
    size_t n = 0;
    for (const Entry& e : entries) {
        if (!e.hasCtxt && e.id.empty()) continue;   // 머리말 항목
        const size_t comma = e.ctxt.find(',');
        if (!e.hasCtxt || comma == std::string::npos || !e.hasId || !e.hasStr) {
            if (errors) errors->push_back("line " + std::to_string(e.line) + ": entry needs msgctxt \"ns,key\", msgid and msgstr");
            continue;
        }
        if (!utf8::isValid(e.str) || !utf8::isValid(e.id)) {
            if (errors) errors->push_back("line " + std::to_string(e.line) + ": not valid UTF-8");
            continue;
        }
        table[tableKey(std::string_view(e.ctxt).substr(0, comma), std::string_view(e.ctxt).substr(comma + 1))] =
            TableEntry{ e.id, e.str };
        ++n;
    }
    ++state().revision;
    return n;
}

size_t loadCultureFiles(const std::string& cultureName, std::vector<std::string>* errors) {
    size_t n = 0;
    const std::pair<std::string, Layer> files[] = {
        { "/Engine/Localization/" + cultureName + "/Engine.po", Layer::Engine },
        { "/Game/Localization/" + cultureName + "/Game.po",     Layer::Game },
    };
    for (const auto& [path, layer] : files) {
        if (!hvfs::exists(path)) continue;
        std::string txt, err;
        if (!hvfs::readText(path, txt, &err)) {
            if (errors) errors->push_back(path + ": " + err);
            continue;
        }
        std::vector<std::string> errs;
        n += loadPo(txt, cultureName, layer, &errs);
        if (errors) for (auto& e : errs) errors->push_back(path + " " + e);
    }
    return n;
}

NumberFormat numberFormat(const std::string& c) {
    const std::string lang = c.substr(0, c.find_first_of("-_"));
    if (lang == "de" || lang == "es" || lang == "it" || lang == "pt" || lang == "nl" || lang == "tr" || lang == "id" ||
        lang == "da")
        return { ".", "," };
    if (lang == "fr" || lang == "ru" || lang == "pl" || lang == "cs" || lang == "sv" || lang == "fi" || lang == "uk" ||
        lang == "nb")
        return { "\xC2\xA0", "," };   // 줄 바꿈 없는 공백
    return { ",", "." };              // en · ko · ja · zh 등
}

std::string formatInteger(int64_t v, const std::string& c) {
    const NumberFormat nf = numberFormat(c);
    const bool neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    std::string digits = std::to_string(u);
    std::string out;
    const size_t n = digits.size();
    for (size_t i = 0; i < n; ++i) {
        if (i > 0 && (n - i) % 3 == 0) out += nf.group;
        out.push_back(digits[i]);
    }
    return neg ? "-" + out : out;
}

std::string formatNumber(double v, int maxFrac, const std::string& c) {
    if (!std::isfinite(v)) return std::isnan(v) ? "NaN" : (v < 0 ? "-Inf" : "Inf");
    if (maxFrac < 0) maxFrac = 0;
    if (maxFrac > 9) maxFrac = 9;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", maxFrac, std::fabs(v));
    std::string s = buf;
    std::string frac;
    if (const size_t dot = s.find('.'); dot != std::string::npos) {
        frac = s.substr(dot + 1);
        s.resize(dot);
        while (!frac.empty() && frac.back() == '0') frac.pop_back();   // 끝의 0 은 뺌(1.50 → 1.5)
    }
    const NumberFormat nf = numberFormat(c);
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i > 0 && (s.size() - i) % 3 == 0) out += nf.group;
        out.push_back(s[i]);
    }
    if (!frac.empty()) out += nf.decimal + frac;
    const bool neg = v < 0 && (out.find_first_not_of("0,. \xC2\xA0") != std::string::npos);
    return neg ? "-" + out : out;
}

} // namespace htext
