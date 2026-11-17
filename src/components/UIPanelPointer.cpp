#include "UIPanelPointer.hpp"
#include "../core/Actor.hpp"

#include <algorithm>
#include <cmath>

// ── 광선 · 패널 판정 ───────────────────────────────────────────────────────────
bool UIPanelPointer::intersect(const UIPanelComponent& panel, const glm::vec3& origin, const glm::vec3& dir,
                               Hit& out, bool bounded) {
    const Actor* a = panel.owner;
    if (!a || panel.width <= 0.f || panel.height <= 0.f) return false;
    const glm::mat4 world = a->worldMatrix();
    const glm::mat4 inv   = glm::inverse(world);
    const glm::vec3 ro    = glm::vec3(inv * glm::vec4(origin, 1.f));
    const glm::vec3 rd    = glm::vec3(inv * glm::vec4(dir, 0.f));
    if (std::fabs(rd.z) < 1e-8f) return false;          // 평면과 나란함
    const float t = -ro.z / rd.z;
    if (!(t >= 0.f)) return false;                      // 뒤쪽(NaN 포함)
    const bool front = rd.z < 0.f;                      // +Z 쪽에서 들어옴
    if (bounded && !front && !panel.twoSided) return false;

    const glm::vec3 p = ro + rd * t;
    glm::vec2 uv{ p.x / panel.width + 0.5f, 0.5f - p.y / panel.height };
    if (!front) uv.x = 1.f - uv.x;                      // 뒷면은 좌우를 뒤집어 그림(ui_panel.frag)
    if (bounded && (uv.x < 0.f || uv.x > 1.f || uv.y < 0.f || uv.y > 1.f)) return false;

    out.panel    = const_cast<UIPanelComponent*>(&panel);
    out.point    = glm::vec3(world * glm::vec4(p, 1.f));
    out.distance = glm::length(out.point - origin);
    out.uv       = uv;
    out.pixel    = uv * glm::vec2(panel.textureSize());
    out.front    = front;
    out.onPlane  = true;
    return true;
}

bool UIPanelPointer::rayFromScreen(const glm::mat4& viewProj, glm::vec2 frac, Ray& out) {
    out.valid = false;
    if (!(frac.x >= 0.f && frac.x <= 1.f && frac.y >= 0.f && frac.y <= 1.f)) return false;
    const glm::mat4 inv = glm::inverse(viewProj);
    const glm::vec2 ndc = frac * 2.f - 1.f;
    const glm::vec4 pN  = inv * glm::vec4(ndc, 0.f, 1.f);
    const glm::vec4 pF  = inv * glm::vec4(ndc, 1.f, 1.f);
    if (std::fabs(pN.w) < 1e-6f || std::fabs(pF.w) < 1e-6f) return false;
    const glm::vec3 o = glm::vec3(pN) / pN.w;
    const glm::vec3 d = glm::vec3(pF) / pF.w - o;
    if (glm::dot(d, d) < 1e-12f) return false;
    out.origin = o;
    out.dir    = glm::normalize(d);
    out.valid  = true;
    return true;
}

// ── XR 컨트롤러 (구간 6) ──────────────────────────────────────────────────────
UIPanelPointer::Ray UIPanelPointer::rayFromAim(const glm::mat4& originToWorld, const glm::vec3& position,
                                               const glm::quat& orientation) {
    Ray r;
    const glm::vec3 o = glm::vec3(originToWorld * glm::vec4(position, 1.f));
    const glm::vec3 d = glm::vec3(originToWorld * glm::vec4(orientation * glm::vec3(0.f, 0.f, -1.f), 0.f));
    if (glm::dot(d, d) < 1e-12f) return r;
    r.origin = o;
    r.dir    = glm::normalize(d);
    r.valid  = true;
    return r;
}

float UIPanelPointer::stickWheel(float stickY, float dt) {
    constexpr float kDead = 0.25f, kNotchesPerSec = 8.f;
    const float a = std::fabs(stickY);
    if (a <= kDead || dt <= 0.f) return 0.f;
    const float t = std::min(1.f, (a - kDead) / (1.f - kDead));
    return (stickY > 0.f ? 1.f : -1.f) * t * kNotchesPerSec * dt;
}

void UIPanelPointer::appendLaser(const glm::vec3& rayOrigin, std::vector<Segment>& out) const {
    if (!hit_.panel || !hit_.panel->owner || !hit_.onPlane) return;
    const glm::vec4 color = capturing() ? glm::vec4(1.f, 1.f, 1.f, 1.f) : glm::vec4(0.45f, 0.75f, 1.f, 1.f);
    out.push_back({ rayOrigin, hit_.point, color });
    // 커서: 패널 평면의 십자(한 변 1.5 cm). 액터 크기와 상관없이 월드 길이로 맞춥니다.
    const glm::mat4 w  = hit_.panel->owner->worldMatrix();
    glm::vec3       ax = glm::vec3(w[0]), ay = glm::vec3(w[1]);
    const float     lx = glm::length(ax), ly = glm::length(ay);
    if (lx < 1e-6f || ly < 1e-6f) return;
    ax *= 0.0075f / lx;
    ay *= 0.0075f / ly;
    // 패널 앞쪽으로 1 mm 띄워 패널 화소와 깊이가 겹쳐 깜빡이지 않게 합니다.
    glm::vec3 n = glm::cross(ax, ay);
    const float ln = glm::length(n);
    if (ln > 1e-9f) n *= (hit_.front ? 0.001f : -0.001f) / ln;
    const glm::vec3 c = hit_.point + n;
    out.push_back({ c - ax, c + ax, color });
    out.push_back({ c - ay, c + ay, color });
}

