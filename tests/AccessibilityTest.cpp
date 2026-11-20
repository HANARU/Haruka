// 접근성 시험(2026-11-21, 정본 Docs/Accessibility.md 3절 A1 ~ A6).
// 자막 큐(자동 길이 · 같은 id · 최대 줄 수 · 우선순위 · 기다림), 자막 판(크기 · 머리 고정 · 화면 아래 자리 · 켜기 · 줄바꿈), 3D UI 패널의
// 글자 배율 레이아웃, 비네트(속도 → 세기 · 필터 · 모양 · 이동 컴포넌트 알림), 앉은 높이 · 다시 맞추기, 손 바꾸기 매핑, 색 보정 행렬,
// 사용자 설정 왕복을 창 · GPU 없이 검산합니다.

#include "../src/audio/AudioComponent.hpp"
#include "../src/components/SubtitlePanel.hpp"
#include "../src/components/UIPanelComponent.hpp"
#include "../src/components/XrComponents.hpp"
#include "../src/core/Accessibility.hpp"
#include "../src/core/Subtitles.hpp"
#include "../src/core/Utf8.hpp"
#include "../src/core/UserSettings.hpp"
#include "../src/input/HInputSystem.hpp"
#include "../src/scene/Actor.hpp"
#include "../src/scene/World.hpp"
#include "../src/ui/slate/widgets/STextBlock.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failed = 0;
int g_passed = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (cond) ++g_passed;
    else ++g_failed;
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

SubtitleLine line(const std::string& text, int priority = 0, float seconds = 0.f, const std::string& id = {}) {
    SubtitleLine l;
    l.text     = HText::fromString(text);
    l.priority = priority;
    l.duration = seconds;
    l.id       = id;
    return l;
}

// ── A1-1 자막 큐 ────────────────────────────────────────────────────────────────
void testQueue() {
    std::printf("[A1-1] 자막 큐\n");
    check(near(haccess::subtitleDuration("abc"), 1.5f + 3.f / 15.f), "자동 길이: 1.5 + 글자 수 / 15");
    check(near(haccess::subtitleDuration("\xEC\x95\x88\xEB\x85\x95"), 1.5f + 2.f / 15.f), "자동 길이는 바이트가 아니라 글자 수(한글 두 글자)");
    check(near(haccess::subtitleDuration(std::string(400, 'x')), haccess::kSubtitleMaxSeconds), "긴 글은 8 초에서 자름");
    check(near(haccess::subtitleDuration(""), haccess::kSubtitleMinSeconds), "빈 글은 최소 1.5 초");

    SubtitleQueue q;
    check(q.maxLines() == 2, "기본 최대 줄 수 2");
    check(q.push(line("one", 0, 2.f)) && q.push(line("two", 0, 3.f)), "두 줄은 바로 보임");
    check(q.visible().size() == 2 && q.pendingCount() == 0, "보이는 줄 2 · 기다림 0");
    check(q.push(line("three", 0, 3.f)), "같은 우선순위 셋째 줄은 기다림에 들어감");
    check(q.visible().size() == 2 && q.pendingCount() == 1, "가득 차면 같은 우선순위는 밀어내지 않음");
    const uint64_t r0 = q.revision();
    q.update(1.f);
    check(q.visible().size() == 2 && q.revision() == r0, "시간이 남은 줄은 그대로(변경 번호 그대로)");
    q.update(1.01f);
    check(q.visible().size() == 2 && q.visible()[0].line.text.toString() == "two" && q.visible()[1].line.text.toString() == "three",
          "첫 줄이 끝나면 기다리던 줄이 아래에 올라옴(위 → 아래 = 먼저 온 것부터)");
    check(q.revision() != r0, "보이는 줄이 바뀌면 변경 번호가 오름");

    // 우선순위 밀어내기
    SubtitleQueue p;
    p.push(line("low-old", 0, 5.f));
    p.push(line("low-new", 0, 5.f));
    check(p.push(line("urgent", 5, 5.f)) && p.pendingCount() == 0, "더 높은 우선순위는 바로 보임");
    bool hasOld = false, hasUrgent = false;
    for (const auto& s : p.visible()) {
        hasOld    |= s.line.text.toString() == "low-old";
        hasUrgent |= s.line.text.toString() == "urgent";
    }
    check(!hasOld && hasUrgent && p.visible().size() == 2, "우선순위가 가장 낮은 줄 중 오래된 것을 밀어냄");
    check(p.push(line("low-3", 0, 0.5f)) && p.pendingCount() == 1, "낮은 우선순위는 기다림");
    p.update(0.6f);
    check(p.pendingCount() == 0 && p.visible().size() == 2, "자기 길이보다 오래 기다린 줄은 버림");

    // 같은 id 바꾸기
    SubtitleQueue d;
    d.push(line("Hello", 0, 2.f, "npc1"));
    d.update(1.5f);
    const uint64_t r1 = d.revision();
    d.push(line("Hello again", 0, 2.f, "npc1"));
    check(d.visible().size() == 1 && d.visible()[0].line.text.toString() == "Hello again", "같은 id 는 새 줄을 만들지 않고 글을 바꿈");
    check(near(d.visible()[0].remaining, 2.f) && d.revision() != r1, "같은 id 는 시간을 다시 셈 · 변경 번호 오름");
    check(d.stop("npc1") && d.visible().empty(), "stop(id) 로 지움");
    check(!d.stop("npc1"), "없는 id 는 false");

    // 최대 줄 수
    SubtitleQueue m;
    m.setMaxLines(3);
    m.push(line("a", 0, 5.f)); m.push(line("b", 0, 5.f)); m.push(line("c", 0, 5.f));
    check(m.visible().size() == 3, "최대 줄 수 3 이면 셋이 보임");
    m.setMaxLines(1);
    check(m.visible().size() == 1 && m.visible()[0].line.text.toString() == "c", "최대 줄 수를 줄이면 오래된 것부터 뺌");
    m.setMaxLines(99);
    check(m.maxLines() == SubtitleQueue::kMaxLinesLimit, "최대 줄 수 상한");
    m.clear();
    check(m.visible().empty() && m.pendingCount() == 0, "clear");
}

