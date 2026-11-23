#include "ContentBrowserPanel.hpp"
#include "../vfs/LogicalPath.hpp"
#include "EditorCommands.hpp"
#include "EditorTheme.hpp"
#include "../assets/AssetManager.hpp"
#include "../audio/AudioComponent.hpp"
#include "../components/StaticMeshComponent.hpp"
#include "../core/HText.hpp"
#include "../core/ProjectPaths.hpp"
#include "../platform/IWindow.hpp"
#include "../scene/Actor.hpp"
#include "../scene/World.hpp"
#include "../sceneio/PrefabManager.hpp"
#include "../vfs/PathUtf8.hpp"
#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;
using hcb::AssetKind;

namespace {

constexpr float kTreeW   = 200.f;
constexpr float kTileW   = 88.f;
constexpr float kTileH   = 84.f;
constexpr float kTileGap = 6.f;
constexpr double kDoubleClickSec = 0.45;

glm::vec4 kindColor(AssetKind k) {
    switch (k) {
        case AssetKind::Folder:  return EditorTheme::srgb(0xC9A44A);
        case AssetKind::Scene:   return EditorTheme::badgeScene;
        case AssetKind::Model:   return EditorTheme::badgeModel;
        case AssetKind::Audio:   return EditorTheme::badgeAudio;
        case AssetKind::Cue:     return EditorTheme::srgb(0xD98A3A);
        case AssetKind::Prefab:  return EditorTheme::badgePrefab;
        case AssetKind::Nav:     return EditorTheme::srgb(0x3FB6A8);
        case AssetKind::Texture: return EditorTheme::srgb(0xB06FD8);
        default:                 return EditorTheme::textSecondary;
    }
}

const char* kindIcon(AssetKind k) {
    switch (k) {
        case AssetKind::Folder:  return "folder";
        case AssetKind::Scene:   return "film";
        case AssetKind::Model:   return "box";
        case AssetKind::Audio:   return "music";
        case AssetKind::Cue:     return "volume-2";
        case AssetKind::Prefab:  return "package";
        case AssetKind::Nav:     return "map";
        case AssetKind::Texture: return "image";
        default:                 return "file";
    }
}

// 아이콘 파일이 없을 때 그릴 글자(예전 목록의 배지와 같은 모양).
const char* kindFallback(AssetKind k) {
    switch (k) {
        case AssetKind::Folder:  return "[D]";
        case AssetKind::Scene:   return "[S]";
        case AssetKind::Model:   return "[M]";
        case AssetKind::Audio:   return "[A]";
        case AssetKind::Cue:     return "[C]";
        case AssetKind::Prefab:  return "[P]";
        case AssetKind::Nav:     return "[N]";
        case AssetKind::Texture: return "[T]";
        default:                 return "[?]";
    }
}

} // namespace

// 디테일 에셋 칸 · 고르기 팝업(H3b)도 같은 종류 아이콘 · 색을 씁니다.
namespace hcbui {
glm::vec4   kindColor(hcb::AssetKind k)    { return ::kindColor(k); }
const char* kindIcon(hcb::AssetKind k)     { return ::kindIcon(k); }
const char* kindFallback(hcb::AssetKind k) { return ::kindFallback(k); }
} // namespace hcbui

namespace {

// 콘텐츠 브라우저에서 소리 파일을 지정(2026-10-15). setFile 이 이전 소리를 놓아 주므로 값 문자열을 다시 읽는
// 일반 경로(readField) 대신 이것으로 되돌립니다. 액터는 ID 로 찾습니다(EditorCommands.hpp 머리말).
struct AudioFileCmd : ICommand {
    World* world; uint64_t id; std::string before, after;
    AudioFileCmd(World& w, const Actor* a, std::string b, std::string af)
        : world(&w), id(a->id), before(std::move(b)), after(std::move(af)) {}
    void set(const std::string& f) {
        if (Actor* a = world->findById(id))
            if (auto* ac = a->getComponent<AudioComponent>()) ac->setFile(f);
    }
    void undo() override { set(before); }
    void redo() override { set(after); }
};

} // namespace

void ContentBrowserPanel::init(Deps deps) {
    deps_ = std::move(deps);
}

// ── 훑기 ──────────────────────────────────────────────────────────────────────────
void ContentBrowserPanel::scanAll() {
    // 프로젝트 Content 폴더 전체를 훑습니다. 이름은 UTF-8 로 다룹니다(fs::path(std::string) 은 코드 페이지로 해석해 한글이 깨짐).
    std::vector<std::string> files, dirs;
    std::error_code ec;
    const fs::path root = hproj::root() / "Content";   // /Game 마운트 자체(resolve 는 /Game/... 만 바꿈)
    if (fs::exists(root, ec)) {
        for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            const std::string rel = hpak::utf8Of(fs::relative(it->path(), root, ec));
            if (ec || rel.empty()) { ec.clear(); continue; }
            std::string relFwd = rel;
            std::replace(relFwd.begin(), relFwd.end(), char(92), '/');
            if (it->is_directory(ec)) {
                if (hcb::hiddenName(hcb::nameOf(relFwd))) it.disable_recursion_pending();
                else dirs.push_back(relFwd);
            } else if (it->is_regular_file(ec)) {
                files.push_back(relFwd);
            }
            ec.clear();
        }
    }
    index_.build(files, dirs);
    sceneFiles_ = index_.pathsOfKind(AssetKind::Scene);
    // 지금 폴더가 사라졌으면(바깥에서 지움 · 옮김) 가장 가까운 남은 상위 폴더로 갑니다.
    while (folder_ != hcb::kRoot && !index_.hasFolder(folder_)) folder_ = hcb::parentOf(folder_);
    if (!selected_.empty() && !index_.find(selected_) && !index_.hasFolder(selected_)) selected_.clear();
    markAll();
}

