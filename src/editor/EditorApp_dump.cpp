// 에디터 회귀 덤프(--ui-dump · --viewport-dump · --ui-*) 적용 및 출력(S6g).
#include "EditorApp.hpp"
#include "../runtime/EngineOptions.hpp"
#include "../platform/IWindow.hpp"
#include "../scene/Actor.hpp"
#include "../scene/World.hpp"
#include "../core/FileUtil.hpp"
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>

static constinit HLogCategory LogEditorDump{ "Editor", ELogVerbosity::Log };

void EditorApp::setOptions(const EngineOptions& options) {
    uiDumpPath_         = options.uiDumpPath;
    cultureOverride_    = options.culture;
    uiSelect_           = options.uiSelect;
    uiOpenMenu_         = options.uiOpenMenu;
    uiMenuKeys_.clear();
    uiMenuKeyNext_ = 0;
    for (size_t pos = 0; pos < options.uiMenuKeys.size();) {
        const size_t comma = options.uiMenuKeys.find(',', pos);
        const std::string name = options.uiMenuKeys.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        hmenu::Key k;
        const bool isAltTok = (name == "alt" || name == "space") ||
                              (name.size() == 5 && name.rfind("alt+", 0) == 0 && hmenu::isAsciiAlpha(name[4])) ||
                              (name.size() == 6 && name.rfind("char:", 0) == 0 && hmenu::isAsciiAlnum(name[5]));
        if (hmenu::parseKey(name, k) || isAltTok) uiMenuKeys_.push_back(name);
        else HLOG(LogEditorDump, Warning) << "--ui-menu-keys: unknown key '" << name << "'";
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    uiBottomTab_        = options.uiBottomTab;
    uiLogCategories_    = options.uiLogCategories;
    uiLogTime_          = options.uiLogTime;
    uiLogCollapse_      = options.uiLogCollapse;
    uiLogWrap_          = options.uiLogWrap;
    uiLogOptions_       = options.uiLogOptions;
    uiLogSample_        = options.uiLogSample;
    uiOutlinerSearch_   = options.uiOutlinerSearch;
    uiConsole_          = options.uiConsole;
    uiConsoleRecall_    = options.uiConsoleRecall;
    uiConsoleType_      = options.uiConsoleType;
    uiConsolePick_      = options.uiConsolePick;
    uiConsoleTab_       = options.uiConsoleTab;
    uiRename_           = options.uiRename;
    uiAssetActions_     = options.uiAssetActions;
    uiActorPicker_      = options.uiActorPicker;
    uiAddComponent_         = options.uiAddComponent;
    uiContentFolder_   = options.uiContentFolder;
    uiContentSearch_   = options.uiContentSearch;
    uiContentFilter_   = options.uiContentFilter;
    uiContentView_     = options.uiContentView;
    uiContentSelect_   = options.uiContentSelect;
    uiContentMenu_     = options.uiContentMenu;
    uiContentMenuSet_  = options.uiContentMenuSet;
    uiContentActivate_ = options.uiContentActivate;
    uiContentDrag_     = options.uiContentDrag;
    uiContentDrop_     = options.uiContentDrop;
    uiAddComponentSearch_   = options.uiAddComponentSearch;
    uiDetailsSet_           = options.uiDetailsSet;
    uiDetailsCombo_         = options.uiDetailsCombo;
    uiDetailsMenu_          = options.uiDetailsMenu;
    uiDetailsEdit_          = options.uiDetailsEdit;
    uiDetailsSplit_         = options.uiDetailsSplit;
    uiDetailsAsset_         = options.uiDetailsAsset;
    uiDetailsAssetSearch_   = options.uiDetailsAssetSearch;
    uiDetailsColor_         = options.uiDetailsColor;
    uiDetailsColorRgba_     = options.uiDetailsColorRgba;
    uiDetailsColorHex_      = options.uiDetailsColorHex;
    uiDetailsSearch_        = options.uiDetailsSearch;
    uiDetailsDrop_          = options.uiDetailsDrop;
    uiSaveSceneCopy_        = options.uiSaveSceneCopy;
    uiAddComponentCategory_ = options.uiAddComponentCategory;
    uiAddComponentDown_     = options.uiAddComponentDown;
    uiAddComponentCommit_   = options.uiAddComponentCommit;
    uiMoveAsset_        = options.uiMoveAsset;
    uiType_             = options.uiType;
    uiCaretLeft_        = options.uiCaretLeft;
    uiSelectLeft_       = options.uiSelectLeft;
    uiImeCompose_       = options.uiImeCompose;
    uiOutlinerCollapse_ = options.uiOutlinerCollapse;
    uiPlay_             = options.uiPlay;
    uiLayout_           = options.uiLayout;
    viewportDumpPath_   = options.viewportDumpPath;
    uiGizmoMode_        = options.uiGizmoMode;
    uiLocalSpace_       = options.uiLocalSpace;
    uiPick_             = options.uiPick;
    uiHover_            = options.uiHover;
    uiHoverActive_      = false;
    uiFloatCycle_       = options.uiFloatCycle ? 1 : 0;
    uiFloatInput_       = options.uiFloatInput;
    layoutStatsMode_    = options.uiLayoutStats;
    if (layoutStatsMode_ == 2) hslate::layoutCounters().cacheEnabled = false;
    floatsHidden_       = options.hiddenWindow;
    if (!uiHover_.empty()) {
        const size_t comma = uiHover_.find(',');
        if (comma != std::string::npos) {
            uiHoverPos_.x  = std::strtof(uiHover_.substr(0, comma).c_str(), nullptr);
            uiHoverPos_.y  = std::strtof(uiHover_.substr(comma + 1).c_str(), nullptr);
            uiHoverActive_ = true;
        }
    }
    dumpMode_           = !uiDumpPath_.empty() || !viewportDumpPath_.empty();
    autoReloadScene_    = options.autoReloadScene;
    touchAssetAt_.clear();
    for (const auto& t : options.touchAssetAt) touchAssetAt_.push_back({ t.frame, t.command });
    std::stable_sort(touchAssetAt_.begin(), touchAssetAt_.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
}

HTextEditState* EditorApp::focusedEdit() {
    // 글 칸은 등록 목록에서 찾습니다(H1a-2, 칸마다 줄을 더하지 않음). 대화상자는 따로입니다.
    if (currentFocus() == EFocusTarget::Dialog) return &dialogEdit_;
    return textInput_.focusedTextEdit();
}

// 배치 계측(H1b). tick 시작마다 직전 tick 이후의 차이를 한 프레임 값으로 셉니다(휠 · 입력 · 그리기 arrange 가 한 프레임).
void EditorApp::sampleLayoutStats() {
    const auto& st = slateRenderer_.context().layoutStats();
    const uint64_t m = hslate::layoutCounters().measures;
    if (lsFrames_ > 0 || lsPrevCalls_ > 0) {
        const uint64_t dc = st.arrangeCalls - lsPrevCalls_, dr = st.arrangeRuns - lsPrevRuns_, dm = m - lsPrevMeasures_;
        lsSumCalls_ += dc; lsSumRuns_ += dr; lsSumMeasures_ += dm;
        lsMaxCalls_ = std::max(lsMaxCalls_, dc); lsMaxRuns_ = std::max(lsMaxRuns_, dr); lsMaxMeasures_ = std::max(lsMaxMeasures_, dm);
        ++lsFrames_;
    }
    lsPrevCalls_ = st.arrangeCalls; lsPrevRuns_ = st.arrangeRuns; lsPrevMeasures_ = m;
    lsArrangeMs_ = st.arrangeMs;
}

void EditorApp::logLayoutStats() {
    if (!layoutStatsMode_ || lsFrames_ == 0) return;
    const double n = (double)lsFrames_;
    char buf[320];
    snprintf(buf, sizeof(buf),
             "UI layout stats (%s): frames %llu | arrange calls avg %.2f max %llu | arranged avg %.2f max %llu | "
             "measures avg %.1f max %llu | arrange ms total %.2f avg %.3f",
             layoutStatsMode_ == 2 ? "nocache" : "cache", (unsigned long long)lsFrames_,
             lsSumCalls_ / n, (unsigned long long)lsMaxCalls_, lsSumRuns_ / n, (unsigned long long)lsMaxRuns_,
             lsSumMeasures_ / n, (unsigned long long)lsMaxMeasures_, lsArrangeMs_, lsArrangeMs_ / n);
    HLOG(LogEditorDump, Log) << buf;
}

void EditorApp::onRunFinished() {
    logLayoutStats();
    if (!dumpMode_ || !ctx_.window) return;
    if (!uiDumpPath_.empty()) {
        int fw = 1600, fh = 900;
        ctx_.window->getFramebufferSize(fw, fh);
        if (fw <= 0 || fh <= 0) { fw = 1600; fh = 900; }
        if (slateRenderer_.dumpElements(glm::vec2((float)fw, (float)fh), uiDumpPath_))
            HLOG(LogEditorDump, Log) << "UI dump written: " << uiDumpPath_;
        else
            HLOG(LogEditorDump, Warning) << "UI dump failed: " << uiDumpPath_;
        // 떠 있는 창마다 그 문맥을 <덤프>.float<번호>.txt 로 남깁니다(M3a). 메인 덤프 파일은 떠 있는 창이 없을 때와 같은 형식입니다.
        for (const auto& w : floatWindows_.windows()) {
            if (!w->window || !w->ctx) continue;
            int aw = 0, ah = 0;
            w->window->getFramebufferSize(aw, ah);
            const std::string path = uiDumpPath_ + ".float" + std::to_string(w->floatId) + ".txt";
            if (aw > 0 && ah > 0 && slateRenderer_.dumpElements(*w->ctx, glm::vec2((float)aw, (float)ah), path))
                HLOG(LogEditorDump, Log) << "UI dump written: " << path;
            else
                HLOG(LogEditorDump, Warning) << "UI dump failed: " << path;
        }
    }
    if (!viewportDumpPath_.empty()) {
        std::ofstream vf(viewportDumpPath_);
        if (!vf) {
            HLOG(LogEditorDump, Warning) << "Viewport dump failed: " << viewportDumpPath_;
            return;
        }
        auto fmt = [](float x) -> float { return std::abs(x) < 0.0005f ? 0.f : x; };
        auto writeVerts = [&](const char* kind, const std::vector<LineVertex>& verts) {
            vf << kind << ' ' << verts.size() << '\n';
            char buf[128];
            for (const LineVertex& v : verts) {
                std::snprintf(buf, sizeof buf, "%.3f %.3f %.3f %.3f %.3f %.3f %.3f\n",
                              fmt(v.pos.x), fmt(v.pos.y), fmt(v.pos.z),
                              fmt(v.color.r), fmt(v.color.g), fmt(v.color.b), fmt(v.color.a));
                vf << buf;
            }
        };
        writeVerts("lines", lastEditorLines_);
        writeVerts("tris",  lastGizmoTris_);
    }
}

void EditorApp::applyDumpState() {
    if (!uiSelect_.empty() && ctx_.world) {
        bool first = true;
        size_t pos = 0;
        while (pos <= uiSelect_.size()) {
            const size_t comma = uiSelect_.find(',', pos);
            const std::string name = comma == std::string::npos
                ? uiSelect_.substr(pos)
                : uiSelect_.substr(pos, comma - pos);
            if (!name.empty()) {
                for (const auto& a : ctx_.world->getActors()) {
                    if (a->name == name) {
                        if (first) { selectOnly(a.get()); first = false; }
                        else       { addSelect(a.get()); }
                        break;
                    }
                }
            }
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    }
    if (!uiOpenMenu_.empty()) menus_.openByName(uiOpenMenu_);
    if (!uiBottomTab_.empty()) {
        int tab = -1;
        if      (uiBottomTab_ == "Content Browser" || uiBottomTab_ == "content") tab = 0;
        else if (uiBottomTab_ == "Output Log"      || uiBottomTab_ == "log")     tab = 1;
        else if (uiBottomTab_ == "GPU Stats"       || uiBottomTab_ == "gpu")     tab = 2;
        if (tab >= 0 && tab != bottomTab_) {
            bottomTab_   = tab;
            requestBottomRebuild();
        }
    }
    if (uiLogCategories_) {
        if (bottomTab_ != 1) {
            bottomTab_ = 1;
            rebuildBottomPanel();
        }
        logPanel_.openPopup();
    }
    if (!uiLogTime_.empty() || uiLogCollapse_ || uiLogWrap_ || uiLogOptions_ || uiLogSample_) {
        if (bottomTab_ != 1) {
            bottomTab_ = 1;
            rebuildBottomPanel();
        }
        if (uiLogSample_) {
            if (uiLogWrap_) {
                const auto t0 = std::chrono::steady_clock::now();
                size_t totalLines = 0;
                const std::string sampleEn = "Long English message for Output Log word wrap verification: the quick brown fox jumps over the lazy dog 0123456789.";
                const std::string sampleKo = "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4 \xEA\xB8\xB4 \xEC\xA4\x84 \xEC\xA4\x84 \xEB\xB0\x94\xEA\xBF\x88 \xED\x99\x95\xEC\x9D\xB8: \xEC\xB6\x9C\xEB\xA0\xA5 \xEB\xA1\x9C\xEA\xB7\xB8 \xED\x8C\xA8\xEB\x84\x90 \xED\x8F\xAD\xEC\x97\x90 \xEB\xA7\x9E\xEC\xB6\xB0 \xEC\x97\xAC\xEB\x9F\xAC \xEC\xA4\x84\xEB\xA1\x9C \xEB\x82\x98\xEB\x89\x98\xEB\x8A\x94\xEC\xA7\x80 \xEC\xB8\xA1\xEC\xA0\x95\xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4.";
                auto measureFn = [](std::string_view sv) { return EditorUiKit::textWidth(std::string(sv), 12.f); };
                for (int i = 0; i < 2048; ++i) {
                    const std::string& s = (i & 1) ? sampleKo : sampleEn;
                    totalLines += hlogview::wrapLogText(s, 420.f, measureFn).size();
                }
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                HLOG(LogEditorDump, Log) << "WordWrap 2048-row bench: " << ms << " ms (lines=" << totalLines << ")";
            }
            logPanel_.clear();
            logPanel_.setSampleMode(true);
            for (int i = 0; i < 3; ++i) HLOG(LogEditorDump, Log) << "Sample repeated log line";
            for (int i = 0; i < 2; ++i) HLOG(LogEditorDump, Warning) << "Sample repeated warning";
            HLOG(LogEditorDump, Error) << "Sample error line";
            HLOG(LogEditorDump, Log) << "Long English message for Output Log word wrap verification: the quick brown fox jumps over the lazy dog 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ and even more words to wrap across multiple lines cleanly.";
            HLOG(LogEditorDump, Log) << "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4 \xEA\xB8\xB4 \xEC\xA4\x84 \xEC\xA4\x84 \xEB\xB0\x94\xEA\xBF\x88 \xED\x99\x95\xEC\x9D\xB8: \xEC\xB6\x9C\xEB\xA0\xA5 \xEB\xA1\x9C\xEA\xB7\xB8 \xED\x8C\xA8\xEB\x84\x90\xEC\x9D\x98 \xEA\xB8\x80 \xEC\x97\xB4 \xED\x8F\xAD\xEC\x97\x90 \xEB\xA7\x9E\xEC\xB6\xB0 \xEC\x97\xAC\xEB\x9F\xAC \xEC\xA4\x84\xEB\xA1\x9C \xEB\x82\x98\xEB\x89\x98\xEA\xB3\xA0 \xED\x96\x89 \xEB\x86\x92\xEC\x9D\xB4\xEA\xB0\x80 \xED\x95\xA8\xEA\xBB\x98 \xEB\x8A\x98\xEC\x96\xB4\xEB\x82\x98\xEB\x8A\x94\xEC\xA7\x80 \xEA\xB8\xB4 \xEB\xAC\xB8\xEC\x9E\xA5\xEC\x9C\xBC\xEB\xA1\x9C \xEC\x9E\xAC\xED\x99\x95\xEC\x9D\xB8\xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4.";
            HLOG(LogEditorDump, Log) << "Line one with newline\nLine two after newline";
        }
        if (!uiLogTime_.empty()) {
            ELogTimeMode tm = ELogTimeMode::Uptime;
            if (hlogview::parseTimeMode(uiLogTime_, tm)) logPanel_.setTimeMode(tm);
        }
        if (uiLogCollapse_) logPanel_.setCollapse(true);
        if (uiLogWrap_)     logPanel_.setWordWrap(true);
        if (uiLogOptions_)  logPanel_.openOptionsMenu();
    }
    if (!uiOutlinerSearch_.empty()) outlinerPanel_.setSearchText(uiOutlinerSearch_);
    // 콘솔 줄 흉내(D9): Output Log 탭을 열고 명령을 차례로 Enter 한 것과 같이 실행한 뒤, 칸에 포커스를 둡니다. recall 은 위 화살표 횟수.
    if (!uiConsole_.empty() || uiConsoleRecall_ > 0) {
        if (bottomTab_ != 1) {
            bottomTab_ = 1;
            rebuildBottomPanel();
        }
        setTextFocus(EFocusTarget::Console);
        for (const std::string& cmd : uiConsole_) {
            logPanel_.consoleEdit().setText(cmd);
            logPanel_.submitConsole();
        }
        for (int i = 0; i < uiConsoleRecall_; ++i) logPanel_.stepHistory(-1);
    }
    if (!uiConsoleType_.empty()) {
        if (bottomTab_ != 1) {
            bottomTab_ = 1;
            rebuildBottomPanel();
        }
        setTextFocus(EFocusTarget::Console);
        logPanel_.setConsoleTextForDump(uiConsoleType_);
        if (uiConsolePick_ >= 0) logPanel_.pickCompletionForDump(uiConsolePick_);
        if (uiConsoleTab_)       logPanel_.tabCompletionForDump();
    }
    if (!uiOutlinerCollapse_.empty() && ctx_.world) {
        for (const auto& ap : ctx_.world->getActors()) {
            if (ap && ap->name == uiOutlinerCollapse_) {
                outlinerPanel_.collapseActor(ap->id);
                break;
            }
        }
    }
    if (!uiRename_.empty() && ctx_.world) {
        for (const auto& ap : ctx_.world->getActors()) {
            if (ap && ap->name == uiRename_) {
                selectOnly(ap.get());
                syncEulerCache();
                detailsPanel_.rebuild();
                prevSelected2_ = prevSelected_;
                prevSelected_  = ctx_.selected;
                setTextFocus(EFocusTarget::Rename);
                break;
            }
        }
    }
    // 글 칸 입력 흉내(글자 체계 T1 · T2). 숨은 창은 IME 를 켜지 않으므로 조합 글은 입력 처리기가 지우지 않습니다.
    if (uiDetailsEdit_.empty() && (!uiType_.empty() || uiCaretLeft_ > 0 || uiSelectLeft_ > 0 || !uiImeCompose_.empty())) {
        if (!focusedEdit()) setTextFocus(EFocusTarget::Search);
        if (HTextEditState* st = focusedEdit()) {
            if (!uiType_.empty()) st->insert(uiType_);
            for (int i = 0; i < uiCaretLeft_; ++i)  st->moveLeft(false, false);
            for (int i = 0; i < uiSelectLeft_; ++i) st->moveLeft(false, true);
            if (!uiImeCompose_.empty()) st->setComposition(uiImeCompose_, uiImeCompose_.size());
            outlinerPanel_.markDirty();
        }
    }
    if (!uiMoveAsset_.empty()) {
        const size_t bar = uiMoveAsset_.find('|');
        moveAsset(uiMoveAsset_.substr(0, bar), uiMoveAsset_.substr(bar + 1));
    }
    if (!uiAssetActions_.empty()) openAssetActions(uiAssetActions_);
    {
        const bool any = !uiContentFolder_.empty() || !uiContentSearch_.empty() || !uiContentFilter_.empty() ||
                         !uiContentView_.empty() || !uiContentSelect_.empty() || uiContentMenuSet_ || !uiContentActivate_.empty() ||
                         !uiContentDrag_.empty() || !uiContentDrop_.empty();
        if (any) {
            if (bottomTab_ != 0) { bottomTab_ = 0; rebuildBottomPanel(); }
            if (!uiContentFolder_.empty()) contentPanel_.navigate(uiContentFolder_);
            if (!uiContentView_.empty()) contentPanel_.setTileView(uiContentView_ != "list");
            if (!uiContentSearch_.empty()) contentPanel_.setSearch(uiContentSearch_);
            for (size_t pos = 0; pos < uiContentFilter_.size();) {
                const size_t comma = uiContentFilter_.find(',', pos);
                const std::string name = uiContentFilter_.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                for (hcb::AssetKind k : hcb::filterKinds())
                    if (name == hcb::kindName(k)) contentPanel_.toggleFilter(k);
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
            if (!uiContentSelect_.empty()) contentPanel_.select(uiContentSelect_);
            if (!uiContentActivate_.empty()) contentPanel_.activate(uiContentActivate_);
            // <경로>@x,y 를 나눕니다(창 좌표).
            auto splitAt = [](const std::string& s, std::string& path, glm::vec2& at) {
                const size_t atPos = s.rfind('@');
                const size_t comma = s.find(',', atPos == std::string::npos ? 0 : atPos);
                if (atPos == std::string::npos || comma == std::string::npos) return false;
                path = s.substr(0, atPos);
                at.x = std::strtof(s.substr(atPos + 1, comma - atPos - 1).c_str(), nullptr);
                at.y = std::strtof(s.substr(comma + 1).c_str(), nullptr);
                return true;
            };
            std::string path;
            glm::vec2 at{ 0.f };
            if (!uiContentDrag_.empty() && splitAt(uiContentDrag_, path, at)) {
                contentPanel_.forceDrag(path);
                dumpDrag_      = true;
                uiHoverActive_ = true;   // 마우스 자리를 그 좌표로(handleInput)
                uiHoverPos_    = at;
            }
            // 놓기는 ';' 로 여럿(차례로). 앞의 놓기가 만든 액터 위에 다음을 놓아 볼 수 있습니다.
            for (size_t pos = 0; ctx_.window && pos < uiContentDrop_.size();) {
                const size_t semi = uiContentDrop_.find(';', pos);
                const std::string one = uiContentDrop_.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
                pos = semi == std::string::npos ? uiContentDrop_.size() : semi + 1;
                if (!splitAt(one, path, at)) continue;
                int w = 0, h = 0;
                ctx_.window->getWindowSize(w, h);
                glm::vec3 point{ 0.f };
                Actor* hit = nullptr;
                if (contentPanel_.canDropInViewport(path) && viewport_.dropPoint(at.x, at.y, (float)w, (float)h, point, &hit))
                    contentPanel_.dropInViewport(path, point, hit);
                else
                    HLOG(LogEditorDump, Warning) << "ui-content-drop: cannot drop " << path << " at " << at.x << "," << at.y;
            }
            // 오른쪽 누름 메뉴는 콘텐츠 브라우저가 있는 창의 팝업 층에 뜹니다(H2a, 자리는 그 창 좌표).
            if (uiContentMenuSet_) {
                if (!uiContentMenu_.empty()) contentPanel_.select(uiContentMenu_);
                contextMenu_.open(glm::vec2{ 640.f, 640.f }, contentPanel_.contextItems(uiContentMenu_));
            }
        }
    }
    if (uiAddComponent_ && ctx_.selected) {   // 팝업은 디테일이 있는 창의 팝업 층(H2a)
        // 디테일의 Add Component 단추를 누른 것과 같습니다. 단추 자리는 다음 배치 뒤에 정해지므로 팝업은 그때 그려집니다.
        detailsPanel_.rebuild();
        componentPicker_.open(ctx_.selected, detailsPanel_.addComponentButton());
        if (!uiAddComponentCategory_.empty()) componentPicker_.enterCategory(uiAddComponentCategory_);
        if (!uiAddComponentSearch_.empty()) componentPicker_.setQuery(uiAddComponentSearch_);
        for (int i = 0; i < uiAddComponentDown_; ++i) componentPicker_.moveHighlight(+1);
        if (uiAddComponentCommit_) componentPicker_.activateHighlighted();
    }
    if (uiActorPicker_ && ctx_.selected) {
        // 선택한 액터의 첫 액터 참조 칸(디테일 패널에서 그 칸을 누른 것과 같음).
        bool opened = false;
        for (const auto& c : ctx_.selected->getComponents()) {
            for (const PropDesc& pd : c->buildProps()) {
                if (pd.kind != PropDesc::ActorRef || !pd.getG) continue;
                openActorPicker(pd.label ? pd.label : "", pd.getG(), [](uint64_t) {});
                opened = true;
                break;
            }
            if (opened) break;
        }
    }
    if (uiPlay_ && playState_ == EPlayState::Editor) beginPlay();
    if (!uiGizmoMode_.empty()) {
        if      (uiGizmoMode_ == "select") gizmoMode_ = 0;
        else if (uiGizmoMode_ == "move")   gizmoMode_ = 1;
        else if (uiGizmoMode_ == "rotate") gizmoMode_ = 2;
        else if (uiGizmoMode_ == "scale")  gizmoMode_ = 3;
    }
    if (uiLocalSpace_) worldSpace_ = false;
    if (!uiPick_.empty() && ctx_.window) {
        const size_t comma = uiPick_.find(',');
        if (comma != std::string::npos) {
            const float px = std::strtof(uiPick_.substr(0, comma).c_str(), nullptr);
            const float py = std::strtof(uiPick_.substr(comma + 1).c_str(), nullptr);
            int wW = 1600, wH = 900;
            ctx_.window->getWindowSize(wW, wH);
            viewport_.pickActor(px, py, (float)wW, (float)wH);
        }
    }
    if (uiHoverActive_ && ctx_.window) {
        int fw = 1600, fh = 900;
        ctx_.window->getFramebufferSize(fw, fh);
        slateRenderer_.arrange({ (float)fw, (float)fh });
        HPointerEvent he;
        he.position = uiHoverPos_;
        he.button   = -1;
        he.down     = false;
        slateRenderer_.handlePointerEvent(he);
        slateRenderer_.context().tickTooltip(1.0f);
    }
}

// 디테일 속성 행 덤프(H3a). 선택을 바꾼 다음 프레임에 디테일이 다시 만들어지므로(tickSelectionSync) 그 뒤 프레임에 합니다. 그 전에 열면
// 다시 만들 때 디테일이 연 메뉴 · 콤보가 닫힙니다(DetailsPanel::rebuild).
void EditorApp::applyDetailsDumpState() {
    // 디테일 속성 행(H3a): 값 넣기(되돌리기 화살표) · 열 나눔 · 콤보 · 행 메뉴 · 글 칸. 행은 지금 디테일을 다시 만든 뒤 이름으로 찾습니다.
    const bool h3b = !uiDetailsAsset_.empty() || !uiDetailsColor_.empty() || !uiDetailsSearch_.empty() || !uiDetailsDrop_.empty();
    if (ctx_.selected && (!uiDetailsSet_.empty() || !uiDetailsCombo_.empty() || !uiDetailsMenu_.empty() || !uiDetailsEdit_.empty() ||
                          uiDetailsSplit_ >= 0.f || h3b)) {
        // 속성 검색(H3b)은 칸 글을 넣은 것과 같습니다(목록을 다시 만들면 맞는 행만).
        if (!uiDetailsSearch_.empty()) detailsPanel_.setSearch(uiDetailsSearch_);
        detailsPanel_.rebuild();
        if (uiDetailsSplit_ >= 0.f) detailsPanel_.columns()->nameFraction = std::clamp(uiDetailsSplit_, 0.f, 1.f);
        for (size_t pos = 0; pos < uiDetailsSet_.size();) {
            const size_t semi = uiDetailsSet_.find(';', pos);
            const std::string one = uiDetailsSet_.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
            pos = semi == std::string::npos ? uiDetailsSet_.size() : semi + 1;
            const size_t eq = one.find('=');
            if (eq == std::string::npos) continue;
            if (!detailsPanel_.dumpSetValue(one.substr(0, eq), one.substr(eq + 1)))
                HLOG(LogEditorDump, Warning) << "--ui-details-set: no row or bad value '" << one << "'";
            detailsPanel_.rebuild();   // 토글 · 열거가 행 목록을 바꿀 수 있음
        }
        syncEulerCache();
        detailsPanel_.rebuild();
        // 행 자리가 정해져야 메뉴를 그 행 위에 엽니다(다시 만든 위젯은 아직 배치 전, 디테일이 메인에 있을 때).
        if (ctx_.window && dock_.floatOfPanel("Details") == 0) {
            int fw = 1600, fh = 900;
            ctx_.window->getFramebufferSize(fw, fh);
            slateRenderer_.arrange({ (float)fw, (float)fh });
        }
        if (!uiDetailsCombo_.empty() && !detailsPanel_.dumpOpenCombo(uiDetailsCombo_))
            HLOG(LogEditorDump, Warning) << "--ui-details-combo: no enum row '" << uiDetailsCombo_ << "'";
        if (!uiDetailsMenu_.empty() && !detailsPanel_.dumpOpenRowMenu(uiDetailsMenu_))
            HLOG(LogEditorDump, Warning) << "--ui-details-menu: no row '" << uiDetailsMenu_ << "'";
        if (!uiDetailsEdit_.empty()) {
            if (!detailsPanel_.dumpBeginEdit(uiDetailsEdit_))
                HLOG(LogEditorDump, Warning) << "--ui-details-edit: no text row '" << uiDetailsEdit_ << "'";
            else if (HTextEditState* st = focusedEdit()) {   // 글자 흉내(--ui-type · --ui-caret-left)는 이 칸에
                if (!uiType_.empty()) st->insert(uiType_);
                for (int i = 0; i < uiCaretLeft_; ++i) st->moveLeft(false, false);
            }
            detailsPanel_.rebuild();
        }
        // H3b: 에셋 칸에 끌어 놓기(행 가운데) · 고르기 팝업 · 색 선택기. 행 자리는 위 배치를 씁니다.
        if (!uiDetailsDrop_.empty()) {
            const size_t eq = uiDetailsDrop_.find('=');
            const std::string row = uiDetailsDrop_.substr(0, eq), path = eq == std::string::npos ? std::string() : uiDetailsDrop_.substr(eq + 1);
            if (!detailsPanel_.dumpDropAsset(row, path))
                HLOG(LogEditorDump, Warning) << "--ui-details-drop: rejected '" << path << "' on row '" << row << "'";
            else
                HLOG(LogEditorDump, Log) << "--ui-details-drop: dropped '" << path << "' on row '" << row << "'";
            detailsPanel_.rebuild();
            if (ctx_.window && dock_.floatOfPanel("Details") == 0) {
                int fw = 1600, fh = 900;
                ctx_.window->getFramebufferSize(fw, fh);
                slateRenderer_.arrange({ (float)fw, (float)fh });
            }
        }
        if (!uiDetailsAsset_.empty()) {
            if (!detailsPanel_.dumpOpenAssetPicker(uiDetailsAsset_))
                HLOG(LogEditorDump, Warning) << "--ui-details-asset: no asset row '" << uiDetailsAsset_ << "'";
            else if (!uiDetailsAssetSearch_.empty())
                assetPicker_.setQuery(uiDetailsAssetSearch_);
        }
        if (!uiDetailsColor_.empty()) {
            if (!detailsPanel_.dumpOpenColorPicker(uiDetailsColor_, uiDetailsColorRgba_))
                HLOG(LogEditorDump, Warning) << "--ui-details-color: no color row '" << uiDetailsColor_ << "'";
            else if (!uiDetailsColorHex_.empty() && !colorPicker_.setHex(uiDetailsColorHex_))
                HLOG(LogEditorDump, Warning) << "--ui-details-color-hex: bad value '" << uiDetailsColorHex_ << "'";
        }
    }
    // 씬 글을 다른 파일로(저장과 같은 글 = serializeWorld, 문서 · 원본 파일은 건드리지 않음). 열기 → 저장이 바이트 단위로 같은지 봅니다.
    if (!uiSaveSceneCopy_.empty()) {
        std::string err;
        if (hfile::writeAtomic(std::filesystem::path(uiSaveSceneCopy_), serializeWorld(), &err))
            HLOG(LogEditorDump, Log) << "--ui-save-scene-copy: wrote " << uiSaveSceneCopy_;
        else
            HLOG(LogEditorDump, Error) << "--ui-save-scene-copy: " << err;
    }
}
