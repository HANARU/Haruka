// 에디터의 씬 파일 관리와 모달 대화상자(2026-10-15).
//
// 예전에는 저장 · 불러오기가 실행 폴더의 고정 파일 scene.hscene 하나였습니다. 그래서 저장한 씬이 프로젝트에
// 남지 않았고, 지금 열린 씬이 무엇인지 · 저장하지 않은 변경이 있는지도 몰랐습니다. 이제 씬은 프로젝트 폴더의
// assets/scenes 아래 논리 경로로 열고 저장하며(hproj), SceneDocument 가 열린 파일과 변경 여부를 기억합니다.
// 파일 고르기와 이름 입력은 에디터 안의 대화상자로 합니다(OS 대화상자는 플랫폼마다 다르고 assets 밖의
// 경로를 고를 수 있어 논리 경로 규칙을 지키기 어렵습니다).
#include "../core/Utf8.hpp"
#include "EditorApp.hpp"
#include "../scene/World.hpp"
#include "../scene/Actor.hpp"
#include "../core/FileUtil.hpp"
#include "../core/AppPaths.hpp"
#include "../core/ProjectPaths.hpp"
#include "../core/ProjectSettings.hpp"
#include "../core/AssetRefs.hpp"
#include "../vfs/LogicalPath.hpp"
#include "../core/TextUtf.hpp"
#include "../audio/AudioSystem.hpp"
#include "../platform/IWindow.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

static constinit HLogCategory LogEditorScene{ "Editor", ELogVerbosity::Log };

static constexpr const char* kScenesDir = "/Game/scenes";
static constexpr const char* kStatePath = "Config/editor_state.txt";

// ── 에디터 상태(기계별) ──────────────────────────────────────────────────────────
// 마지막으로 연 씬처럼 프로젝트가 아니라 이 PC 의 에디터에 속한 값입니다. 레이아웃 · 로그 필터와 같이 실행 폴더의
// Config/ 에 둡니다(UE 의 EditorPerProjectUserSettings 와 같은 성격, 버전 관리하지 않음). 실행 폴더는 작업 폴더가 아니라
// 실행 파일 기준입니다(2026-10-19, core/AppPaths.hpp).
void EditorApp::loadEditorState() {
    std::string text;
    if (!hfile::readAll(happ::inWriteRoot(kStatePath), text)) return;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = project_settings::trim(line.substr(0, eq));
        const std::string val = project_settings::trim(line.substr(eq + 1));
        if (key == "lastScene") lastScene_ = val;
        else if (key == "culture") culture_ = val;
        else if (key == "audioOutput") audioOutput_ = val;
        else if (key == "audioHeadsetInVr") headsetAudioInVr_ = (val != "0");
        else if (key == "audioHrtf" && !AudioSystem::hrtfModeFromText(val, hrtfMode_))
            HLOG(LogEditorScene, Warning) << "Editor state: unknown audioHrtf '" << val << "' (auto, on, off); using auto";
    }
}

void EditorApp::saveEditorState() const {
    std::string text = "# Haruka editor state for this machine (not part of the project).\n";
    if (!lastScene_.empty()) text += "lastScene=" + lastScene_ + "\n";
    if (!culture_.empty() && culture_ != "en") text += "culture=" + culture_ + "\n";   // 에디터 언어(글자 체계 T3)
    // 소리 출력 장치(비면 시스템 기본)와 XR Play 동안 헤드셋으로 바꿀지(2026-10-16).
    if (!audioOutput_.empty()) text += "audioOutput=" + audioOutput_ + "\n";
    text += std::string("audioHeadsetInVr=") + (headsetAudioInVr_ ? "1" : "0") + "\n";
    text += std::string("audioHrtf=") + AudioSystem::hrtfModeName(hrtfMode_) + "\n";   // HRTF 를 켜는 때(2026-11-06)
    std::string err;
    if (!hfile::writeAtomic(happ::inWriteRoot(kStatePath), text, &err))
        HLOG(LogEditorScene, Warning) << "Editor state not saved: " << err;
}

std::string EditorApp::sceneTitle() const {
    return doc_.displayName() + (sceneDirty() ? "*" : "");
}

