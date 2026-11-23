#pragma once
// 실행 중 변수(CVar)와 콘솔 명령(2026-11-20, Docs/Gameplay_Tools.md D6 ~ D8).
//
// 이름 · 형 · 범위 · 도움말이 있는 개발 · 디버그 값입니다(UE IConsoleManager 와 같은 자리). ini(<쓰기 루트>/Config/cvars.ini) · 명령줄
// (--cvar=이름=값) · 콘솔(`이름 값`) · 게임 코드(set)가 같은 이름으로 바꿉니다. 저장하지 않습니다(플레이어 값은 UserSettings).
//
// 엔진 단위입니다: Engine 이 하나 소유하고 EngineContext::cvars 와 월드 서비스(world.service<CVarRegistry>())로 엽니다. 전역 싱글턴이
// 아니라서(S3 규칙) 등록은 Engine 과 게임 모듈이 합니다. 등록 전에 들어온 값(명령줄 · ini)은 보관했다가 등록할 때 적용합니다.
// 값은 set 하면 바로 바뀌고, 변경 콜백은 dispatchChanges(엔진의 프레임 처음)에서 바뀐 변수마다 한 번 부릅니다. 메인 스레드 전용입니다.
//
//   CVarRegistry::CVar* godMode = ctx.cvars.addBool("game.godMode", false, "Player takes no damage");
//   if (godMode->asBool()) ...
//   ctx.cvars.addCommand("give", "give <item>: adds an item", [](const auto& args) { ...; return std::string("ok"); });

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "HLog.hpp"

// 콘솔 명령과 결과를 남기는 분류(에디터 Output Log 의 Console).
inline constinit HLogCategory LogConsole{ "Console", ELogVerbosity::Log };

class CVarRegistry {
public:
    enum class Kind   { Bool, Int, Float, String };
    enum class Source { Default, Ini, CommandLine, Console, Code };

    struct CVar {
        std::string name;    // 등록한 그대로(찾기는 대소문자 무시)
        std::string help;
        Kind        kind = Kind::Bool;
        float       lo = 0.f, hi = 0.f;   // Int · Float
        std::string defaultText;
        Source      source = Source::Default;
        std::function<void(const CVar&)> onChanged;

        bool               asBool()   const { return b_; }
        int                asInt()    const { return i_; }
        float              asFloat()  const { return f_; }
        const std::string& asString() const { return s_; }
        std::string        text()     const;   // 정규 글(true/false, floatToText …)

    private:
        friend class CVarRegistry;
        bool        b_ = false;
        int         i_ = 0;
        float       f_ = 0.f;
        std::string s_;
        bool        dirty_ = false;   // dispatchChanges 를 기다림
    };

    // 등록. 이름이 틀렸거나 이미 있으면(변수 · 명령) nullptr. 포인터는 레지스트리가 살아 있는 동안 그대로입니다.
    CVar* addBool  (std::string_view name, bool def, std::string_view help, std::function<void(const CVar&)> onChanged = {});
    CVar* addInt   (std::string_view name, int def, int lo, int hi, std::string_view help, std::function<void(const CVar&)> onChanged = {});
    CVar* addFloat (std::string_view name, float def, float lo, float hi, std::string_view help, std::function<void(const CVar&)> onChanged = {});
    CVar* addString(std::string_view name, std::string_view def, std::string_view help, std::function<void(const CVar&)> onChanged = {});

    CVar*       find(std::string_view name);
    const CVar* find(std::string_view name) const;

    // 값을 바꿉니다. 범위 밖은 잘라 true 이고 why 에 알림. 형이 틀리면 false. 아직 등록되지 않은 이름은 ini · 명령줄이면 보관하고 true
    // (why 에 알림), 콘솔 · 코드면 false.
    bool set(std::string_view name, std::string_view text, Source src, std::string* why = nullptr);
    bool reset(std::string_view name);   // 기본값으로(출처 Default)

    // 바뀐 변수마다 onChanged 를 한 번 부르고 바뀐 수를 돌려줍니다.
    int dispatchChanges();
    // 보관했지만 아직 등록되지 않은 이름(시작이 끝난 뒤 알림용).
    std::vector<std::string> pendingNames() const;
    // ini 글(이름=값 줄, # 주석). 알릴 것은 messages 에.
    void loadIni(const std::string& text, std::vector<std::string>& messages);

    // 콘솔 명령. args 는 명령 이름 뒤의 인자(따옴표로 묶은 것은 하나). 돌려준 글을 콘솔에 보입니다.
    using CommandFn = std::function<std::string(const std::vector<std::string>& args)>;
    bool addCommand(std::string_view name, std::string_view help, CommandFn fn);
    // 한 줄을 실행하고 결과 글(여러 줄일 수 있음)을 돌려줍니다. 기본 명령: help [이름], list [앞부분], reset <이름>, <변수> [값].
    std::string execute(std::string_view line);
    // execute 하고 "> 명령" 과 결과를 LogConsole 에 남깁니다(에디터 콘솔 줄 · --console-at).
    std::string executeLogged(std::string_view line);

    enum class CompletionKind { Variable, Command, Builtin };
    struct Completion {
        std::string    name;
        CompletionKind kind = CompletionKind::Variable;
        std::string    valueText;   // 변수만
        std::string    help;
    };

    // 첫 낱말 완성 후보(앞부분 일치 우선 · 포함 순, 이름순, 대소문자 무시). 입력에 공백이 있으면 인자 단계로 한 줄 도움말 1개만 돌려줍니다.
    std::vector<Completion> complete(std::string_view input, size_t max = 10) const;

    std::vector<const CVar*> all() const;   // 이름순
    static std::vector<std::string> splitArgs(std::string_view line);
    static bool        validName(std::string_view name);
    static const char* sourceName(Source s);

private:
    CVar* add(std::string_view name, Kind kind, float lo, float hi, std::string_view def, std::string_view help,
              std::function<void(const CVar&)> onChanged);
    bool  assign(CVar& v, std::string_view text, Source src, std::string* why);
    std::string describe(const CVar& v) const;

    struct Command { std::string name, help; CommandFn fn; };
    struct Pending { std::string text; Source source; };
    std::map<std::string, std::unique_ptr<CVar>> vars_;      // 열쇠 = 소문자 이름
    std::map<std::string, Command>               commands_;
    std::map<std::string, Pending>               pending_;
};
