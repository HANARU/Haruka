#include "HInputSystem.hpp"
#include "../platform/IWindow.hpp"
#include "../core/TextFloat.hpp"
#include "../core/UserSettings.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

// ── 소스 번호(D5 소비 규칙). 같은 물리 입력은 같은 번호입니다(XR 트리거 버튼 = 트리거 축, 패드 lt 버튼 = lt 축) ─────────
namespace {

constexpr int kSrcKey = 1 << 16, kSrcXr = 2 << 16, kSrcPad = 3 << 16, kSrcMouse = 4 << 16;

int xrButtonSrc(HXrButton b) { return kSrcXr | int(b); }   // 0 ~ 10 (LeftTrigger 7, LeftGrip 8, RightTrigger 9, RightGrip 10)
int xrAxisSrc(HXrAxis a) {
    switch (a) {
        case HXrAxis::LeftTrigger:  return kSrcXr | 7;
        case HXrAxis::LeftSqueeze:  return kSrcXr | 8;
        case HXrAxis::RightTrigger: return kSrcXr | 9;
        case HXrAxis::RightSqueeze: return kSrcXr | 10;
        case HXrAxis::LeftStickX:   return kSrcXr | 11;
        case HXrAxis::LeftStickY:   return kSrcXr | 12;
        case HXrAxis::RightStickX:  return kSrcXr | 13;
        case HXrAxis::RightStickY:  return kSrcXr | 14;
    }
    return kSrcXr | 15;
}
constexpr int kPadAxisBase = int(HPadButton::Count);
int padAxisSrc(HPadAxis a) { return kSrcPad | (kPadAxisBase + int(a)); }
int padButtonSrc(HPadButton b) {
    if (b == HPadButton::LeftTrigger)  return padAxisSrc(HPadAxis::LeftTrigger);
    if (b == HPadButton::RightTrigger) return padAxisSrc(HPadAxis::RightTrigger);
    return kSrcPad | int(b);
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}
bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// GLFW 키 코드(IWindow 의 HInput 값과 같음). 글자 · 숫자는 대문자 ASCII 그대로입니다.
struct KeyName { const char* n; int c; };
const KeyName kKeyNames[] = {
    { "space", 32 }, { "apostrophe", 39 }, { "comma", 44 }, { "minus", 45 }, { "period", 46 }, { "slash", 47 },
    { "semicolon", 59 }, { "equal", 61 }, { "lbracket", 91 }, { "backslash", 92 }, { "rbracket", 93 }, { "grave", 96 },
    { "escape", 256 }, { "enter", 257 }, { "tab", 258 }, { "backspace", 259 }, { "insert", 260 }, { "delete", 261 },
    { "right", 262 }, { "left", 263 }, { "down", 264 }, { "up", 265 }, { "pageup", 266 }, { "pagedown", 267 },
    { "home", 268 }, { "end", 269 }, { "capslock", 280 },
    { "shift", 340 }, { "ctrl", 341 }, { "alt", 342 }, { "rshift", 344 }, { "rctrl", 345 }, { "ralt", 346 },
};

bool keyCode(const std::string& name, int& out) {
    const std::string n = lower(name);
    if (n.size() == 1 && std::isalpha((unsigned char)n[0])) { out = std::toupper((unsigned char)n[0]); return true; }
    if (n.size() == 1 && std::isdigit((unsigned char)n[0])) { out = n[0]; return true; }
    for (const KeyName& k : kKeyNames) if (n == k.n) { out = k.c; return true; }
    if (n.size() >= 2 && n[0] == 'f') {
        const int f = std::atoi(n.c_str() + 1);
        if (f >= 1 && f <= 12 && n == "f" + std::to_string(f)) { out = 289 + f; return true; }
    }
    if (n.size() == 3 && n[0] == 'k' && n[1] == 'p' && std::isdigit((unsigned char)n[2])) { out = 320 + (n[2] - '0'); return true; }
    // 이름 없는 키는 GLFW 번호로(#번호). 재지정이 이름표에 없는 키를 받아도 저장 · 다시 읽기가 되게 합니다.
    if (n.size() >= 2 && n[0] == '#') {
        char* end = nullptr;
        const long v = std::strtol(n.c_str() + 1, &end, 10);
        if (end && *end == '\0' && v > 0 && v <= 348) { out = int(v); return true; }
    }
    return false;
}

std::string keyName(int code) {
    if (code >= 'A' && code <= 'Z') return std::string(1, char(code));
    if (code >= '0' && code <= '9') return std::string(1, char(code));
    for (const KeyName& k : kKeyNames) if (k.c == code) return k.n;
    if (code >= 290 && code <= 301) return "f" + std::to_string(code - 289);
    if (code >= 320 && code <= 329) return "kp" + std::to_string(code - 320);
    return "#" + std::to_string(code);
}

struct XrButtonName { const char* n; HXrButton b; };
const XrButtonName kXrButtons[] = {
    { "left.x", HXrButton::LeftPrimary }, { "left.y", HXrButton::LeftSecondary },
    { "left.stick", HXrButton::LeftStickClick }, { "left.menu", HXrButton::LeftMenu },
    { "left.trigger", HXrButton::LeftTrigger }, { "left.grip", HXrButton::LeftGrip },
    { "right.a", HXrButton::RightPrimary }, { "right.b", HXrButton::RightSecondary },
    { "right.stick", HXrButton::RightStickClick },
    { "right.trigger", HXrButton::RightTrigger }, { "right.grip", HXrButton::RightGrip },
};
struct XrAxisName { const char* n; HXrAxis a; };
const XrAxisName kXrAxes[] = {
    { "left.trigger", HXrAxis::LeftTrigger }, { "right.trigger", HXrAxis::RightTrigger },
    { "left.grip", HXrAxis::LeftSqueeze }, { "right.grip", HXrAxis::RightSqueeze },
    { "left.stick.x", HXrAxis::LeftStickX }, { "left.stick.y", HXrAxis::LeftStickY },
    { "right.stick.x", HXrAxis::RightStickX }, { "right.stick.y", HXrAxis::RightStickY },
};
// 게임패드(D3). 면 버튼은 Xbox 배치(아래 = a).
struct PadButtonName { const char* n; HPadButton b; };
const PadButtonName kPadButtons[] = {
    { "a", HPadButton::A }, { "b", HPadButton::B }, { "x", HPadButton::X }, { "y", HPadButton::Y },
    { "lb", HPadButton::LeftBumper }, { "rb", HPadButton::RightBumper },
    { "back", HPadButton::Back }, { "start", HPadButton::Start }, { "guide", HPadButton::Guide },
    { "ls", HPadButton::LeftThumb }, { "rs", HPadButton::RightThumb },
    { "dpad.up", HPadButton::DpadUp }, { "dpad.right", HPadButton::DpadRight },
    { "dpad.down", HPadButton::DpadDown }, { "dpad.left", HPadButton::DpadLeft },
    { "lt", HPadButton::LeftTrigger }, { "rt", HPadButton::RightTrigger },
};
struct PadAxisName { const char* n; HPadAxis a; };
const PadAxisName kPadAxes[] = {
    { "left.x", HPadAxis::LeftX }, { "left.y", HPadAxis::LeftY }, { "right.x", HPadAxis::RightX },
    { "right.y", HPadAxis::RightY }, { "lt", HPadAxis::LeftTrigger }, { "rt", HPadAxis::RightTrigger },
};

template <class T, size_t N, class V>
bool lookup(const T (&table)[N], const std::string& name, V T::*field, V& out) {
    const std::string n = lower(name);
    for (const T& e : table) if (n == e.n) { out = e.*field; return true; }
    return false;
}
template <class T, size_t N, class V>
const char* nameOf(const T (&table)[N], V T::*field, V v) {
    for (const T& e : table) if (e.*field == v) return e.n;
    return "?";
}

bool toNumber(const std::string& s, float& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return end && *end == '\0' && std::isfinite(out);
}

std::vector<std::string> splitSources(const std::string& value) {
    std::vector<std::string> out;
    std::stringstream ss(value);
    for (std::string part; std::getline(ss, part, ',');) {
        const std::string s = trim(part);
        if (!s.empty()) out.push_back(s);
    }
    return out;
}

std::string join(const std::vector<std::string>& v) {
    std::string out;
    for (const std::string& s : v) out += (out.empty() ? "" : ", ") + s;
    return out;
}

bool isNone(const std::string& v) { return lower(trim(v)) == "none"; }

const HAxisMods kNoMods{};

} // namespace

