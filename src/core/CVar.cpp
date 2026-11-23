#include "CVar.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "TextFloat.hpp"

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return out;
}

std::string_view trimView(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

bool parseBool(std::string_view t, bool& out) {
    const std::string l = lower(t);
    if (l == "1" || l == "true" || l == "on" || l == "yes")  { out = true;  return true; }
    if (l == "0" || l == "false" || l == "off" || l == "no") { out = false; return true; }
    return false;
}

bool parseInt(std::string_view t, long long& out) {
    if (t.empty() || t.size() > 20) return false;
    const std::string s(t);
    char* end = nullptr;
    out = std::strtoll(s.c_str(), &end, 10);
    return end && *end == '\0';
}

bool parseFloat(std::string_view t, float& out) {
    if (t.empty() || t.size() > 48) return false;
    const std::string s(t);
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return end && *end == '\0' && std::isfinite(out);
}

} // namespace

const char* CVarRegistry::sourceName(Source s) {
    switch (s) {
    case Source::Default:     return "default";
    case Source::Ini:         return "ini";
    case Source::CommandLine: return "command line";
    case Source::Console:     return "console";
    case Source::Code:        return "code";
    }
    return "?";
}

bool CVarRegistry::validName(std::string_view name) {
    if (name.empty() || name.size() > 64 || name.front() == '.' || name.back() == '.') return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) return false;
    return true;
}

std::string CVarRegistry::CVar::text() const {
    switch (kind) {
    case Kind::Bool:   return b_ ? "true" : "false";
    case Kind::Int:    return std::to_string(i_);
    case Kind::Float:  return floatToText(f_);
    case Kind::String: return s_;
    }
    return {};
}

CVarRegistry::CVar* CVarRegistry::add(std::string_view name, Kind kind, float lo, float hi, std::string_view def,
                                      std::string_view help, std::function<void(const CVar&)> onChanged) {
    const std::string key = lower(name);
    if (!validName(name) || vars_.count(key) || commands_.count(key)) return nullptr;
    auto v      = std::make_unique<CVar>();
    v->name     = std::string(name);
    v->help     = std::string(help);
    v->kind     = kind;
    v->lo       = lo;
    v->hi       = hi;
    std::string why;
    assign(*v, def, Source::Default, &why);
    v->defaultText = v->text();
    v->dirty_      = false;
    v->onChanged   = std::move(onChanged);
    CVar* out = v.get();
    vars_.emplace(key, std::move(v));
    // 등록 전에 들어온 값(명령줄 · ini)을 적용합니다. 콜백은 다음 dispatchChanges 에서 부릅니다.
    if (auto it = pending_.find(key); it != pending_.end()) {
        assign(*out, it->second.text, it->second.source, &why);
        pending_.erase(it);
    }
    return out;
}

CVarRegistry::CVar* CVarRegistry::addBool(std::string_view name, bool def, std::string_view help, std::function<void(const CVar&)> fn) {
    return add(name, Kind::Bool, 0.f, 0.f, def ? "true" : "false", help, std::move(fn));
}
CVarRegistry::CVar* CVarRegistry::addInt(std::string_view name, int def, int lo, int hi, std::string_view help,
                                         std::function<void(const CVar&)> fn) {
    return add(name, Kind::Int, (float)lo, (float)hi, std::to_string(def), help, std::move(fn));
}
CVarRegistry::CVar* CVarRegistry::addFloat(std::string_view name, float def, float lo, float hi, std::string_view help,
                                           std::function<void(const CVar&)> fn) {
    return add(name, Kind::Float, lo, hi, floatToText(def), help, std::move(fn));
}
CVarRegistry::CVar* CVarRegistry::addString(std::string_view name, std::string_view def, std::string_view help,
                                            std::function<void(const CVar&)> fn) {
    return add(name, Kind::String, 0.f, 0.f, def, help, std::move(fn));
}

