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
    uiSelect_           = options.uiSelect;
    uiOpenMenu_         = options.uiOpenMenu;
    uiBottomTab_        = options.uiBottomTab;
    uiLogCategories_    = options.uiLogCategories;
    uiOutlinerSearch_   = options.uiOutlinerSearch;
    uiRename_           = options.uiRename;
    uiOutlinerCollapse_ = options.uiOutlinerCollapse;
    uiPlay_             = options.uiPlay;
    uiLayout_           = options.uiLayout;
    viewportDumpPath_   = options.viewportDumpPath;
    uiGizmoMode_        = options.uiGizmoMode;
    uiLocalSpace_       = options.uiLocalSpace;
    uiPick_             = options.uiPick;
    dumpMode_           = !uiDumpPath_.empty() || !viewportDumpPath_.empty();
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
}