bool EditorApp::blockedDuringPlay(const char* what) const {
    if (playState_ == EPlayState::Editor) return false;
    HLOG(LogEditorScene, Warning) << "Stop Play before " << what
                                  << " (the Play world is discarded on Stop)";
    return true;
}

// ── 열기 · 새 씬 ─────────────────────────────────────────────────────────────────
void EditorApp::applyCulture(const std::string& c, bool save) {
    std::vector<std::string> errors;
    const size_t n = htext::useCulture(c, &errors);
    for (const std::string& e : errors) HLOG(LogEditorScene, Warning) << "Localization: " << e;
    if (n) HLOG(LogEditorScene, Log) << "Localization: " << c << " (" << n << " entries)";
    if (save) { culture_ = c; saveEditorState(); }
    // 글 폭이 바뀌므로 UI 를 처음부터 다시 만듭니다(배율이 바뀔 때와 같은 경로).
    buildWidgetTree();
    outlinerPanel_.rebuild();
    detailsPanel_.rebuild();
    rebuildBottomPanel();
    toolbar_.markDirty();
}

void EditorApp::openInitialScene(const std::string& startScene) {
    loadEditorState();
    {
        const std::string c = !cultureOverride_.empty() ? cultureOverride_ : culture_;
        if (!c.empty() && c != htext::culture()) applyCulture(c, false);
    }
    if (audio_ && !audioOutput_.empty()) audio_->setOutputDevice(audioOutput_);
    if (audio_) audio_->setHrtfMode(hrtfMode_);
    contentPanel_.scanScenes();
    std::error_code ec;
    auto usable = [&](const std::string& p) {
        return project_settings::isSceneLogicalPath(p) && fs::exists(hproj::resolve(p), ec);
    };
    if (usable(lastScene_))      openScene(lastScene_);
    else if (usable(startScene)) openScene(startScene);
    else {
        HLOG(LogEditorScene, Warning) << "Start scene not found: " << startScene
                                      << " (set startScene in " << ProjectSettings::kPath << "); starting empty";
        doc_.reset("", cmdStack_.stateToken());
    }
}

void EditorApp::openScene(const std::string& logicalPath) {
    std::string text;
    if (!hfile::readAll(hproj::resolve(logicalPath), text)) {
        HLOG(LogEditorScene, Error) << "Open failed: " << logicalPath << " not found";
        return;
    }
    deserializeWorld(text);
    cmdStack_.clear();
    doc_.reset(logicalPath, cmdStack_.stateToken());
    lastScene_ = logicalPath;
    saveEditorState();
    bottomDirty_ = true;
    // 에디터 카메라를 빼고 셉니다.
    const size_t count = ctx_.world->getActors().size() - (viewport_.cameraActor() ? 1 : 0);
    HLOG(LogEditorScene, Log) << "Scene opened: " << logicalPath << " (" << count << " actors)";
}

void EditorApp::newScene() {
    if (blockedDuringPlay("creating a new scene")) return;
    guardUnsaved(HTEXT("Editor.Dialog", "Verb.NewScene", "creating a new scene"), [this] {
        deserializeWorld("");   // 에디터 카메라만 남기고 비움
        cmdStack_.clear();
        doc_.reset("", cmdStack_.stateToken());
        HLOG(LogEditorScene, Log) << "New scene (untitled)";
    });
}