CVarRegistry::CVar* CVarRegistry::find(std::string_view name) {
    auto it = vars_.find(lower(name));
    return it != vars_.end() ? it->second.get() : nullptr;
}
const CVarRegistry::CVar* CVarRegistry::find(std::string_view name) const {
    auto it = vars_.find(lower(name));
    return it != vars_.end() ? it->second.get() : nullptr;
}

bool CVarRegistry::assign(CVar& v, std::string_view rawText, Source src, std::string* why) {
    const std::string_view t = trimView(rawText);
    const std::string before = v.text();
    switch (v.kind) {
    case Kind::Bool: {
        bool b = false;
        if (!parseBool(t, b)) {
            if (why) *why = v.name + " expects true or false, got '" + std::string(t) + "'";
            return false;
        }
        v.b_ = b;
        break;
    }
    case Kind::Int: {
        long long i = 0;
        if (!parseInt(t, i)) {
            if (why) *why = v.name + " expects a whole number, got '" + std::string(t) + "'";
            return false;
        }
        const long long c = std::clamp<long long>(i, (long long)v.lo, (long long)v.hi);
        if (c != i && why) *why = v.name + " = " + std::string(t) + " is out of range [" + std::to_string((int)v.lo) + ", "
                                  + std::to_string((int)v.hi) + "]; using " + std::to_string(c);
        v.i_ = (int)c;
        break;
    }
    case Kind::Float: {
        float f = 0.f;
        if (!parseFloat(t, f)) {
            if (why) *why = v.name + " expects a number, got '" + std::string(t) + "'";
            return false;
        }
        const float c = std::clamp(f, v.lo, v.hi);
        if (c != f && why) *why = v.name + " = " + std::string(t) + " is out of range [" + floatToText(v.lo) + ", " + floatToText(v.hi)
                                  + "]; using " + floatToText(c);
        v.f_ = c;
        break;
    }
    case Kind::String:
        v.s_ = std::string(t);
        break;
    }
    v.source = src;
    if (v.text() != before) v.dirty_ = true;
    return true;
}

bool CVarRegistry::set(std::string_view name, std::string_view text, Source src, std::string* why) {
    if (CVar* v = find(name)) return assign(*v, text, src, why);
    if (!validName(name)) {
        if (why) *why = "bad variable name '" + std::string(name) + "'";
        return false;
    }
    if (src == Source::Ini || src == Source::CommandLine) {
        pending_[lower(name)] = { std::string(trimView(text)), src };
        if (why) *why = "'" + std::string(name) + "' is not registered yet; the value is kept until it is";
        return true;
    }
    if (why) *why = "unknown variable '" + std::string(name) + "'";
    return false;
}

bool CVarRegistry::reset(std::string_view name) {
    CVar* v = find(name);
    if (!v) return false;
    std::string why;
    return assign(*v, v->defaultText, Source::Default, &why);
}

int CVarRegistry::dispatchChanges() {
    int n = 0;
    // 콜백이 다른 변수를 바꾸면 그 변수는 다음 프레임에 부릅니다(이번에 본 것만 처리).
    std::vector<CVar*> dirty;
    for (auto& [k, v] : vars_)
        if (v->dirty_) dirty.push_back(v.get());
    for (CVar* v : dirty) {
        v->dirty_ = false;
        ++n;
        if (v->onChanged) v->onChanged(*v);
    }
    return n;
}

std::vector<std::string> CVarRegistry::pendingNames() const {
    std::vector<std::string> out;
    for (const auto& [k, p] : pending_) out.push_back(k);
    return out;
}

void CVarRegistry::loadIni(const std::string& text, std::vector<std::string>& messages) {
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string_view line = trimView(std::string_view(text).substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) { messages.push_back("line '" + std::string(line) + "' has no '='; skipped"); continue; }
        const std::string name(trimView(line.substr(0, eq)));
        std::string why;
        const bool ok = set(name, line.substr(eq + 1), Source::Ini, &why);
        if (!ok) messages.push_back(why + "; skipped");
        else if (!why.empty() && find(name)) messages.push_back(why);   // 범위로 자름(보관한 이름은 시작 끝에 한 번에 알림)
    }
}

