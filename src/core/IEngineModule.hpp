#pragma once

class IEngineModule {
public:
    virtual ~IEngineModule() = default;

    virtual void        startup()      = 0;
    virtual void        shutdown()     = 0;
    virtual void        tick(float)    {}
    virtual void        onBeginPlay()  {}
    virtual const char* name() const   = 0;
};
