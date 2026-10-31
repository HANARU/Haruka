// 에디터의 씬 파일 관리와 모달 대화상자(2026-10-15).
//
// 예전에는 저장 · 불러오기가 실행 폴더의 고정 파일 scene.hscene 하나였습니다. 그래서 저장한 씬이 프로젝트에
// 남지 않았고, 지금 열린 씬이 무엇인지 · 저장하지 않은 변경이 있는지도 몰랐습니다. 이제 씬은 프로젝트 폴더의
// assets/scenes 아래 논리 경로로 열고 저장하며(hproj), SceneDocument 가 열린 파일과 변경 여부를 기억합니다.
// 파일 고르기와 이름 입력은 에디터 안의 대화상자로 합니다(OS 대화상자는 플랫폼마다 다르고 assets 밖의
// 경로를 고를 수 있어 논리 경로 규칙을 지키기 어렵습니다).
#include "EditorApp.hpp"
#include "../core/World.hpp"
#include "../core/Actor.hpp"
#include "../core/FileUtil.hpp"
#include "../core/AppPaths.hpp"
#include "../core/ProjectPaths.hpp"
#include "../core/ProjectSettings.hpp"
#include "../core/TextUtf.hpp"
#include "../audio/AudioSystem.hpp"
#include "../platform/IWindow.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

static constinit HLogCategory LogEditorScene{ "Editor", ELogVerbosity::Log };

static constexpr const char* kScenesDir = "assets/scenes";
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
        else if (key == "audioOutput") audioOutput_ = val;
        else if (key == "audioHeadsetInVr") headsetAudioInVr_ = (val != "0");
    }
}

void EditorApp::saveEditorState() const {
    std::string text = "# Haruka editor state for this machine (not part of the project).\n";
    if (!lastScene_.empty()) text += "lastScene=" + lastScene_ + "\n";
    // 소리 출력 장치(비면 시스템 기본)와 XR Play 동안 헤드셋으로 바꿀지(2026-10-16).
    if (!audioOutput_.empty()) text += "audioOutput=" + audioOutput_ + "\n";
    text += std::string("audioHeadsetInVr=") + (headsetAudioInVr_ ? "1" : "0") + "\n";
    std::string err;
    if (!hfile::writeAtomic(happ::inWriteRoot(kStatePath), text, &err))
        HLOG(LogEditorScene, Warning) << "Editor state not saved: " << err;
}

void EditorApp::scanSceneFiles() {
    sceneFiles_.clear();
    std::error_code ec;
    const fs::path dir = hproj::resolve(kScenesDir);
    if (!fs::exists(dir, ec)) return;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".hscene") continue;
        const std::string rel = fs::relative(it->path(), dir, ec).generic_string();
        if (!ec && !rel.empty()) sceneFiles_.push_back(std::string(kScenesDir) + "/" + rel);
    }
    std::sort(sceneFiles_.begin(), sceneFiles_.end());
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
void EditorApp::openInitialScene(const std::string& startScene) {
    loadEditorState();
    if (audio_ && !audioOutput_.empty()) audio_->setOutputDevice(audioOutput_);
    scanSceneFiles();
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
    const size_t count = ctx_.world->getActors().size() - (editorCamActor_ ? 1 : 0);
    HLOG(LogEditorScene, Log) << "Scene opened: " << logicalPath << " (" << count << " actors)";
}

