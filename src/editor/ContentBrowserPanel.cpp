#include "ContentBrowserPanel.hpp"
#include "../vfs/LogicalPath.hpp"
#include "EditorCommands.hpp"
#include "../assets/AssetManager.hpp"
#include "../audio/AudioComponent.hpp"
#include "../components/StaticMeshComponent.hpp"
#include "../core/ProjectPaths.hpp"
#include "../scene/Actor.hpp"
#include "../scene/World.hpp"
#include "../sceneio/PrefabManager.hpp"
#include "../vfs/PathUtf8.hpp"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

constexpr const char* kScenesDir = "/Game/scenes";
constexpr const char* kModelsDir = "/Game/models";
constexpr const char* kAudioDir  = "/Game/audio";

// 논리 경로의 파일 이름에서 확장자를 뺀 것(UTF-8 그대로). fs::path(std::string) 은 Windows 코드 페이지로 해석해 한글 이름이 깨집니다.
std::string stemOf(const std::string& logical) {
    const size_t slash = logical.find_last_of('/');
    std::string name = slash == std::string::npos ? logical : logical.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name.resize(dot);
    return name;
}

// dir 아래 파일 중 확장자가 맞는 것을 "dir/상대 경로"(UTF-8)로 모아 정렬합니다. 하위 폴더도 훑습니다(옮긴 에셋도 보이게).
std::vector<std::string> listFiles(const char* logicalDir, std::initializer_list<const char*> exts) {
    std::vector<std::string> out;
    std::error_code ec;
    const fs::path dir = hproj::resolve(logicalDir);
    if (!fs::exists(dir, ec)) return out;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string ext = hpak::utf8Of(it->path().extension());
        for (char& c : ext) c = (char)std::tolower((unsigned char)c);
        if (std::find_if(exts.begin(), exts.end(), [&](const char* e) { return ext == e; }) == exts.end()) continue;
        const std::string rel = hpak::utf8Of(fs::relative(it->path(), dir, ec));
        if (!ec && !rel.empty()) out.push_back(std::string(logicalDir) + "/" + rel);
        ec.clear();
    }
    std::sort(out.begin(), out.end());
    return out;
}

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

void ContentBrowserPanel::scanAll() {
    // 프로젝트 폴더를 훑지만, 컴포넌트와 씬에는 논리 경로(/Game/models/...)를 남깁니다(hproj). 이름은 UTF-8 로 다룹니다(글자 체계).
    modelFiles_ = listFiles(kModelsDir, { ".glb", ".gltf" });
    // 소리는 하위 폴더도 훑습니다(참조 체계: 옮긴 소리도 목록에 남게). 항목은 /Game/audio 아래 상대 경로입니다.
    audioFiles_.clear();
    for (const std::string& p : listFiles(kAudioDir, { ".wav", ".mp3", ".ogg", ".flac" }))
        audioFiles_.push_back(p.substr(std::string(kAudioDir).size() + 1));

    refreshPrefabs();
    scanScenes();
}

void ContentBrowserPanel::scanScenes() {
    sceneFiles_ = listFiles(kScenesDir, { ".hscene" });
}

void ContentBrowserPanel::refreshPrefabs() {
    prefabFiles_ = PrefabManager::scan();
}

std::shared_ptr<HWidget> ContentBrowserPanel::withActions(std::shared_ptr<HWidget> row, const std::string& path) const {
    auto more = deps_.ui->textButton("...", 12.f, EditorUiKit::kTextDim, {4.f, 2.f},
                                     EditorTheme::transparent, EditorTheme::ctlHover,
                                     [this, path] { if (deps_.onAssetActions) deps_.onAssetActions(path); });
    more->setTooltip(HTEXT("Editor.Content", "Tip.AssetActions", "References, rename and move"));
    return HNew(SHorizontalBox)
        ->addSlot(row, SHorizontalBox::ESizeRule::Fill)
        ->addSlot(more, SHorizontalBox::ESizeRule::Fixed, s(28.f));
}

void ContentBrowserPanel::buildTab(SVerticalBox& content) const {
    buildScenesSection(content);
    buildModelsSection(content);
    buildAudioSection(content);
    buildPrefabsSection(content);

    if (sceneFiles_.empty() && modelFiles_.empty() && audioFiles_.empty() && prefabFiles_.empty()) {
        content.addSlot(
            HNew(SBorder)->background(EditorTheme::transparent)->padding({s(EditorTheme::space12), s(EditorTheme::space2)})
                ->content(HNew(STextBlock)->text("No assets found")
                    ->fontSize(s(12.f))->color(EditorUiKit::kTextDim)->hAlign(EHAlign::Left)),
            SVerticalBox::ESizeRule::Fixed, s(EditorTheme::rowH));
    }
}