// ── A1-2 소리가 내는 자막 ───────────────────────────────────────────────────────
void testAudioSubtitle() {
    std::printf("[A1-2] 사운드 컴포넌트 자막\n");
    World world;
    SubtitleQueue q;
    Actor* a = world.spawnActor("Speaker");
    auto*  ac = a->addComponent<AudioComponent>();
    ac->emitSubtitle();
    check(q.visible().empty(), "큐 서비스가 없으면 아무 일도 없음");
    world.setService<SubtitleQueue>(&q);
    ac->emitSubtitle();
    check(q.visible().empty(), "subtitle 이 비면 넣지 않음");
    ac->subtitle        = "The door opens.";
    ac->subtitleKey     = "Game:DoorOpens";
    ac->subtitleSpeaker = "Door";
    ac->emitSubtitle();
    ac->emitSubtitle();
    check(q.visible().size() == 1, "같은 컴포넌트가 다시 내면 줄을 바꿈(id = audio:<액터>)");
    const SubtitleLine& l = q.visible()[0].line;
    check(l.text.toString() == "The door opens." && l.text.key() == "DoorOpens" && l.text.ns() == "Game", "subtitleKey 는 번역 키(표가 없으면 원문)");
    check(l.speaker.toString() == "Door" && l.id == "audio:" + std::to_string(a->id), "화자 · id");
    check(ac->readField("subtitleKey", "Game:Other") && ac->subtitleKey == "Game:Other", "씬 파일 칸을 읽음");
    world.setService<SubtitleQueue>(nullptr);
}

// ── 줄바꿈 ─────────────────────────────────────────────────────────────────────
void testWrap() {
    std::printf("[A1-3] 자막 줄바꿈\n");
    auto measure = [](std::string_view s) {   // 글자 하나 = 10
        size_t i = 0, n = 0;
        while (i < s.size()) { utf8::next(s, i); ++n; }
        return float(n) * 10.f;
    };
    auto r = haccess::wrapText("hello world foo", 120.f, 2, measure);
    check(r.size() == 2 && r[0] == "hello world" && r[1] == "foo", "폭 안에서 공백 단위로 나눔");
    r = haccess::wrapText("one two three four five six", 80.f, 2, measure);
    check(r.size() == 2 && r[1].size() >= 3 && r[1].substr(r[1].size() - 3) == "\xE2\x80\xA6" && measure(r[1]) <= 80.f,
          "두 줄을 넘으면 마지막 줄 끝을 … 로 자르고 폭 안");
    r = haccess::wrapText("abcdefghijkl", 50.f, 3, measure);
    check(r.size() == 3 && r[0] == "abcde" && r[1] == "fghij" && r[2] == "kl", "공백 없는 긴 단어는 글자 단위로 자름");
    r = haccess::wrapText("\xEA\xB0\x80\xEB\x82\x98\xEB\x8B\xA4 \xEB\x9D\xBC", 30.f, 2, measure);
    check(r.size() == 2 && r[0] == "\xEA\xB0\x80\xEB\x82\x98\xEB\x8B\xA4", "한글도 글자 수로 잼(바이트를 자르지 않음)");
    r = haccess::wrapText("a\nb", 100.f, 2, measure);
    check(r.size() == 2 && r[0] == "a" && r[1] == "b", "있던 줄바꿈은 지킴");
}

