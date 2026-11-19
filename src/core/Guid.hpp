#pragma once
// 액터 고유 ID(참조 체계 R-1, Docs/Object_References.md).
// 64비트 무작위 값이며 0 은 "없음"입니다. 파일에는 16자리 소문자 16진수로 씁니다. Unity 의 fileID 와 같은 크기이고,
// 액터 10만 개에서 겹칠 확률은 약 3e-10 입니다. 겹치면 읽는 쪽(SceneSerializer)이 찾아 새 값을 줍니다.
// 실행 중 번호(Actor::id)와 다릅니다: id 는 열 때마다 새로 매기고, guid 는 파일에 남아 참조가 가리킵니다.

#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <string_view>

struct HGuid {
    uint64_t value = 0;

    bool valid() const { return value != 0; }
    bool operator==(const HGuid& o) const { return value == o.value; }
    bool operator!=(const HGuid& o) const { return value != o.value; }
    bool operator<(const HGuid& o) const { return value < o.value; }

    // 0 이 아닌 새 값. 프로세스마다 OS 난수로 씨앗을 준 생성기 하나를 씁니다(메인 스레드 전용, Threading.md).
    static HGuid make() {
        static std::mt19937_64 gen = [] {
            std::random_device rd;
            std::seed_seq seq{ rd(), rd(), rd(), rd(), rd(), rd() };
            return std::mt19937_64(seq);
        }();
        uint64_t v = 0;
        while (v == 0) v = gen();
        return HGuid{ v };
    }

    // 16자리 소문자 16진수(앞의 0 포함).
    std::string toString() const {
        static const char* kHex = "0123456789abcdef";
        std::string s(16, '0');
        for (int i = 15; i >= 0; --i) s[(size_t)(15 - i)] = kHex[(value >> (i * 4)) & 0xF];
        return s;
    }

    // 정확히 16자리 16진수(대소문자 무시)만 받습니다. 0 도 거절합니다.
    static bool parse(std::string_view s, HGuid& out) {
        if (s.size() != 16) return false;
        uint64_t v = 0;
        for (char c : s) {
            int d;
            if (c >= '0' && c <= '9')      d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return false;
            v = (v << 4) | (uint64_t)d;
        }
        if (v == 0) return false;
        out.value = v;
        return true;
    }
};

template <>
struct std::hash<HGuid> {
    size_t operator()(const HGuid& g) const noexcept { return std::hash<uint64_t>{}(g.value); }
};
