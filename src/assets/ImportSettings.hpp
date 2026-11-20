#pragma once
// 가져오기 설정(2026-11-20, Docs/Asset_Hot_Reload.md D8). 에셋별 .meta 를 만들지 않으므로(참조 체계 D6) 프로젝트의
// /Config/import.ini 에 경로 패턴 표로 둡니다. 맞는 절을 파일 순서대로 겹쳐 적용하므로 뒤 절이 이깁니다.
//
//   [/Game/models/props/**]
//   scale   = 0.01      가져올 때 정점에 곱함(정적 · 모프 메시)
//   autoLod = false     자동 LOD 를 만들지 않음
//   srgb    = false     albedo · emissive 이미지가 선형으로 저장됨
//
// 결과 설정의 hash() 가 .gasset 캐시 키에 들어갑니다(설정을 바꾸면 그 모델만 다시 가져옴).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct ImportSettings {
    float scale   = 1.f;
    bool  autoLod = true;
    bool  srgb    = true;

    // 기본값이면 0 이 아닌 고정 값입니다. 값이 하나라도 다르면 다른 값입니다(FNV-1a, 실행 · 기기와 무관).
    uint64_t hash() const;
    bool operator==(const ImportSettings& o) const {
        return scale == o.scale && autoLod == o.autoLod && srgb == o.srgb;
    }
};

class ImportSettingsTable {
public:
    static constexpr const char* kPath = "/Config/import.ini";

    struct Rule {
        std::string          pattern;   // 정식 논리 경로 패턴(대소문자 무시)
        std::optional<float> scale;
        std::optional<bool>  autoLod;
        std::optional<bool>  srgb;
        int                  line = 0;  // 절 머리 줄(로그용)
    };

    // 글을 읽습니다. 잘못된 줄은 건너뛰고 "줄 N: 이유" 를 messages 에 더합니다. '#' · ';' 로 시작하는 줄은 주석, 값 뒤의 '#' 도 주석입니다.
    static ImportSettingsTable parse(std::string_view text, std::vector<std::string>& messages);

    // 정식 논리 경로(또는 그 밖의 경로)에 맞는 설정입니다. 맞는 절이 없으면 기본값입니다.
    ImportSettings resolve(std::string_view path) const;

    const std::vector<Rule>& rules() const { return rules_; }
    bool empty() const { return rules_.empty(); }

private:
    std::vector<Rule> rules_;
};

namespace himport {
// 패턴 일치(ASCII 대소문자 무시): '*' 는 '/' 를 넘지 않는 0 글자 이상, '**' 는 '/' 를 넘는 0 글자 이상, '?' 는 '/' 가 아닌 한 글자.
bool globMatch(std::string_view pattern, std::string_view path);
}