// ── A1-3 자막 판 ───────────────────────────────────────────────────────────────
void testPanel() {
    std::printf("[A1-3] 자막 판 크기 · 자리 · 켜기\n");
    const glm::vec2 s1 = SubtitlePanel::layoutSize(1200.f, 40.f, 2);
    const glm::vec2 s2 = SubtitlePanel::layoutSize(1200.f, 80.f, 2);
    check(near(s1.y, 2.f * (2.f * 48.f + 2.f * 14.f) + 10.f), "판 높이 = 줄 수 × (두 행 + 위아래 여백) + 줄 사이");
    check(near(s2.y, 2.f * s1.y), "글자 크기가 두 배면 판 높이도 두 배(레이아웃 다시 계산)");
    check(SubtitlePanel::layoutSize(1200.f, 40.f, 3).y > s1.y, "최대 줄 수가 늘면 판이 높아짐");

    // 머리 고정(XR)
    const glm::vec3 head(1.f, 1.6f, 2.f);
    const glm::quat yaw = glm::angleAxis(glm::radians(30.f), glm::vec3(0.f, 1.f, 0.f));
    const SubtitlePanel::Placement x = SubtitlePanel::placeXr(head, yaw);
    check(near(glm::length(x.position - head), SubtitlePanel::kXrDistance), "머리에서 1.2 m");
    const glm::vec3 toPanel = glm::normalize(x.position - head);
    const glm::vec3 fwd     = yaw * glm::vec3(0.f, 0.f, -1.f);
    const float     down    = glm::degrees(std::asin(-toPanel.y));
    check(near(down, SubtitlePanel::kXrDownDeg, 0.01f), "시선 아래 15°");
    check(glm::dot(glm::normalize(glm::vec3(toPanel.x, 0.f, toPanel.z)), fwd) > 0.9999f, "머리가 보는 쪽(요를 따름)");
    check(glm::dot(x.rotation * glm::vec3(0.f, 0.f, 1.f), -toPanel) > 0.9999f, "판 앞면이 머리를 향함");
    check(near(x.layoutWidth * x.metersPerPixel, SubtitlePanel::kXrWidth), "폭 0.9 m");

    // 화면 아래(데스크톱): 카메라 공간에서 판의 위 · 아래 끝이 화면 아래쪽 안
    const glm::vec3 cam(0.f, 2.f, 5.f);
    const glm::quat pitch = glm::angleAxis(glm::radians(-20.f), glm::vec3(1.f, 0.f, 0.f));
    const float fov = 60.f, aspect = 16.f / 9.f;
    const SubtitlePanel::Placement dsk = SubtitlePanel::placeDesktop(cam, pitch, fov, aspect, 1.f, 2);
    const glm::vec3 local = glm::inverse(pitch) * (dsk.position - cam);
    const float halfH = std::tan(glm::radians(fov) * 0.5f) * SubtitlePanel::kDeskDistance;
    const float hM    = SubtitlePanel::layoutSize(dsk.layoutWidth, 40.f, 2).y * dsk.metersPerPixel;
    const float bottomNdc = (local.y - hM * 0.5f) / halfH;
    const float topNdc    = (local.y + hM * 0.5f) / halfH;
    check(near(local.z, -SubtitlePanel::kDeskDistance) && near(local.x, 0.f), "카메라 앞 0.5 m 가운데");
    check(near(bottomNdc, -1.f + 2.f * SubtitlePanel::kDeskBottomFrac, 1e-3f), "아래 끝 = 화면 아래 여백 6 %");
    check(topNdc < 0.f, "판 전체가 화면 아래쪽 절반");
    check(near(dsk.layoutWidth * dsk.metersPerPixel, SubtitlePanel::kDeskWidthFrac * 2.f * halfH * aspect, 2e-3f), "폭 = 화면 폭의 80 %");
    check(glm::dot(dsk.rotation * glm::vec3(0.f, 0.f, 1.f), pitch * glm::vec3(0.f, 0.f, 1.f)) > 0.9999f, "판이 카메라를 향함(카메라와 같은 회전)");
    check(near(40.f * dsk.metersPerPixel / (2.f * halfH), SubtitlePanel::kDeskFontFrac), "배율 1 의 글자 높이 = 화면 높이의 4.5 %");


    // 켜기 · 줄 · 배율(글꼴이 없는 시험이라 폭은 STextBlock 과 같은 근사)
    SubtitleQueue q;
    SubtitleLine l = line("Hello world", 0, 3.f);
    l.speaker = HText::fromString("Ann");
    q.push(std::move(l));
    SubtitlePanel panel;
    AccessSettings off;
    panel.update(q, off);
    check(!panel.visible(), "access.subtitles 가 꺼져 있으면 보이지 않음(큐는 돎)");
    AccessSettings on;
    on.subtitles = true;
    panel.update(q, on);
    panel.place(head, yaw, true, 90.f, 1.f);
    check(panel.visible() && panel.rows().size() == 1 && panel.rows()[0] == "Ann: Hello world", "켜면 보이고 화자를 앞에 붙임");
    check(panel.textureSize().x == (uint32_t)SubtitlePanel::kXrLayoutWidth && panel.textureSize().y == (uint32_t)std::lround(s1.y), "텍스처 = 배치 픽셀(머리 고정 1200 폭)");
    check(panel.takeDirty(), "새로 만든 트리는 다시 그림");
    panel.update(q, on);
    panel.place(head, yaw, true, 90.f, 1.f);
    check(!panel.takeDirty(), "줄 · 설정이 그대로면 트리를 다시 만들지 않음");
    AccessSettings big = on;
    big.textScale = 2.f;
    panel.update(q, big);
    panel.place(head, yaw, true, 90.f, 1.f);
    check(panel.takeDirty() && near(panel.fontPx(), 80.f) && panel.textureSize().y == (uint32_t)std::lround(s2.y),
          "글자 배율 2 → 글자 80 px · 판 높이 두 배(다시 만듦)");
    AccessSettings huge = on;
    huge.subtitleSize = 3.f; huge.textScale = 3.f;
    panel.update(q, huge);
    check(near(panel.fontPx(), SubtitlePanel::kBaseFontPx * SubtitlePanel::kMaxFontScale), "크기 × 배율은 4 배에서 자름");
    q.update(3.1f);
    panel.update(q, on);
    check(!panel.visible(), "줄이 끝나면 보이지 않음");
}

