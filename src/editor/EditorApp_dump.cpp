// 에디터 회귀 덤프(--ui-dump · --viewport-dump · --ui-*) 적용 및 출력(S6g).
#include "EditorApp.hpp"
#include "../runtime/EngineOptions.hpp"
#include "../platform/IWindow.hpp"
#include "../scene/Actor.hpp"
#include "../scene/World.hpp"
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
    uiBottomTab_        = options.uiBottomTab;
    uiLogCategories_    = options.uiLogCategories;
    uiOutlinerSearch_   = options.uiOutlinerSearch;
    uiConsole_          = options.uiConsole;
    uiConsoleRecall_    = options.uiConsoleRecall;
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
    switch (currentFocus()) {
        case EFocusTarget::Rename:    return &outlinerPanel_.renameEdit();
        case EFocusTarget::Search:    return &outlinerPanel_.searchEdit();
        case EFocusTarget::LogSearch: return &logPanel_.searchField().edit;
        case EFocusTarget::Console:   return &logPanel_.consoleEdit();
        case EFocusTarget::Dialog:    return &dialogEdit_;
        case EFocusTarget::Picker:    return &componentPicker_.edit();
        case EFocusTarget::ContentSearch: return &contentPanel_.searchEdit();
        default:                      return nullptr;
    }
}

void EditorApp::onRunFinished() {
    if (!dumpMode_ || !ctx_.window) return;
    if (!uiDumpPath_.empty()) {
        int fw = 1600, fh = 900;
        ctx_.window->getFramebufferSize(fw, fh);
        if (fw <= 0 || fh <= 0) { fw = 1600; fh = 900; }
        if (slateRenderer_.dumpElements(glm::vec2((float)fw, (float)fh), uiDumpPath_))
            HLOG(LogEditorDump, Log) << "UI dump written: " << uiDumpPath_;
        else
            HLOG(LogEditorDump, Warning) << "UI dump failed: " << uiDumpPath_;
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
            bottomDirty_ = true;
        }
    }
    if (uiLogCategories_) {
        if (bottomTab_ != 1) {
            bottomTab_ = 1;
            rebuildBottomPanel();
        }
        logPanel_.openPopup();
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
    if (!uiType_.empty() || uiCaretLeft_ > 0 || uiSelectLeft_ > 0 || !uiImeCompose_.empty()) {
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
            if (uiContentMenuSet_) {
                if (!uiContentMenu_.empty()) contentPanel_.select(uiContentMenu_);
                contextMenu_.open(glm::vec2{ 640.f, 640.f }, contentPanel_.contextItems(uiContentMenu_));
            }
        }
    }
    if (uiAddComponent_ && ctx_.selected) {
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