// ── 바인딩 API(default 컨텍스트) ────────────────────────────────────────────────
void HInputSystem::bind(const std::string& action, int keyCode) {
    Binding& b = contexts_[kDefaultContext].actions[action];
    b.keys.push_back(keyCode);
    b.sources.push_back("key:" + keyName(keyCode));
    touch();
}

void HInputSystem::bindXrButton(const std::string& action, HXrButton button) {
    Binding& b = contexts_[kDefaultContext].actions[action];
    b.buttons.push_back(button);
    b.sources.push_back(std::string("xr:") + nameOf(kXrButtons, &XrButtonName::b, button));
    touch();
}

void HInputSystem::bindPad(const std::string& action, HPadButton button) {
    Binding& b = contexts_[kDefaultContext].actions[action];
    b.pads.push_back(button);
    b.sources.push_back(std::string("pad:") + nameOf(kPadButtons, &PadButtonName::b, button));
    touch();
}

void HInputSystem::unbind(const std::string& action) {
    if (auto it = contexts_.find(kDefaultContext); it != contexts_.end()) it->second.actions.erase(action);
    touch();
}

void HInputSystem::bindAxis(const std::string& axis, HXrAxis source) {
    AxisBinding& b = contexts_[kDefaultContext].axes[axis];
    b.xr.push_back(source);
    b.sources.push_back(std::string("xr:") + nameOf(kXrAxes, &XrAxisName::a, source));
    touch();
}

void HInputSystem::bindPadAxis(const std::string& axis, HPadAxis source) {
    AxisBinding& b = contexts_[kDefaultContext].axes[axis];
    b.pads.push_back(source);
    b.sources.push_back(std::string("pad:") + nameOf(kPadAxes, &PadAxisName::a, source));
    touch();
}

void HInputSystem::bindKeyAxis(const std::string& axis, int posKey, int negKey) {
    AxisBinding& b = contexts_[kDefaultContext].axes[axis];
    b.keys.push_back({ posKey, negKey });
    b.sources.push_back("keys:" + keyName(posKey) + "/" + keyName(negKey));
    touch();
}