// ── 탭 짓기 ───────────────────────────────────────────────────────────────────────
void ContentBrowserPanel::buildTab(SVerticalBox& content) {
    using VR = SVerticalBox;
    using HR = SHorizontalBox;

    toolbarHost_ = HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ s(4.f), s(3.f) });
    filterHost_  = HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ s(6.f), s(2.f) });

    searchBox_ = HReuse(searchBoxKeep_);
    searchBox_->focusable(true)
        ->style(EditorUiKit::searchBoxStyle(s(1.f)))
        ->placeholder(HTEXT("Editor.Content", "SearchHint", "Search assets...").toString())
        ->edit(&searchEdit_)
        ->focused([this] { return searchFocused_; })
        ->onClick([this] { if (deps_.setFocus) deps_.setFocus(EFocusTarget::ContentSearch); })
        ->onEdited([this] { if (searchEdit_.text != shownSearch_) viewDirty_ = true; })
        ->fontSize(s(12.f));

    treeBox_    = HNew(SVerticalBox);
    treeScroll_ = HNew(SScrollBox);
    treeScroll_->style(EditorUiKit::scrollBoxStyle(s(1.f)))->content(treeBox_);
    viewBox_    = HNew(SVerticalBox);
    viewScroll_ = HNew(SScrollBox);
    viewScroll_->style(EditorUiKit::scrollBoxStyle(s(1.f)))->content(viewBox_);

    auto status = HNew(STextBlock)
        ->text(AttrStr(std::function<std::string()>([this] {
            std::string t = std::to_string(viewCount_) + (viewCount_ == 1 ? " item" : " items");
            if (!selected_.empty()) t += "  (1 selected)   " + selected_;
            return t;
        })))
        ->fontSize(s(11.f))->color(EditorUiKit::kTextDim)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center);

    auto body = HNew(SHorizontalBox)
        ->addSlot(HNew(SBorder)->background(EditorTheme::bgWindow)->content(treeScroll_), HR::ESizeRule::Fixed, s(kTreeW))
        ->addSlot(HNew(SBorder)->background(EditorTheme::lineDivider), HR::ESizeRule::Fixed, s(1.f))
        ->addSlot(HNew(SBorder)->background(EditorTheme::transparent)->padding(HPadding{ s(6.f), s(4.f) })->content(viewScroll_),
                  HR::ESizeRule::Fill);

    content.addSlot(toolbarHost_, VR::ESizeRule::Fixed, s(32.f));
    content.addSlot(filterHost_, VR::ESizeRule::Fixed, s(26.f));
    content.addSlot(HNew(SBorder)->background(EditorTheme::lineDivider), VR::ESizeRule::Fixed, s(1.f));
    content.addSlot(body, VR::ESizeRule::Fill, 1.f);
    content.addSlot(HNew(SBorder)->background(EditorTheme::bgWindow)->padding(HPadding{ s(8.f), 0.f })->content(status),
                    VR::ESizeRule::Fixed, s(20.f));

    viewWidthSig_ = -1;
    rebuildToolbar();
    rebuildFilterBar();
    rebuildTree();
    rebuildView();
    toolbarDirty_ = treeDirty_ = viewDirty_ = filterDirty_ = false;
}

void ContentBrowserPanel::releaseTab(bool stayOnTab) {
    if (!stayOnTab && searchFocused_ && deps_.setFocus) deps_.setFocus(EFocusTarget::None);
    toolbarHost_.reset();
    filterHost_.reset();
    searchBox_.reset();
    treeBox_.reset();
    viewBox_.reset();
    treeScroll_.reset();
    viewScroll_.reset();
    hitItems_.clear();
}

void ContentBrowserPanel::tick(bool tabActive) {
    if (!tabActive || !viewScroll_ || !tiles_) return;
    const int sig = (int)(viewScroll_->cachedGeometry().size.x / 4.f);
    if (sig != viewWidthSig_) viewDirty_ = true;
}

void ContentBrowserPanel::flushRebuild() {
    if (!viewBox_) return;   // 탭이 보이지 않음(다음 buildTab 이 모두 그림)
    if (toolbarDirty_) { toolbarDirty_ = false; rebuildToolbar(); }
    if (filterDirty_)  { filterDirty_  = false; rebuildFilterBar(); }
    if (treeDirty_)    { treeDirty_    = false; rebuildTree(); }
    if (viewDirty_)    { viewDirty_    = false; rebuildView(); }
}

TextInputField ContentBrowserPanel::searchField() {
    TextInputField f{
        searchEdit_,
        searchFocused_,
        [this](glm::vec2 p) { return searchBox_ && searchBox_->cachedGeometry().contains(p); },
        // 글이 바뀔 때만 다시 그립니다(포커스가 풀릴 때 다시 그리면 그 누름으로 고른 타일 단추가 사라져 클릭이 되지 않음).
        [this] { if (searchEdit_.text != shownSearch_) viewDirty_ = true; },
    };
    return f;
}

bool ContentBrowserPanel::contains(glm::vec2 p) const {
    return (treeScroll_ && treeScroll_->cachedGeometry().contains(p)) ||
           (viewScroll_ && viewScroll_->cachedGeometry().contains(p));
}

// ── 탐색 · 고르기 · 실행 ───────────────────────────────────────────────────────────
void ContentBrowserPanel::navigate(const std::string& folder, bool pushHistory) {
    if (!index_.hasFolder(folder) || folder == folder_) return;
    if (pushHistory) {
        back_.push_back(folder_);
        if (back_.size() > 64) back_.erase(back_.begin());
        forward_.clear();
    }
    folder_ = folder;
    // 트리에서 보이도록 상위 폴더를 펼칩니다.
    for (std::string p = folder_; p != hcb::kRoot; p = hcb::parentOf(p)) expanded_.insert(hcb::parentOf(p));
    selected_.clear();
    markAll();
    if (viewScroll_) viewScroll_->scrollToTop();
}

