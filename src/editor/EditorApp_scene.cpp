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
#include "../projectops/ProjectOps.hpp"
#include "../runtime/EngineOptions.hpp"

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
    if (!hfile::readAll(happ::inWriteRoot(heprefs::kStateRelPath), text)) return;
    std::vector<std::string> warnings;
    const heprefs::EditorStateData s = heprefs::parseEditorState(text, warnings);
    for (const std::string& w : warnings) HLOG(LogEditorScene, Warning) << w;
    lastScene_        = s.lastScene;
    culture_          = (s.culture == "en") ? std::string() : s.culture;
    gizmoSize_        = s.gizmoSize;
    audioOutput_      = s.audioOutput;
    headsetAudioInVr_ = s.audioHeadsetInVr;
    AudioSystem::hrtfModeFromText(s.audioHrtf, hrtfMode_);
}

void EditorApp::saveEditorState() const {
    heprefs::EditorStateData s;
    s.lastScene        = lastScene_;
    s.culture          = culture_.empty() ? std::string("en") : culture_;
    s.gizmoSize        = gizmoSize_;
    s.audioOutput      = audioOutput_;
    s.audioHeadsetInVr = headsetAudioInVr_;
    s.audioHrtf        = AudioSystem::hrtfModeName(hrtfMode_);
    std::string err;
    if (!hfile::writeAtomic(happ::inWriteRoot(heprefs::kStateRelPath), heprefs::formatEditorState(s), &err))
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
    projectSettingsPanel_.rebuild();
    editorPreferencesPanel_.rebuild();
    rebuildBottomPanel();
    toolbar_.markDirty();
}