void HInputSystem::bindMouseAxis(const std::string& axis, HMouseAxis which, float scale) {
    AxisBinding& b = contexts_[kDefaultContext].axes[axis];
    b.mouse.push_back({ which, scale });
    b.sources.push_back(std::string("mouse:") + (which == HMouseAxis::X ? "x" : "y") + (scale == 1.f ? "" : "*" + floatToText(scale)));
    touch();
}

void HInputSystem::unbindAxis(const std::string& axis) {
    if (auto it = contexts_.find(kDefaultContext); it != contexts_.end()) it->second.axes.erase(axis);
    touch();
}

void HInputSystem::setAxisMods(const std::string& axis, const HAxisMods& mods) {
    contexts_[kDefaultContext].mods[axis] = mods;
}

void HInputSystem::clearBindings() {
    contexts_.clear();
    overridesApplied_.clear();
    touch();
}

HInputSystem::Handle HInputSystem::subscribe(const std::string& action, EInputEvent when, Fn fn) {
    Handle h = next_++;
    listeners_[h] = { action, when, std::move(fn) };
    return h;
}

void HInputSystem::unsubscribe(Handle h) {
    listeners_.erase(h);
}

bool HInputSystem::held(const std::string& action) const {
    auto it = actionHeld_.find(action);
    return it != actionHeld_.end() && it->second;
}

// ── 장치 값 ─────────────────────────────────────────────────────────────────────
float HInputSystem::axisValue(const HXrInputState& s, HXrAxis a) {
    if (!s.focused) return 0.f;
    const HHandInput& L = s.hands[0];
    const HHandInput& R = s.hands[1];
    switch (a) {
        case HXrAxis::LeftTrigger:  return L.trigger;
        case HXrAxis::RightTrigger: return R.trigger;
        case HXrAxis::LeftSqueeze:  return L.squeeze;
        case HXrAxis::RightSqueeze: return R.squeeze;
        case HXrAxis::LeftStickX:   return L.thumbstick.x;
        case HXrAxis::LeftStickY:   return L.thumbstick.y;
        case HXrAxis::RightStickX:  return R.thumbstick.x;
        case HXrAxis::RightStickY:  return R.thumbstick.y;
    }
    return 0.f;
}

bool HInputSystem::buttonDown(const HXrInputState& s, HXrButton b) {
    if (!s.focused) return false;
    const HHandInput& L = s.hands[0];
    const HHandInput& R = s.hands[1];
    switch (b) {
        case HXrButton::LeftPrimary:     return L.primary;
        case HXrButton::LeftSecondary:   return L.secondary;
        case HXrButton::LeftStickClick:  return L.thumbstickClick;
        case HXrButton::LeftMenu:        return L.menu;
        case HXrButton::RightPrimary:    return R.primary;
        case HXrButton::RightSecondary:  return R.secondary;
        case HXrButton::RightStickClick: return R.thumbstickClick;
        case HXrButton::LeftTrigger:     return L.trigger > 0.5f;
        case HXrButton::LeftGrip:        return L.squeeze > 0.5f;
        case HXrButton::RightTrigger:    return R.trigger > 0.5f;
        case HXrButton::RightGrip:       return R.squeeze > 0.5f;
    }
    return false;
}

bool HInputSystem::padDown(const HGamepadState& s, HPadButton b) {
    if (!s.connected) return false;
    if (b == HPadButton::LeftTrigger)  return s.axes[size_t(HPadAxis::LeftTrigger)] > 0.5f;
    if (b == HPadButton::RightTrigger) return s.axes[size_t(HPadAxis::RightTrigger)] > 0.5f;
    return size_t(b) < size_t(HPadButton::Count) && s.buttons[size_t(b)];
}

float HInputSystem::shapeAnalog(float v, float other, bool stick, const HAxisMods& m, float dz) {
    if (dz > 0.f) {
        dz = std::min(dz, 0.95f);
        if (stick && !m.axial) {
            // 원형: 스틱 벡터 길이로 자르고 데드존 밖을 0 ~ 1 로 다시 펼칩니다(방향은 그대로).
            const float len = std::sqrt(v * v + other * other);
            if (len <= dz) return 0.f;
            v *= std::min(1.f, (len - dz) / (1.f - dz)) / len;
        } else {
            const float a = std::fabs(v);
            if (a <= dz) return 0.f;
            v = std::copysign(std::min(1.f, (a - dz) / (1.f - dz)), v);
        }
    }
    if (m.curve != 1.f && v != 0.f) v = std::copysign(std::pow(std::fabs(v), m.curve), v);
    return v;
}

// ── 컨텍스트(D5) ────────────────────────────────────────────────────────────────
void HInputSystem::pushContext(const std::string& name, int priority, bool blockLower) {
    if (name.empty() || name == kDefaultContext) return;
    for (Active& a : stack_)
        if (a.name == name) { a.priority = priority; a.blockLower = blockLower; touch(); return; }
    stack_.push_back({ name, priority, blockLower, ++pushSeq_ });
    touch();
}

bool HInputSystem::popContext(const std::string& name) {
    const auto it = std::find_if(stack_.begin(), stack_.end(), [&](const Active& a) { return a.name == name; });
    if (it == stack_.end()) return false;
    stack_.erase(it);
    touch();
    return true;
}