void ContentBrowserPanel::goBack() {
    while (!back_.empty()) {
        const std::string f = back_.back();
        back_.pop_back();
        if (!index_.hasFolder(f)) continue;
        forward_.push_back(folder_);
        navigate(f, false);
        return;
    }
}

void ContentBrowserPanel::goForward() {
    while (!forward_.empty()) {
        const std::string f = forward_.back();
        forward_.pop_back();
        if (!index_.hasFolder(f)) continue;
        back_.push_back(folder_);
        navigate(f, false);
        return;
    }
}

void ContentBrowserPanel::select(const std::string& path) {
    if (selected_ == path) return;
    selected_ = path;
    viewDirty_ = true;
}

void ContentBrowserPanel::setTileView(bool tiles) {
    if (tiles_ == tiles) return;
    tiles_ = tiles;
    toolbarDirty_ = viewDirty_ = true;
}

void ContentBrowserPanel::setSearch(const std::string& text) {
    searchEdit_.setText(text);
    viewDirty_ = true;
}

void ContentBrowserPanel::toggleFilter(AssetKind k) {
    filterMask_ ^= hcb::kindBit(k);
    filterDirty_ = viewDirty_ = true;
}

void ContentBrowserPanel::onItemClick(const std::string& path) {
    const auto now = std::chrono::steady_clock::now();
    const bool dbl = path == lastClick_ && std::chrono::duration<double>(now - lastClickAt_).count() < kDoubleClickSec;
    lastClick_   = dbl ? std::string() : path;   // 세 번째 누름은 다시 첫 누름
    lastClickAt_ = now;
    select(path);
    if (dbl) activate(path);
}

void ContentBrowserPanel::activate(const std::string& path) {
    if (index_.hasFolder(path)) { navigate(path); return; }
    const hcb::Item* it = index_.find(path);
    if (!it) return;
    switch (it->kind) {
        case AssetKind::Scene:  if (deps_.onOpenScene) deps_.onOpenScene(path); break;
        case AssetKind::Model:  placeModel(path); break;
        case AssetKind::Prefab: placePrefab(path); break;
        case AssetKind::Audio:
        case AssetKind::Cue:    if (deps_.onPreviewSound) deps_.onPreviewSound(path); break;
        default:
            if (deps_.logCat) HLOG(*deps_.logCat, Log) << path << ": no editor action for this kind (right-click for options)";
            break;
    }
}

void ContentBrowserPanel::placeModel(const std::string& path, const glm::vec3* atIn) {
    if (!deps_.ctx || !deps_.ctx->assets || !deps_.ctx->world) return;
    try {
        auto pairs = deps_.ctx->assets->loadAll(path);
        const std::string display = hcb::stemOf(path);
        const glm::vec3 at = atIn ? *atIn : deps_.placePosition ? deps_.placePosition() : glm::vec3(0.f);
        Actor* first = nullptr;
        std::vector<Actor*> made;
        for (int i = 0; i < (int)pairs.size(); ++i) {
            std::string nm = display + (pairs.size() > 1 && i > 0 ? "_" + std::to_string(i) : "");
            Actor* a  = deps_.ctx->world->spawnActor(nm);
            a->setLocalPosition(at);
            auto*  mc = a->addComponent<StaticMeshComponent>();
            mc->mesh        = pairs[i].mesh;
            mc->collision   = pairs[i].collision;
            mc->material    = pairs[i].material;
            mc->modelPath   = path;
            mc->meshIndex   = i;
            mc->transparent = pairs[i].isBlend;
            mc->vertCount   = pairs[i].vertCount;
            mc->triCount    = pairs[i].triCount;
            if (!first) first = a;
            made.push_back(a);
        }
        if (deps_.onSelectOnly) deps_.onSelectOnly(first);
        // 메시가 여럿인 모델은 한 번의 실행이 액터 여럿을 만들므로 한 커맨드로 쌓습니다.
        if (deps_.onRecordCreation) deps_.onRecordCreation(made);
    } catch (const std::exception& ex) {
        if (deps_.logCat) HLOG(*deps_.logCat, Error) << "Spawn failed: " << ex.what();
    }
}

void ContentBrowserPanel::placePrefab(const std::string& path, const glm::vec3* at) {
    if (!deps_.ctx || !deps_.ctx->world || !deps_.ctx->assets) return;
    Actor* a = PrefabManager::spawn(path, *deps_.ctx->world, *deps_.ctx->assets);
    if (!a) return;
    if (at) a->setLocalPosition(*at);
    else if (deps_.placePosition) a->setLocalPosition(deps_.placePosition());
    if (deps_.onSelectOnly) deps_.onSelectOnly(a);
    if (deps_.onRecordCreation) deps_.onRecordCreation({ a });
}

