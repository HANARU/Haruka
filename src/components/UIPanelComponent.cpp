#include "UIPanelComponent.hpp"
#include "../math/Damping.hpp"
#include "../core/Accessibility.hpp"
#include "../scene/Actor.hpp"
#include "../scene/World.hpp"
#include "../scene/ComponentFields.hpp"
#include "../scene/Reflect.hpp"
#include "../ui/slate/widgets/SBorder.hpp"
#include "../ui/slate/widgets/SButton.hpp"
#include "../ui/slate/widgets/STextBlock.hpp"
#include "../ui/slate/widgets/SVerticalBox.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>

// ── 내용 등록 ─────────────────────────────────────────────────────────────────
// std::map 은 노드가 옮겨지지 않아 이름의 c_str() 이 등록 뒤로 그대로입니다(디테일 패널 열거 칸이 그 포인터를 씀).
static std::map<std::string, UIPanelComponent::ContentFactory>& contentRegistry() {
    static std::map<std::string, UIPanelComponent::ContentFactory> reg;
    return reg;
}

void UIPanelComponent::registerContent(const std::string& name, ContentFactory fn) {
    if (name.empty() || !fn) return;
    contentRegistry()[name] = std::move(fn);
}

std::vector<std::string> UIPanelComponent::contentNames() {
    std::vector<std::string> out;
    for (const auto& kv : contentRegistry()) out.push_back(kv.first);
    return out;
}

// ── 게임 API (구간 4) ─────────────────────────────────────────────────────────
bool UIPanelComponent::isValidParamName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name)
        if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')) return false;
    return true;
}

bool UIPanelComponent::setParam(const std::string& name, std::string value) {
    if (!isValidParamName(name)) return false;
    for (char& ch : value) if (ch == '\n' || ch == '\r') ch = ' ';   // 씬 파일은 한 줄에 값 하나
    params_[name] = std::move(value);
    return true;
}

std::string UIPanelComponent::param(const std::string& name, const std::string& fallback) const {
    const auto it = params_.find(name);
    return it != params_.end() ? it->second : fallback;
}

float UIPanelComponent::paramFloat(const std::string& name, float fallback) const {
    const auto it = params_.find(name);
    float v = fallback;
    if (it != params_.end() && !fieldparse::toFloat(it->second, v)) v = fallback;
    return v;
}

Attr<std::string> UIPanelComponent::paramAttr(const std::string& name, std::string fallback) {
    return Attr<std::string>(std::function<std::string()>(
        [this, name, fallback = std::move(fallback)] { return param(name, fallback); }));
}

void UIPanelComponent::fireAction(const std::string& action) {
    if (actionHandler_) actionHandler_(*this, action);
}

std::function<void()> UIPanelComponent::actionCallback(std::string action) {
    return [this, action = std::move(action)] { fireAction(action); };
}

// ── 따라오기 (구간 7) ─────────────────────────────────────────────────────────
static glm::vec3 flatForward(const glm::quat& rot, const glm::vec3& fallback) {
    glm::vec3 f = rot * glm::vec3(0.f, 0.f, -1.f);
    f.y = 0.f;
    if (glm::dot(f, f) < 1e-4f) f = glm::vec3(fallback.x, 0.f, fallback.z);   // 바로 위 · 아래를 봄
    if (glm::dot(f, f) < 1e-8f) f = glm::vec3(0.f, 0.f, -1.f);
    return glm::normalize(f);
}

UIPanelComponent::Pose UIPanelComponent::followTarget(const glm::vec3& headPos, const glm::quat& headRot, float distance,
                                                      float height, const glm::vec3& fallbackForward) {
    const glm::vec3 f = flatForward(headRot, fallbackForward);
    Pose p;
    p.position = headPos + f * distance + glm::vec3(0.f, height, 0.f);
    // 앞면(+Z)이 머리를 향하게: R·(0,0,1) = -f 인 요 회전.
    p.rotation = glm::angleAxis(std::atan2(-f.x, -f.z), glm::vec3(0.f, 1.f, 0.f));
    return p;
}