std::vector<std::string> HInputSystem::activeContexts() const {
    std::vector<const Active*> order;
    for (const Active& a : stack_) order.push_back(&a);
    std::sort(order.begin(), order.end(), [](const Active* x, const Active* y) {
        return x->priority != y->priority ? x->priority > y->priority : x->seq > y->seq;
    });
    std::vector<std::string> out;
    for (const Active* a : order) out.push_back(a->name);
    out.push_back(kDefaultContext);
    return out;
}

void HInputSystem::claims(const Binding& b, std::unordered_set<int>& out) {
    for (int k : b.keys) out.insert(kSrcKey | k);
    for (HXrButton x : b.buttons) out.insert(xrButtonSrc(x));
    for (HPadButton p : b.pads) out.insert(padButtonSrc(p));
}

void HInputSystem::claims(const AxisBinding& b, std::unordered_set<int>& out) {
    for (HXrAxis a : b.xr) out.insert(xrAxisSrc(a));
    for (HPadAxis a : b.pads) out.insert(padAxisSrc(a));
    for (const KeyAxis& k : b.keys) { out.insert(kSrcKey | k.pos); out.insert(kSrcKey | k.neg); }
    for (const MouseAxis& m : b.mouse) out.insert(kSrcMouse | int(m.which));
}

const std::vector<HInputSystem::Layer>& HInputSystem::layers() const {
    if (!layersDirty_) return layers_;
    layers_.clear();
    std::unordered_set<int> hidden;
    auto add = [&](const std::string& n) {
        Layer L;
        L.name   = n;
        L.hidden = hidden;
        if (auto it = contexts_.find(n); it != contexts_.end()) {
            L.ctx = &it->second;
            for (const auto& [_, b] : L.ctx->actions) claims(b, hidden);
            for (const auto& [_, b] : L.ctx->axes) claims(b, hidden);
        }
        layers_.push_back(std::move(L));
    };
    bool blocked = false;
    const std::vector<std::string> order = activeContexts();
    for (const std::string& n : order) {
        if (n == kDefaultContext) break;
        add(n);
        const auto it = std::find_if(stack_.begin(), stack_.end(), [&](const Active& a) { return a.name == n; });
        if (it != stack_.end() && it->blockLower) { blocked = true; break; }
    }
    if (!blocked) add(kDefaultContext);
    layersDirty_ = false;
    return layers_;
}

// ── 값 읽기 ──────────────────────────────────────────────────────────────────────
HXrAxis HInputSystem::swapHands(HXrAxis a) {
    switch (a) {
        case HXrAxis::LeftStickX:  return HXrAxis::RightStickX;
        case HXrAxis::LeftStickY:  return HXrAxis::RightStickY;
        case HXrAxis::RightStickX: return HXrAxis::LeftStickX;
        case HXrAxis::RightStickY: return HXrAxis::LeftStickY;
        default:                   return a;
    }
}

HPadAxis HInputSystem::swapHands(HPadAxis a) {
    switch (a) {
        case HPadAxis::LeftX:  return HPadAxis::RightX;
        case HPadAxis::LeftY:  return HPadAxis::RightY;
        case HPadAxis::RightX: return HPadAxis::LeftX;
        case HPadAxis::RightY: return HPadAxis::LeftY;
        default:               return a;
    }
}

float HInputSystem::axis(const std::string& name) const {
    float total = 0.f;
    const Context* def = nullptr;
    if (auto it = contexts_.find(kDefaultContext); it != contexts_.end()) def = &it->second;
    for (const Layer& L : layers()) {
        if (!L.ctx) continue;
        const auto it = L.ctx->axes.find(name);
        if (it == L.ctx->axes.end()) continue;
        const AxisBinding& b = it->second;
        // 수정자: 그 컨텍스트 것, 없으면 default 컨텍스트 것, 없으면 기본값.
        const HAxisMods* m = &kNoMods;
        if (auto mi = L.ctx->mods.find(name); mi != L.ctx->mods.end()) m = &mi->second;
        else if (def) if (auto di = def->mods.find(name); di != def->mods.end()) m = &di->second;
        auto hidden = [&](int src) { return L.hidden.count(src) != 0; };

        float best = 0.f;
        for (HXrAxis la : b.xr) {
            if (hidden(xrAxisSrc(la))) continue;
            // 한 손 조작(Docs/Accessibility.md D8): 바인딩 · 소비는 논리 소스 그대로, 값만 반대 손 스틱에서 읽습니다.
            const HXrAxis a = handSwap_ ? swapHands(la) : la;
            float other = 0.f;
            bool  stick = true;
            switch (a) {
                case HXrAxis::LeftStickX:  other = axisValue(xr_, HXrAxis::LeftStickY);  break;
                case HXrAxis::LeftStickY:  other = axisValue(xr_, HXrAxis::LeftStickX);  break;
                case HXrAxis::RightStickX: other = axisValue(xr_, HXrAxis::RightStickY); break;
                case HXrAxis::RightStickY: other = axisValue(xr_, HXrAxis::RightStickX); break;
                default: stick = false; break;
            }
            const float v = shapeAnalog(axisValue(xr_, a), other, stick, *m, std::max(m->deadzone, 0.f));
            if (std::fabs(v) > std::fabs(best)) best = v;
        }
        for (HPadAxis la : b.pads) {
            if (hidden(padAxisSrc(la)) || !pad_.connected) continue;
            const HPadAxis a = handSwap_ ? swapHands(la) : la;
            const size_t i = size_t(a);
            const bool  stick = a != HPadAxis::LeftTrigger && a != HPadAxis::RightTrigger;
            const float other = stick ? pad_.axes[i ^ 1u] : 0.f;   // LeftX ↔ LeftY, RightX ↔ RightY
            const float v = shapeAnalog(pad_.axes[i], other, stick, *m, m->deadzone < 0.f ? 0.15f : m->deadzone);
            if (std::fabs(v) > std::fabs(best)) best = v;
        }
        float keys = 0.f;
        for (const KeyAxis& k : b.keys)
            keys += (!hidden(kSrcKey | k.pos) && keysDown_.count(k.pos) ? 1.f : 0.f)
                  - (!hidden(kSrcKey | k.neg) && keysDown_.count(k.neg) ? 1.f : 0.f);
        keys = std::clamp(keys, -1.f, 1.f);
        float mouse = 0.f;
        for (const MouseAxis& mm : b.mouse)
            if (!hidden(kSrcMouse | int(mm.which))) mouse += (mm.which == HMouseAxis::X ? lookDx_ : lookDy_) * mm.scale;
        float v = (best + keys + mouse) * m->sens;
        if (m->invert) v = -v;
        total += v;
    }
    return total;
}

