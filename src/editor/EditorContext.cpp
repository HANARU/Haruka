#include "EditorContext.hpp"

void EditorContext::addLog(const std::string& line) {
    {
        std::lock_guard<std::mutex> lock(logMutex_);
        logLines_.push_back(line);
        while ((int)logLines_.size() > kMaxLogLines)
            logLines_.pop_front();
    }
    logDirty.store(true, std::memory_order_release);
}

std::vector<std::string> EditorContext::snapshotLog() const {
    std::lock_guard<std::mutex> lock(logMutex_);
    return { logLines_.begin(), logLines_.end() };
}

LogStreamBuf::LogStreamBuf(EditorContext* ctx, std::streambuf* orig)
    : ctx_(ctx), orig_(orig) {}

LogStreamBuf::~LogStreamBuf() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!line_.empty()) flushLine();
}

int LogStreamBuf::overflow(int c) {
    if (c == EOF) return EOF;
    std::lock_guard<std::mutex> lock(mtx_);
    if (orig_) orig_->sputc(static_cast<char>(c));
    if (c == '\n') flushLine();
    else           line_ += static_cast<char>(c);
    return c;
}

std::streamsize LogStreamBuf::xsputn(const char* s, std::streamsize n) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (orig_) orig_->sputn(s, n);
    for (std::streamsize i = 0; i < n; ++i) {
        if (s[i] == '\n') flushLine();
        else              line_ += s[i];
    }
    return n;
}

void LogStreamBuf::flushLine() {
    if (ctx_) ctx_->addLog(line_);
    line_.clear();
}