// ── A2 글자 배율: 3D UI 패널 ───────────────────────────────────────────────────
void testPanelTextScale() {
    std::printf("[A2-1] 3D UI 패널 글자 배율\n");
    static int   builds = 0;
    static float lastFont = 0.f;
    static std::shared_ptr<STextBlock> lastText;
    UIPanelComponent::registerContent("AccessProbe", [](UIPanelComponent& p, float s) {
        ++builds;
        lastFont = 30.f * s * p.textScale();
        lastText = std::make_shared<STextBlock>();
        lastText->text("Scale me")->fontSize(lastFont);
        return std::static_pointer_cast<HWidget>(lastText);
    });
    World world;
    Actor* a = world.spawnActor("Panel");
    auto*  pc = a->addComponent<UIPanelComponent>();
    pc->content = "AccessProbe";
    check(pc->textScale() == 1.f, "서비스가 없으면 배율 1");
    pc->slate();
    const float f1 = lastFont;
    const float w1 = lastText->computeDesiredSize().x;
    const int   b1 = builds;
    AccessibilityState acc;
    world.setService<AccessibilityState>(&acc);
    pc->slate();
    check(builds == b1, "배율 1 이면 트리를 다시 만들지 않음(지금 화면과 같음)");
    acc.access.textScale = 1.5f;
    pc->slate();
    check(builds == b1 + 1 && near(lastFont, f1 * 1.5f), "배율이 바뀌면 트리를 다시 만들고 글자 × 1.5");
    check(near(lastText->computeDesiredSize().x, w1 * 1.5f, 0.01f), "다시 계산한 배치 폭도 × 1.5");
    world.setService<AccessibilityState>(nullptr);
}