void ContentBrowserPanel::buildScenesSection(SVerticalBox& content) const {
    if (sceneFiles_.empty()) return;
    // Content Browser — Scenes(2026-10-15). 누르면 그 씬을 엽니다(저장하지 않은 변경이 있으면 먼저 묻습니다).
    // 이 버튼은 하단 패널 안에 있으므로, 패널을 다시 만드는 일은 프레임 끝으로 미룹니다(bottomDirty_).
    content.addSlot(
        deps_.ui->sectionHeader("SCENES", EditorUiKit::kTextDim),
        SVerticalBox::ESizeRule::Fixed, s(22.f));
    const std::string curPath = deps_.currentScenePath ? hvfs::toCanonical(deps_.currentScenePath()) : std::string{};
    for (const auto& path : sceneFiles_) {
        const std::string display = path.substr(std::string(kScenesDir).size() + 1);
        const bool current = (hvfs::toCanonical(path) == curPath);
        content.addSlot(withActions(
            HNew(SButton)
                ->style(EditorUiKit::buttonStyle())
                ->cornerRadius(0.f)
                ->normalColor (current ? EditorTheme::selectRow : EditorTheme::transparent)
                ->hoveredColor(current ? EditorTheme::selectRowHover : EditorTheme::ctlHover)
                ->pressedColor(EditorTheme::accentPressed)
                ->padding({s(EditorTheme::space8), s(EditorTheme::space2)})
                ->hAlign(EHAlign::Fill)
                ->onClick([this, path]{
                    if (deps_.onOpenScene) deps_.onOpenScene(path);
                })
                ->content(HNew(SHorizontalBox)
                    ->addSlot(HNew(STextBlock)->text("[S]")->fontSize(s(12.f))->color(EditorTheme::badgeScene)
                        ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fixed, s(30.f))
                    ->addSlot(HNew(STextBlock)->text(display)->fontSize(s(13.f))->color(EditorUiKit::kTextBody)
                        ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fill)), path),
            SVerticalBox::ESizeRule::Fixed, s(EditorTheme::rowH));
    }
    content.addSlot(deps_.ui->spacer(), SVerticalBox::ESizeRule::Fixed, s(4.f));
}