void HInputSystem::update(float dt, IWindow& win, bool keyboardCaptured) {
    HGamepadState pad;
    win.readGamepad(pad);
    pad_ = pad;
    update(dt, [&win](int key) { return win.isKeyDown(key); }, keyboardCaptured);
}

void HInputSystem::update(float dt, const std::function<bool(int)>& isKeyDown, bool keyboardCaptured) {
    const std::vector<Layer>& ls = layers();
    // 바인딩된 키만 한 번씩 물어 둡니다(axis() 는 창 없이 읽음).
    keysDown_.clear();
    if (!keyboardCaptured) {
        auto probe = [&](int k) { if (!keysDown_.count(k) && isKeyDown(k)) keysDown_.insert(k); };
        for (const Layer& L : ls) {
            if (!L.ctx) continue;
            for (const auto& [_, b] : L.ctx->actions) for (int k : b.keys) probe(k);
            for (const auto& [_, b] : L.ctx->axes) for (const KeyAxis& k : b.keys) { probe(k.pos); probe(k.neg); }
        }
    }

    // 동작 이름별 눌림: 켜진 컨텍스트에서 위 컨텍스트가 가져가지 않은 소스 하나라도 눌렸으면 눌림(D5).
    std::unordered_map<std::string, bool> now;
    for (const auto& [name, _] : actionHeld_) now[name] = false;
    for (const Layer& L : ls) {
        if (!L.ctx) continue;
        for (const auto& [name, b] : L.ctx->actions) {
            bool& h = now[name];
            if (h) continue;
            for (int k : b.keys)
                if (!L.hidden.count(kSrcKey | k) && keysDown_.count(k)) { h = true; break; }
            if (!h)
                for (HXrButton x : b.buttons)
                    if (!L.hidden.count(xrButtonSrc(x)) && buttonDown(xr_, x)) { h = true; break; }
            if (!h)
                for (HPadButton p : b.pads)
                    if (!L.hidden.count(padButtonSrc(p)) && padDown(pad_, p)) { h = true; break; }
        }
    }

    for (const auto& [action, h] : now) {
        bool& prev = actionHeld_[action];
        const bool pressed  = h && !prev;
        const bool released = !h && prev;
        prev = h;
        if (!pressed && !released && !h) continue;

        for (auto& [handle, lst] : listeners_) {
            if (lst.action != action) continue;
            switch (lst.when) {
                case EInputEvent::Pressed:  if (pressed)  lst.fn(dt); break;
                case EInputEvent::Released: if (released) lst.fn(dt); break;
                case EInputEvent::Held:     if (h)        lst.fn(dt); break;
            }
        }
    }
}

// ── 설정 줄 해석 (2026-10-15, 컨텍스트 · 패드 · 수정자 2026-11-21) ──────────────────────
bool HInputSystem::isBindingKey(const std::string& k) {
    return startsWith(k, "input.action.") || startsWith(k, "input.axis.") || startsWith(k, "input.mod.")
        || startsWith(k, "input.context.");
}

bool HInputSystem::parseKey(const std::string& key, std::string& ctx, KeyKind& kind, std::string& name, std::string& error) {
    std::string rest;
    if (startsWith(key, "input.context.")) {
        const std::string r = key.substr(14);
        const size_t dot = r.find('.');
        if (dot == std::string::npos || dot == 0) { error = "expected input.context.NAME.action|axis|mod.X"; return false; }
        ctx  = r.substr(0, dot);
        rest = r.substr(dot + 1);
    } else if (startsWith(key, "input.")) {
        ctx  = kDefaultContext;
        rest = key.substr(6);
    } else {
        error = "expected input.action.NAME, input.axis.NAME or input.mod.NAME";
        return false;
    }
    if (startsWith(rest, "action."))    { kind = KeyKind::Action; name = rest.substr(7); }
    else if (startsWith(rest, "axis.")) { kind = KeyKind::Axis;   name = rest.substr(5); }
    else if (startsWith(rest, "mod."))  { kind = KeyKind::Mod;    name = rest.substr(4); }
    else { error = "expected input.action.NAME, input.axis.NAME or input.mod.NAME"; return false; }
    if (name.empty()) { error = "missing name"; return false; }
    return true;
}

