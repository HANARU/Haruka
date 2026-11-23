#include "LauncherApp.hpp"
#include "LauncherHost.hpp"
#include "core/HText.hpp"
#include "core/Utf8.hpp"
#include "ui/slate/HSlateTheme.hpp"
#include "platform/InputKeys.hpp"
#include "vfs/PathUtf8.hpp"

#include <algorithm>
#include <utility>

namespace fs = std::filesystem;

namespace {

SButton::Style makeBtnStyle() {
    return {
        HSlateTheme::ctlNormal,
        HSlateTheme::ctlHover,
        HSlateTheme::ctlPressed,
        HSlateTheme::radiusControl,
        HSlateTheme::textDisabled
    };
}

SButton::Style makeAccentBtnStyle() {
    return {
        HSlateTheme::accent,
        HSlateTheme::accentHover,
        HSlateTheme::accentPressed,
        HSlateTheme::radiusControl,
        HSlateTheme::textDisabled
    };
}

SScrollBox::Style makeScrollStyle() {
    return {
        HSlateTheme::transparent,
        HSlateTheme::ctlPressed,
        HSlateTheme::textDisabled,
        6.f,
        3.f
    };
}

SEditableText::Style makeEditStyle() {
    return {
        HSlateTheme::textPrimary,
        HSlateTheme::textDisabled,
        HSlateTheme::selectRow,
        HSlateTheme::accentHover,
        HSlateTheme::textPrimary,
        1.5f
    };
}

std::shared_ptr<SButton> makeButton(const std::string& label,
                                    const SButton::Style& st,
                                    std::function<void()> onClick,
                                    bool enabled = true,
                                    HPadding pad = { 12.f, 6.f, 12.f, 6.f }) {
    auto btn = std::make_shared<SButton>();
    btn->style(st)
        ->padding(pad)
        ->onClick(std::move(onClick))
        ->content(
            std::make_shared<STextBlock>()
                ->text(label)
                ->fontSize(13.f)
                ->color(enabled ? HSlateTheme::textPrimary : HSlateTheme::textDisabled)
        );
    btn->enabled.set(enabled);
    return btn;
}

} // namespace

LauncherApp::LauncherApp(const LauncherAppOptions& opts) : opts_(opts) {
    userDir_ = hprojops::resolveUserDir(opts_.userDirOverride);
    page_ = opts_.initialPage;

    if (!opts_.newDir.empty()) {
        editLocation_.setText(opts_.newDir);
    } else {
        fs::path loc = hprojops::loadLastProjectLocation(userDir_);
        if (loc.empty()) {
            loc = hprojops::defaultProjectsDir();
        }
        editLocation_.setText(hpak::utf8Of(loc));
    }

    editName_.setText(opts_.newName.empty() ? "MyHarukaGame" : opts_.newName);
    editCompany_.setText(opts_.newCompany.empty() ? "Haruka" : opts_.newCompany);
    updateAutoPackageId();
    validateNewForm();
}

LauncherApp::~LauncherApp() {
    joinWorker();
}

void LauncherApp::joinWorker() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

void LauncherApp::init(LauncherHost& host) {
    host_ = &host;
    templates_ = hprojops::listTemplates(opts_.engineRoot);
    refreshRecent();
    validateNewForm();
    rebuildUi();

    if (opts_.autoCreate) {
        startCreateProject();
    }
}

void LauncherApp::refreshRecent() {
    recentEntries_.clear();
    std::string err;
    if (!hprojops::loadRecentProjects(userDir_, recentEntries_, err)) {
        recentStatus_ = err;
    } else {
        recentStatus_.clear();
    }
    dirty_ = true;
}

void LauncherApp::updateAutoPackageId() {
    if (packageIdManual_) return;
    editPackageId_.setText(hprojops::makeDefaultPackageId(editCompany_.text, editName_.text));
}

void LauncherApp::validateNewForm() {
    nameError_.clear();
    locationError_.clear();

    if (!hprojops::isValidProjectName(editName_.text)) {
        nameError_ = HTEXT("Launcher", "New.ErrorName",
                           "Project name must start with an ASCII letter and use only letters, digits, or '_'.").toString();
    }

    fs::path baseDir;
    if (editLocation_.text.empty() || !hpak::pathFromUtf8(editLocation_.text, baseDir)) {
        locationError_ = HTEXT("Launcher", "New.ErrorLocation", "Enter a valid destination folder.").toString();
    } else if (nameError_.empty()) {
        const fs::path targetDir = baseDir / editName_.text;
        std::error_code ec;
        if (fs::exists(targetDir, ec)) {
            if (!fs::is_directory(targetDir, ec) || !fs::is_empty(targetDir, ec)) {
                locationError_ = HTEXT("Launcher", "New.ErrorExists",
                                       "Target folder already exists and is not empty.").toString();
            }
        }
    }
}

void LauncherApp::setFocusedField(ENewField f) {
    if (focusedField_ == f) return;
    focusedField_ = f;
    if (host_ && host_->window()) {
        host_->window()->setTextInputEnabled(f != ENewField::None);
    }
    dirty_ = true;
}