// ── 소리 출력 장치 (2026-10-16) ──────────────────────────────────────────────────
// 목록에서 고르면 바로 그 장치로 바꾸고 Config/editor_state.txt 에 기억합니다. 마지막 항목은 XR Play 동안 헤드셋 장치로
// 바꿀지 켜고 끄는 줄입니다. UI 글꼴이 ASCII 만 그리므로 장치 이름의 한글은 빼고 보여 줍니다(로그에는 원래 이름).
void EditorApp::openAudioOutputDialog() {
    if (!audio_) {
        HLOG(LogEditorScene, Warning) << "Audio output: no audio system";
        return;
    }
    const std::vector<AudioOutputDevice> devices = audio_->outputDevices();
    ModalSpec m;
    m.title   = HTEXT("Editor.Dialog", "Audio.Title", "Audio Output").toString();
    m.message = "Now playing on: " + hutf::displayLabel(audio_->activeOutputName(), "(none)") +
                (audio_->overrideActive() ? " (headset during VR Play)" : "") +   // 헤드셋 장치가 실제로 열렸을 때만(DEF-147)
                "\nChoose the editor's output device:";
    // 항목 글 -> 동작. 같은 이름의 장치가 둘이면 번호를 붙여 구별합니다.
    std::vector<std::pair<std::string, std::function<void()>>> items;
    auto choose = [this](const std::string& id) {
        audioOutput_ = id;
        audio_->setOutputDevice(id);
        saveEditorState();
    };
    items.push_back({ std::string(audioOutput_.empty() ? "> " : "  ") + "System default", [choose] { choose(""); } });
    for (size_t i = 0; i < devices.size(); ++i) {
        const AudioOutputDevice& d = devices[i];
        std::string label = std::string(hutf::equalsIgnoreCase(d.id, audioOutput_) ? "> " : "  ") +
                            hutf::displayLabel(d.name, "Device " + std::to_string(i + 1)) +
                            (d.isDefault ? "  (Windows default)" : "");
        for (const auto& it : items)
            if (it.first == label) { label += " #" + std::to_string(i + 1); break; }
        const std::string id = d.id;
        items.push_back({ label, [choose, id] { choose(id); } });
    }
    items.push_back({ std::string("Headset during VR Play: ") + (headsetAudioInVr_ ? "On (click to turn off)" : "Off (click to turn on)"),
                      [this] {
                          headsetAudioInVr_ = !headsetAudioInVr_;
                          saveEditorState();
                          HLOG(LogEditorScene, Log) << "Headset audio during VR Play: " << (headsetAudioInVr_ ? "on" : "off")
                                                    << " (applies from the next Play)";
                      } });
    // HRTF(2026-11-06, Audio_Design.md 9-6): 누를 때마다 Auto → On → Off 로 바뀌고 바로 적용됩니다(다음 틱에 소리마다 사슬을 바꿈).
    // HRTF 는 헤드폰 · 헤드셋용이라 기본(Auto)은 VR Play 동안만 켭니다.
    static const char* const kHrtfLabels[] = { "Auto (VR Play only)", "On (headphones)", "Off (stereo panning)" };
    items.push_back({ std::string("3D audio (HRTF): ") + kHrtfLabels[(int)hrtfMode_] + "  - now " + audio_->hrtfStatus() +
                          " (click to change)",
                      [this] {
                          hrtfMode_ = hrtfMode_ == EHrtfMode::Auto ? EHrtfMode::On
                                    : hrtfMode_ == EHrtfMode::On   ? EHrtfMode::Off : EHrtfMode::Auto;
                          audio_->setHrtfMode(hrtfMode_);
                          saveEditorState();
                      } });
    for (const auto& it : items) m.list.push_back(it.first);
    m.onPick = [items](const std::string& label) {
        for (const auto& it : items)
            if (it.first == label) { it.second(); return; }
    };
    m.buttons.push_back({ HTEXT("Editor.Dialog", "Button.Close", "Close").toString(), {}, false, true });
    openModal(std::move(m));
}

void EditorApp::openSceneDialog() {
    if (blockedDuringPlay("opening a scene")) return;
    contentPanel_.scanScenes();
    ModalSpec m;
    m.title = HTEXT("Editor.Dialog", "Open.Title", "Open Scene").toString();
    const auto& scenes = contentPanel_.sceneFiles();
    if (scenes.empty()) {
        m.message = HTEXT("Editor.Dialog", "Open.None", "No .hscene files in assets/scenes.").toString();
    } else {
        m.message = HTEXT("Editor.Dialog", "Open.List", "Scenes in assets/scenes:").toString();
        m.list    = scenes;
        m.onPick  = [this](const std::string& path) {
            guardUnsaved(HTEXT("Editor.Dialog", "Verb.OpenAnother", "opening another scene"), [this, path] { openScene(path); });
        };
    }
    m.buttons.push_back({ HTEXT("Editor.Dialog", "Button.Cancel", "Cancel").toString(), {}, false, true });
    openModal(std::move(m));
}