void ContentBrowserPanel::assignSound(const std::string& fullPath, Actor* target) {
    // 선택한 액터(끌어 놓기면 놓은 액터)에 지정합니다. 오디오 컴포넌트가 없으면 붙입니다(Undo 가능, 2026-10-15).
    Actor* sel = target ? target : deps_.ctx ? deps_.ctx->selected : nullptr;
    if (!sel || (deps_.isEditorCam && deps_.isEditorCam(sel))) {
        if (deps_.logCat) HLOG(*deps_.logCat, Warning) << "select an actor first to assign '" << fullPath << "'";
        return;
    }
    if (auto* ac = sel->getComponent<AudioComponent>()) {
        const std::string before = ac->filePath;
        ac->setFile(fullPath);
        if (before != fullPath)
            deps_.cmdStack->push(std::make_unique<AudioFileCmd>(*deps_.ctx->world, sel, before, fullPath));
    } else {
        ac = sel->addComponent<AudioComponent>();
        ac->setFile(fullPath);
        deps_.cmdStack->push(std::make_unique<ComponentPresenceCmd>(
            ComponentPresenceCmd::Kind::Add, *deps_.ctx->world, sel, ac,
            ComponentLoadContext{ deps_.ctx->assets },
            [this] { if (deps_.onDetailsDirty) deps_.onDetailsDirty(); }));
    }
    if (deps_.onDetailsDirty) deps_.onDetailsDirty();
    if (deps_.logCat) HLOG(*deps_.logCat, Log) << "Assigned " << fullPath << " to " << sel->name;
}

void ContentBrowserPanel::spawnSoundActor(const std::string& path, const glm::vec3& at) {
    // 빈 자리에 놓은 소리: 그 자리에 소리 액터를 만듭니다(UE 가 Ambient Sound 액터를 만드는 것과 같은 몫).
    if (!deps_.ctx || !deps_.ctx->world) return;
    Actor* a = deps_.ctx->world->spawnActor(hcb::stemOf(path));
    a->setLocalPosition(at);
    auto* ac = a->addComponent<AudioComponent>();
    ac->setFile(path);
    if (deps_.onSelectOnly) deps_.onSelectOnly(a);
    if (deps_.onRecordCreation) deps_.onRecordCreation({ a });
    if (deps_.logCat) HLOG(*deps_.logCat, Log) << "Placed sound actor '" << a->name << "' (" << path << ")";
}

// ── 끌어 놓기 ──────────────────────────────────────────────────────────────────────
std::string ContentBrowserPanel::hitItemAt(glm::vec2 p, bool viewOnly) const {
    if (viewOnly && !(viewScroll_ && viewScroll_->cachedGeometry().contains(p))) return {};
    for (const auto& [path, w] : hitItems_)
        if (auto sp = w.lock(); sp && sp->cachedGeometry().contains(p)) return path;
    return {};
}

void ContentBrowserPanel::pointerPress(glm::vec2 p) {
    pressPath_ = hitItemAt(p, true);
    if (!pressPath_.empty() && index_.hasFolder(pressPath_)) pressPath_.clear();   // 폴더는 끌지 않음
    pressPos_ = p;
}

void ContentBrowserPanel::pointerMove(glm::vec2 p, bool leftDown) {
    if (dragging_) return;
    if (!leftDown) { pressPath_.clear(); return; }
    if (pressPath_.empty()) return;
    if (glm::length(p - pressPos_) < s(6.f)) return;
    dragging_ = true;
    dragPath_ = pressPath_;
    pressPath_.clear();
    select(dragPath_);
}

void ContentBrowserPanel::cancelDrag() {
    dragging_ = false;
    dragPath_.clear();
    pressPath_.clear();
    if (dragHost_ && dragShown_) { dragHost_->content(nullptr); dragShown_ = false; }
}

void ContentBrowserPanel::forceDrag(const std::string& path) {
    if (!index_.find(path)) return;
    dragging_ = true;
    dragPath_ = path;
    select(path);
}

bool ContentBrowserPanel::canDropInViewport(const std::string& path) const {
    const hcb::Item* it = index_.find(path);
    if (!it) return false;
    return it->kind == AssetKind::Model || it->kind == AssetKind::Prefab || it->kind == AssetKind::Audio ||
           it->kind == AssetKind::Cue;
}

bool ContentBrowserPanel::dropInViewport(const std::string& path, const glm::vec3& point, Actor* hitActor) {
    const hcb::Item* it = index_.find(path);
    if (!it) return false;
    switch (it->kind) {
        case AssetKind::Model:  placeModel(path, &point);  return true;
        case AssetKind::Prefab: placePrefab(path, &point); return true;
        case AssetKind::Audio:
        case AssetKind::Cue:
            // 소리 액터(오디오 컴포넌트가 있음) 위면 그 소리를 바꾸고, 그 밖(바닥 · 벽 · 다른 물체)은 그 자리에 소리 액터를
            // 만듭니다. 예전 규칙(닿은 액터에 지정)은 바닥에 놓으면 Ground 에 소리가 붙었습니다.
            if (hitActor && !(deps_.isEditorCam && deps_.isEditorCam(hitActor)) && hitActor->getComponent<AudioComponent>()) {
                if (deps_.onSelectOnly) deps_.onSelectOnly(hitActor);
                assignSound(path, hitActor);
            } else {
                spawnSoundActor(path, point);
            }
            return true;
        default:
            if (deps_.logCat) HLOG(*deps_.logCat, Log) << path << ": cannot be placed in the scene";
            return false;
    }
}

std::shared_ptr<SBorder> ContentBrowserPanel::resetDragHost() {
    dragHost_  = HNew(SBorder)->background(EditorTheme::transparent);
    dragShown_ = false;
    return dragHost_;
}