HTextEditState* LauncherApp::activeEditState() {
    switch (focusedField_) {
        case ENewField::Location:  return &editLocation_;
        case ENewField::Name:      return &editName_;
        case ENewField::Company:   return &editCompany_;
        case ENewField::PackageId: return &editPackageId_;
        default:                   return nullptr;
    }
}

void LauncherApp::handleTextFieldKeys() {
    if (!host_ || !host_->window()) return;
    IWindow* win = host_->window();
    HTextEditState* st = activeEditState();
    if (!st) {
        std::vector<unsigned int> unused;
        win->drainCharQueue(unused);
        return;
    }

    bool changed = false;
    std::vector<unsigned int> chars;
    win->drainCharQueue(chars);
    for (unsigned int cp : chars) {
        if (st->insertCodepoint(cp)) changed = true;
    }

    std::string comp;
    size_t compCaret = 0;
    if (win->getComposition(comp, compCaret)) {
        if (st->composition != comp || st->compositionCaret != compCaret) {
            st->composition = std::move(comp);
            st->compositionCaret = compCaret;
            dirty_ = true;
        }
    } else if (!st->composition.empty()) {
        st->composition.clear();
        st->compositionCaret = 0;
        dirty_ = true;
    }

    const bool ctrl = win->isCtrlDown();
    const bool shift = win->isShiftDown();

    if (win->keyPressCount(HInput::KEY_BACKSPACE) > 0) {
        if (st->backspace(ctrl)) changed = true;
    }
    if (win->keyPressCount(HInput::KEY_DELETE) > 0) {
        if (st->del(ctrl)) changed = true;
    }
    if (win->keyPressCount(HInput::KEY_LEFT) > 0) {
        st->moveLeft(ctrl, shift);
        dirty_ = true;
    }
    if (win->keyPressCount(HInput::KEY_RIGHT) > 0) {
        st->moveRight(ctrl, shift);
        dirty_ = true;
    }
    if (win->keyPressCount(HInput::KEY_HOME) > 0) {
        st->home(shift);
        dirty_ = true;
    }
    if (win->keyPressCount(HInput::KEY_END) > 0) {
        st->end(shift);
        dirty_ = true;
    }
    if (ctrl && win->keyPressCount(HInput::KEY_A) > 0) {
        st->selectAll();
        dirty_ = true;
    }
    if (ctrl && win->keyPressCount(HInput::KEY_C) > 0 && st->hasSelection()) {
        win->setClipboardText(st->selectedText());
    }
    if (ctrl && win->keyPressCount(HInput::KEY_V) > 0) {
        if (st->insert(win->getClipboardText())) changed = true;
    }
    if (win->keyPressCount(HInput::KEY_ESCAPE) > 0) {
        setFocusedField(ENewField::None);
    }

    if (st->caretRect.z > 0.f) {
        win->setTextInputRect(st->caretRect.x, st->caretRect.y, st->caretRect.z, st->caretRect.w);
    }

    if (changed) {
        if (focusedField_ == ENewField::PackageId) {
            packageIdManual_ = !editPackageId_.text.empty();
        } else if (focusedField_ == ENewField::Name || focusedField_ == ENewField::Company) {
            updateAutoPackageId();
        }
        validateNewForm();
        dirty_ = true;
    }
}

void LauncherApp::handleInput(float /*dt*/) {
    if (!host_ || !host_->window()) return;
    IWindow* win = host_->window();
    HSlateContext& ctx = host_->slateContext();

    float mx = 0.f, my = 0.f;
    win->getMousePosition(mx, my);
    const bool mdown = win->isMouseButtonDown(0);
    const float scroll = win->getScrollDelta();
    const bool shift = win->isShiftDown();

    if (mx != prevMouseX_ || my != prevMouseY_) {
        HPointerEvent e;
        e.position = { mx, my };
        e.delta = { mx - prevMouseX_, my - prevMouseY_ };
        e.down = mdown;
        e.shift = shift;
        ctx.handlePointerEvent(e);
        prevMouseX_ = mx;
        prevMouseY_ = my;
    }

    if (mdown != prevMouseDown_) {
        HPointerEvent e;
        e.position = { mx, my };
        e.button = 0;
        e.down = mdown;
        e.shift = shift;
        if (mdown && page_ == ELauncherPage::New) {
            auto hit = [&](const std::shared_ptr<SEditableText>& w) {
                return w && w->cachedGeometry().contains({ mx, my });
            };
            if (!hit(wLocation_) && !hit(wName_) && !hit(wCompany_) && !hit(wPackageId_)) {
                setFocusedField(ENewField::None);
            }
        }
        if (ctx.handlePointerEvent(e).handled) {
            dirty_ = true;
        }
        prevMouseDown_ = mdown;
    }

    if (scroll != 0.f) {
        HPointerEvent e;
        e.position = { mx, my };
        e.wheelDelta = scroll;
        e.shift = shift;
        if (ctx.handlePointerEvent(e).handled) {
            dirty_ = true;
        }
    }

    handleTextFieldKeys();
}