// ── 저장 ─────────────────────────────────────────────────────────────────────────
bool EditorApp::writeSceneTo(const std::string& logicalPath) {
    std::string err;
    if (!hfile::writeAtomic(hproj::resolve(logicalPath), serializeWorld(), &err)) {
        HLOG(LogEditorScene, Error) << "Save failed: " << err;
        return false;
    }
    doc_.markSaved(logicalPath, cmdStack_.stateToken());
    lastScene_ = logicalPath;
    saveEditorState();
    contentPanel_.scanScenes();
    bottomDirty_ = true;   // 콘텐츠 브라우저의 SCENES 목록(지금 씬 강조)
    HLOG(LogEditorScene, Log) << "Scene saved: " << logicalPath;
    return true;
}

void EditorApp::saveScene() {
    if (blockedDuringPlay("saving the scene")) return;
    if (doc_.untitled()) { saveSceneAs(); return; }
    writeSceneTo(doc_.path());
}

void EditorApp::saveSceneAs(std::function<void()> then) {
    if (blockedDuringPlay("saving the scene")) return;
    // 입력 칸은 지금 이름(폴더 · 확장자 제외)으로 채우고 전체 선택해 바로 바꿔 쓸 수 있게 합니다.
    if (doc_.untitled()) {
        dialogEdit_.setText("NewScene");
    } else {
        // assets/scenes/ 아래면 그 접두를 떼고, 다른 폴더의 씬이면 파일 이름 부분만 채웁니다. 예전에는 접두가 늘
        // 있다고 보고 14글자를 잘라, 다른 폴더의 씬이면 엉뚱한 이름이 채워졌습니다(DEF-138).
        const std::string& path   = doc_.path();
        const std::string  prefix = std::string(kScenesDir) + "/";
        std::string rel = path.rfind(prefix, 0) == 0 ? path.substr(prefix.size())
                                                     : path.substr(path.find_last_of('/') + 1);   // 없으면 npos + 1 = 0
        if (rel.size() > 7 && rel.compare(rel.size() - 7, 7, ".hscene") == 0) rel.resize(rel.size() - 7);
        dialogEdit_.setText(rel);
    }
    ModalSpec m;
    m.title            = HTEXT("Editor.Dialog", "SaveAs.Title", "Save Scene As").toString();
    m.message          = HTEXT("Editor.Dialog", "SaveAs.Message", "Name under /Game/scenes (letters in any language, digits, space, - _ and / for folders):").toString();
    m.hasInput         = true;
    m.inputPlaceholder = "SceneName";
    m.buttons.push_back({ HTEXT("Editor.Dialog", "Button.Save", "Save").toString(), [this, then] {
        // 이름 규칙(모든 언어의 글자 · 숫자 · 공백 · - _ /)은 project_settings::normalizeSceneName(글자 체계 T1-11).
        std::string name;
        const bool charsOk = project_settings::normalizeSceneName(dialogEdit_.text, name);
        const std::string logical = std::string(kScenesDir) + "/" + name + ".hscene";
        if (!charsOk || !project_settings::isSceneLogicalPath(logical)) {
            HLOG(LogEditorScene, Warning) << "Invalid scene name '" << dialogEdit_.text
                                          << "'. Use letters, digits, space, - _ and / between folder names.";
            const std::string typed = dialogEdit_.text;
            saveSceneAs(then);   // 입력한 내용 그대로 다시 묻습니다
            dialogEdit_.setText(typed);
            return;
        }
        std::error_code ec;
        auto commit = [this, logical, then] { if (writeSceneTo(logical) && then) then(); };
        if (hvfs::toCanonical(logical) != hvfs::toCanonical(doc_.path()) && fs::exists(hproj::resolve(logical), ec)) {
            ModalSpec c;
            c.title   = HTEXT("Editor.Dialog", "Replace.Title", "Replace Scene").toString();
            c.message = HText::format(HTEXT("Editor.Dialog", "Replace.Message", "{0} already exists.\nReplace it?"), { logical }).toString();
            c.buttons.push_back({ HTEXT("Editor.Dialog", "Button.Replace", "Replace").toString(), commit, true, false });
            c.buttons.push_back({ HTEXT("Editor.Dialog", "Button.Cancel", "Cancel").toString(), {}, false, true });
            openModal(std::move(c));
            return;
        }
        commit();
    }, true, false });
    m.buttons.push_back({ HTEXT("Editor.Dialog", "Button.Cancel", "Cancel").toString(), {}, false, true });
    openModal(std::move(m));
}