bool UIPanelComponent::stepFollow(const glm::vec3& headPos, const glm::quat& headRot, float dt) {
    if (!owner) return false;
    const float dist   = std::clamp(followDistance, kMinFollowDistance, kMaxFollowDistance);
    const float height = std::clamp(followHeight, -kMaxFollowHeight, kMaxFollowHeight);
    const glm::vec3 cur = owner->worldPosition();
    // 머리가 바로 아래를 볼 때는 지금 패널 쪽을 앞으로 봅니다(패널이 머리 위로 튀지 않게).
    const Pose target = followTarget(headPos, headRot, dist, height, cur - headPos);

    if (!followStarted_) {   // 처음: 바로 목표로
        owner->setWorldPosition(target.position);
        owner->setWorldRotation(target.rotation);
        followStarted_ = true;
        followMoving_  = false;
        return false;
    }

    if (!followMoving_) {
        // 머리 수평 앞쪽과 머리 → 패널 수평 방향의 각, 수평 거리 · 높이의 어긋남으로 출발을 정합니다.
        const glm::vec3 f  = flatForward(headRot, cur - headPos);
        glm::vec3       to = cur - headPos;
        to.y = 0.f;
        const float horiz = glm::length(to);
        const float cosA  = horiz > 1e-4f ? glm::dot(f, to / horiz) : -1.f;
        const bool  turned = cosA < std::cos(glm::radians(kFollowStartAngleDeg));
        const bool  far    = std::fabs(horiz - dist) > kFollowStartOffset;
        const bool  high   = std::fabs((cur.y - headPos.y) - height) > kFollowStartOffset;
        followMoving_ = turned || far || high;
    }
    if (!followMoving_) return false;

    // 프레임률 독립 지수 감쇠(공통 수학 hmath::damp, 시간 상수 1 / kFollowRate). 예전 식 1 − exp(−rate·dt) 와 같은 값입니다.
    const float halfLife = hmath::halfLifeFromTau(1.f / kFollowRate);
    const float dts = std::max(dt, 0.f);
    const glm::vec3 pos = hmath::damp(cur, target.position, halfLife, dts);
    const glm::quat rot = hmath::dampRotation(owner->worldRotation(), target.rotation, halfLife, dts);
    owner->setWorldPosition(pos);
    owner->setWorldRotation(rot);
    if (glm::length(target.position - pos) < 0.01f) followMoving_ = false;   // 1 cm 안이면 멈춤
    return followMoving_;
}

// ── 크기 ──────────────────────────────────────────────────────────────────────
glm::uvec2 UIPanelComponent::computeTextureSize(float w, float h, float ppm) {
    auto side = [&](float m) {
        const float px = std::round(std::clamp(m, kMinSize, kMaxSize) * std::clamp(ppm, kMinPixelsPerMeter, kMaxPixelsPerMeter));
        return (uint32_t)std::clamp(px, 1.f, (float)kMaxTextureSide);
    };
    return { side(w), side(h) };
}

float UIPanelComponent::computeLayoutScale(float w, float h, float ppm) {
    const glm::uvec2 tex = computeTextureSize(w, h, ppm);
    const float      p   = std::clamp(ppm, kMinPixelsPerMeter, kMaxPixelsPerMeter);
    float s = p / kReferencePixelsPerMeter;
    // 한 변이 상한(kMaxTextureSide)에 잘렸으면 그 변에 맞게 줄입니다(내용이 텍스처 밖으로 넘치지 않게). 잘리지 않은 변은
    // 반올림 오차(0.5 px 미만)만 있으므로 ppm 비율을 그대로 씁니다(편집할 때마다 글자 크기가 조금씩 달라지지 않게).
    auto fit = [&](float m, uint32_t px) {
        const float want = std::round(std::clamp(m, kMinSize, kMaxSize) * p);
        if ((float)px < want) s = std::min(s, (float)px / (std::clamp(m, kMinSize, kMaxSize) * kReferencePixelsPerMeter));
    };
    fit(w, tex.x);
    fit(h, tex.y);
    return s;
}