bool CVarRegistry::addCommand(std::string_view name, std::string_view help, CommandFn fn) {
    const std::string key = lower(name);
    static const char* kBuiltin[] = { "help", "list", "reset" };
    for (const char* b : kBuiltin)
        if (key == b) return false;
    if (!validName(name) || !fn || vars_.count(key) || commands_.count(key)) return false;
    commands_.emplace(key, Command{ std::string(name), std::string(help), std::move(fn) });
    return true;
}

std::vector<std::string> CVarRegistry::splitArgs(std::string_view line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size()) break;
        std::string tok;
        if (line[i] == '"') {
            ++i;
            while (i < line.size() && line[i] != '"') tok += line[i++];
            if (i < line.size()) ++i;   // 닫는 따옴표
        } else {
            while (i < line.size() && line[i] != ' ' && line[i] != '\t') tok += line[i++];
        }
        out.push_back(std::move(tok));
    }
    return out;
}

std::vector<const CVarRegistry::CVar*> CVarRegistry::all() const {
    std::vector<const CVar*> out;
    for (const auto& [k, v] : vars_) out.push_back(v.get());
    return out;
}

std::vector<CVarRegistry::Completion> CVarRegistry::complete(std::string_view input, size_t max) const {
    std::vector<Completion> out;
    if (max == 0) return out;
    while (!input.empty() && (input.front() == ' ' || input.front() == '\t')) input.remove_prefix(1);
    if (input.empty()) return out;

    struct BuiltinDef { const char* name; const char* help; };
    static constexpr BuiltinDef kBuiltins[] = {
        { "help",  "help [name] -- show commands or help for a variable/command" },
        { "list",  "list [prefix] -- list variables (optionally filtered by prefix)" },
        { "reset", "reset <variable> -- reset a variable to its default value" },
    };

    auto varArgInfo = [](const CVar& v) -> std::string {
        std::string info;
        switch (v.kind) {
        case Kind::Bool:   info = "bool"; break;
        case Kind::Int:    info = "int [" + std::to_string((int)v.lo) + ".." + std::to_string((int)v.hi) + "]"; break;
        case Kind::Float:  info = "float [" + floatToText(v.lo) + ".." + floatToText(v.hi) + "]"; break;
        case Kind::String: info = "string"; break;
        }
        info += " \xC2\xB7 current " + v.text();
        return info;
    };

    const size_t sp = input.find_first_of(" \t");
    if (sp != std::string_view::npos) {
        const std::string key = lower(input.substr(0, sp));
        if (const CVar* v = find(key)) {
            out.push_back({ v->name, CompletionKind::Variable, varArgInfo(*v), v->help });
            return out;
        }
        if (auto it = commands_.find(key); it != commands_.end()) {
            out.push_back({ it->second.name, CompletionKind::Command, {}, it->second.help });
            return out;
        }
        for (const auto& b : kBuiltins) {
            if (key == b.name) {
                out.push_back({ b.name, CompletionKind::Builtin, {}, b.help });
                return out;
            }
        }
        return out;
    }

    const std::string q = lower(input);
    struct Entry { std::string key; Completion c; };
    std::vector<Entry> prefix, contains;
    auto pushIfMatch = [&](const std::string& key, Completion c) {
        const size_t pos = key.find(q);
        if (pos == 0) prefix.push_back({ key, std::move(c) });
        else if (pos != std::string::npos) contains.push_back({ key, std::move(c) });
    };

    for (const auto& [k, v] : vars_)
        pushIfMatch(k, { v->name, CompletionKind::Variable, v->text(), v->help });
    for (const auto& [k, c] : commands_)
        pushIfMatch(k, { c.name, CompletionKind::Command, {}, c.help });
    for (const auto& b : kBuiltins)
        pushIfMatch(b.name, { b.name, CompletionKind::Builtin, {}, b.help });

    auto byKey = [](const Entry& a, const Entry& b) { return a.key < b.key; };
    std::sort(prefix.begin(), prefix.end(), byKey);
    std::sort(contains.begin(), contains.end(), byKey);

    for (auto& e : prefix) {
        if (out.size() >= max) break;
        out.push_back(std::move(e.c));
    }
    for (auto& e : contains) {
        if (out.size() >= max) break;
        out.push_back(std::move(e.c));
    }
    return out;
}

