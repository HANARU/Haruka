#include "PakFormat.hpp"

// xxHash 는 헤더 하나로 씁니다(BSD-2). 이 번역 단위 안에만 넣어 다른 곳이 xxHash 를 모르게 합니다.
#define XXH_INLINE_ALL
#include "xxhash.h"

#include <new>

namespace hpak {

bool normalizePath(std::string_view in, std::string& out, std::string* why) {
    auto fail = [&](const char* reason) {
        if (why) *why = reason;
        return false;
    };
    std::string s(in);
    for (char& c : s)
        if (c == '\\') c = '/';
    while (s.size() >= 2 && s[0] == '.' && s[1] == '/') s.erase(0, 2);
    if (s.empty())                 return fail("empty path");
    if (s.size() > kMaxPathBytes)  return fail("path longer than 1024 bytes");
    if (s.front() == '/')          return fail("absolute path");
    if (s.back() == '/')           return fail("path ends with '/'");
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7F) return fail("control character in path");
        switch (c) {
        case ':': case '<': case '>': case '"': case '|': case '?': case '*':
            return fail("forbidden character in path (: < > \" | ? *)");
        default: break;
        }
    }
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find('/', start);
        if (end == std::string::npos) end = s.size();
        const std::string_view part(s.data() + start, end - start);
        if (part.empty())               return fail("empty path component");
        if (part == "." || part == "..") return fail("'.' or '..' in path");
        start = end + 1;
    }
    out = std::move(s);
    return true;
}

std::string foldKey(std::string_view normalized) {
    std::string k(normalized);
    for (char& c : k)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return k;
}

uint64_t hash64(const void* data, size_t size) { return XXH3_64bits(data, size); }

// 상태를 잡지 못하면 빈 포인터로 계속하지 않고 bad_alloc 을 던집니다(읽기 함수의 예외 경계가 받음).
Hasher::Hasher() : state_(XXH3_createState()) {
    if (!state_) throw std::bad_alloc();
    XXH3_64bits_reset(static_cast<XXH3_state_t*>(state_));
}
Hasher::~Hasher() { XXH3_freeState(static_cast<XXH3_state_t*>(state_)); }
void Hasher::update(const void* data, size_t size) { XXH3_64bits_update(static_cast<XXH3_state_t*>(state_), data, size); }
uint64_t Hasher::digest() const { return XXH3_64bits_digest(static_cast<XXH3_state_t*>(state_)); }

} // namespace hpak