void EditorApp::guardUnsaved(const HText& verb, std::function<void()> action) {
    if (!sceneDirty()) { action(); return; }
    ModalSpec m;
    m.title   = HTEXT("Editor.Dialog", "Unsaved.Title", "Unsaved Changes").toString();
    // 어순이 언어마다 다르므로 문장 전체를 틀로 번역합니다(글자 체계 T3-5).
    m.message = HText::format(HTEXT("Editor.Dialog", "Unsaved.Message", "Save changes to {0}\nbefore {1}?"),
                              { doc_.displayName(), verb }).toString();
    m.buttons.push_back({ HTEXT("Editor.Dialog", "Button.Save", "Save").toString(), [this, action] {
        if (doc_.untitled()) saveSceneAs(action);
        else if (writeSceneTo(doc_.path())) action();
    }, true, false });
    m.buttons.push_back({ HTEXT("Editor.Dialog", "Button.DontSave", "Don't Save").toString(), action, false, false });
    m.buttons.push_back({ HTEXT("Editor.Dialog", "Button.Cancel", "Cancel").toString(), {}, false, true });
    openModal(std::move(m));
}

bool EditorApp::requestClose() {
    if (quitApproved_) return true;
    if (modalOpen()) {
        if (closeFlow_) return false;   // 이미 닫기를 묻고 있으면 그 답을 기다립니다
        // 닫기와 관계없는 대화상자(Audio Output · Open Scene · Save As 등)는 취소로 닫고 닫기를 이어 갑니다. 예전에는
        // 어떤 대화상자든 떠 있으면 창의 X 를 아무 알림 없이 버렸습니다(DEF-137). 이 함수는 엔진 루프 맨 앞(위젯 처리
        // 밖)에서 불리므로 여기서 닫아도 됩니다.
        cancelModalNow();
        if (modalOpen()) return false;   // 취소 동작이 다른 대화상자를 열었으면 그 답을 기다립니다
    }
    if (!sceneDirty()) return true;
    // Play 중이면 먼저 멈춥니다. 저장은 Play 월드가 아니라 편집 중이던 월드를 써야 합니다.
    if (playState_ != EPlayState::Editor) stopPlay();
    guardUnsaved(HTEXT("Editor.Dialog", "Verb.Closing", "closing"), [this] {
        quitApproved_ = true;
        if (ctx_.window) ctx_.window->setShouldClose(true);
    });
    // 닫기 흐름(Unsaved Changes → Save As → Replace Scene)의 대화상자가 떠 있는 동안 표시해 둡니다. 흐름이 대화상자
    // 없이 끝나면(취소 · 저장 실패 · 닫기 승인) flushModal 이 지웁니다.
    closeFlow_ = modalOpen();
    return false;
}

void EditorApp::cancelModalNow() {
    if (!modal_) return;
    std::function<void()> cancel;
    for (const ModalButton& b : modal_->buttons)
        if (b.isCancel) { cancel = b.action; break; }
    modalPending_ = nullptr;
    if (currentFocus() == EFocusTarget::Dialog) setTextFocus(EFocusTarget::None);
    modal_.reset();
    modalDirty_ = true;
    if (cancel) cancel();
    HLOG(LogEditorScene, Log) << "Dialog closed by the window close request";
}

// ── 모달 대화상자 ─────────────────────────────────────────────────────────────────
void EditorApp::openModal(ModalSpec spec) {
    // 열려 있던 편집 · 메뉴 · 팝업을 정리합니다. 대화상자가 떠 있는 동안 그쪽으로 입력이 가면 안 됩니다.
    if (currentFocus() != EFocusTarget::None) setTextFocus(EFocusTarget::None);
    menus_.close();
    logPanel_.closePopup();
    modal_ = std::make_unique<ModalSpec>(std::move(spec));
    if (modal_->hasInput) setTextFocus(EFocusTarget::Dialog);
    modalDirty_ = true;
}

void EditorApp::triggerModal(std::function<void()> action) {
    if (!modal_ || modalPending_) return;   // 같은 프레임의 두 번째 누름은 무시
    modalPending_ = action ? std::move(action) : std::function<void()>([] {});
    modalDirty_   = true;
}