std::string HInputSystem::makeKey(const std::string& ctx, KeyKind kind, const std::string& name) {
    const char* k = kind == KeyKind::Action ? "action." : kind == KeyKind::Axis ? "axis." : "mod.";
    return (ctx == kDefaultContext ? std::string("input.") : "input.context." + ctx + ".") + k + name;
}

bool HInputSystem::parseActionLine(const std::string& value, Binding& act, std::string& error) {
    for (const std::string& src : splitSources(value)) {
        const size_t colon = src.find(':');
        if (colon == std::string::npos) { error = "source '" + src + "' has no kind (key: · pad: · xr:)"; return false; }
        const std::string kind = lower(src.substr(0, colon));
        const std::string arg  = trim(src.substr(colon + 1));
        int code = 0; HXrButton b{}; HPadButton p{};
        if (kind == "key" && keyCode(arg, code)) {
            act.keys.push_back(code);
            act.sources.push_back("key:" + keyName(code));
        } else if (kind == "xr" && lookup(kXrButtons, arg, &XrButtonName::b, b)) {
            act.buttons.push_back(b);
            act.sources.push_back("xr:" + lower(arg));
        } else if (kind == "pad" && lookup(kPadButtons, arg, &PadButtonName::b, p)) {
            act.pads.push_back(p);
            act.sources.push_back("pad:" + lower(arg));
        } else {
            error = "unknown action source '" + src + "'";
            return false;
        }
    }
    if (act.sources.empty()) { error = "no sources"; return false; }
    return true;
}

bool HInputSystem::parseAxisLine(const std::string& value, AxisBinding& ax, std::string& error) {
    for (const std::string& src : splitSources(value)) {
        const size_t colon = src.find(':');
        if (colon == std::string::npos) { error = "source '" + src + "' has no kind (keys: · pad: · xr: · mouse:)"; return false; }
        const std::string kind = lower(src.substr(0, colon));
        const std::string arg  = trim(src.substr(colon + 1));
        HXrAxis a{}; HPadAxis pa{};
        if (kind == "xr" && lookup(kXrAxes, arg, &XrAxisName::a, a)) {
            ax.xr.push_back(a);
            ax.sources.push_back("xr:" + lower(arg));
            continue;
        }
        if (kind == "pad" && lookup(kPadAxes, arg, &PadAxisName::a, pa)) {
            ax.pads.push_back(pa);
            ax.sources.push_back("pad:" + lower(arg));
            continue;
        }
        if (kind == "keys") {
            const size_t slash = arg.find('/');
            int p = 0, n = 0;
            if (slash != std::string::npos && keyCode(trim(arg.substr(0, slash)), p) && keyCode(trim(arg.substr(slash + 1)), n)) {
                ax.keys.push_back({ p, n });
                ax.sources.push_back("keys:" + keyName(p) + "/" + keyName(n));
                continue;
            }
        }
        if (kind == "mouse") {
            std::string axisName = lower(arg);
            float scale = 1.f;
            if (const size_t star = axisName.find('*'); star != std::string::npos) {
                if (!toNumber(trim(axisName.substr(star + 1)), scale)) { error = "bad scale in '" + src + "'"; return false; }
                axisName = trim(axisName.substr(0, star));
            }
            if (axisName == "x" || axisName == "y") {
                ax.mouse.push_back({ axisName == "x" ? HMouseAxis::X : HMouseAxis::Y, scale });
                ax.sources.push_back("mouse:" + axisName + (scale == 1.f ? "" : "*" + floatToText(scale)));
                continue;
            }
        }
        error = "unknown axis source '" + src + "'";
        return false;
    }
    if (ax.sources.empty()) { error = "no sources"; return false; }
    return true;
}

bool HInputSystem::parseModLine(const std::string& value, HAxisMods& m, std::string& error) {
    for (const std::string& tok : splitSources(value)) {
        const std::string t = lower(tok);
        const size_t colon = t.find(':');
        const std::string k = trim(t.substr(0, colon));
        const std::string v = colon == std::string::npos ? std::string() : trim(t.substr(colon + 1));
        float f = 0.f;
        auto num = [&](float lo, float hi) {
            if (!toNumber(v, f) || f < lo || f > hi) {
                error = "'" + tok + "' expects a number in [" + floatToText(lo) + ", " + floatToText(hi) + "]";
                return false;
            }
            return true;
        };
        if (k == "deadzone")    { if (!num(0.f, 0.95f)) return false; m.deadzone = f; }
        else if (k == "curve")  { if (!num(0.1f, 10.f)) return false; m.curve = f; }
        else if (k == "sens")   { if (!num(0.01f, 100.f)) return false; m.sens = f; }
        else if (k == "radial" && v.empty()) m.axial = false;
        else if (k == "axial" && v.empty())  m.axial = true;
        else if (k == "invert" && v.empty()) m.invert = true;
        else { error = "unknown modifier '" + tok + "' (deadzone: · radial · axial · curve: · sens: · invert)"; return false; }
    }
    return true;
}

