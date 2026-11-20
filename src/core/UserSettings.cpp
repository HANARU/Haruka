#include "UserSettings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "FieldParse.hpp"
#include "FileUtil.hpp"
#include "TextFloat.hpp"

namespace {

using K = UserSettings::Kind;
using A = UserSettings::Apply;

std::string_view trimView(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return out;
}

bool choiceHas(std::string_view choices, std::string_view v) {
    size_t pos = 0;
    while (pos <= choices.size()) {
        size_t bar = choices.find('|', pos);
        if (bar == std::string_view::npos) bar = choices.size();
        if (choices.substr(pos, bar - pos) == v) return true;
        pos = bar + 1;
    }
    return false;
}

bool validGameName(std::string_view rest) {
    if (rest.empty()) return false;
    for (char c : rest)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-'))
            return false;
    return true;
}

} // namespace

const std::vector<UserSettings::KeyInfo>& UserSettings::keys() {
    static const std::vector<KeyInfo> k = {
        { "audio.master",          K::Float,  0.f, 2.f,   {}, A::Live,    false, "Master volume (1 = as authored, up to 2)" },
        { "audio.sfx",             K::Float,  0.f, 2.f,   {}, A::Live,    false, "Sound effects bus volume" },
        { "audio.music",           K::Float,  0.f, 2.f,   {}, A::Live,    false, "Music bus volume" },
        { "audio.voice",           K::Float,  0.f, 2.f,   {}, A::Live,    false, "Voice bus volume" },
        { "audio.ambient",         K::Float,  0.f, 2.f,   {}, A::Live,    false, "Ambient bus volume" },
        { "audio.ui",              K::Float,  0.f, 2.f,   {}, A::Live,    false, "UI bus volume" },
        { "render.ao",             K::Bool,   0.f, 0.f,   {}, A::Live,    false, "Ambient occlusion" },
        { "render.bloom",          K::Bool,   0.f, 0.f,   {}, A::Live,    false, "Bloom" },
        { "render.csmCascades",    K::Int,    1.f, 2.f,   {}, A::Restart, false, "Shadow cascades (1 or 2, restart)" },
        { "render.shadowDistance", K::Float,  8.f, 200.f, {}, A::Restart, false, "Shadow distance in meters (restart)" },
        { "render.contactShadows", K::Bool,   0.f, 0.f,   {}, A::Restart, false, "Contact shadows (restart)" },
        { "render.heightFog",      K::Bool,   0.f, 0.f,   {}, A::Restart, false, "Height fog (restart)" },
        { "comfort.snapTurn",      K::Float,  0.f, 180.f, {}, A::Live,    false, "Snap turn angle in degrees (0 = scene value, else 5..180)" },
        { "comfort.teleport",      K::Choice, 0.f, 0.f,   "scene|on|off", A::Live, false, "VR teleport" },
        { "comfort.height",        K::Choice, 0.f, 0.f,   "scene|tracked|fixed", A::Live, false, "VR height: tracked (standing) or fixed eye height (seated)" },
        { "comfort.eyeHeight",     K::Float,  0.f, 2.5f,  {}, A::Live,    false, "Fixed eye height in meters (0 = scene value, else 0.5..2.5)" },
        { "comfort.vignette",      K::Float,  0.f, 1.f,   {}, A::Live,    false, "Darken the view edges while moving smoothly (0 = off, up to 1)" },
        { "comfort.seated",        K::Bool,   0.f, 0.f,   {}, A::Live,    false, "Seated play: lift the view by standing minus seated head height (VR floor space)" },
        { "comfort.standingHeight", K::Float, 0.f, 2.5f,  {}, A::Live,    false, "Standing eye height for seated play in meters (0 = scene value, else 1..2.5)" },
        { "access.subtitles",      K::Bool,   0.f, 0.f,   {}, A::Live,    false, "Show subtitles" },
        { "access.subtitleSize",   K::Float,  0.5f, 3.f,  {}, A::Live,    false, "Subtitle text size (1 = default)" },
        { "access.subtitleBackground", K::Float, 0.f, 1.f, {}, A::Live,   false, "Subtitle background opacity (0 = none, 1 = solid)" },
        { "access.textScale",      K::Float,  0.5f, 3.f,  {}, A::Live,    false, "Text scale for 3D UI panels and subtitles (1 = default)" },
        { "access.hand",           K::Choice, 0.f, 0.f,   "right|left", A::Live, false, "Dominant hand: left swaps the move and turn/teleport sticks" },
        { "access.colorFilter",    K::Choice, 0.f, 0.f,   "none|protanopia|deuteranopia|tritanopia", A::Live, false, "Color vision correction filter" },
        { "input.action.",         K::Text,   0.f, 0.f,   {}, A::Live,    true,  "Action binding, same text as project.ini (none = unbound)" },
        { "input.axis.",           K::Text,   0.f, 0.f,   {}, A::Live,    true,  "Axis binding, same text as project.ini (none = unbound)" },
        { "input.mod.",            K::Text,   0.f, 0.f,   {}, A::Live,    true,  "Axis modifiers: deadzone:0.2, axial, curve:2, sens:1.5, invert (none = defaults)" },
        { "input.context.",        K::Text,   0.f, 0.f,   {}, A::Live,    true,  "Binding in a named input context: input.context.NAME.action|axis|mod.X" },
        { "game.",                 K::Text,   0.f, 0.f,   {}, A::Live,    true,  "Free values for the game" },
    };
    return k;
}