void ContentBrowserPanel::updateDragVisual(glm::vec2 p, bool canDrop) {
    if (!dragHost_) return;
    if (!dragging_) {
        if (dragShown_) { dragHost_->content(nullptr); dragShown_ = false; }
        return;
    }
    const hcb::Item* it = index_.find(dragPath_);
    if (!it) return;
    using HR = SHorizontalBox;
    const EditorUiKit& u = *deps_.ui;
    const std::string label = it->name + (canDrop ? std::string() : "  (" +
        HTEXT("Editor.Content", "Drag.CannotDrop", "cannot drop here").toString() + ")");
    const float w = EditorUiKit::textWidth(label, s(12.f)) + s(40.f);
    auto tag = HNew(SBox)->widthOverride(w)->heightOverride(s(24.f))->content(
        HNew(SBorder)->background(EditorTheme::withAlpha(EditorTheme::bgPopup, 0.92f))
            ->borderColor(canDrop ? EditorTheme::accent : EditorTheme::borderPopup)->borderThickness(EditorTheme::borderThin)
            ->cornerRadius(s(4.f))->padding(HPadding{ s(6.f), 0.f })
            ->content(HNew(SHorizontalBox)
                ->addSlot(u.icon(kindIcon(it->kind), 14.f, kindColor(it->kind), kindFallback(it->kind), 11.f),
                          HR::ESizeRule::Fixed, s(20.f), {}, EVAlign::Center)
                ->addSlot(HNew(STextBlock)->text(label)->fontSize(s(12.f))
                              ->color(canDrop ? EditorUiKit::kTextBody : EditorUiKit::kTextDim)->vAlign(EVAlign::Center),
                          HR::ESizeRule::Fill, 1.f)));
    const HGeometry& host = dragHost_->cachedGeometry();
    auto ov = HNew(SOverlay);
    ov->addSlot(tag, EHAlign::Left, EVAlign::Top,
                HPadding{ p.x - host.position.x + s(14.f), p.y - host.position.y + s(10.f), 0.f, 0.f });
    dragHost_->content(ov);
    dragShown_ = true;
}

void ContentBrowserPanel::showInExplorer(const std::string& path) {
    const std::string disk = path == hcb::kRoot ? hpak::utf8Of(hproj::root() / "Content") : hproj::resolve(path);
    if (deps_.dumpMode) {   // 덤프 실행은 창을 띄우지 않습니다
        if (deps_.logCat) HLOG(*deps_.logCat, Log) << "Show in Explorer (dump run, not opened): " << path;
        return;
    }
#ifdef _WIN32
    fs::path p;
    if (!hpak::pathFromUtf8(disk, p)) return;
    p.make_preferred();
    const std::wstring args = L"/select,\"" + p.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
#else
    if (deps_.logCat) HLOG(*deps_.logCat, Log) << "Show in Explorer is Windows-only: " << disk;
#endif
}

void ContentBrowserPanel::copyPath(const std::string& path) {
    if (deps_.ctx && deps_.ctx->window) deps_.ctx->window->setClipboardText(path);
    if (deps_.logCat) HLOG(*deps_.logCat, Log) << "Copied path: " << path;
}

std::vector<EditorMenuEntry> ContentBrowserPanel::contextItems(const std::string& path) {
    // H2b: 메뉴 모델(EditorMenuEntry). 항목은 그 자리 경로에 묶인 동작이고, 예전 "이 항목 위에 구분선" 은 구분선 항목입니다.
    using E = EditorMenuEntry;
    std::vector<E> items;
    if (path.empty()) {
        const std::string f = folder_;
        items.push_back(E::act(HTEXT("Editor.Content", "Menu.ShowFolder", "Show in Explorer").toString(), [this, f] { showInExplorer(f); }, true, "external-link", "Show in Explorer"));
        items.push_back(E::act(HTEXT("Editor.Content", "Menu.Refresh", "Refresh").toString(), [this] { scanAll(); }, true, "refresh-cw", "Refresh"));
        return items;
    }
    if (index_.hasFolder(path)) {
        items.push_back(E::act(HTEXT("Editor.Content", "Menu.OpenFolder", "Open").toString(), [this, path] { navigate(path); }, true, "folder-open", "Open"));
        items.push_back(E::separator());
        items.push_back(E::act(HTEXT("Editor.Content", "Menu.ShowInExplorer", "Show in Explorer").toString(), [this, path] { showInExplorer(path); }, true, "external-link", "Show in Explorer"));
        items.push_back(E::act(HTEXT("Editor.Content", "Menu.CopyPath", "Copy Path").toString(), [this, path] { copyPath(path); }, true, "clipboard-copy", "Copy Path"));
        return items;
    }
    const hcb::Item* it = index_.find(path);
    if (!it) return items;
    switch (it->kind) {
        case AssetKind::Scene:
            items.push_back(E::act(HTEXT("Editor.Content", "Menu.OpenScene", "Open Scene").toString(), [this, path] { activate(path); }, true, "map", "Open Scene"));
            break;
        case AssetKind::Model:
        case AssetKind::Prefab:
            items.push_back(E::act(HTEXT("Editor.Content", "Menu.Place", "Place in Scene").toString(), [this, path] { activate(path); }, true, "map-pin", "Place in Scene"));
            break;
        case AssetKind::Audio:
        case AssetKind::Cue: {
            items.push_back(E::act(HTEXT("Editor.Content", "Menu.Play", "Play").toString(), [this, path] { activate(path); }, true, "play", "Play"));
            const bool canAssign = deps_.ctx && deps_.ctx->selected && !(deps_.isEditorCam && deps_.isEditorCam(deps_.ctx->selected));
            items.push_back(E::act(HTEXT("Editor.Content", "Menu.Assign", "Assign to Selected Actor").toString(), [this, path] { assignSound(path); }, canAssign, "link-2", "Assign to Selected Actor"));
            break;
        }
        default: break;
    }
    if (!items.empty()) items.push_back(E::separator());
    items.push_back(E::act(HTEXT("Editor.Content", "Menu.AssetActions", "References, Rename, Move...").toString(),
                           [this, path] { if (deps_.onAssetActions) deps_.onAssetActions(path); }, true, "folder-input", "References, Rename, Move..."));
    items.push_back(E::act(HTEXT("Editor.Content", "Menu.ShowInExplorer", "Show in Explorer").toString(), [this, path] { showInExplorer(path); }, true, "external-link", "Show in Explorer"));
    items.push_back(E::act(HTEXT("Editor.Content", "Menu.CopyPath", "Copy Path").toString(), [this, path] { copyPath(path); }, true, "clipboard-copy", "Copy Path"));
    return items;
}