void LauncherApp::tick(float dt) {
    if (!opts_.hiddenWindow) {
        handleInput(dt);
    }

    static size_t s_lastLogCount = 0;
    static EProgressStep s_lastStep = EProgressStep::Idle;
    {
        std::lock_guard<std::mutex> lock(progressMutex_);
        const EProgressStep curStep = progressStep_.load();
        if (progressLogs_.size() != s_lastLogCount || curStep != s_lastStep) {
            s_lastLogCount = progressLogs_.size();
            s_lastStep = curStep;
            dirty_ = true;
        }
    }

    if (dirty_) {
        rebuildUi();
        dirty_ = false;
    }
}

bool LauncherApp::isAsyncFinished() const {
    const EProgressStep s = progressStep_.load();
    return s == EProgressStep::Idle || s == EProgressStep::Done || s == EProgressStep::Failed;
}

void LauncherApp::startCreateProject() {
    validateNewForm();
    if (!nameError_.empty() || !locationError_.empty()) return;

    joinWorker();

    fs::path baseDir;
    hpak::pathFromUtf8(editLocation_.text, baseDir);
    const fs::path targetDir = baseDir / editName_.text;
    activeProjectDir_ = targetDir;

    hprojops::NewProjectOptions nopts;
    nopts.targetDir = targetDir;
    nopts.name = editName_.text;
    nopts.company = editCompany_.text.empty() ? "Haruka" : editCompany_.text;
    nopts.packageId = editPackageId_.text;
    nopts.templateName = (selectedTemplate_ < templates_.size()) ? templates_[selectedTemplate_].name : "Blank";
    nopts.engineRoot = opts_.engineRoot;

    {
        std::lock_guard<std::mutex> lock(progressMutex_);
        progressLogs_.clear();
        progressSummary_.clear();
    }
    progressStep_.store(EProgressStep::Copying);
    failedAtStep_.store(EProgressStep::Idle);
    progressBuildSelected_.store(buildAfterCreate_);
    page_ = ELauncherPage::Progress;
    setFocusedField(ENewField::None);
    dirty_ = true;

    hprojops::saveLastProjectLocation(userDir_, baseDir);

    const fs::path userDir = userDir_;
    const fs::path enginesIni = opts_.enginesIniOverride;
    const bool doBuild = buildAfterCreate_;

    worker_ = std::thread([this, nopts, userDir, enginesIni, doBuild]() {
        auto pushLog = [this](const std::string& line) {
            std::lock_guard<std::mutex> lock(progressMutex_);
            progressLogs_.push_back(line);
        };

        pushLog("[1/2] Creating project '" + nopts.name + "' from template '" + nopts.templateName + "'...");
        const hprojops::OpResult createRes = hprojops::createProjectFromTemplate(nopts);
        if (!createRes.ok) {
            pushLog("ERROR: " + createRes.message);
            if (!createRes.fixHint.empty()) pushLog("Hint: " + createRes.fixHint);
            std::lock_guard<std::mutex> lock(progressMutex_);
            progressSummary_ = createRes.message;
            failedAtStep_.store(EProgressStep::Copying);
            progressStep_.store(EProgressStep::Failed);
            return;
        }
        pushLog(createRes.message);
        hprojops::registerProject(nopts.targetDir, userDir, nullptr);

        progressStep_.store(EProgressStep::Generating);
        pushLog("[2/2] Generating Visual Studio solution...");
        const hprojops::OpResult genRes = hprojops::generateProjectFiles(
            nopts.targetDir, {}, enginesIni, pushLog);
        if (!genRes.ok) {
            pushLog("ERROR: " + genRes.message);
            if (!genRes.fixHint.empty()) pushLog("Hint: " + genRes.fixHint);
            std::lock_guard<std::mutex> lock(progressMutex_);
            progressSummary_ = genRes.message;
            failedAtStep_.store(EProgressStep::Generating);
            progressStep_.store(EProgressStep::Failed);
            return;
        }
        pushLog(genRes.message);

        if (doBuild) {
            progressStep_.store(EProgressStep::Building);
            pushLog("[3/3] Building Release configuration...");
            const hprojops::OpResult buildRes = hprojops::buildProjectFiles(nopts.targetDir, pushLog);
            if (!buildRes.ok) {
                pushLog("ERROR: " + buildRes.message);
                std::lock_guard<std::mutex> lock(progressMutex_);
                progressSummary_ = buildRes.message;
                failedAtStep_.store(EProgressStep::Building);
                progressStep_.store(EProgressStep::Failed);
                return;
            }
            pushLog(buildRes.message);
        }

        {
            std::lock_guard<std::mutex> lock(progressMutex_);
            progressSummary_ = HTEXT("Launcher", "Progress.Complete", "Project is ready.").toString();
        }
        progressStep_.store(EProgressStep::Done);
    });
}