void EditorApp::triggerModalDefault() {
    if (!modal_) return;
    for (const ModalButton& b : modal_->buttons)
        if (b.isDefault) { triggerModal(b.action); return; }
}

void EditorApp::triggerModalCancel() {
    if (!modal_) return;
    for (const ModalButton& b : modal_->buttons)
        if (b.isCancel) { triggerModal(b.action); return; }
    triggerModal({});
}

void EditorApp::flushModal() {
    if (modalPending_) {
        std::function<void()> act = std::move(modalPending_);
        modalPending_ = nullptr;
        if (currentFocus() == EFocusTarget::Dialog) setTextFocus(EFocusTarget::None);
        modal_.reset();
        modalDirty_ = true;
        act();   // 다른 대화상자를 열 수 있습니다
        if (!modal_) closeFlow_ = false;   // 닫기 흐름이 대화상자 없이 끝남(DEF-137)
    }
    if (modalDirty_) { modalDirty_ = false; rebuildModal(); }
}

void EditorApp::rebuildModal() {
    if (!modalHost_) return;
    if (!modal_) {
        modalHost_->content(nullptr);
        modalPanel_.reset();
        dialogBox_.reset();
        return;
    }
    const ModalSpec& m = *modal_;
    using VR = SVerticalBox;
    using HR = SHorizontalBox;

    const float panelW = s(440.f);
    const float lineH  = s(20.f);
    auto col = HNew(VR);
    float height = 0.f;
    auto add = [&](std::shared_ptr<HWidget> w, float h) { col->addSlot(std::move(w), VR::ESizeRule::Fixed, h); height += h; };

    add(HNew(SBorder)->background(kBgHeader)->padding(HPadding{ s(14.f), s(8.f) })
            ->content(HNew(STextBlock)->text(m.title)->fontSize(s(14.f))->color(kTextHdr)
                ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
        s(36.f));
    add(spacer(), s(8.f));
    std::stringstream lines(m.message);
    for (std::string ln; std::getline(lines, ln);) {
        add(HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ s(16.f), 0.f })
                ->content(HNew(STextBlock)->text(ln)->fontSize(s(13.f))->color(kTextBody)
                    ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
            lineH);
    }
    if (m.hasInput) {
        add(spacer(), s(8.f));
        dialogBox_ = HNew(SSearchBox)
            ->style(EditorUiKit::searchBoxStyle(s(1.f)))
            ->edit(&dialogEdit_)
            ->placeholder(m.inputPlaceholder)
            ->focused([this] { return dialogFocused_; })
            ->onClick([this] { setTextFocus(EFocusTarget::Dialog); })
            ->icon(false)
            ->fontSize(s(13.f));
        add(HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ s(16.f), 0.f })->content(dialogBox_),
            s(28.f));
    } else {
        dialogBox_.reset();
    }
    if (!m.list.empty()) {
        add(spacer(), s(8.f));
        auto items = HNew(VR);
        const float rowH = s(26.f);
        for (const std::string& item : m.list) {
            const std::string label = item.rfind(std::string(kScenesDir) + "/", 0) == 0
                                    ? item.substr(std::string(kScenesDir).size() + 1) : item;
            items->addSlot(
                HNew(SButton)
                    ->normalColor(EditorTheme::transparent)
                    ->hoveredColor(EditorTheme::selectRow)
                    ->pressedColor(EditorTheme::accentPressed)
                    ->padding(HPadding{ s(10.f), s(3.f) })
                    ->hAlign(EHAlign::Fill)
                    ->onClick([this, item] {
                        auto pick = modal_ ? modal_->onPick : nullptr;
                        triggerModal([pick, item] { if (pick) pick(item); });
                    })
                    ->content(HNew(STextBlock)->text(label)->fontSize(s(13.f))->color(kTextBody)
                        ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
                VR::ESizeRule::Fixed, rowH);
        }
        const float listH = std::min((float)m.list.size() * rowH, s(300.f));
        add(HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ s(12.f), 0.f })->content(
                HNew(SBorder)->background(EditorTheme::bgInput)->borderColor(kBorder)->borderThickness(1.f)
                    ->content(HNew(SScrollBox)->style(EditorUiKit::scrollBoxStyle(s(1.f)))
                        ->content(items)->wheelStep(rowH * 3.f))),
            listH);
    }
    add(spacer(), s(12.f));
    auto row = HNew(HR);
    row->addSlot(spacer(), HR::ESizeRule::Fill, 1.f);
    for (int i = 0; i < (int)m.buttons.size(); ++i) {
        const ModalButton& b = m.buttons[i];
        const glm::vec4 normal  = b.isDefault ? EditorTheme::accent      : EditorTheme::ctlNormal;
        const glm::vec4 hovered = b.isDefault ? EditorTheme::accentHover : EditorTheme::ctlHover;
        row->addSlot(textButton(b.label, 13.f, kTextBody, { 10.f, 4.f }, normal, hovered, [this, i] {
                         if (!modal_ || i >= (int)modal_->buttons.size()) return;
                         triggerModal(modal_->buttons[i].action);
                     }),
                     HR::ESizeRule::Fixed, s(104.f));
        row->addSlot(spacer(), HR::ESizeRule::Fixed, s(10.f));
    }
    add(row, s(30.f));
    add(spacer(), s(12.f));

    auto panel = HNew(SBox)->widthOverride(panelW)->heightOverride(height)->content(
        HNew(SBorder)->background(EditorTheme::bgPopup)
            ->borderColor(EditorTheme::borderPopup)->borderThickness(EditorTheme::borderThin)
            ->cornerRadius(s(EditorTheme::radiusPopup))
            ->shadow({ 0.f, s(EditorTheme::shadowOffset.y) }, s(EditorTheme::shadowBlur), EditorTheme::shadowColor)
            ->content(col));
    // 화면 전체를 어둡게 덮는 캐처가 바깥 누름과 휠을 삼킵니다. 대화상자 밖을 눌러도 닫히지 않습니다
    // (저장 확인을 실수로 넘기지 않게).
    auto catcher = HNew(SBorder)->background(EditorTheme::modalScrim)->onWheel([](float) { return true; })
        ->content(HNew(SButton)
                      ->normalColor(EditorTheme::transparent)
                      ->hoveredColor(EditorTheme::transparent)
                      ->pressedColor(EditorTheme::transparent)
                      ->onClick([] {})->content(spacer()));
    auto inner = HNew(SOverlay);
    inner->addSlot(catcher);
    inner->addSlot(panel, EHAlign::Center, EVAlign::Center);
    modalHost_->content(inner);
    modalPanel_ = panel;
}