bool ContentBrowserPanel::openContextMenuAt(glm::vec2 p) {
    if (!deps_.contextMenu || !contains(p)) return false;
    std::string hit;
    for (const auto& [path, w] : hitItems_)
        if (auto sp = w.lock(); sp && sp->cachedGeometry().contains(p)) { hit = path; break; }
    const bool inTree = treeScroll_ && treeScroll_->cachedGeometry().contains(p);
    if (hit.empty() && inTree) return true;   // 트리의 빈 자리: 메뉴 없음
    if (!hit.empty() && !inTree) select(hit);
    deps_.contextMenu->open(p, contextItems(hit));
    return true;
}

// ── 위젯 ──────────────────────────────────────────────────────────────────────────
void ContentBrowserPanel::rebuildToolbar() {
    if (!toolbarHost_) return;
    using HR = SHorizontalBox;
    const EditorUiKit& u = *deps_.ui;
    auto row = HNew(SHorizontalBox);

    auto navBtn = [&](const char* icon, bool enabled, std::function<void()> fn, const HText& tip) {
        auto b = u.iconButton(icon, 16.f, enabled ? EditorUiKit::kTextBody : EditorTheme::textDisabled, "", 12.f,
                              EditorUiKit::kTextBody, { 5.f, 3.f }, EditorTheme::transparent, EditorTheme::ctlHover, std::move(fn));
        b->enabled.set(enabled);
        b->setTooltip(tip);
        row->addSlot(b, HR::ESizeRule::Fixed, s(28.f));
    };
    navBtn("chevron-left", !back_.empty(), [this] { goBack(); }, HTEXT("Editor.Content", "Tip.Back", "Back"));
    navBtn("chevron-right", !forward_.empty(), [this] { goForward(); }, HTEXT("Editor.Content", "Tip.Forward", "Forward"));
    row->addSlot(u.spacer(), HR::ESizeRule::Fixed, s(6.f));

    // 경로: Content > models > hands. 마디를 누르면 그 폴더로 갑니다.
    std::vector<std::string> chain;
    for (std::string p = folder_;; p = hcb::parentOf(p)) {
        chain.push_back(p);
        if (p == hcb::kRoot) break;
    }
    std::reverse(chain.begin(), chain.end());
    for (size_t i = 0; i < chain.size(); ++i) {
        const std::string p = chain[i];
        const std::string label = p == hcb::kRoot ? std::string("Content") : hcb::nameOf(p);
        const bool last = i + 1 == chain.size();
        auto b = u.textButton(label, 12.f, last ? EditorUiKit::kTextHdr : EditorUiKit::kTextDim, { 5.f, 3.f },
                              EditorTheme::transparent, EditorTheme::ctlHover, [this, p] { navigate(p); });
        row->addSlot(b, HR::ESizeRule::Auto);
        if (!last)
            row->addSlot(u.icon("chevron-right", 12.f, EditorUiKit::kTextDim, ">", 11.f), HR::ESizeRule::Auto, 0.f, {}, EVAlign::Center);
    }
    row->addSlot(u.spacer(), HR::ESizeRule::Fill, 1.f);
    row->addSlot(searchBox_, HR::ESizeRule::Fixed, s(220.f));
    row->addSlot(u.spacer(), HR::ESizeRule::Fixed, s(6.f));
    auto viewBtn = [&](const char* icon, bool on, bool tiles, const HText& tip) {
        auto b = u.iconButton(icon, 15.f, on ? EditorTheme::accentHover : EditorUiKit::kTextDim, "", 12.f, EditorUiKit::kTextBody,
                              { 5.f, 3.f }, on ? EditorTheme::ctlNormal : EditorTheme::transparent, EditorTheme::ctlHover,
                              [this, tiles] { setTileView(tiles); });
        b->setTooltip(tip);
        row->addSlot(b, HR::ESizeRule::Fixed, s(28.f));
    };
    viewBtn("layout-grid", tiles_, true, HTEXT("Editor.Content", "Tip.Tiles", "Tile view"));
    viewBtn("list", !tiles_, false, HTEXT("Editor.Content", "Tip.List", "List view"));
    toolbarHost_->content(row);
}

void ContentBrowserPanel::rebuildFilterBar() {
    if (!filterHost_) return;
    using HR = SHorizontalBox;
    const EditorUiKit& u = *deps_.ui;
    auto row = HNew(SHorizontalBox);
    row->addSlot(HNew(STextBlock)->text(HTEXT("Editor.Content", "Filters", "Filters").toString())->fontSize(s(11.f))
                     ->color(EditorUiKit::kTextDim)->vAlign(EVAlign::Center), HR::ESizeRule::Auto);
    row->addSlot(u.spacer(), HR::ESizeRule::Fixed, s(8.f));
    for (AssetKind k : hcb::filterKinds()) {
        const bool on = (filterMask_ & hcb::kindBit(k)) != 0;
        auto chip = HNew(SButton)
            ->style(EditorUiKit::buttonStyle())->cornerRadius(s(3.f))
            ->normalColor(on ? EditorTheme::selectRow : EditorTheme::ctlNormal)
            ->hoveredColor(on ? EditorTheme::selectRowHover : EditorTheme::ctlHover)
            ->pressedColor(EditorTheme::accentPressed)
            ->padding(HPadding{ s(7.f), s(1.f) })
            ->onClick([this, k] { toggleFilter(k); })
            ->content(HNew(SHorizontalBox)
                ->addSlot(HNew(SBorder)->background(kindColor(k)), HR::ESizeRule::Fixed, s(3.f))
                ->addSlot(u.spacer(), HR::ESizeRule::Fixed, s(5.f))
                ->addSlot(HNew(STextBlock)->text(hcb::kindName(k))->fontSize(s(11.f))
                              ->color(on ? EditorUiKit::kTextHdr : EditorUiKit::kTextDim)->vAlign(EVAlign::Center), HR::ESizeRule::Auto));
        row->addSlot(chip, HR::ESizeRule::Auto);
        row->addSlot(u.spacer(), HR::ESizeRule::Fixed, s(4.f));
    }
    if (filterMask_) {
        row->addSlot(u.textButton(HTEXT("Editor.Content", "ClearFilters", "Clear").toString(), 11.f, EditorTheme::accentHover,
                                  { 6.f, 1.f }, EditorTheme::transparent, EditorTheme::ctlHover,
                                  [this] { filterMask_ = 0; filterDirty_ = viewDirty_ = true; }),
                     HR::ESizeRule::Auto);
    }
    filterHost_->content(row);
}

