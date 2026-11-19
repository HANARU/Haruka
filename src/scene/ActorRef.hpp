#pragma once
// 액터 참조(참조 체계 R-4, Docs/Object_References.md).
// 컴포넌트가 다른 액터를 가리킬 때 포인터 대신 이것을 듭니다. 대상의 GUID 를 저장하고, 찾은 포인터는 구조 버전
// (hcore::gStructureVersion: 액터 생성 · 삭제 · GUID 변경 때 오름)이 같을 때만 다시 씁니다. 그래서 대상이 지워지면 nullptr,
// Undo 로 같은 GUID 가 되살아나면 다시 찾습니다(UE TSoftObjectPtr 와 같은 성질).
//
// 파일 값은 `actor:<16진수>`, 비었으면 빈 값입니다. 프리팹 꺼내기 · 붙여넣기 · 복제는 이 토큰을 글 단계에서 새 GUID 로 바꿉니다
// (sceneio/GuidRemap.hpp). 그러니 다른 형식으로 쓰지 마십시오.

#include <string>
#include <string_view>

#include "../core/Guid.hpp"
#include "Actor.hpp"
#include "World.hpp"

class HActorRef {
public:
    static constexpr std::string_view kPrefix = "actor:";

    HActorRef() = default;
    explicit HActorRef(HGuid g) : guid_(g) {}
    explicit HActorRef(const Actor* a) : guid_(a ? a->guid() : HGuid{}) {}

    HGuid guid() const  { return guid_; }
    bool  isSet() const { return guid_.valid(); }
    void  set(const Actor* a) { guid_ = a ? a->guid() : HGuid{}; cache_ = nullptr; world_ = nullptr; }
    void  setGuid(HGuid g)    { guid_ = g; cache_ = nullptr; world_ = nullptr; }
    void  clear()             { setGuid(HGuid{}); }

    // 대상 액터(없거나 지워졌으면 nullptr).
    Actor* resolve(const World& world) const {
        if (!guid_.valid()) return nullptr;
        if (world_ == &world && version_ == hcore::gStructureVersion) return cache_;
        cache_   = world.findByGuid(guid_);
        world_   = &world;
        version_ = hcore::gStructureVersion;
        return cache_;
    }
    // 소유 액터의 월드에서 찾습니다(컴포넌트가 owner 로 부를 때).
    Actor* resolveFrom(const Actor* owner) const {
        return owner && owner->world() ? resolve(*owner->world()) : nullptr;
    }

    std::string toText() const { return guid_.valid() ? std::string(kPrefix) + guid_.toString() : std::string(); }

    // 빈 값(공백만 포함) · "none" 은 없음으로 읽고 true. "actor:<16진수>" 가 아니면 false(out 은 없음).
    static bool fromText(std::string_view s, HActorRef& out) {
        out.clear();
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
        if (s.empty() || s == "none") return true;
        if (s.substr(0, kPrefix.size()) != kPrefix) return false;
        HGuid g;
        if (!HGuid::parse(s.substr(kPrefix.size()), g)) return false;
        out.setGuid(g);
        return true;
    }

    bool operator==(const HActorRef& o) const { return guid_ == o.guid_; }
    bool operator!=(const HActorRef& o) const { return guid_ != o.guid_; }

private:
    HGuid                 guid_;
    mutable Actor*        cache_   = nullptr;
    mutable const World*  world_   = nullptr;
    mutable uint64_t      version_ = 0;
};
