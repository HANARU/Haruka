#pragma once

class IEngineModule {
public:
    virtual ~IEngineModule() = default;

    virtual void        startup()      = 0;
    virtual void        shutdown()     = 0;
    virtual void        tick(float)    {}
    virtual void        onBeginPlay()  {}
    virtual void        onEndPlay()    {}
    // 플레이 중 일시정지와 재개입니다. 일시정지 동안 Engine 은 tick 을 부르지 않지만,
    // 자기 스레드에서 도는 일(오디오 믹싱 등)은 모듈이 여기서 직접 멈춰야 합니다.
    virtual void        onPauseChanged(bool /*paused*/) {}
    virtual const char* name() const   = 0;
};
