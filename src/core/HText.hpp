#pragma once
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// ── HText: 화면에 보이는 글 (2026-11-19, 글자 체계 T3, Docs/Text_System.md) ─────────────────────────
// UE 의 FText 에 해당합니다. 사용자 데이터(액터 이름 · 경로 · 로그)는 std::string 그대로 두고, 엔진 · 에디터 · 게임이 사람에게
// 보이는 글만 HText 로 둡니다. 종류는 넷입니다.
//   - 원문 그대로(fromString): 번역하지 않음.
//   - 번역(HTEXT("ns", "key", "원문")): 지금 문화권 표에 같은 원문의 번역이 있으면 번역, 없으면 원문.
//   - 서식(format): 틀(HText) + 인자. 틀 안의 {0} · {이름} 자리를 인자로 채움. 틀도 번역되므로 어순이 언어마다 달라도 됨.
//   - 숫자(asNumber): 문화권별 자리 묶음 기호 · 소수점.
// 값은 공유 불변(shared_ptr)이라 복사가 가볍고, toString() 은 문화권 판 번호(revision)가 바뀌기 전까지 결과를 캐시합니다.
// 스레드: 문화권 표와 캐시는 게임 · UI 스레드(주 스레드)에서만 씁니다.
class HText;

// 서식 인자 하나: HText · 정수 · 실수 · 문자열(원문 그대로).
struct HTextArg {
    enum class Kind : uint8_t { Text, Int, Double, String } kind = Kind::String;
    std::shared_ptr<const HText> text;
    int64_t     i = 0;
    double      d = 0.0;
    std::string s;

    HTextArg(const HText& t);
    HTextArg(int v)                : kind(Kind::Int), i(v) {}
    HTextArg(int64_t v)            : kind(Kind::Int), i(v) {}
    HTextArg(uint32_t v)           : kind(Kind::Int), i((int64_t)v) {}
    HTextArg(size_t v)             : kind(Kind::Int), i((int64_t)v) {}
    HTextArg(double v)             : kind(Kind::Double), d(v) {}
    HTextArg(float v)              : kind(Kind::Double), d(v) {}
    HTextArg(const char* v)        : kind(Kind::String), s(v ? v : "") {}
    HTextArg(std::string v)        : kind(Kind::String), s(std::move(v)) {}
    std::string toString() const;
};

class HText {
public:
    HText() = default;

    static HText fromString(std::string s);
    static HText localized(std::string_view ns, std::string_view key, std::string_view source);
    static HText asNumber(double v, int maxFractionDigits = 2);
    static HText asInteger(int64_t v);
    // 틀의 {0} {1} ... 을 차례 인자로.
    static HText format(const HText& pattern, std::initializer_list<HTextArg> args);
    // 틀의 {이름} 을 이름 인자로.
    static HText formatNamed(const HText& pattern, std::vector<std::pair<std::string, HTextArg>> args);

    // 지금 문화권으로 푼 글(UTF-8).
    const std::string& toString() const;
    bool isEmpty() const { return toString().empty(); }
    bool isLocalized() const;
    // 번역 키(없으면 빈 글).
    const std::string& ns() const;
    const std::string& key() const;
    // 코드에 적힌 원문(번역 · 서식 전).
    const std::string& source() const;
    // 같은 값(같은 공유 데이터)인지.
    bool identicalTo(const HText& o) const { return d_ == o.d_; }

    struct Data;

private:
    std::shared_ptr<const Data> d_;
    explicit HText(std::shared_ptr<const Data> d) : d_(std::move(d)) {}
};

// 코드의 번역 글. 모으기 도구(HarukaPak gather-text)가 이 매크로를 찾아 .pot 를 만듭니다. 세 인자는 모두 문자열 리터럴이어야 합니다.
#define HTEXT(ns, key, source) (::HText::localized((ns), (key), (source)))

// ── 문화권 · 번역 표 ─────────────────────────────────────────────────────────────────────────────
namespace htext {

// 지금 문화권("en", "ko" ...). 바꾸면 판 번호가 오르고, 이미 읽은 표에서 그 문화권 번역을 씁니다.
const std::string& culture();
void               setCulture(std::string culture);
uint32_t           revision();

// 표 층: 엔진(/Engine/Localization) 아래, 프로젝트(Content/Localization)가 위. 같은 키면 위층이 이김.
enum class Layer : uint8_t { Engine = 0, Game = 1 };

// .po 글을 culture 표의 layer 층에 읽습니다(msgctxt "ns,key", msgid 원문, msgstr 번역). 고장 난 항목은 건너뛰고 errors 에 적음.
// 읽은 항목 수.
size_t loadPo(std::string_view poText, const std::string& culture, Layer layer, std::vector<std::string>* errors = nullptr);
// 가상 파일 시스템에서 culture 의 표를 읽습니다: /Engine/Localization/<c>/Engine.po, /Game/Localization/<c>/Game.po(없으면 건너뜀).
// 읽은 항목 수.
size_t loadCultureFiles(const std::string& culture, std::vector<std::string>* errors = nullptr);
// 모든 표를 비웁니다(시험).
void   clearTables();
// 문화권을 쓰기 시작합니다: 그 문화권 표를 아직 읽지 않았으면 가상 파일 시스템에서 읽고(loadCultureFiles) setCulture.
// 읽은 항목 수(이미 읽었으면 0).
size_t useCulture(const std::string& culture, std::vector<std::string>* errors = nullptr);
// 고를 수 있는 문화권(에디터 메뉴): 코드와 그 언어로 쓴 이름.
struct CultureInfo { const char* code; const char* nativeName; };
const std::vector<CultureInfo>& knownCultures();

// 번역을 찾습니다. 표에 있고 원문이 같으면 번역, 아니면 nullptr(원문이 바뀐 낡은 번역은 쓰지 않음, UE 와 같음).
const std::string* lookup(std::string_view ns, std::string_view key, std::string_view source);

// 문화권별 숫자: 자리 묶음 기호와 소수점.
struct NumberFormat { std::string group = ","; std::string decimal = "."; };
NumberFormat numberFormat(const std::string& culture);
std::string  formatNumber(double v, int maxFractionDigits, const std::string& culture);
std::string  formatInteger(int64_t v, const std::string& culture);

// .po 문자열 이스케이프(\" \\ \n \t) 풀기 · 넣기(모으기 도구와 시험이 씀).
std::string poUnescape(std::string_view s);
std::string poEscape(std::string_view s);

} // namespace htext