void LauncherApp::startBuildAndOpen(const fs::path& projDir) {
    joinWorker();
    activeProjectDir_ = projDir;
    {
        std::lock_guard<std::mutex> lock(progressMutex_);
        progressLogs_.clear();
        progressSummary_.clear();
    }
    progressStep_.store(EProgressStep::Building);
    page_ = ELauncherPage::Progress;
    dirty_ = true;

    const fs::path userDir = userDir_;
    worker_ = std::thread([this, projDir, userDir]() {
        auto pushLog = [this](const std::string& line) {
            std::lock_guard<std::mutex> lock(progressMutex_);
            progressLogs_.push_back(line);
        };

        hprojops::OpenPlan plan;
        hprojops::planOpenProject(projDir, plan);
        if (plan.needsBuild) {
            pushLog("Building project before opening editor...");
            const hprojops::OpResult buildRes = hprojops::buildProjectFiles(projDir, pushLog);
            if (!buildRes.ok) {
                pushLog("ERROR: " + buildRes.message);
                std::lock_guard<std::mutex> lock(progressMutex_);
                progressSummary_ = buildRes.message;
                progressStep_.store(EProgressStep::Failed);
                return;
            }
            pushLog(buildRes.message);
        }

        const hprojops::OpResult openRes = hprojops::openProject(projDir, userDir, false);
        if (!openRes.ok) {
            pushLog("ERROR: " + openRes.message);
            std::lock_guard<std::mutex> lock(progressMutex_);
            progressSummary_ = openRes.message;
            progressStep_.store(EProgressStep::Failed);
            return;
        }
        pushLog(openRes.message);
        {
            std::lock_guard<std::mutex> lock(progressMutex_);
            progressSummary_ = openRes.message;
        }
        progressStep_.store(EProgressStep::Done);
    });
}

std::shared_ptr<HWidget> LauncherApp::buildHeader() {
    auto tabBtn = [&](const std::string& label, ELauncherPage target, bool enabled = true) {
        const bool active = (page_ == target);
        SButton::Style st = makeBtnStyle();
        if (active) {
            st.normal = HSlateTheme::selectRow;
            st.hovered = HSlateTheme::selectRowHover;
        }
        return makeButton(label, st, [this, target]() {
            page_ = target;
            if (target == ELauncherPage::Recent) refreshRecent();
            dirty_ = true;
        }, enabled, { 16.f, 8.f, 16.f, 8.f });
    };

    auto row = std::make_shared<SHorizontalBox>();
    row->addSlot(
        std::make_shared<STextBlock>()
            ->text(HTEXT("Launcher", "Title", "Haruka Launcher").toString())
            ->fontSize(17.f)
            ->bold(true)
            ->color(HSlateTheme::textPrimary),
        SHorizontalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 16.f, 0.f }, EVAlign::Center
    );
    row->addSlot(
        tabBtn(HTEXT("Launcher", "Tab.Recent", "Recent Projects").toString(), ELauncherPage::Recent),
        SHorizontalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 8.f, 0.f }
    );
    row->addSlot(
        tabBtn(HTEXT("Launcher", "Tab.New", "New Project").toString(), ELauncherPage::New),
        SHorizontalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 8.f, 0.f }
    );
    if (page_ == ELauncherPage::Progress || progressStep_.load() != EProgressStep::Idle) {
        row->addSlot(
            tabBtn(HTEXT("Launcher", "Tab.Progress", "Progress").toString(), ELauncherPage::Progress),
            SHorizontalBox::ESizeRule::Auto
        );
    }
    row->addSlot(std::make_shared<SBox>(), SHorizontalBox::ESizeRule::Fill, 1.f);

    return std::make_shared<SBorder>()
        ->background(HSlateTheme::bgWindow)
        ->borderColor(HSlateTheme::lineDivider)
        ->borderThickness(1.f)
        ->padding({ 16.f, 10.f, 16.f, 10.f })
        ->content(row);
}