// ── 에셋 참조 · 옮기기(참조 체계 A-5, Docs/Object_References.md) ─────────────────────────────
namespace {
// 열린 프로젝트의 루트. /Game = Content, /Config = Config(엔진이 같은 폴더를 마운트함). 엔진 콘텐츠는 옮기지 않습니다.
std::vector<hrefs::Root> projectRefRoots() {
    const fs::path& root = hproj::root();
    if (root.empty()) return {};
    return { { "/Game", root / "Content" }, { "/Config", root / "Config" } };
}
} // namespace

void EditorApp::openAssetActions(const std::string& path) {
    const std::vector<hrefs::Root> roots = projectRefRoots();
    if (roots.empty()) return;
    const std::string canon = hvfs::toCanonical(path);
    const hrefs::Index idx = hrefs::scan(roots);
    std::vector<std::string> files;
    for (const hrefs::Ref& r : hrefs::referencesTo(idx, canon))
        if (std::find(files.begin(), files.end(), r.file) == files.end()) files.push_back(r.file);

    ModalSpec m;
    m.title = HTEXT("Editor.Dialog", "Asset.Title", "Asset").toString();
    if (files.empty()) {
        m.message = HText::format(HTEXT("Editor.Dialog", "Asset.NoRefs", "{0}\nNo scene, prefab or setting refers to it."), { canon }).toString();
    } else {
        m.message = HText::format(HTEXT("Editor.Dialog", "Asset.Refs", "{0}\nReferenced by {1} file(s). Pick a scene to open it."),
                                  { canon, HText::asInteger((int64_t)files.size()) }).toString();
        m.list   = files;
        m.onPick = [this](const std::string& file) {
            if (file.size() < 7 || file.compare(file.size() - 7, 7, ".hscene") != 0) return;
            if (hvfs::toCanonical(file) == hvfs::toCanonical(doc_.path()) || blockedDuringPlay("opening a scene")) return;
            guardUnsaved(HTEXT("Editor.Dialog", "Verb.OpenAnother", "opening another scene"), [this, file] { openScene(file); });
        };
    }
    m.buttons.push_back({ HTEXT("Editor.Dialog", "Button.RenameMove", "Rename / Move...").toString(),
                          [this, canon] { openAssetMoveDialog(canon); }, true, false });
    m.buttons.push_back({ HTEXT("Editor.Dialog", "Button.Close", "Close").toString(), {}, false, true });
    openModal(std::move(m));
}

