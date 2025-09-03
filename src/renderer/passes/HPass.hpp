#pragma once

// ── HPass ─────────────────────────────────────────────────────────────────────
// 패스 공통 수명주기 (RendererRefactor.md §3-④).
// create()/record()는 패스별 입력이 달라 의도적으로 인터페이스에 없다.
// 리사이즈는 2단계다: 스왑체인 파괴 전에 release(메모리 코얼레싱을 위해
// 크기 의존 자원을 먼저 반납), 스왑체인 재생성 후 acquire.
struct HPass {
    virtual ~HPass() = default;

    virtual void destroy() = 0;              // 전체 자원 파괴 (idempotent)
    virtual void releaseSizeDependent() {}   // 리사이즈 1단계: 크기 의존 자원 해제
    virtual void acquireSizeDependent() {}   // 리사이즈 2단계: 재생성 (rc.extent 갱신 후)
    virtual void rebuildPipelines() {}       // 셰이더 핫리로드 (PSO만 재생성)
};
