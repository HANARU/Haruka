#include "Subtitles.hpp"

#include "Accessibility.hpp"

#include <algorithm>

float SubtitleQueue::durationOf(const SubtitleLine& l) {
    return l.duration > 0.f ? l.duration : haccess::subtitleDuration(l.text.toString());
}

void SubtitleQueue::show(Shown s) {
    visible_.push_back(std::move(s));
    std::sort(visible_.begin(), visible_.end(), [](const Shown& a, const Shown& b) { return a.serial < b.serial; });
    ++revision_;
}

bool SubtitleQueue::push(SubtitleLine line) {
    const float dur = durationOf(line);
    // 같은 id: 글을 바꾸고 시간을 다시 셉니다(자리 · 순서는 그대로).
    if (!line.id.empty()) {
        for (Shown& s : visible_) {
            if (s.line.id != line.id) continue;
            s.line      = std::move(line);
            s.total     = dur;
            s.remaining = dur;
            ++revision_;
            return true;
        }
        for (Waiting& w : pending_) {
            if (w.s.line.id != line.id) continue;
            w.s.line      = std::move(line);
            w.s.total     = dur;
            w.s.remaining = dur;
            w.waited      = 0.f;
            return true;
        }
    }
    Shown s;
    s.line      = std::move(line);
    s.total     = dur;
    s.remaining = dur;
    s.serial    = ++serial_;
    if ((int)visible_.size() < maxLines_) {
        show(std::move(s));
        return true;
    }
    // 가득 참: 보이는 줄 중 우선순위가 가장 낮은 것(같으면 오래된 것)보다 높으면 밀어냅니다.
    auto low = std::min_element(visible_.begin(), visible_.end(), [](const Shown& a, const Shown& b) {
        return a.line.priority != b.line.priority ? a.line.priority < b.line.priority : a.serial < b.serial;
    });
    if (low != visible_.end() && s.line.priority > low->line.priority) {
        visible_.erase(low);
        show(std::move(s));
        return true;
    }
    if (pending_.size() >= kMaxPending) return false;
    pending_.push_back({ std::move(s), 0.f });
    return true;
}

bool SubtitleQueue::stop(const std::string& id) {
    if (id.empty()) return false;
    bool found = false;
    for (size_t i = visible_.size(); i-- > 0;)
        if (visible_[i].line.id == id) { visible_.erase(visible_.begin() + (ptrdiff_t)i); found = true; ++revision_; }
    for (size_t i = pending_.size(); i-- > 0;)
        if (pending_[i].s.line.id == id) { pending_.erase(pending_.begin() + (ptrdiff_t)i); found = true; }
    if (found) promote();
    return found;
}

void SubtitleQueue::clear() {
    if (!visible_.empty()) ++revision_;
    visible_.clear();
    pending_.clear();
}

void SubtitleQueue::promote() {
    while ((int)visible_.size() < maxLines_ && !pending_.empty()) {
        // 우선순위가 높은 것, 같으면 먼저 온 것.
        auto best = std::max_element(pending_.begin(), pending_.end(), [](const Waiting& a, const Waiting& b) {
            return a.s.line.priority != b.s.line.priority ? a.s.line.priority < b.s.line.priority : a.s.serial > b.s.serial;
        });
        Shown s = std::move(best->s);
        pending_.erase(best);
        show(std::move(s));
    }
}

void SubtitleQueue::update(float dt) {
    if (dt > 0.f) {
        for (size_t i = visible_.size(); i-- > 0;) {
            visible_[i].remaining -= dt;
            if (visible_[i].remaining <= 0.f) {
                visible_.erase(visible_.begin() + (ptrdiff_t)i);
                ++revision_;
            }
        }
        // 자기 길이보다 오래 기다린 줄은 이미 늦었으므로 버립니다.
        for (size_t i = pending_.size(); i-- > 0;) {
            pending_[i].waited += dt;
            if (pending_[i].waited >= pending_[i].s.total) pending_.erase(pending_.begin() + (ptrdiff_t)i);
        }
    }
    promote();
}

void SubtitleQueue::setMaxLines(int n) {
    maxLines_ = std::clamp(n, 1, kMaxLinesLimit);
    while ((int)visible_.size() > maxLines_) {   // 넘치는 줄은 오래된 것부터 뺍니다
        visible_.erase(visible_.begin());
        ++revision_;
    }
    promote();
}