void EditorApp::openAssetMoveDialog(const std::string& path) {
    if (blockedDuringPlay("moving an asset")) return;
    dialogEdit_.setText(path);
    ModalSpec m;
    m.title            = HTEXT("Editor.Dialog", "Move.Title", "Rename / Move Asset").toString();
    m.message          = HText::format(HTEXT("Editor.Dialog", "Move.Message",
                                             "New path under /Game for {0}\n(folders are created; files that refer to it are updated;\nthe Undo history is cleared):"),
                                       { path }).toString();
    m.hasInput         = true;
    m.inputPlaceholder = path;
    m.buttons.push_back({ HTEXT("Editor.Dialog", "Button.Move", "Move").toString(), [this, path] {
        const std::string typed = dialogEdit_.text;
        if (!moveAsset(path, typed)) {
            // 실패 이유는 대화상자로 알렸습니다(moveAsset). 같은 입력으로 다시 물을 수 있게 입력은 그대로 둡니다.
            dialogEdit_.setText(typed);
        }
    }, true, false });
    m.buttons.push_back({ HTEXT("Editor.Dialog", "Button.Cancel", "Cancel").toString(), {}, false, true });
    openModal(std::move(m));
}

bool EditorApp::moveAsset(const std::string& from, const std::string& to) {
    if (blockedDuringPlay("moving an asset")) return false;
    const std::vector<hrefs::Root> roots = projectRefRoots();
    if (roots.empty()) return false;
    const hrefs::Index    idx  = hrefs::scan(roots);
    const hrefs::MovePlan plan = hrefs::planMove(idx, roots, from, to, { hproj::root() / "Source" });
    std::string err = plan.error;
    const bool moved = plan.ok() && hrefs::applyMove(plan, roots, &err);
    if (!moved) {
        HLOG(LogEditorScene, Warning) << "Cannot move " << from << " to " << to << ": " << err;
        ModalSpec m;
        m.title   = HTEXT("Editor.Dialog", "MoveFailed.Title", "Cannot Move").toString();
        m.message = err;
        m.buttons.push_back({ HTEXT("Editor.Dialog", "Button.Close", "Close").toString(), {}, false, true });
        openModal(std::move(m));
        return false;
    }
    HLOG(LogEditorScene, Log) << "Moved " << plan.from << " -> " << plan.to << " (" << plan.valueCount() << " reference(s) in "
                              << plan.edits.size() << " file(s) updated)";
    for (const std::string& w : plan.codeWarnings)
        HLOG(LogEditorScene, Warning) << w << " has the old path " << plan.from << " in code; change it by hand";

    // 열린 씬(메모리)도 디스크와 같게 고칩니다. 저장하지 않은 편집은 그대로 남습니다(D7). Undo 스냅샷에는 옛 경로가 남아
    // 되살리면 끊긴 참조가 되므로 Undo 기록은 비웁니다.
    const bool wasDirty = sceneDirty();
    int changed = 0;
    const std::string fixed = hrefs::rewriteText("/Game/open.hscene", serializeWorld(), plan, &changed);
    if (changed > 0) deserializeWorld(fixed);
    cmdStack_.clear();
    const std::string docPath = doc_.untitled() ? std::string() : hrefs::mapPath(plan, hvfs::toCanonical(doc_.path()));
    doc_.reset(docPath, cmdStack_.stateToken());
    if (wasDirty) doc_.markDirty();
    if (!lastScene_.empty()) lastScene_ = hrefs::mapPath(plan, hvfs::toCanonical(lastScene_));
    saveEditorState();
    contentPanel_.scanAll();
    bottomDirty_ = true;
    detailsPanel_.markDirty();
    outlinerPanel_.markDirty();
    return true;
}