std::shared_ptr<HWidget> LauncherApp::buildRecentPage() {
    auto list = std::make_shared<SVerticalBox>();

    if (recentEntries_.empty()) {
        list->addSlot(
            std::make_shared<STextBlock>()
                ->text(HTEXT("Launcher", "Recent.Empty", "No recent projects yet. Create a new project to get started.").toString())
                ->fontSize(14.f)
                ->color(HSlateTheme::textSecondary),
            SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 24.f, 0.f, 8.f }
        );
    }

    for (const auto& entry : recentEntries_) {
        fs::path projDir;
        std::error_code ec;
        const bool validPath = hpak::pathFromUtf8(entry.path, projDir) && fs::is_directory(projDir, ec);

        auto info = std::make_shared<SVerticalBox>();
        info->addSlot(
            std::make_shared<STextBlock>()
                ->text(entry.name + (validPath ? "" : ("  [" + HTEXT("Launcher", "Recent.Missing", "Missing").toString() + "]")))
                ->fontSize(15.f)
                ->bold(true)
                ->color(validPath ? HSlateTheme::textPrimary : HSlateTheme::textDisabled)
        );
        info->addSlot(
            std::make_shared<STextBlock>()
                ->text(entry.path)
                ->fontSize(12.f)
                ->color(validPath ? HSlateTheme::textSecondary : HSlateTheme::textDisabled),
            SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 3.f, 0.f, 0.f }
        );
        info->addSlot(
            std::make_shared<STextBlock>()
                ->text(entry.engine + "  |  " + hprojops::formatUtcIsoToLocalMinute(entry.lastOpened))
                ->fontSize(11.f)
                ->color(HSlateTheme::textDisabled),
            SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 2.f, 0.f, 0.f }
        );

        auto actions = std::make_shared<SHorizontalBox>();
        actions->addSlot(
            makeButton(HTEXT("Launcher", "Recent.Open", "Open").toString(), makeAccentBtnStyle(),
                       [this, projDir]() { startBuildAndOpen(projDir); }, validPath),
            SHorizontalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 6.f, 0.f }, EVAlign::Center
        );
        actions->addSlot(
            makeButton(HTEXT("Launcher", "Recent.OpenFolder", "Open Folder").toString(), makeBtnStyle(),
                       [projDir]() { hprojops::openFolderInExplorer(projDir); }, validPath),
            SHorizontalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 6.f, 0.f }, EVAlign::Center
        );
        actions->addSlot(
            makeButton(HTEXT("Launcher", "Recent.Remove", "Remove").toString(), makeBtnStyle(),
                       [this, projDir]() {
                           hprojops::removeRecentProject(projDir, userDir_);
                           refreshRecent();
                       }),
            SHorizontalBox::ESizeRule::Auto, 0.f, {}, EVAlign::Center
        );

        auto row = std::make_shared<SHorizontalBox>();
        row->addSlot(info, SHorizontalBox::ESizeRule::Fill, 1.f, {}, EVAlign::Center);
        row->addSlot(actions, SHorizontalBox::ESizeRule::Auto, 0.f, {}, EVAlign::Center);

        list->addSlot(
            std::make_shared<SBorder>()
                ->background(HSlateTheme::bgPanel)
                ->borderColor(HSlateTheme::lineDivider)
                ->borderThickness(1.f)
                ->cornerRadius(HSlateTheme::radiusPanel)
                ->padding({ 14.f, 12.f, 14.f, 12.f })
                ->content(row),
            SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 0.f, 8.f }
        );
    }

    auto scroll = std::make_shared<SScrollBox>();
    scroll->style(makeScrollStyle())
        ->content(list);
    scroll->setScrollOffset(recentScroll_);

    auto page = std::make_shared<SVerticalBox>();
    auto topBar = std::make_shared<SHorizontalBox>();
    topBar->addSlot(
        std::make_shared<STextBlock>()
            ->text(HTEXT("Launcher", "Recent.Heading", "Recent Projects").toString())
            ->fontSize(16.f)
            ->bold(true)
            ->color(HSlateTheme::textPrimary),
        SHorizontalBox::ESizeRule::Fill, 1.f, {}, EVAlign::Center
    );
    topBar->addSlot(
        makeButton(HTEXT("Launcher", "Recent.Refresh", "Refresh").toString(), makeBtnStyle(),
                   [this]() { refreshRecent(); }),
        SHorizontalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 8.f, 0.f }
    );
    topBar->addSlot(
        makeButton(HTEXT("Launcher", "Recent.NewProject", "New Project").toString(), makeAccentBtnStyle(),
                   [this]() { page_ = ELauncherPage::New; dirty_ = true; }),
        SHorizontalBox::ESizeRule::Auto
    );

    page->addSlot(topBar, SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 0.f, 12.f });
    if (!recentStatus_.empty()) {
        page->addSlot(
            std::make_shared<STextBlock>()->text(recentStatus_)->fontSize(12.f)->color(HSlateTheme::warn),
            SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 0.f, 8.f }
        );
    }
    page->addSlot(scroll, SVerticalBox::ESizeRule::Fill, 1.f);

    return std::make_shared<SBorder>()
        ->background(HSlateTheme::bgWindow)
        ->padding({ 20.f, 16.f, 20.f, 16.f })
        ->content(page);
}