void UIPanelComponent::applyTextureSize() {
    texSize_     = computeTextureSize(width, height, pixelsPerMeter);
    layoutScale_ = computeLayoutScale(width, height, pixelsPerMeter);
    dirty_       = true;
}

void UIPanelComponent::onPropsCommitted() { applyTextureSize(); }
void UIPanelComponent::onFieldsLoaded(const ComponentLoadContext&) { applyTextureSize(); }

bool UIPanelComponent::localBounds(glm::vec3& outMin, glm::vec3& outMax) const {
    const glm::vec3 half{ 0.5f * width, 0.5f * height, 0.5f * kThickness };
    outMin = -half;
    outMax =  half;
    return true;
}

// ── 위젯 트리 ─────────────────────────────────────────────────────────────────
float UIPanelComponent::textScale() const {
    const AccessibilityState* acc = owner && owner->world() ? owner->world()->service<AccessibilityState>() : nullptr;
    return acc ? acc->access.textScale : 1.f;
}

HSlateContext& UIPanelComponent::slate() {
    const float ts = textScale();
    if (!built_ || builtContent_ != content || builtScale_ != layoutScale_ || builtTextScale_ != ts) {
        const float s = layoutScale_;
        std::shared_ptr<HWidget> root;
        auto& reg = contentRegistry();
        auto  it  = reg.find(content);
        if (it != reg.end()) root = it->second(*this, s);
        if (!root) {
            // 모르는 이름: 빈 판 대신 이유를 보여 줍니다(씬 파일의 이름이 틀렸거나 그 내용을 등록하는 모듈이 없음).
            auto msg = std::make_shared<STextBlock>();
            msg->text("Unknown UI panel content: \"" + content + "\"")->fontSize(32.f * s * ts)
               ->color(glm::vec4(1.f, 0.4f, 0.4f, 1.f));
            auto border = std::make_shared<SBorder>();
            border->background(glm::vec4(0.1f, 0.05f, 0.05f, 1.f))->padding(HPadding(32.f * s))->content(msg);
            root = border;
        }
        slate_.setRootWidget(std::move(root));
        builtContent_ = content;
        builtScale_   = s;
        builtTextScale_ = ts;
        built_        = true;
        dirty_        = true;
    }
    return slate_;
}

// ── 에디터 ────────────────────────────────────────────────────────────────────
std::vector<PropDesc> UIPanelComponent::buildProps() {
    auto* p = this;
    std::vector<PropDesc> props;
    props.push_back(makePropFloat("Width", PropColor::Red, 0.01f,
        [p]{ return p->width; },  [p](float v){ p->width  = std::clamp(v, kMinSize, kMaxSize); }, PropUnit::Size));
    props.push_back(makePropFloat("Height", PropColor::Green, 0.01f,
        [p]{ return p->height; }, [p](float v){ p->height = std::clamp(v, kMinSize, kMaxSize); }, PropUnit::Size));
    props.push_back(makePropFloat("Pixels / m", PropColor::Neutral, 10.f,
        [p]{ return p->pixelsPerMeter; },
        [p](float v){ p->pixelsPerMeter = std::clamp(v, kMinPixelsPerMeter, kMaxPixelsPerMeter); }));
    props.push_back(makePropToggle("Two Sided",
        [p]{ return p->twoSided; }, [p](bool v){ p->twoSided = v; }));
    props.push_back(makePropEnum("Placement", { "World", "Follow Head" },
        [p]{ return (int)p->placement; },
        [p](int v){ p->placement = v == 1 ? Placement::FollowHead : Placement::World; }));
    props.push_back(makePropFloat("Follow Distance", PropColor::Neutral, 0.01f,
        [p]{ return p->followDistance; },
        [p](float v){ p->followDistance = std::clamp(v, kMinFollowDistance, kMaxFollowDistance); }, PropUnit::Size));
    props.push_back(makePropFloat("Follow Height", PropColor::Neutral, 0.01f,
        [p]{ return p->followHeight; },
        [p](float v){ p->followHeight = std::clamp(v, -kMaxFollowHeight, kMaxFollowHeight); }, PropUnit::Offset));
    props.push_back(makePropFloat("Brightness", PropColor::Neutral, 0.01f,
        [p]{ return p->brightness; }, [p](float v){ p->brightness = std::clamp(v, kMinBrightness, 1.f); }));
    // 열거 칸은 등록된 이름 목록입니다. 목록에 없는 이름(모르는 내용)은 맨 앞에 그대로 보여 줍니다.
    // 이름 목록은 두 람다가 함께 붙잡아, 열거 칸이 가리키는 문자열이 칸보다 먼저 사라지지 않게 합니다.
    auto names = std::make_shared<std::vector<std::string>>(contentNames());
    if (std::find(names->begin(), names->end(), content) == names->end()) names->insert(names->begin(), content);
    std::vector<const char*> opts;
    for (const auto& n : *names) opts.push_back(n.c_str());
    props.push_back(makePropEnum("Content", opts,
        [p, names]{ return (int)(std::find(names->begin(), names->end(), p->content) - names->begin()); },
        [p, names](int v){ if (v >= 0 && v < (int)names->size()) { p->content = (*names)[(size_t)v]; p->markDirty(); } }));
    return props;
}

