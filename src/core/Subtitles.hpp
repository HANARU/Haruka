#pragma once
// 자막 큐(2026-11-21, Docs/Accessibility.md D3).
//
// 소리 · 사운드 컴포넌트 · 게임 코드가 줄(번역 글 HText · 화자 · 길이 · 우선순위 · id)을 넣으면, 보이는 줄 수 · 우선순위 · 표시 시간을
// 정합니다. 엔진이 월드 서비스(world.setService<SubtitleQueue>)로 두고 프레임마다 update 를 부릅니다. 표시(components/SubtitlePanel)는
// visible() 만 읽습니다. 표시를 꺼도 큐는 돌아, 켜는 순간 지금 말하는 줄이 보입니다. 메인 스레드 전용입니다.
//
//   auto* subs = world.service<SubtitleQueue>();
//   SubtitleLine l;
//   l.text    = HTEXT("Game", "Intro_01", "Welcome aboard.");
//   l.speaker = HTEXT("Game", "Captain", "Captain");
//   subs->push(std::move(l));

#include "HText.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct SubtitleLine {
    HText       text;
    HText       speaker;            // 비면 화자 없이
    float       duration = 0.f;     // 초, 0 이하면 글자 수로 자동(haccess::subtitleDuration)
    int         priority = 0;       // 클수록 먼저
    std::string id;                 // 같은 id 가 다시 오면 그 줄을 바꿈(빈 id 는 언제나 새 줄)
};

class SubtitleQueue {
public:
    static constexpr int kDefaultMaxLines = 2;
    static constexpr int kMaxLinesLimit   = 6;

    struct Shown {
        SubtitleLine line;
        float        total     = 0.f;   // 표시 시간(초)
        float        remaining = 0.f;
        uint64_t     serial    = 0;     // 도착 순서
    };

    // 줄을 넣습니다. 보이게 됐거나 기다리는 줄에 들어갔으면 true, 기다릴 자리도 없어(밀어낼 수 없음) 버렸으면 false.
    // 대기 줄은 kMaxPending 개까지입니다.
    static constexpr size_t kMaxPending = 16;
    bool push(SubtitleLine line);
    // id 의 줄을 지웁니다(보이는 줄 · 기다리는 줄). 있었으면 true.
    bool stop(const std::string& id);
    void clear();
    // 시간을 진행합니다. 끝난 줄을 빼고 기다리던 줄을 올립니다.
    void update(float dt);

    void setMaxLines(int n);
    int  maxLines() const { return maxLines_; }

    // 보이는 줄, 먼저 온 것부터(위 → 아래).
    const std::vector<Shown>& visible() const { return visible_; }
    size_t pendingCount() const { return pending_.size(); }
    // 보이는 줄 구성이 바뀔 때마다 오릅니다(표시가 다시 그릴지 정함).
    uint64_t revision() const { return revision_; }

private:
    struct Waiting { Shown s; float waited = 0.f; };
    void show(Shown s);
    void promote();
    static float durationOf(const SubtitleLine& l);

    std::vector<Shown>   visible_;
    std::vector<Waiting> pending_;
    int                  maxLines_ = kDefaultMaxLines;
    uint64_t             serial_   = 0;
    uint64_t             revision_ = 0;
};