void ContentBrowserPanel::rebuildTree() {
    if (!treeBox_) return;
    treeBox_->clearSlots();
    hitItems_.erase(std::remove_if(hitItems_.begin(), hitItems_.end(),
                                   [](const auto& h) { return h.second.expired(); }), hitItems_.end());
    treeBox_->addSlot(deps_.ui->spacer(), SVerticalBox::ESizeRule::Fixed, s(4.f));
    addTreeRows(hcb::kRoot, 0);
}

void ContentBrowserPanel::addTreeRows(const std::string& folder, int depth) {
    using HR = SHorizontalBox;
    const EditorUiKit& u = *deps_.ui;
    const auto children = index_.childFolders(folder);
    const bool open = expanded_.count(folder) != 0;
    const bool current = folder == folder_;

    std::shared_ptr<HWidget> arrow;
    if (!children.empty()) {
        arrow = HNew(SButton)
            ->style(EditorUiKit::buttonStyle())->cornerRadius(0.f)
            ->normalColor(EditorTheme::transparent)->hoveredColor(EditorTheme::ctlHover)->pressedColor(EditorTheme::accentPressed)
            ->padding(HPadding{ 0.f, 0.f })
            ->onClick([this, folder] {
                if (!expanded_.erase(folder)) expanded_.insert(folder);
                treeDirty_ = true;
            })
            ->content(u.icon(open ? "chevron-down" : "chevron-right", 12.f, EditorUiKit::kTextDim, open ? "v" : ">", 11.f));
    } else {
        arrow = u.spacer();
    }
    const std::string label = folder == hcb::kRoot ? std::string("Content") : hcb::nameOf(folder);
    auto rowBtn = HNew(SButton)
        ->style(EditorUiKit::buttonStyle())->cornerRadius(0.f)
        ->normalColor(current ? EditorTheme::selectRow : EditorTheme::transparent)
        ->hoveredColor(current ? EditorTheme::selectRowHover : EditorTheme::ctlHover)
        ->pressedColor(EditorTheme::accentPressed)
        ->padding(HPadding{ s(4.f), 0.f })->hAlign(EHAlign::Fill)
        ->onClick([this, folder] { navigate(folder); })
        ->content(HNew(SHorizontalBox)
            ->addSlot(u.icon(open && !children.empty() ? "folder-open" : "folder", 14.f, kindColor(AssetKind::Folder), "[D]", 11.f),
                      HR::ESizeRule::Fixed, s(18.f), {}, EVAlign::Center)
            ->addSlot(u.spacer(), HR::ESizeRule::Fixed, s(5.f))
            ->addSlot(HNew(STextBlock)->text(label)->fontSize(s(12.f))->color(EditorUiKit::kTextBody)
                          ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), HR::ESizeRule::Fill));
    hitItems_.push_back({ folder, rowBtn });
    auto line = HNew(SHorizontalBox)
        ->addSlot(u.spacer(), HR::ESizeRule::Fixed, s(4.f) + s(12.f) * (float)depth)
        ->addSlot(arrow, HR::ESizeRule::Fixed, s(16.f))
        ->addSlot(rowBtn, HR::ESizeRule::Fill, 1.f);
    treeBox_->addSlot(line, SVerticalBox::ESizeRule::Fixed, s(22.f));
    if (open)
        for (const auto& c : children) addTreeRows(c, depth + 1);
}

std::shared_ptr<HWidget> ContentBrowserPanel::makeTile(const hcb::Item& it, float w, float h) {
    using VR = SVerticalBox;
    const EditorUiKit& u = *deps_.ui;
    const bool sel = it.path == selected_;
    const std::string path = it.path;
    auto inner = HNew(SVerticalBox)
        ->addSlot(u.spacer(), VR::ESizeRule::Fixed, s(8.f))
        ->addSlot(HNew(SBorder)->background(EditorTheme::transparent)->hAlign(EHAlign::Center)->vAlign(EVAlign::Center)
                      ->content(u.icon(kindIcon(it.kind), 30.f, kindColor(it.kind), kindFallback(it.kind), 16.f)),
                  VR::ESizeRule::Fixed, s(34.f))
        ->addSlot(u.spacer(), VR::ESizeRule::Fixed, s(4.f))
        ->addSlot(HNew(SBorder)->background(EditorTheme::transparent)->clipChildren(true)->padding(HPadding{ s(4.f), 0.f })
                      ->content(HNew(STextBlock)->text(it.name)->fontSize(s(11.f))->color(EditorUiKit::kTextBody)
                                    ->hAlign(EHAlign::Center)->vAlign(EVAlign::Center)),
                  VR::ESizeRule::Fixed, s(16.f))
        ->addSlot(HNew(STextBlock)->text(hcb::kindName(it.kind))->fontSize(s(10.f))->color(EditorUiKit::kTextDim)
                      ->hAlign(EHAlign::Center)->vAlign(EVAlign::Center), VR::ESizeRule::Fixed, s(13.f))
        ->addSlot(u.spacer(), VR::ESizeRule::Fill, 1.f)
        ->addSlot(HNew(SBorder)->background(kindColor(it.kind)), VR::ESizeRule::Fixed, s(3.f));   // 종류 색 띠(UE 타일)
    auto btn = HNew(SButton)
        ->style(EditorUiKit::buttonStyle())->cornerRadius(s(3.f))
        ->normalColor(sel ? EditorTheme::selectRow : EditorTheme::ctlNormal)
        ->hoveredColor(sel ? EditorTheme::selectRowHover : EditorTheme::ctlHover)
        ->pressedColor(EditorTheme::accentPressed)
        ->padding(HPadding{ 0.f, 0.f })->hAlign(EHAlign::Fill)->vAlign(EVAlign::Fill)
        ->onClick([this, path] { onItemClick(path); })
        ->content(inner);
    btn->setTooltip(HText::fromString(it.path));
    hitItems_.push_back({ it.path, btn });
    return HNew(SBox)->widthOverride(w)->heightOverride(h)->content(btn);
}