void ContentBrowserPanel::buildModelsSection(SVerticalBox& content) const {
    if (modelFiles_.empty()) return;
    content.addSlot(
        deps_.ui->sectionHeader("MODELS", EditorUiKit::kTextDim),
        SVerticalBox::ESizeRule::Fixed, s(22.f));
    for (const auto& path : modelFiles_) {
        std::string display = stemOf(path);
        content.addSlot(withActions(
            HNew(SButton)
                ->style(EditorUiKit::buttonStyle())
                ->cornerRadius(0.f)
                ->normalColor (EditorTheme::transparent)
                ->hoveredColor(EditorTheme::ctlHover)
                ->pressedColor(EditorTheme::accentPressed)
                ->padding({s(EditorTheme::space8), s(EditorTheme::space2)})
                ->hAlign(EHAlign::Fill)   // 기본 Center 면 항목이 가운데로 몰립니다(DEF-87)
                ->onClick([this, path, display]{
                    try {
                        auto pairs = deps_.ctx->assets->loadAll(path);
                        Actor* first = nullptr;
                        std::vector<Actor*> made;
                        for (int i = 0; i < (int)pairs.size(); ++i) {
                            std::string nm = display +
                                (pairs.size() > 1 && i > 0 ? "_" + std::to_string(i) : "");
                            Actor* a  = deps_.ctx->world->spawnActor(nm);
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
                        // 메시가 여럿인 모델은 한 번의 클릭이 액터 여럿을 만들므로 한 커맨드로 쌓습니다.
                        if (deps_.onRecordCreation) deps_.onRecordCreation(made);
                    } catch (const std::exception& ex) {
                        if (deps_.logCat) HLOG(*deps_.logCat, Error) << "Spawn failed: " << ex.what();
                    }
                })
                ->content(HNew(SHorizontalBox)
                    ->addSlot(HNew(STextBlock)->text("[M]")->fontSize(s(12.f))->color(EditorTheme::badgeModel)
                        ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fixed, s(30.f))
                    ->addSlot(HNew(STextBlock)->text(display)->fontSize(s(13.f))->color(EditorUiKit::kTextBody)
                        ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fill)), path),
            SVerticalBox::ESizeRule::Fixed, s(EditorTheme::rowH));
    }
}

void ContentBrowserPanel::buildAudioSection(SVerticalBox& content) const {
    if (audioFiles_.empty()) return;
    content.addSlot(deps_.ui->spacer(), SVerticalBox::ESizeRule::Fixed, s(4.f));
    content.addSlot(
        deps_.ui->sectionHeader("AUDIO", EditorUiKit::kTextDim),
        SVerticalBox::ESizeRule::Fixed, s(22.f));
    for (const auto& stem : audioFiles_) {
        std::string fullPath = std::string(kAudioDir) + "/" + stem;
        content.addSlot(withActions(
            HNew(SButton)
                ->style(EditorUiKit::buttonStyle())
                ->cornerRadius(0.f)
                ->normalColor (EditorTheme::transparent)
                ->hoveredColor(EditorTheme::ctlHover)
                ->pressedColor(EditorTheme::accentPressed)
                ->padding({s(EditorTheme::space8), s(EditorTheme::space2)})
                ->hAlign(EHAlign::Fill)   // 기본 Center 면 항목이 가운데로 몰립니다(DEF-87)
                ->onClick([this, stem, fullPath]{
                    // 선택한 액터에 지정합니다. 오디오 컴포넌트가 없으면 붙입니다. 예전에는
                    // 선택이 없거나 컴포넌트가 없으면 아무 반응 없이 무시됐습니다.
                    Actor* sel = deps_.ctx->selected;
                    if (!sel || (deps_.isEditorCam && deps_.isEditorCam(sel))) {
                        if (deps_.logCat)
                            HLOG(*deps_.logCat, Warning) << "select an actor first to assign '" << stem << "'";
                        return;
                    }
                    // Undo 가능(2026-10-15): 컴포넌트를 새로 붙였으면 "붙임"(되살릴 때 파일까지), 있던
                    // 컴포넌트면 파일만 바꾼 것으로 쌓습니다.
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
                })
                ->content(HNew(SHorizontalBox)
                    ->addSlot(HNew(STextBlock)->text("[A]")->fontSize(s(12.f))
                        ->color(EditorTheme::badgeAudio)
                        ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fixed, s(30.f))
                    ->addSlot(HNew(STextBlock)->text(stem)->fontSize(s(13.f))->color(EditorUiKit::kTextBody)
                        ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fill)), fullPath),
            SVerticalBox::ESizeRule::Fixed, s(EditorTheme::rowH));
    }
}

void ContentBrowserPanel::buildPrefabsSection(SVerticalBox& content) const {
    if (prefabFiles_.empty()) return;
    content.addSlot(deps_.ui->spacer(), SVerticalBox::ESizeRule::Fixed, s(4.f));
    content.addSlot(
        deps_.ui->sectionHeader("PREFABS", EditorUiKit::kTextDim),
        SVerticalBox::ESizeRule::Fixed, s(22.f));
    for (const auto& stem : prefabFiles_) {
        std::string path = std::string(PrefabManager::kDir) + "/" + stem + PrefabManager::kExt;
        content.addSlot(withActions(
            HNew(SButton)
                ->style(EditorUiKit::buttonStyle())
                ->cornerRadius(0.f)
                ->normalColor (EditorTheme::transparent)
                ->hoveredColor(EditorTheme::ctlHover)
                ->pressedColor(EditorTheme::accentPressed)
                ->padding({s(EditorTheme::space8), s(EditorTheme::space2)})
                ->hAlign(EHAlign::Fill)   // 기본 Center 면 항목이 가운데로 몰립니다(DEF-87)
                ->onClick([this, path]{
                    if (!deps_.ctx->world || !deps_.ctx->assets) return;
                    Actor* a = PrefabManager::spawn(path, *deps_.ctx->world, *deps_.ctx->assets);
                    if (a) {
                        if (deps_.onSelectOnly) deps_.onSelectOnly(a);
                        if (deps_.onRecordCreation) deps_.onRecordCreation({ a });
                    }
                })
                ->content(HNew(SHorizontalBox)
                    ->addSlot(HNew(STextBlock)->text("[P]")->fontSize(s(12.f))->color(EditorTheme::badgePrefab)
                        ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fixed, s(30.f))
                    ->addSlot(HNew(STextBlock)->text(stem)->fontSize(s(13.f))->color(EditorUiKit::kTextBody)
                        ->hAlign(EHAlign::Left)->vAlign(EVAlign::Center), SHorizontalBox::ESizeRule::Fill)), path),
            SVerticalBox::ESizeRule::Fixed, s(EditorTheme::rowH));
    }
}