// ── A3 비네트 ──────────────────────────────────────────────────────────────────
void testVignette() {
    std::printf("[A3-1] 비네트 세기 · 모양\n");
    check(haccess::vignetteTarget(0.f, 0.f, 1.f) == 0.f, "멈춰 있으면 0");
    check(haccess::vignetteTarget(3.f, 0.f, 0.f) == 0.f, "설정 0 이면 0(끔)");
    check(near(haccess::vignetteTarget(2.f, 0.f, 0.5f), 0.5f), "2 m/s 이상은 설정 세기 그대로");
    check(near(haccess::vignetteTarget(1.05f, 0.f, 1.f), 0.5f), "선속도 0.1 → 2 m/s 사이는 선형(1.05 m/s = 0.5)");
    check(near(haccess::vignetteTarget(0.f, 60.f, 0.8f), 0.8f), "회전 60 °/s 이상은 설정 세기");
    check(near(haccess::vignetteTarget(0.5f, -32.5f, 1.f), 0.5f), "선 · 회전 중 큰 값(음수 회전도)");
    const float up   = haccess::vignetteStep(0.f, 1.f, 0.05f);
    const float down = 1.f - haccess::vignetteStep(1.f, 0.f, 0.05f);
    check(up > down && up > 0.4f, "들어갈 때가 나올 때보다 빠름");
    float v = 1.f;
    for (int i = 0; i < 200; ++i) v = haccess::vignetteStep(v, 0.f, 0.016f);
    check(v == 0.f, "멈추면 정확히 0 으로 돌아감(셰이더가 단계를 건너뜀)");
    check(haccess::vignetteMask(1.41f, 0.f) == 1.f, "세기 0 이면 모서리도 그대로");
    check(haccess::vignetteMask(0.f, 1.f) == 1.f, "가운데는 세기 1 에서도 밝음");
    check(haccess::vignetteMask(1.41f, 1.f) < 0.01f, "세기 1 이면 모서리는 검음");
    check(haccess::vignetteMask(0.9f, 0.5f) < haccess::vignetteMask(0.6f, 0.5f), "가장자리로 갈수록 어두움");

    std::printf("[A3-1] 이동 컴포넌트가 속도를 알림\n");
    World world;
    AccessibilityState acc;
    acc.vignetteStrength = 1.f;
    world.setService<AccessibilityState>(&acc);
    Actor* rig = world.spawnActor("Rig");
    auto*  loco = rig->addComponent<RigLocomotionComponent>();
    HInputSystem& in = HInputSystem::get();
    in.clearBindings();
    std::string err;
    in.applyBinding("input.axis.MoveY", "keys:W/S", err);
    in.update(0.1f, [](int k) { return k == 87; });   // W
    loco->tick(0.1f);
    check(near(glm::length(loco->velocity()), loco->moveSpeed), "W 로 2 m/s 이동");
    acc.tickVignette(0.1f);
    check(acc.vignette() > 0.5f, "알린 속도로 비네트가 켜짐");
    in.update(0.1f, [](int) { return false; });
    loco->tick(0.1f);
    acc.tickVignette(0.1f);
    const float after = acc.vignette();
    for (int i = 0; i < 100; ++i) { loco->tick(0.016f); acc.tickVignette(0.016f); }
    check(after > 0.f && acc.vignette() == 0.f, "멈추면 줄어 0");
    acc.vignetteStrength = 0.f;
    acc.reportMotion(5.f, 100.f);
    acc.tickVignette(0.1f);
    check(acc.vignette() == 0.f, "설정 0 이면 움직여도 0");
    in.clearBindings();
    world.setService<AccessibilityState>(nullptr);
}