void EditorApp::openInitialScene(const std::string& startScene) {
    loadEditorState();
    viewport_.gizmo().setSizeFactor(heprefs::gizmoFactorFromName(gizmoSize_));
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
    sceneDiskHash_ = sceneTextHash(text);
    lastScene_ = logicalPath;
    saveEditorState();
    requestBottomRebuild();
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

// ── 프로젝트 설정 · 에디터 개인 설정 창 (PS1b · PS1d) ────────────────────────────────
void EditorApp::openProjectSettings() {
    projectSettingsPanel_.reloadFiles();
    projectSettingsPanel_.markDirty();
    if (dock_.contains("ProjectSettings")) {
        dock_.showPanel("ProjectSettings");
        return;
    }
    int wx = 100, wy = 100, ww = 1600, wh = 900;
    if (ctx_.window) {
        ctx_.window->getWindowPos(wx, wy);
        ctx_.window->getWindowSize(ww, wh);
    }
    const int pw = 960, ph = 640;
    const int px = wx + std::max(0, (ww - pw) / 2);
    const int py = wy + std::max(0, (wh - ph) / 2);
    dock_.showPanelFloating("ProjectSettings", pw, ph, px, py);
}

void EditorApp::openEditorPreferences(const std::string& category) {
    if (!category.empty()) editorPreferencesPanel_.selectCategory(category);
    editorPreferencesPanel_.markDirty();
    if (dock_.contains("EditorPreferences")) {
        dock_.showPanel("EditorPreferences");
        return;
    }
    int wx = 100, wy = 100, ww = 1600, wh = 900;
    if (ctx_.window) {
        ctx_.window->getWindowPos(wx, wy);
        ctx_.window->getWindowSize(ww, wh);
    }
    const int pw = 880, ph = 580;
    const int px = wx + std::max(0, (ww - pw) / 2);
    const int py = wy + std::max(0, (wh - ph) / 2);
    dock_.showPanelFloating("EditorPreferences", pw, ph, px, py);
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
    const std::string text = serializeWorld();
    if (!hfile::writeAtomic(hproj::resolve(logicalPath), text, &err)) {
        HLOG(LogEditorScene, Error) << "Save failed: " << err;
        return false;
    }
    sceneDiskHash_ = sceneTextHash(text);   // 이 저장이 부르는 감시 알림은 무시(핫리로드 D7)
    doc_.markSaved(logicalPath, cmdStack_.stateToken());
    lastScene_ = logicalPath;
    saveEditorState();
    contentPanel_.scanScenes();
    requestBottomRebuild();   // 콘텐츠 브라우저의 SCENES 목록(지금 씬 강조)
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
    // 덤프 실행(--ui-dump · --viewport-dump)은 답할 사람이 없으므로 묻지 않고 저장 없이 닫습니다(2026-11-21). 예전에는 덤프 옵션이
    // 씬을 바꾸면(--ui-add-component-commit) 종료 때 Unsaved Changes 대화상자가 떠 시간 제한까지 기다렸습니다.
    if (dumpMode_) {
        HLOG(LogEditorScene, Log) << "Closing with unsaved changes (dump run; not saved)";
        return true;
    }
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

bool EditorApp::canRestart() const {
    return (!floatsHidden_ && !headlessRun_) || restartDryRun_;
}

void EditorApp::requestRestart() {
    if (!canRestart()) return;
    if (playState_ != EPlayState::Editor) stopPlay();
    if (restartDryRun_) {
        performRestart();
        return;
    }
    guardUnsaved(HTEXT("Editor.Dialog", "Verb.Restarting", "restarting"), [this] {
        performRestart();
    });
}

void EditorApp::performRestart() {
    saveEditorState();
    dock_.saveLayout();

    const fs::path exe = hprojops::currentExecutablePath();
    std::vector<std::string> args;
    args.push_back(exe.empty() ? std::string("HarukaEngine.exe") : hutf::fromUtf16(exe.c_str()));
    const uint32_t pid = hprojops::currentProcessId();
    for (std::string& a : hopt::filterRestartArgs(rawCliArgs_, pid))
        args.push_back(std::move(a));

    const std::string cmdLine = hprojops::formatCommandLine(args);
    if (restartDryRun_) {
        HLOG(LogEditorScene, Log) << "Restart (dry-run): " << cmdLine;
        return;
    }

    HLOG(LogEditorScene, Log) << "Restarting editor: " << cmdLine;
    const hprojops::LaunchResult lr = hprojops::launchExecutable(args, {}, false);
    if (!lr.launched) {
        HLOG(LogEditorScene, Error) << "Restart failed: " << lr.error;
        return;
    }
    quitApproved_ = true;
    if (ctx_.window) ctx_.window->setShouldClose(true);
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
    componentPicker_.close();
    assetPicker_.close();
    colorPicker_.confirm();
    contextMenu_.close();
    // 대화상자는 메인 창에 뜹니다(D5). 떠 있는 창에 키보드가 있으면 메인을 앞으로 가져와 키 입력이 대화상자로 가게 합니다(숨은 창은 그대로).
    if (keyboardWindowId() != 0 && ctx_.window) ctx_.window->focus();
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
    requestBottomRebuild();
    detailsPanel_.markDirty();
    outlinerPanel_.markDirty();
    return true;
}

// 콘텐츠 브라우저에서 소리 · 큐를 두 번 누르면 미리 듣습니다(2026-11-21). 공간화 없이 sfx 버스로 한 번 냅니다(UE 의 에셋 미리 듣기와 같은 몫).
void EditorApp::previewSound(const std::string& path) {
    if (!audio_) {
        HLOG(LogEditorScene, Warning) << "Preview: no audio system";
        return;
    }
    AudioAttenuationSettings at;
    at.spatialize = false;
    const glm::vec3 pos = viewport_.cameraActor() ? viewport_.cameraActor()->worldPosition() : glm::vec3(0.f);
    bool ok = false;
    if (AudioSystem::isCuePath(path)) ok = audio_->playCue(path, pos, 1.f, at) != 0;
    else                              ok = audio_->playOneShot(path.c_str(), pos, 1.f, EAudioBus::Sfx, at);
    HLOG(LogEditorScene, Log) << "Preview " << path << (ok ? "" : " (not played; see the audio log)");
}