std::string HInputSystem::modText(const HAxisMods& m) {
    std::vector<std::string> t;
    if (m.deadzone >= 0.f) t.push_back("deadzone:" + floatToText(m.deadzone));
    if (m.axial) t.push_back("axial");
    if (m.curve != 1.f) t.push_back("curve:" + floatToText(m.curve));
    if (m.sens != 1.f) t.push_back("sens:" + floatToText(m.sens));
    if (m.invert) t.push_back("invert");
    return t.empty() ? std::string("none") : join(t);
}

bool HInputSystem::applyBinding(const std::string& key, const std::string& value, std::string& error) {
    std::string ctx, name;
    KeyKind kind{};
    if (!parseKey(key, ctx, kind, name, error)) return false;
    // 먼저 모두 읽어 보고, 다 맞으면 바꿉니다(틀린 줄이 있던 바인딩을 반쯤 지우지 않게).
    switch (kind) {
    case KeyKind::Action: {
        Binding act;
        if (!parseActionLine(value, act, error)) return false;
        contexts_[ctx].actions[name] = std::move(act);
        break;
    }
    case KeyKind::Axis: {
        AxisBinding ax;
        if (!parseAxisLine(value, ax, error)) return false;
        contexts_[ctx].axes[name] = std::move(ax);
        break;
    }
    case KeyKind::Mod: {
        if (isNone(value)) {
            if (auto it = contexts_.find(ctx); it != contexts_.end()) it->second.mods.erase(name);
            break;
        }
        HAxisMods m;
        if (!parseModLine(value, m, error)) return false;
        contexts_[ctx].mods[name] = m;
        break;
    }
    }
    touch();
    return true;
}

HInputSystem::Lines HInputSystem::defaultBindings() {
    return {
        { "input.axis.MoveX",       "keys:D/A, xr:left.stick.x, pad:left.x" },
        { "input.axis.MoveY",       "keys:W/S, xr:left.stick.y, pad:left.y" },
        { "input.axis.MoveUp",      "keys:E/Q" },
        { "input.axis.TurnX",       "xr:right.stick.x" },
        { "input.axis.Teleport",    "xr:right.stick.y" },
        { "input.axis.LookX",       "mouse:x" },
        { "input.axis.LookY",       "mouse:y" },
        { "input.action.GrabLeft",  "xr:left.grip" },
        { "input.action.GrabRight", "xr:right.grip" },
    };
}

// ── 사용자 재지정 · 덮어쓰기(D6 · D7) ─────────────────────────────────────────────
std::string HInputSystem::bindingLine(const std::string& key) const {
    std::string ctx, name, err;
    KeyKind kind{};
    if (!parseKey(key, ctx, kind, name, err)) return "none";
    const auto ci = contexts_.find(ctx);
    if (ci == contexts_.end()) return "none";
    const Context& c = ci->second;
    if (kind == KeyKind::Action) {
        const auto it = c.actions.find(name);
        return it == c.actions.end() || it->second.sources.empty() ? "none" : join(it->second.sources);
    }
    if (kind == KeyKind::Axis) {
        const auto it = c.axes.find(name);
        return it == c.axes.end() || it->second.sources.empty() ? "none" : join(it->second.sources);
    }
    const auto it = c.mods.find(name);
    return it == c.mods.end() ? "none" : modText(it->second);
}

const std::string* HInputSystem::projectLine(const std::string& key) const {
    std::string ctx, name, err;
    KeyKind kind{};
    if (!parseKey(key, ctx, kind, name, err)) return nullptr;
    const std::string want = makeKey(ctx, kind, name);
    for (const auto& [k, v] : projectLines_) {
        std::string c2, n2;
        KeyKind k2{};
        if (parseKey(k, c2, k2, n2, err) && makeKey(c2, k2, n2) == want) return &v;
    }
    return nullptr;
}

void HInputSystem::clearTarget(const std::string& key) {
    std::string ctx, name, err;
    KeyKind kind{};
    if (!parseKey(key, ctx, kind, name, err)) return;
    const auto ci = contexts_.find(ctx);
    if (ci == contexts_.end()) return;
    if (kind == KeyKind::Action)    ci->second.actions.erase(name);
    else if (kind == KeyKind::Axis) ci->second.axes.erase(name);
    else                            ci->second.mods.erase(name);
    touch();
}

void HInputSystem::restoreProject(const std::string& key) {
    std::string err;
    if (const std::string* p = projectLine(key); p && applyBinding(key, *p, err)) return;
    clearTarget(key);
}

std::vector<std::string> HInputSystem::conflictsFor(const std::string& source, const std::string& context,
                                                    const std::string& exceptKey) const {
    std::vector<std::string> out;
    std::unordered_set<int> want;
    std::string err;
    Binding b;
    AxisBinding a;
    if (parseActionLine(source, b, err)) claims(b, want);
    else if (parseAxisLine(source, a, err)) claims(a, want);
    else return out;
    const auto ci = contexts_.find(context);
    if (ci == contexts_.end()) return out;
    auto hits = [&](const std::unordered_set<int>& s) {
        for (int x : s) if (want.count(x)) return true;
        return false;
    };
    for (const auto& [name, bb] : ci->second.actions) {
        std::unordered_set<int> s;
        claims(bb, s);
        const std::string k = makeKey(context, KeyKind::Action, name);
        if (k != exceptKey && hits(s)) out.push_back(k);
    }
    for (const auto& [name, ab] : ci->second.axes) {
        std::unordered_set<int> s;
        claims(ab, s);
        const std::string k = makeKey(context, KeyKind::Axis, name);
        if (k != exceptKey && hits(s)) out.push_back(k);
    }
    std::sort(out.begin(), out.end());
    return out;
}