// ── A4 앉은 자세 ───────────────────────────────────────────────────────────────
void testSeated() {
    std::printf("[A4-1] 앉은 자세 높이\n");
    check(near(haccess::seatedLift(1.7f, 1.1f), 0.6f), "선 1.7 − 앉은 1.1 = 0.6 m");
    check(haccess::seatedLift(1.5f, 1.6f) == 0.f, "앉은 머리가 더 높으면 0");
    check(near(haccess::seatedLift(2.5f, 0.5f), haccess::kMaxSeatedLift), "1.2 m 에서 자름");
    check(haccess::seatedLift(1.7f, 0.f) == 0.f, "아직 재지 않았으면 0");
    AccessibilityState a;
    check(a.seatedLiftFor(false, true, 1.7f, true, 1.1f) == 0.f, "꺼져 있으면 0");
    check(a.seatedLiftFor(true, false, 1.7f, true, 1.1f) == 0.f, "머리 기준 공간(눈높이 고정)이면 더하지 않음");
    check(a.seatedLiftFor(true, true, 1.7f, false, 1.1f) == 0.f && a.seatedHead() == 0.f, "머리 자세가 없으면 기다림");
    check(near(a.seatedLiftFor(true, true, 1.7f, true, 1.1f), 0.6f), "처음 받은 머리 높이로 잼");
    check(near(a.seatedLiftFor(true, true, 1.7f, true, 1.3f), 0.6f), "그 뒤 머리가 움직여도 잰 값 그대로");
    a.requestSeatedRecalibrate();
    check(near(a.seatedLiftFor(true, true, 1.7f, true, 1.3f), 0.4f), "다시 맞추기 요청 뒤 새로 잼");
    check(near(a.seatedLiftFor(true, true, 1.8f, true, 1.0f), 0.5f), "선 눈높이 설정이 바뀌면 바로 따름");
    a.seatedLiftFor(false, true, 1.7f, true, 1.0f);
    check(a.seatedHead() == 0.f, "끄면 잰 값을 지움(다시 켜면 새로 잼)");
}

// ── A5 한 손 조작 ──────────────────────────────────────────────────────────────
void testHandSwap() {
    std::printf("[A5-1] 손 바꾸기\n");
    HInputSystem& in = HInputSystem::get();
    in.clearBindings();
    std::string err;
    for (const auto& [k, v] : HInputSystem::defaultBindings()) in.applyBinding(k, v, err);
    HXrInputState xr;
    xr.focused = true;
    xr.hands[0].active = xr.hands[1].active = true;
    xr.hands[0].thumbstick = glm::vec2(0.8f, 0.f);    // 왼쪽 스틱 오른쪽
    xr.hands[1].thumbstick = glm::vec2(0.f, 0.9f);    // 오른쪽 스틱 앞
    in.setXrInput(xr);
    in.update(0.016f, [](int) { return false; });
    check(near(in.axis("MoveX"), 0.8f) && near(in.axis("TurnX"), 0.f) && near(in.axis("Teleport"), 0.9f),
          "기본: 왼쪽 = 이동, 오른쪽 = 회전 · 텔레포트");
    in.setHandSwap(true);
    check(near(in.axis("MoveX"), 0.f) && near(in.axis("MoveY"), 0.9f), "바꾸면 이동은 오른쪽 스틱");
    check(near(in.axis("TurnX"), 0.8f) && near(in.axis("Teleport"), 0.f), "바꾸면 회전 · 텔레포트는 왼쪽 스틱");
    check(in.bindingLine("input.axis.MoveX").find("xr:left.stick.x") != std::string::npos, "바인딩 글은 그대로(재지정과 겹치지 않음)");
    HGamepadState pad;
    pad.connected = true;
    pad.axes[size_t(HPadAxis::RightX)] = 0.7f;
    in.setGamepad(pad);
    in.setXrInput(HXrInputState{});
    check(in.axis("MoveX") > 0.6f, "패드도 오른쪽 스틱이 이동");
    check(HInputSystem::swapHands(HXrAxis::LeftTrigger) == HXrAxis::LeftTrigger
          && HInputSystem::swapHands(HPadAxis::RightTrigger) == HPadAxis::RightTrigger, "트리거는 바꾸지 않음");
    in.setHandSwap(false);
    check(in.axis("MoveX") == 0.f, "되돌리면 패드 오른쪽 스틱은 이동이 아님");
    in.setGamepad(HGamepadState{});
    in.clearBindings();
}