const UserSettings::KeyInfo* UserSettings::find(std::string_view name) {
    for (const KeyInfo& k : keys())
        if (!k.prefix && k.name == name) return &k;
    for (const KeyInfo& k : keys())
        if (k.prefix && name.size() > k.name.size() && name.substr(0, k.name.size()) == k.name) return &k;
    return nullptr;
}

bool UserSettings::store(std::string_view name, std::string value) {
    auto it = values_.find(name);
    if (it != values_.end() && it->second == value) return false;
    if (it == values_.end()) values_.emplace(std::string(name), std::move(value));
    else it->second = std::move(value);
    ++revision_;
    return true;
}

bool UserSettings::set(std::string_view name, std::string_view rawValue, std::string* why) {
    const std::string_view value = trimView(rawValue);
    const std::string n(name);
    const auto fail = [&](const std::string& msg) {
        if (why) *why = msg;
        return false;
    };
    if (value.find_first_of("\r\n") != std::string_view::npos) return fail("'" + n + "' value has a line break");
    const KeyInfo* k = find(name);
    if (!k) return fail("unknown setting '" + n + "' (game values go under game.)");
    if (k->prefix && k->name == "game." && !validGameName(name.substr(5))) return fail("bad game setting name '" + n + "'");
    const std::string v(value);
    switch (k->kind) {
    case K::Bool: {
        bool b = false;
        if (!fieldparse::toBool(v, b)) return fail("'" + n + "' expects true or false, got '" + v + "'");
        store(name, b ? "true" : "false");
        return true;
    }
    case K::Int: {
        int i = 0;
        if (!fieldparse::toInt(v, i)) return fail("'" + n + "' expects a whole number, got '" + v + "'");
        const int c = std::clamp(i, (int)k->lo, (int)k->hi);
        if (c != i && why) *why = "'" + n + "' = " + v + " is out of range [" + std::to_string((int)k->lo) + ", " + std::to_string((int)k->hi) + "]; using " + std::to_string(c);
        store(name, std::to_string(c));
        return true;
    }
    case K::Float: {
        float f = 0.f;
        if (!fieldparse::toFloat(v, f) || !std::isfinite(f)) return fail("'" + n + "' expects a number, got '" + v + "'");
        const float c = std::clamp(f, k->lo, k->hi);
        if (c != f && why) *why = "'" + n + "' = " + v + " is out of range [" + floatToText(k->lo) + ", " + floatToText(k->hi) + "]; using " + floatToText(c);
        store(name, floatToText(c));
        return true;
    }
    case K::Choice: {
        const std::string l = lower(v);
        if (!choiceHas(k->choices, l)) return fail("'" + n + "' expects one of " + std::string(k->choices) + ", got '" + v + "'");
        store(name, l);
        return true;
    }
    case K::Text:
        store(name, v);
        return true;
    }
    return false;
}

bool UserSettings::setFloat(std::string_view name, float v, std::string* why) { return set(name, floatToText(v), why); }
bool UserSettings::setBool(std::string_view name, bool v, std::string* why)   { return set(name, v ? "true" : "false", why); }
bool UserSettings::setInt(std::string_view name, int v, std::string* why)     { return set(name, std::to_string(v), why); }

