#include "Component.hpp"
#include "Actor.hpp"

// 실효 실행 상태(enabled_ && 소유 액터의 activeInHierarchy)로의 전이를 감지해,
// 전이할 때만 onEnable/onDisable 을 한 번 발화합니다. live_ 가 엣지를 기억하므로
// 같은 상태로의 중복 호출은 아무 일도 하지 않습니다(멱등).
//
// owner 가 없는(아직 부착 전) 컴포넌트는 실효가 false 입니다. 부착 시
// Actor::addComponent 가 owner 를 세운 뒤 syncLive 를 호출해 초기 onEnable 을 발화합니다.
void Component::syncLive() {
    bool want = enabled_ && owner && owner->activeInHierarchy();
    if (want == live_) return;
    live_ = want;
    if (want) onEnable();
    else      onDisable();
}