std::string UIPanelComponent::readOnlyInfo() const {
    char scale[32];
    std::snprintf(scale, sizeof(scale), "%.2f", layoutScale_);
    std::string info = "Texture " + std::to_string(texSize_.x) + " x " + std::to_string(texSize_.y) + " px, layout scale " + scale;
    // 디테일의 정보 줄은 한 줄 높이라 값은 개수만 보입니다(값 자체는 씬 파일의 param.이름 줄).
    if (!params_.empty()) info += ", " + std::to_string(params_.size()) + " param(s)";
    return info;
}

// ── 직렬화 (DEF-22) ───────────────────────────────────────────────────────────
void UIPanelComponent::writeFields(ComponentFieldWriter& w) const {
    w.f("width",          width);
    w.f("height",         height);
    w.f("pixelsPerMeter", pixelsPerMeter);
    w.b("twoSided",       twoSided);
    w.s("content",        content);
    w.f("brightness",     brightness);
    w.s("placement",      placement == Placement::FollowHead ? "followHead" : "world");
    w.f("followDistance", followDistance);
    w.f("followHeight",   followHeight);
    for (const auto& kv : params_) w.s(("param." + kv.first).c_str(), kv.second);
}

bool UIPanelComponent::readField(const std::string& key, const std::string& value) {
    auto ranged = [&](float& field, float lo, float hi) {
        float v;
        if (!fieldparse::toFloat(value, v) || !(v >= lo && v <= hi)) return false;
        field = v;
        return true;
    };
    bool ok = true;
    if      (key == "width")          ok = ranged(width,  kMinSize, kMaxSize);
    else if (key == "height")         ok = ranged(height, kMinSize, kMaxSize);
    else if (key == "pixelsPerMeter") ok = ranged(pixelsPerMeter, kMinPixelsPerMeter, kMaxPixelsPerMeter);
    else if (key == "twoSided")       ok = fieldparse::toBool(value, twoSided);
    else if (key == "content")      { ok = !value.empty(); if (ok) content = value; }
    else if (key == "brightness")     ok = ranged(brightness, kMinBrightness, 1.f);
    else if (key == "placement") {
        if      (value == "world")      placement = Placement::World;
        else if (value == "followHead") placement = Placement::FollowHead;
        else ok = false;
    }
    else if (key == "followDistance") ok = ranged(followDistance, kMinFollowDistance, kMaxFollowDistance);
    else if (key == "followHeight")   ok = ranged(followHeight, -kMaxFollowHeight, kMaxFollowHeight);
    else if (key.rfind("param.", 0) == 0) ok = setParam(key.substr(6), value);
    else return false;
    if (!ok) reportBadField(key, value);
    return true;
}