void EditorApp::newScene() {
    if (blockedDuringPlay("creating a new scene")) return;
    guardUnsaved("creating a new scene", [this] {
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
    m.title   = "Audio Output";
    m.message = "Now playing on: " + hutf::asciiLabel(audio_->activeOutputName(), "(none)") +
                (audio_->outputOverride().empty() ? "" : " (headset during VR Play)") +
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
                            hutf::asciiLabel(d.name, "Device " + std::to_string(i + 1)) +
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
    for (const auto& it : items) m.list.push_back(it.first);
    m.onPick = [items](const std::string& label) {
        for (const auto& it : items)
            if (it.first == label) { it.second(); return; }
    };
    m.buttons.push_back({ "Close", {}, false, true });
    openModal(std::move(m));
}

void EditorApp::openSceneDialog() {
    if (blockedDuringPlay("opening a scene")) return;
    scanSceneFiles();
    ModalSpec m;
    m.title = "Open Scene";
    if (sceneFiles_.empty()) {
        m.message = "No .hscene files in assets/scenes.";
    } else {
        m.message = "Scenes in assets/scenes:";
        m.list    = sceneFiles_;
        m.onPick  = [this](const std::string& path) {
            guardUnsaved("opening another scene", [this, path] { openScene(path); });
        };
    }
    m.buttons.push_back({ "Cancel", {}, false, true });
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
    scanSceneFiles();
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
        dialogText_ = "NewScene";
    } else {
        std::string rel = doc_.path().substr(std::string(kScenesDir).size() + 1);
        if (rel.size() > 7) rel.resize(rel.size() - 7);   // ".hscene"
        dialogText_ = rel;
    }
    ModalSpec m;
    m.title            = "Save Scene As";
    m.message          = "Name under assets/scenes (letters, digits, space, - _ and / for folders):";
    m.hasInput         = true;
    m.inputPlaceholder = "SceneName";
    m.buttons.push_back({ "Save", [this, then] {
        std::string name = project_settings::trim(dialogText_);
        if (name.size() > 7 && name.compare(name.size() - 7, 7, ".hscene") == 0) name.resize(name.size() - 7);
        const bool charsOk = !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
            return std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_' || c == '/';
        });
        const std::string logical = std::string(kScenesDir) + "/" + name + ".hscene";
        if (!charsOk || name.front() == '/' || name.back() == '/' || name.find("//") != std::string::npos ||
            !project_settings::isSceneLogicalPath(logical)) {
            HLOG(LogEditorScene, Warning) << "Invalid scene name '" << dialogText_
                                          << "'. Use letters, digits, space, - _ and / between folder names.";
            const std::string typed = dialogText_;
            saveSceneAs(then);   // 입력한 내용 그대로 다시 묻습니다
            dialogText_ = typed;
            return;
        }
        std::error_code ec;
        auto commit = [this, logical, then] { if (writeSceneTo(logical) && then) then(); };
        if (logical != doc_.path() && fs::exists(hproj::resolve(logical), ec)) {
            ModalSpec c;
            c.title   = "Replace Scene";
            c.message = logical + " already exists.\nReplace it?";
            c.buttons.push_back({ "Replace", commit, true, false });
            c.buttons.push_back({ "Cancel", {}, false, true });
            openModal(std::move(c));
            return;
        }
        commit();
    }, true, false });
    m.buttons.push_back({ "Cancel", {}, false, true });
    openModal(std::move(m));
}

void EditorApp::guardUnsaved(const std::string& verb, std::function<void()> action) {
    if (!sceneDirty()) { action(); return; }
    ModalSpec m;
    m.title   = "Unsaved Changes";
    m.message = "Save changes to " + doc_.displayName() + "\nbefore " + verb + "?";
    m.buttons.push_back({ "Save", [this, action] {
        if (doc_.untitled()) saveSceneAs(action);
        else if (writeSceneTo(doc_.path())) action();
    }, true, false });
    m.buttons.push_back({ "Don't Save", action, false, false });
    m.buttons.push_back({ "Cancel", {}, false, true });
    openModal(std::move(m));
}

bool EditorApp::requestClose() {
    if (quitApproved_) return true;
    if (modalOpen()) return false;            // 이미 묻고 있으면 그 답을 기다립니다
    if (!sceneDirty()) return true;
    // Play 중이면 먼저 멈춥니다. 저장은 Play 월드가 아니라 편집 중이던 월드를 써야 합니다.
    if (playState_ != EPlayState::Editor) stopPlay();
    guardUnsaved("closing", [this] {
        quitApproved_ = true;
        if (ctx_.window) ctx_.window->setShouldClose(true);
    });
    return false;
}