// ── A7-1 색 보정 ───────────────────────────────────────────────────────────────
void testColor() {
    std::printf("[A7-1] 색 보정 행렬\n");
    const glm::vec3 gray(0.5f);
    bool whiteKept = true, grayKept = true, changes = true;
    for (int m = 1; m <= 3; ++m) {
        const glm::vec3 w = haccess::colorSimulation(m) * glm::vec3(1.f);
        whiteKept = whiteKept && near(w.x, 1.f) && near(w.y, 1.f) && near(w.z, 1.f);
        const glm::vec3 g = haccess::colorCorrection(m) * gray;
        grayKept = grayKept && near(g.x, 0.5f) && near(g.y, 0.5f) && near(g.z, 0.5f);
        const glm::vec3 r = haccess::colorCorrection(m) * glm::vec3(1.f, 0.f, 0.f);
        changes = changes && glm::length(r - glm::vec3(1.f, 0.f, 0.f)) > 0.05f;
    }
    check(whiteKept, "시뮬레이션 행렬은 흰색을 지킴(행 합 1)");
    check(grayKept, "보정 행렬은 무채색을 지킴");
    check(changes, "보정 행렬은 순색을 옮김");
    check(haccess::colorCorrection(0) == glm::mat3(1.f), "none 은 단위 행렬");
    // 적록: 빨강 · 초록의 시뮬레이션 차이보다 보정 뒤 시뮬레이션 차이가 큼(구별이 쉬워짐)
    const glm::mat3 S = haccess::colorSimulation(2), C = haccess::colorCorrection(2);
    const glm::vec3 red(0.8f, 0.3f, 0.2f), green(0.5f, 0.5f, 0.2f);
    check(glm::length(S * (C * red) - S * (C * green)) > glm::length(S * red - S * green), "deuteranopia: 보정 뒤 두 색 차이가 커짐");
}

// ── A6 설정 왕복 ───────────────────────────────────────────────────────────────
void testSettings() {
    std::printf("[A6-1] 설정 왕복\n");
    UserSettings u;
    check(AccessSettings::from(u) == AccessSettings{} && ComfortSettings::from(u) == ComfortSettings{}, "파일이 없으면 기본값(지금과 같음)");
    std::string why;
    check(u.setBool("access.subtitles", true) && u.setFloat("access.subtitleSize", 1.5f) && u.setFloat("access.subtitleBackground", 0.25f)
          && u.setFloat("access.textScale", 1.25f) && u.set("access.hand", "left") && u.set("access.colorFilter", "tritanopia")
          && u.setFloat("comfort.vignette", 0.7f) && u.setBool("comfort.seated", true) && u.setFloat("comfort.standingHeight", 1.8f),
          "새 이름 9 개를 모두 받음");
    check(!u.set("access.hand", "both", &why), "고를 수 없는 값은 거절");
    why.clear();
    check(u.setFloat("access.textScale", 10.f, &why) && !why.empty() && u.getFloat("access.textScale", 0.f) == 3.f, "범위 밖은 잘라 알림");
    u.setFloat("access.textScale", 1.25f);
    const std::string text = u.serialize();
    UserSettings r;
    std::vector<std::string> msgs;
    r.parse(text, msgs);
    const AccessSettings  a = AccessSettings::from(r);
    const ComfortSettings c = ComfortSettings::from(r);
    check(msgs.empty(), "다시 읽을 때 알림 없음");
    check(a.subtitles && a.subtitleSize == 1.5f && a.subtitleBackground == 0.25f && a.textScale == 1.25f && a.leftHanded && a.colorFilter == 3,
          "access.* 왕복(다시 시작과 같음)");
    check(c.vignette == 0.7f && c.seated && c.standingHeight == 1.8f, "comfort.* 왕복");
    check(c.standing(1.6f) == 1.8f && ComfortSettings{}.standing(1.6f) == 1.6f, "선 눈높이 0 = 씬 값");
    UserSettings q;
    q.setFloat("comfort.standingHeight", 0.3f);
    check(ComfortSettings::from(q).standingHeight == 1.f, "선 눈높이 0 < x < 1 은 1 m 로");
    bool allLive = true;
    for (const char* k : { "access.subtitles", "access.subtitleSize", "access.subtitleBackground", "access.textScale", "access.hand",
                           "access.colorFilter", "comfort.vignette", "comfort.seated", "comfort.standingHeight" }) {
        const UserSettings::KeyInfo* info = UserSettings::find(k);
        allLive = allLive && info && !info->prefix && info->apply == UserSettings::Apply::Live;
    }
    check(allLive, "새 이름은 모두 알려진 바로 적용(Live) 이름");
}

} // namespace

int main() {
    testQueue();
    testAudioSubtitle();
    testWrap();
    testPanel();
    testPanelTextScale();
    testVignette();
    testSeated();
    testHandSwap();
    testColor();
    testSettings();
    std::printf("\n%s (%d passed, %d failed)\n", g_failed ? "FAILED" : "ALL PASSED", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
