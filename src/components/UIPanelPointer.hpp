#pragma once
#include "UIPanelComponent.hpp"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <functional>
#include <span>
#include <vector>

class Actor;

// ── 월드 UI 패널 포인터 (3D UI 구간 5 · P2a, 2026-11-16) ─────────────────────────────────
// 광선 하나(마우스 · 컨트롤러)로 월드 패널을 가리키고 누르는 장치입니다. 광선이 맞은 패널의 UV 를 텍스처 픽셀로 바꿔
// 그 패널의 HSlateContext 에 HPointerEvent(이동 · 누름 · 뗌 · 휠)로 넘깁니다. 패널 위젯은 에디터 UI 와 같은 코드로
// 호버 · 클릭을 처리하고, 바뀐 그리기 목록은 렌더러가 알아서 다시 그립니다(구간 2).
//
//  - 맞추기: 패널은 액터 로컬 XY 평면의 width × height 사각형(가운데 원점, 앞면 +Z). 한 면 패널은 뒤에서 맞지 않고,
//    양면 패널은 뒤에서 맞으면 u 를 뒤집습니다(셰이더 ui_panel.frag 와 같음). UV 는 왼쪽 위 (0,0).
//  - 가림: occluder 가 있으면 패널보다 가까이 맞은 물체(패널 액터 자신 제외)가 있을 때 그 패널을 맞지 않은 것으로 봅니다.
//    엔진은 물리 레이캐스트를 넘깁니다(충돌체가 없는 메시는 가리지 않음).
//  - 누르는 동안(캡처): 누른 패널에 계속 넘깁니다. 광선이 패널 밖으로 나가도 그 평면의 좌표를 넘겨 끌기가 이어지고,
//    뗌도 그 패널이 받습니다(에디터 UI 의 캡처 규칙과 같음).
//  - 호버가 다른 패널로 옮기거나 패널을 벗어나면, 앞 패널에 "바깥" 이동(HOutsideEvent)을 보내 호버를 풉니다.
//  - 패널 목록은 매 update 에 받습니다. 목록에 없는(지워졌거나 꺼진) 패널은 포인터가 붙잡지 않습니다(낡은 주소를 쓰지 않음).
class UIPanelPointer {
public:
    struct Ray {
        glm::vec3 origin{ 0.f };
        glm::vec3 dir{ 0.f, 0.f, -1.f };   // 길이는 상관없음
        bool      valid = false;           // 거짓이면 아무것도 가리키지 않음(추적 잃음 · 창 밖)
    };
    struct Buttons {
        bool  primary = false;   // 클릭(마우스 왼쪽 · 트리거)
        float wheel   = 0.f;     // 이번 프레임의 휠(위로 +)
    };
    struct Hit {
        UIPanelComponent* panel    = nullptr;
        float             distance = 0.f;   // 광선 시작점에서 맞은 점까지(m)
        glm::vec3         point{ 0.f };     // 월드
        glm::vec2         uv{ 0.f };        // [0,1]², 왼쪽 위 (0,0)
        glm::vec2         pixel{ 0.f };     // uv × textureSize(배치 픽셀)
        bool              front = true;
        bool              onPlane = false;  // point · distance 가 유효함(캡처 중 평면과 만나지 않으면 거짓)
    };
    // 광선 방향(정규화 안 됨 가능)으로 maxDistance 안에서 가장 가까운 충돌체까지 거리와 액터를 돌려줍니다. 없으면 false.
    using Occluder = std::function<bool(const glm::vec3& origin, const glm::vec3& dir, float maxDistance,
                                        float& outDistance, Actor*& outActor)>;

    float maxDistance = 20.f;   // 이보다 먼 패널은 가리키지 않음(m)

    // 패널 하나와 광선 판정. 액터가 없거나 크기가 0 이면 false. bounded 가 거짓이면 사각형 밖이어도 평면과 만나면 맞음(캡처용).
    static bool intersect(const UIPanelComponent& panel, const glm::vec3& origin, const glm::vec3& dir,
                          Hit& out, bool bounded = true);

    // 화면의 한 점(그림 영역 안 비율 [0,1]², 왼쪽 위 (0,0))을 지나는 월드 광선. viewProj 는 그 화면을 그린 행렬입니다
    // (에디터 WorldViewport::ray 와 같은 수학). 게임 실행 파일의 마우스 광선에 씁니다.
    static bool rayFromScreen(const glm::mat4& viewProj, glm::vec2 frac, Ray& out);

    // ── XR 컨트롤러 (3D UI 구간 6) ─────────────────────────────────────────
    // 조준 자세(XR 원점 기준 위치 · 방향)를 월드 광선으로. originToWorld 는 카메라의 XR 원점 행렬, 조준 방향은 -Z(OpenXR aim).
    static Ray rayFromAim(const glm::mat4& originToWorld, const glm::vec3& position, const glm::quat& orientation);
    // 트리거 아날로그 값 → 클릭. 0.55 를 넘으면 누름, 0.35 아래로 내려가야 뗌(떨림으로 클릭이 여러 번 나지 않게).
    struct TriggerLatch {
        bool down = false;
        bool update(float value) {
            if (!down && value > 0.55f) down = true;
            else if (down && value < 0.35f) down = false;
            return down;
        }
    };
    TriggerLatch trigger;   // XR 손 포인터가 씀(reset 이 풂)

    // 스틱 위아래 → 이번 프레임의 휠 값(초당 최대 8 칸, 가운데 0.25 는 무시). 위로 밀면 +(마우스 휠을 위로 굴림과 같음).
    static float stickWheel(float stickY, float dt);
    // 레이저 · 커서 선분(월드). 가리키는 패널이 있을 때만 시작점 → 맞은 점의 선과, 맞은 점에 패널 평면의 작은 십자를 냅니다.
    // 누르는 중이면 밝은 색입니다. 없으면 아무것도 넣지 않습니다.
    struct Segment { glm::vec3 a, b; glm::vec4 color; };
    void appendLaser(const glm::vec3& rayOrigin, std::vector<Segment>& out) const;

    // 한 프레임 갱신. panels 는 지금 보이는(켜진 액터 · 켜진 컴포넌트) 패널들입니다.
    void update(std::span<UIPanelComponent* const> panels, const Ray& ray, const Buttons& buttons,
                const Occluder& occluder = {});
    // 붙잡은 패널에 "바깥"과 뗌을 보내고 상태를 비웁니다(Play 끝 · 장치를 잃음).
    void reset();

    // 이번 프레임에 가리킨 패널(캡처 중이면 누른 패널). 없으면 hit().panel == nullptr.
    const Hit& hit() const { return hit_; }
    bool       overPanel() const { return hit_.panel != nullptr; }
    bool       capturing() const { return pressed_ != nullptr; }

private:
    void send(UIPanelComponent& panel, HPointerEvent e);
    void leave(UIPanelComponent& panel);

    Hit               hit_;
    UIPanelComponent* hover_   = nullptr;   // 마지막으로 이동을 받은 패널
    UIPanelComponent* pressed_ = nullptr;   // 누름을 받은 패널(캡처)
    bool              prevPrimary_ = false;
    glm::vec2         lastPixel_{ 0.f };
};