// ── 모달 대화상자 ─────────────────────────────────────────────────────────────────
void EditorApp::openModal(ModalSpec spec) {
    // 열려 있던 편집 · 메뉴 · 팝업을 정리합니다. 대화상자가 떠 있는 동안 그쪽으로 입력이 가면 안 됩니다.
    if (currentFocus() != EFocusTarget::None) setTextFocus(EFocusTarget::None);
    if (openMenu_ >= 0) { openMenu_ = -1; menuBarDirty_ = menuPopupDirty_ = true; }
    if (logPopupOpen_)  { logPopupOpen_ = false; logPopupDirty_ = true; }
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
        add(HNew(SBorder)->background({ 0.f, 0.f, 0.f, 0.f })->padding(HPadding{ s(16.f), 0.f })
                ->content(HNew(STextBlock)->text(ln)->fontSize(s(13.f))->color(kTextBody)
                    ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)),
            lineH);
    }
    if (m.hasInput) {
        add(spacer(), s(8.f));
        dialogBox_ = HNew(SSearchBox)
            ->text(AttrStr([this]() -> std::string { return dialogText_; }))
            ->placeholder(m.inputPlaceholder)
            ->focused([this] { return dialogFocused_; })
            ->selectedAll([this] { return dialogSelAll_; })
            ->onClick([this] { setTextFocus(EFocusTarget::Dialog); })
            ->icon(false)
            ->fontSize(s(13.f));
        add(HNew(SBorder)->background({ 0.f, 0.f, 0.f, 0.f })->padding(HPadding{ s(16.f), 0.f })->content(dialogBox_),
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
                    ->normalColor({ 0.f, 0.f, 0.f, 0.f })
                    ->hoveredColor({ 0.20f, 0.42f, 0.68f, 1.f })
                    ->pressedColor({ 0.14f, 0.34f, 0.56f, 1.f })
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
        add(HNew(SBorder)->background({ 0.f, 0.f, 0.f, 0.f })->padding(HPadding{ s(12.f), 0.f })->content(
                HNew(SBorder)->background({ 0.08f, 0.08f, 0.09f, 1.f })->borderColor(kBorder)->borderThickness(1.f)
                    ->content(HNew(SScrollBox)->content(items)->wheelStep(rowH * 3.f))),
            listH);
    }
    add(spacer(), s(12.f));
    auto row = HNew(HR);
    row->addSlot(spacer(), HR::ESizeRule::Fill, 1.f);
    for (int i = 0; i < (int)m.buttons.size(); ++i) {
        const ModalButton& b = m.buttons[i];
        const glm::vec4 normal  = b.isDefault ? glm::vec4{ 0.07f, 0.33f, 0.58f, 1.f } : glm::vec4{ 0.18f, 0.18f, 0.21f, 1.f };
        const glm::vec4 hovered = b.isDefault ? glm::vec4{ 0.10f, 0.42f, 0.72f, 1.f } : glm::vec4{ 0.26f, 0.26f, 0.31f, 1.f };
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
        HNew(SBorder)->background({ 0.10f, 0.10f, 0.115f, 1.f })
            ->borderColor({ 0.34f, 0.34f, 0.40f, 1.f })->borderThickness(1.f)
            ->content(col));
    // 화면 전체를 어둡게 덮는 캐처가 바깥 누름과 휠을 삼킵니다. 대화상자 밖을 눌러도 닫히지 않습니다
    // (저장 확인을 실수로 넘기지 않게).
    auto catcher = HNew(SBorder)->background({ 0.f, 0.f, 0.f, 0.45f })->onWheel([](float) { return true; })
        ->content(HNew(SButton)->normalColor({ 0, 0, 0, 0 })->hoveredColor({ 0, 0, 0, 0 })->pressedColor({ 0, 0, 0, 0 })
                      ->onClick([] {})->content(spacer()));
    auto inner = HNew(SOverlay);
    inner->addSlot(catcher);
    inner->addSlot(panel, EHAlign::Center, EVAlign::Center);
    modalHost_->content(inner);
    modalPanel_ = panel;
}