std::shared_ptr<HWidget> ContentBrowserPanel::makeRow(const hcb::Item& it, bool showFolder) {
    using HR = SHorizontalBox;
    const EditorUiKit& u = *deps_.ui;
    const bool sel = it.path == selected_;
    const std::string path = it.path;
    auto row = HNew(SHorizontalBox)
        ->addSlot(u.icon(kindIcon(it.kind), 14.f, kindColor(it.kind), kindFallback(it.kind), 11.f), HR::ESizeRule::Fixed, s(20.f),
                  {}, EVAlign::Center)
        ->addSlot(u.spacer(), HR::ESizeRule::Fixed, s(4.f))
        ->addSlot(HNew(STextBlock)->text(it.name)->fontSize(s(13.f))->color(EditorUiKit::kTextBody)
                      ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), HR::ESizeRule::Fill, 1.f)
        ->addSlot(HNew(STextBlock)->text(hcb::kindName(it.kind))->fontSize(s(11.f))->color(EditorUiKit::kTextDim)
                      ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), HR::ESizeRule::Fixed, s(90.f));
    if (showFolder)
        row->addSlot(HNew(STextBlock)->text(hcb::parentOf(it.path))->fontSize(s(11.f))->color(EditorUiKit::kTextDim)
                         ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), HR::ESizeRule::Fixed, s(220.f));
    auto btn = HNew(SButton)
        ->style(EditorUiKit::buttonStyle())->cornerRadius(0.f)
        ->normalColor(sel ? EditorTheme::selectRow : EditorTheme::transparent)
        ->hoveredColor(sel ? EditorTheme::selectRowHover : EditorTheme::ctlHover)
        ->pressedColor(EditorTheme::accentPressed)
        ->padding(HPadding{ s(6.f), 0.f })->hAlign(EHAlign::Fill)
        ->onClick([this, path] { onItemClick(path); })
        ->content(row);
    hitItems_.push_back({ it.path, btn });
    return btn;
}

void ContentBrowserPanel::rebuildView() {
    if (!viewBox_ || !viewScroll_) return;
    viewBox_->clearSlots();
    hitItems_.erase(std::remove_if(hitItems_.begin(), hitItems_.end(),
                                   [](const auto& h) { return h.second.expired(); }), hitItems_.end());
    const std::string search = searchEdit_.text;
    shownSearch_ = search;
    const auto items = index_.view(folder_, search, filterMask_);
    viewCount_ = items.size();
    const bool recursive = !search.empty() || filterMask_ != 0;

    if (items.empty()) {
        const std::string msg = recursive ? HTEXT("Editor.Content", "NoMatch", "No matching assets").toString()
                                          : HTEXT("Editor.Content", "EmptyFolder", "This folder is empty").toString();
        viewBox_->addSlot(HNew(STextBlock)->text(msg)->fontSize(s(12.f))->color(EditorUiKit::kTextDim)
                              ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                          SVerticalBox::ESizeRule::Fixed, s(EditorTheme::rowH));
        return;
    }
    if (!tiles_) {
        for (const auto& it : items)
            viewBox_->addSlot(makeRow(it, recursive), SVerticalBox::ESizeRule::Fixed, s(EditorTheme::rowH));
        return;
    }
    // 타일: 폭에 맞춰 줄 나눔. 스크롤 막대 자리는 늘 빼 두어 막대가 생기고 없어져도 나눔이 같습니다(디테일 DEF-109 와 같은 방식).
    const float width = viewScroll_->cachedGeometry().size.x;
    viewWidthSig_ = (int)(width / 4.f);
    const float tileW = s(kTileW), tileH = s(kTileH), gap = s(kTileGap);
    const float avail = width > 0.f ? width - s(14.f) : s(600.f);
    const int cols = std::max(1, (int)((avail + gap) / (tileW + gap)));
    for (size_t i = 0; i < items.size(); i += (size_t)cols) {
        auto line = HNew(SHorizontalBox);
        for (size_t j = i; j < std::min(items.size(), i + (size_t)cols); ++j) {
            line->addSlot(makeTile(items[j], tileW, tileH), SHorizontalBox::ESizeRule::Fixed, tileW);
            line->addSlot(deps_.ui->spacer(), SHorizontalBox::ESizeRule::Fixed, gap);
        }
        viewBox_->addSlot(line, SVerticalBox::ESizeRule::Fixed, tileH);
        viewBox_->addSlot(deps_.ui->spacer(), SVerticalBox::ESizeRule::Fixed, gap);
    }
}