// ── 이벤트 ────────────────────────────────────────────────────────────────────
void UIPanelPointer::send(UIPanelComponent& panel, HPointerEvent e) {
    HSlateContext& ctx = panel.slate();
    // 렌더러가 그릴 때 배치하지만, 그리기 전(처음 프레임 · 창 없는 시험)에도 맞는 위젯을 찾도록 같은 크기로 배치합니다.
    ctx.arrange(glm::vec2(panel.textureSize()));
    ctx.handlePointerEvent(e);
}

void UIPanelPointer::leave(UIPanelComponent& panel) {
    HPointerEvent e;
    e.button = -1;
    e.down   = false;
    send(panel, HOutsideEvent(e));
}

void UIPanelPointer::reset() {
    if (pressed_) {
        HPointerEvent up;
        up.position = lastPixel_;
        up.button   = 0;
        up.down     = false;
        send(*pressed_, up);
    }
    if (hover_) leave(*hover_);
    else if (pressed_) leave(*pressed_);
    hit_         = {};
    hover_       = nullptr;
    pressed_     = nullptr;
    prevPrimary_ = false;
    trigger      = {};
}

void UIPanelPointer::update(std::span<UIPanelComponent* const> panels, const Ray& ray, const Buttons& buttons,
                            const Occluder& occluder) {
    // 지난 프레임에 붙잡은 패널이 이번 목록에 없으면(지워짐 · 꺼짐) 주소를 버립니다. 그 패널에는 아무것도 보내지 않습니다.
    auto listed = [&](UIPanelComponent* p) { return p && std::find(panels.begin(), panels.end(), p) != panels.end(); };
    if (!listed(hover_))   hover_   = nullptr;
    if (!listed(pressed_)) pressed_ = nullptr;

    // ── 가리키는 패널 ──────────────────────────────────────────────
    Hit hit;
    const bool rayOk = ray.valid && glm::dot(ray.dir, ray.dir) > 1e-12f;
    if (pressed_) {
        // 누르는 동안은 누른 패널의 평면 좌표(사각형 밖이어도)를 넘깁니다. 평면과 만나지 않으면 마지막 좌표를 씁니다.
        if (!rayOk || !intersect(*pressed_, ray.origin, ray.dir, hit, false)) {
            hit       = {};
            hit.panel = pressed_;
            hit.pixel = lastPixel_;
        }
    } else if (rayOk) {
        for (UIPanelComponent* p : panels) {
            Hit h;
            if (p && intersect(*p, ray.origin, ray.dir, h) && h.distance <= maxDistance &&
                (!hit.panel || h.distance < hit.distance))
                hit = h;
        }
        // 가장 가까운 패널보다 앞에 다른 물체가 있으면 아무 패널도 가리키지 않습니다(더 먼 패널은 그 물체에 더 가려짐).
        if (hit.panel && occluder) {
            float  d = 0.f;
            Actor* blocker = nullptr;
            if (occluder(ray.origin, ray.dir, hit.distance, d, blocker) && d < hit.distance - 1e-3f &&
                blocker != hit.panel->owner)
                hit = {};
        }
    }

    // ── 호버 이동 ──────────────────────────────────────────────────
    if (hover_ && hover_ != hit.panel) leave(*hover_);
    const bool newTarget = hit.panel && hit.panel != hover_;
    hover_ = hit.panel;

    if (hit.panel && (newTarget || hit.pixel != lastPixel_)) {
        HPointerEvent mv;
        mv.position = hit.pixel;
        mv.delta    = newTarget ? glm::vec2(0.f) : hit.pixel - lastPixel_;
        mv.button   = -1;
        mv.down     = buttons.primary && pressed_ != nullptr;
        send(*hit.panel, mv);
    }

    // ── 누름 · 뗌 ──────────────────────────────────────────────────
    if (buttons.primary && !prevPrimary_ && hit.panel) {
        HPointerEvent dn;
        dn.position = hit.pixel;
        dn.button   = 0;
        dn.down     = true;
        send(*hit.panel, dn);
        pressed_ = hit.panel;
    } else if (!buttons.primary && prevPrimary_ && pressed_) {
        HPointerEvent up;
        up.position = hit.pixel;
        up.button   = 0;
        up.down     = false;
        send(*pressed_, up);
        pressed_ = nullptr;
    }

    // ── 휠 ─────────────────────────────────────────────────────────
    if (hit.panel && buttons.wheel != 0.f) {
        HPointerEvent wh;
        wh.position   = hit.pixel;
        wh.wheelDelta = buttons.wheel;
        send(*hit.panel, wh);
    }

    prevPrimary_ = buttons.primary;
    if (hit.panel) lastPixel_ = hit.pixel;
    hit_ = hit;
}