HRebindResult HInputSystem::rebind(UserSettings& settings, const std::string& key, const std::string& from, const std::string& to) {
    HRebindResult r;
    std::string ctx, name;
    KeyKind kind{};
    if (!parseKey(key, ctx, kind, name, r.error)) return r;
    if (kind == KeyKind::Mod) { r.error = "modifiers are not rebound; set " + key + " directly"; return r; }
    const std::string skey = makeKey(ctx, kind, name);

    // 소스 글을 정규 글 하나로.
    auto canon = [&](const std::string& s, std::string& out) {
        std::vector<std::string> srcs;
        if (kind == KeyKind::Action) { Binding b; if (!parseActionLine(s, b, r.error)) return false; srcs = b.sources; }
        else { AxisBinding a; if (!parseAxisLine(s, a, r.error)) return false; srcs = a.sources; }
        if (srcs.size() != 1) { r.error = "expected one source, got '" + s + "'"; return false; }
        out = srcs[0];
        return true;
    };
    std::string f, t;
    if (!trim(from).empty() && !canon(from, f)) return r;
    if (!trim(to).empty() && !canon(to, t)) return r;
    if (f.empty() && t.empty()) { r.error = "nothing to change"; return r; }

    const std::string cur = bindingLine(skey);
    std::vector<std::string> list = cur == "none" ? std::vector<std::string>{} : splitSources(cur);
    if (!f.empty()) {
        const auto it = std::find(list.begin(), list.end(), f);
        if (it == list.end()) { r.error = "'" + f + "' is not bound to " + skey; return r; }
        if (t.empty()) list.erase(it);
        else           *it = t;
    } else {
        list.push_back(t);
    }
    std::vector<std::string> uniq;   // 같은 소스가 두 번 들어가지 않게(순서 유지)
    for (const std::string& s : list)
        if (std::find(uniq.begin(), uniq.end(), s) == uniq.end()) uniq.push_back(s);
    r.line = uniq.empty() ? std::string("none") : join(uniq);

    if (uniq.empty()) clearTarget(skey);
    else if (std::string e; !applyBinding(skey, r.line, e)) { r.error = e; return r; }

    // 프로젝트 줄과 같으면 덮어쓰기를 지웁니다(바뀐 것만 저장).
    std::string base = "none";
    if (const std::string* p = projectLine(skey)) {
        std::string e;
        Binding b; AxisBinding a;
        if (kind == KeyKind::Action) base = parseActionLine(*p, b, e) ? join(b.sources) : *p;
        else                         base = parseAxisLine(*p, a, e) ? join(a.sources) : *p;
    }
    if (r.line == base) settings.reset(skey);
    else                settings.set(skey, r.line);

    if (!t.empty()) r.conflicts = conflictsFor(t, ctx, skey);
    r.ok = true;
    return r;
}

bool HInputSystem::resetBinding(UserSettings& settings, const std::string& key) {
    std::string ctx, name, err;
    KeyKind kind{};
    if (!parseKey(key, ctx, kind, name, err)) return false;
    const std::string skey = makeKey(ctx, kind, name);
    const bool had = settings.reset(skey);
    restoreProject(skey);
    return had;
}

int HInputSystem::resetAllBindings(UserSettings& settings) {
    std::vector<std::string> keys;
    for (const auto& [k, _] : settings.values())
        if (isBindingKey(k)) keys.push_back(k);
    for (const std::string& k : keys) {
        settings.reset(k);
        restoreProject(k);
    }
    return int(keys.size());
}

HInputSystem::OverrideResult HInputSystem::applyUserOverrides(const UserSettings& settings, std::vector<std::string>& warnings) {
    OverrideResult r;
    std::map<std::string, std::string> want;
    for (const auto& [k, v] : settings.values())
        if (isBindingKey(k)) want[k] = v;
    if (want == overridesApplied_) return r;
    r.changed = true;
    // 지운 덮어쓰기: 프로젝트 줄로 돌립니다.
    for (const auto& [k, v] : overridesApplied_)
        if (!want.count(k)) restoreProject(k);
    for (const auto& [k, v] : want) {
        std::string err;
        if (isNone(v)) {
            std::string ctx, name;
            KeyKind kind{};
            if (parseKey(k, ctx, kind, name, err)) { clearTarget(k); ++r.applied; }
            else warnings.push_back(k + " = " + v + ": " + err);
            continue;
        }
        if (applyBinding(k, v, err)) {
            ++r.applied;
        } else {
            warnings.push_back(k + " = " + v + ": " + err);
            restoreProject(k);   // 프로젝트 줄이 없으면 예전 덮어쓰기를 남기지 않음
        }
    }
    // 틀린 줄도 적용한 것으로 기억해, 바뀌기 전까지 프레임마다 다시 알리지 않습니다.
    overridesApplied_ = std::move(want);
    return r;
}