bool UserSettings::reset(std::string_view name) {
    auto it = values_.find(name);
    if (it == values_.end()) return false;
    values_.erase(it);
    ++revision_;
    return true;
}

void UserSettings::resetAll() {
    if (values_.empty()) return;
    values_.clear();
    ++revision_;
}

bool UserSettings::has(std::string_view name) const { return values_.find(name) != values_.end(); }

std::optional<std::string> UserSettings::raw(std::string_view name) const {
    auto it = values_.find(name);
    if (it == values_.end()) return std::nullopt;
    return it->second;
}

float UserSettings::getFloat(std::string_view name, float def) const {
    auto it = values_.find(name);
    float f = 0.f;
    return it != values_.end() && fieldparse::toFloat(it->second, f) && std::isfinite(f) ? f : def;
}
bool UserSettings::getBool(std::string_view name, bool def) const {
    auto it = values_.find(name);
    bool b = false;
    return it != values_.end() && fieldparse::toBool(it->second, b) ? b : def;
}
int UserSettings::getInt(std::string_view name, int def) const {
    auto it = values_.find(name);
    int i = 0;
    return it != values_.end() && fieldparse::toInt(it->second, i) ? i : def;
}
std::string UserSettings::getString(std::string_view name, std::string_view def) const {
    auto it = values_.find(name);
    return it != values_.end() ? it->second : std::string(def);
}

void UserSettings::parse(const std::string& text, std::vector<std::string>& messages) {
    values_.clear();
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        const std::string_view l = trimView(line);
        if (l.empty() || l[0] == '#') continue;
        const size_t eq = l.find('=');
        if (eq == std::string_view::npos) { messages.push_back("line '" + std::string(l) + "' has no '='; skipped"); continue; }
        const std::string key(trimView(l.substr(0, eq)));
        const std::string_view value = trimView(l.substr(eq + 1));
        if (key.empty()) { messages.push_back("line '" + std::string(l) + "' has no name; skipped"); continue; }
        if (!find(key)) {
            // 새 버전이 쓴 이름일 수 있으므로 남겨 둡니다(다시 쓸 때 그대로).
            messages.push_back("unknown setting '" + key + "'; kept as is");
            values_[key] = std::string(value);
            continue;
        }
        std::string why;
        if (!set(key, value, &why)) messages.push_back(why + "; skipped");
        else if (!why.empty()) messages.push_back(why);
    }
    ++revision_;
}

std::string UserSettings::serialize() const {
    std::string s = "# Haruka user settings. Only values changed from the project defaults are listed.\n"
                    "# The engine rewrites this file when settings change; comments are not kept.\n";
    for (const auto& [k, v] : values_) s += k + "=" + v + "\n";
    return s;
}

bool UserSettings::load(const std::filesystem::path& path, std::vector<std::string>& messages) {
    std::string text, why;
    if (!hfile::readConfigText(path, text, &why)) {
        values_.clear();
        ++revision_;
        if (!why.empty()) messages.push_back(why);
        return false;
    }
    parse(text, messages);
    return true;
}

bool UserSettings::save(const std::filesystem::path& path, std::string* why) {
    return hfile::writeAtomic(path, serialize(), why);
}

ComfortSettings ComfortSettings::from(const UserSettings& s) {
    ComfortSettings c;
    const float snap = s.getFloat("comfort.snapTurn", 0.f);
    c.snapTurnDeg = snap > 0.f ? std::clamp(snap, 5.f, 180.f) : 0.f;
    const std::string tp = s.getString("comfort.teleport", "scene");
    c.teleport = tp == "on" ? 1 : tp == "off" ? 0 : -1;
    const std::string h = s.getString("comfort.height", "scene");
    c.height = h == "tracked" ? 0 : h == "fixed" ? 1 : -1;
    const float eye = s.getFloat("comfort.eyeHeight", 0.f);
    c.eyeHeight = eye > 0.f ? std::clamp(eye, 0.5f, 2.5f) : 0.f;
    c.vignette = std::clamp(s.getFloat("comfort.vignette", 0.f), 0.f, 1.f);
    c.seated   = s.getBool("comfort.seated", false);
    const float stand = s.getFloat("comfort.standingHeight", 0.f);
    c.standingHeight = stand > 0.f ? std::clamp(stand, 1.f, 2.5f) : 0.f;
    return c;
}