std::shared_ptr<HWidget> LauncherApp::makeEditableRow(const std::string& label,
                                                      HTextEditState& state,
                                                      ENewField field,
                                                      const std::string& placeholder,
                                                      const std::string& errorMsg,
                                                      std::shared_ptr<HWidget> trailing) {
    auto ed = std::make_shared<SEditableText>();
    ed->state(&state)
        ->focused([this, field]() { return focusedField_ == field; })
        ->onFocus([this, field]() { setFocusedField(field); })
        ->placeholder(placeholder)
        ->fontSize(13.f)
        ->textHeight(20.f)
        ->style(makeEditStyle())
        ->focusable(true);

    switch (field) {
        case ENewField::Location:  wLocation_ = ed; break;
        case ENewField::Name:      wName_ = ed; break;
        case ENewField::Company:   wCompany_ = ed; break;
        case ENewField::PackageId: wPackageId_ = ed; break;
        default: break;
    }

    auto inputBox = std::make_shared<SBorder>()
        ->background(HSlateTheme::bgInput)
        ->borderColor(errorMsg.empty() ? (focusedField_ == field ? HSlateTheme::accent : HSlateTheme::lineDivider) : HSlateTheme::error)
        ->borderThickness(1.f)
        ->cornerRadius(HSlateTheme::radiusInput)
        ->padding({ 8.f, 5.f, 8.f, 5.f })
        ->content(ed);

    auto valRow = std::make_shared<SHorizontalBox>();
    valRow->addSlot(inputBox, SHorizontalBox::ESizeRule::Fill, 1.f, {}, EVAlign::Center);
    if (trailing) {
        valRow->addSlot(std::move(trailing), SHorizontalBox::ESizeRule::Auto, 0.f, { 8.f, 0.f, 0.f, 0.f }, EVAlign::Center);
    }

    auto col = std::make_shared<SVerticalBox>();
    col->addSlot(valRow);
    if (!errorMsg.empty()) {
        col->addSlot(
            std::make_shared<STextBlock>()
                ->text(errorMsg)
                ->fontSize(12.f)
                ->color(HSlateTheme::error),
            SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 4.f, 0.f, 0.f }
        );
    }

    auto row = std::make_shared<SHorizontalBox>();
    row->addSlot(
        std::make_shared<STextBlock>()
            ->text(label)
            ->fontSize(13.f)
            ->color(HSlateTheme::textSecondary),
        SHorizontalBox::ESizeRule::Fixed, 140.f, { 0.f, 6.f, 12.f, 0.f }, EVAlign::Top
    );
    row->addSlot(col, SHorizontalBox::ESizeRule::Fill, 1.f);
    return row;
}

std::shared_ptr<HWidget> LauncherApp::buildNewPage() {
    auto root = std::make_shared<SVerticalBox>();

    root->addSlot(
        std::make_shared<STextBlock>()
            ->text(HTEXT("Launcher", "New.Templates", "Templates").toString())
            ->fontSize(15.f)
            ->bold(true)
            ->color(HSlateTheme::textPrimary),
        SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 0.f, 12.f }
    );

    auto cardRow = std::make_shared<SHorizontalBox>();
    if (templates_.empty()) {
        cardRow->addSlot(
            std::make_shared<STextBlock>()
                ->text(HTEXT("Launcher", "New.NoTemplates", "No templates found under Templates/.").toString())
                ->fontSize(13.f)
                ->color(HSlateTheme::warn)
        );
    } else {
        for (size_t i = 0; i < templates_.size(); ++i) {
            const auto& tmpl = templates_[i];
            const bool sel = (i == selectedTemplate_);
            auto cardContent = std::make_shared<SVerticalBox>();
            cardContent->addSlot(
                std::make_shared<STextBlock>()
                    ->text(tmpl.name)
                    ->fontSize(14.f)
                    ->bold(true)
                    ->color(HSlateTheme::textPrimary)
            );
            cardContent->addSlot(
                std::make_shared<STextBlock>()
                    ->text(tmpl.description)
                    ->fontSize(12.f)
                    ->color(HSlateTheme::textSecondary),
                SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 4.f, 0.f, 0.f }
            );

            auto btn = std::make_shared<SButton>();
            btn->style({
                sel ? HSlateTheme::selectRow : HSlateTheme::bgPanel,
                sel ? HSlateTheme::selectRowHover : HSlateTheme::ctlHover,
                HSlateTheme::ctlPressed,
                HSlateTheme::radiusPanel,
                HSlateTheme::textDisabled
            })
            ->padding({ 14.f, 12.f, 14.f, 12.f })
            ->onClick([this, i]() {
                selectedTemplate_ = i;
                dirty_ = true;
            })
            ->content(cardContent);

            cardRow->addSlot(btn, SHorizontalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 10.f, 0.f });
        }
    }
    root->addSlot(cardRow, SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 0.f, 20.f });

    root->addSlot(
        std::make_shared<STextBlock>()
            ->text(HTEXT("Launcher", "New.Settings", "Project Settings").toString())
            ->fontSize(15.f)
            ->bold(true)
            ->color(HSlateTheme::textPrimary),
        SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 0.f, 12.f }
    );

    auto browseBtn = makeButton(
        HTEXT("Launcher", "New.Browse", "Browse...").toString(),
        makeBtnStyle(),
        [this]() {
            fs::path initDir;
            hpak::pathFromUtf8(editLocation_.text, initDir);
            const fs::path picked = hprojops::pickFolderDialog(initDir);
            if (!picked.empty()) {
                editLocation_.setText(hpak::utf8Of(picked));
                validateNewForm();
                dirty_ = true;
            }
        }
    );

    root->addSlot(
        makeEditableRow(HTEXT("Launcher", "New.Location", "Location").toString(),
                        editLocation_, ENewField::Location, "C:/Projects", locationError_, browseBtn),
        SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 0.f, 10.f }
    );
    root->addSlot(
        makeEditableRow(HTEXT("Launcher", "New.Name", "Project Name").toString(),
                        editName_, ENewField::Name, "MyHarukaGame", nameError_),
        SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 0.f, 10.f }
    );
    root->addSlot(
        makeEditableRow(HTEXT("Launcher", "New.Company", "Company").toString(),
                        editCompany_, ENewField::Company, "Haruka"),
        SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 0.f, 10.f }
    );
    root->addSlot(
        makeEditableRow(HTEXT("Launcher", "New.PackageId", "Package ID").toString(),
                        editPackageId_, ENewField::PackageId, "com.haruka.mygame"),
        SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 0.f, 14.f }
    );

    auto buildCheckRow = std::make_shared<SHorizontalBox>();
    auto chk = std::make_shared<SCheckBox>();
    chk->style({
        HSlateTheme::bgInput, HSlateTheme::borderPopup, HSlateTheme::accent,
        HSlateTheme::accentHover, HSlateTheme::textOnAccent, 16.f, 3.f, 1.f, "check", HSlateTheme::textDisabled
    })
    ->checked([this]() { return buildAfterCreate_; })
    ->onToggle([this](bool v) {
        buildAfterCreate_ = v;
        dirty_ = true;
    });
    buildCheckRow->addSlot(std::make_shared<SBox>(), SHorizontalBox::ESizeRule::Fixed, 152.f);
    buildCheckRow->addSlot(chk, SHorizontalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 8.f, 0.f }, EVAlign::Center);
    buildCheckRow->addSlot(
        std::make_shared<STextBlock>()
            ->text(HTEXT("Launcher", "New.BuildAfter", "Build Release after generating solution").toString())
            ->fontSize(13.f)
            ->color(HSlateTheme::textPrimary),
        SHorizontalBox::ESizeRule::Auto, 0.f, {}, EVAlign::Center
    );
    root->addSlot(buildCheckRow, SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 0.f, 20.f });

    const bool canCreate = nameError_.empty() && locationError_.empty() && !templates_.empty();
    auto footer = std::make_shared<SHorizontalBox>();
    footer->addSlot(std::make_shared<SBox>(), SHorizontalBox::ESizeRule::Fill, 1.f);
    footer->addSlot(
        makeButton(HTEXT("Launcher", "New.Create", "Create Project").toString(),
                   makeAccentBtnStyle(),
                   [this]() { startCreateProject(); },
                   canCreate, { 20.f, 9.f, 20.f, 9.f }),
        SHorizontalBox::ESizeRule::Auto
    );
    root->addSlot(footer);

    return std::make_shared<SBorder>()
        ->background(HSlateTheme::bgWindow)
        ->padding({ 20.f, 16.f, 20.f, 16.f })
        ->content(root);
}