std::string CVarRegistry::describe(const CVar& v) const {
    std::string s = v.name + " = " + v.text();
    if (v.text() != v.defaultText) s += "  (default " + v.defaultText + ", set by " + sourceName(v.source) + ")";
    if (!v.help.empty()) s += "  -- " + v.help;
    return s;
}

std::string CVarRegistry::execute(std::string_view line) {
    const std::vector<std::string> tok = splitArgs(trimView(line));
    if (tok.empty()) return {};
    const std::string cmd = lower(tok[0]);
    const std::vector<std::string> args(tok.begin() + 1, tok.end());
    if (cmd == "help") {
        if (!args.empty()) {
            if (const CVar* v = find(args[0])) return describe(*v);
            if (auto it = commands_.find(lower(args[0])); it != commands_.end()) return it->second.name + "  -- " + it->second.help;
            return "unknown command or variable '" + args[0] + "'";
        }
        std::string s = "Commands: help [name], list [prefix], reset <variable>, <variable> [value]";
        s += "\nEditor console: type to filter suggestions, Up/Down to pick or browse history, Tab to complete, ` to focus (history saved)";
        for (const auto& [k, c] : commands_) s += "\n  " + c.name + "  -- " + c.help;
        s += "\n" + std::to_string(vars_.size()) + " variables (type list)";
        return s;
    }
    if (cmd == "list") {
        const std::string prefix = args.empty() ? std::string() : lower(args[0]);
        std::string s;
        int n = 0;
        for (const auto& [k, v] : vars_) {
            if (k.compare(0, prefix.size(), prefix) != 0) continue;
            s += (n++ ? "\n" : "") + describe(*v);
        }
        return n ? s : "no variables" + (prefix.empty() ? std::string() : " starting with '" + prefix + "'");
    }
    if (cmd == "reset") {
        if (args.empty()) return "usage: reset <variable>";
        CVar* v = find(args[0]);
        if (!v) return "unknown variable '" + args[0] + "'";
        reset(args[0]);
        return v->name + " = " + v->text() + " (default)";
    }
    if (CVar* v = find(cmd)) {
        if (args.empty()) return describe(*v);
        std::string value;
        for (size_t i = 0; i < args.size(); ++i) value += (i ? " " : "") + args[i];
        std::string why;
        if (!assign(*v, value, Source::Console, &why)) return "error: " + why;
        return v->name + " = " + v->text() + (why.empty() ? std::string() : "  (" + why + ")");
    }
    if (auto it = commands_.find(cmd); it != commands_.end()) return it->second.fn(args);
    return "unknown command or variable '" + tok[0] + "' (type help)";
}

std::string CVarRegistry::executeLogged(std::string_view line) {
    const std::string_view t = trimView(line);
    if (t.empty()) return {};
    HLOG(LogConsole, Log) << "> " << std::string(t);
    const std::string out = execute(t);
    size_t pos = 0;
    while (pos < out.size()) {
        size_t end = out.find('\n', pos);
        if (end == std::string::npos) end = out.size();
        HLOG(LogConsole, Log) << out.substr(pos, end - pos);
        pos = end + 1;
    }
    return out;
}