std::shared_ptr<HWidget> LauncherApp::buildProgressPage() {
    const EProgressStep step = progressStep_.load();
    const EProgressStep failedAt = failedAtStep_.load();
    const bool buildSel = progressBuildSelected_.load();
    std::vector<std::string> logsCopy;
    std::string summaryCopy;
    {
        std::lock_guard<std::mutex> lock(progressMutex_);
        logsCopy = progressLogs_;
        summaryCopy = progressSummary_;
    }

    enum class EBadgeState { Pending, Active, Done, Skipped, Failed };
    auto stepBadge = [&](const std::string& label, EBadgeState st) {
        glm::vec4 bg = HSlateTheme::bgPanel;
        glm::vec4 fg = HSlateTheme::textPrimary;
        std::string text = label;
        switch (st) {
            case EBadgeState::Pending: bg = HSlateTheme::bgPanel; fg = HSlateTheme::textSecondary; break;
            case EBadgeState::Active:  bg = HSlateTheme::accent;  fg = HSlateTheme::textPrimary;   break;
            case EBadgeState::Done:    bg = HSlateTheme::ok;      fg = HSlateTheme::textPrimary;   break;
            case EBadgeState::Failed:  bg = HSlateTheme::error;   fg = HSlateTheme::textPrimary;   break;
            case EBadgeState::Skipped:
                bg = HSlateTheme::bgPanel;
                fg = HSlateTheme::textDisabled;
                text += HTEXT("Launcher", "Progress.SkippedSuffix", " (Skipped)").toString();
                break;
        }
        return std::make_shared<SBorder>()
            ->background(bg)
            ->cornerRadius(HSlateTheme::radiusControl)
            ->padding({ 12.f, 6.f, 12.f, 6.f })
            ->content(
                std::make_shared<STextBlock>()
                    ->text(text)
                    ->fontSize(12.f)
                    ->bold(st == EBadgeState::Active || st == EBadgeState::Failed)
                    ->color(fg)
            );
    };

    auto badgeFor = [&](int idx) -> EBadgeState {
        // idx: 1 = Copying, 2 = Generating, 3 = Building
        if (step == EProgressStep::Done) {
            if (idx == 3 && !buildSel) return EBadgeState::Skipped;
            return EBadgeState::Done;
        }
        if (step == EProgressStep::Failed) {
            const int failIdx = (failedAt == EProgressStep::Copying) ? 1 :
                                (failedAt == EProgressStep::Generating) ? 2 : 3;
            if (idx < failIdx) return EBadgeState::Done;
            if (idx == failIdx) return EBadgeState::Failed;
            return EBadgeState::Skipped;
        }
        const int curIdx = (step == EProgressStep::Copying) ? 1 :
                           (step == EProgressStep::Generating) ? 2 :
                           (step == EProgressStep::Building) ? 3 : 0;
        if (idx < curIdx) return EBadgeState::Done;
        if (idx == curIdx) return EBadgeState::Active;
        if (idx == 3 && !buildSel) return EBadgeState::Skipped;
        return EBadgeState::Pending;
    };

    auto stepsRow = std::make_shared<SHorizontalBox>();
    stepsRow->addSlot(
        stepBadge(HTEXT("Launcher", "Progress.StepCopy", "1. Copy Template").toString(), badgeFor(1)),
        SHorizontalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 8.f, 0.f }
    );
    stepsRow->addSlot(
        stepBadge(HTEXT("Launcher", "Progress.StepGen", "2. Generate Solution").toString(), badgeFor(2)),
        SHorizontalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 8.f, 0.f }
    );
    stepsRow->addSlot(
        stepBadge(HTEXT("Launcher", "Progress.StepBuild", "3. Build").toString(), badgeFor(3)),
        SHorizontalBox::ESizeRule::Auto
    );

    auto logCol = std::make_shared<SVerticalBox>();
    for (const auto& line : logsCopy) {
        logCol->addSlot(
            std::make_shared<STextBlock>()
                ->text(line)
                ->fontSize(12.f)
                ->color(HSlateTheme::textPrimary),
            SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 1.f, 0.f, 1.f }
        );
    }

    auto logScroll = std::make_shared<SScrollBox>();
    logScroll->style(makeScrollStyle())
        ->content(logCol);
    logScroll->setScrollOffset(progressScroll_);

    auto logFrame = std::make_shared<SBorder>()
        ->background(HSlateTheme::bgInput)
        ->borderColor(HSlateTheme::lineDivider)
        ->borderThickness(1.f)
        ->cornerRadius(HSlateTheme::radiusPanel)
        ->padding({ 10.f, 8.f, 10.f, 8.f })
        ->content(logScroll);

    const bool finished = (step == EProgressStep::Done || step == EProgressStep::Failed);
    const fs::path sln = hprojops::findProjectSolution(activeProjectDir_);

    auto actions = std::make_shared<SHorizontalBox>();
    actions->addSlot(
        makeButton(HTEXT("Launcher", "Progress.Back", "Back to Recent").toString(), makeBtnStyle(),
                   [this]() {
                       page_ = ELauncherPage::Recent;
                       refreshRecent();
                   }, finished),
        SHorizontalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 8.f, 0.f }
    );
    actions->addSlot(std::make_shared<SBox>(), SHorizontalBox::ESizeRule::Fill, 1.f);
    actions->addSlot(
        makeButton(HTEXT("Launcher", "Progress.OpenVS", "Open in Visual Studio").toString(), makeBtnStyle(),
                   [sln]() { hprojops::openInVisualStudio(sln); },
                   finished && !sln.empty()),
        SHorizontalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 8.f, 0.f }
    );
    actions->addSlot(
        makeButton(HTEXT("Launcher", "Progress.BuildOpen", "Build & Open Editor").toString(), makeAccentBtnStyle(),
                   [this]() { startBuildAndOpen(activeProjectDir_); },
                   finished && step == EProgressStep::Done && !activeProjectDir_.empty()),
        SHorizontalBox::ESizeRule::Auto
    );

    auto root = std::make_shared<SVerticalBox>();
    root->addSlot(stepsRow, SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 0.f, 12.f });
    if (!summaryCopy.empty()) {
        root->addSlot(
            std::make_shared<STextBlock>()
                ->text(summaryCopy)
                ->fontSize(13.f)
                ->bold(true)
                ->color(step == EProgressStep::Failed ? HSlateTheme::error : HSlateTheme::ok),
            SVerticalBox::ESizeRule::Auto, 0.f, { 0.f, 0.f, 0.f, 8.f }
        );
    }
    root->addSlot(logFrame, SVerticalBox::ESizeRule::Fill, 1.f, { 0.f, 0.f, 0.f, 12.f });
    root->addSlot(actions);

    return std::make_shared<SBorder>()
        ->background(HSlateTheme::bgWindow)
        ->padding({ 20.f, 16.f, 20.f, 16.f })
        ->content(root);
}

void LauncherApp::rebuildUi() {
    if (!host_) return;

    auto root = std::make_shared<SVerticalBox>();
    root->addSlot(buildHeader());

    std::shared_ptr<HWidget> body;
    switch (page_) {
        case ELauncherPage::Recent:   body = buildRecentPage(); break;
        case ELauncherPage::New:      body = buildNewPage(); break;
        case ELauncherPage::Progress: body = buildProgressPage(); break;
    }
    root->addSlot(std::move(body), SVerticalBox::ESizeRule::Fill, 1.f);

    host_->slateRenderer().setRootWidget(std::move(root));
}
