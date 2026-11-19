#pragma once
#include "EditorUiKit.hpp"
#include "EditorContext.hpp"
#include "HCommandStack.hpp"
#include "../core/HLog.hpp"
#include "../ui/slate/HSlate.hpp"
#include <functional>
#include <string>
#include <vector>

class Actor;

// 하단 Content Browser 탭(S6c).
// 프로젝트 아래 씬(.hscene) · 모델(.glb/.gltf) · 소리(.wav/.mp3/.ogg/.flac) · 프리팹 목록을
// 자신이 소유하고 탐색·표시합니다. 항목 클릭 시의 편집/열기 동작은 콜백으로 EditorApp 에 위임합니다.
class ContentBrowserPanel {
public:
    struct Deps {
        EditorUiKit*   ui       = nullptr;
        EditorContext* ctx      = nullptr;
        HCommandStack* cmdStack = nullptr;
        HLogCategory*  logCat   = nullptr;
        std::function<std::string()>                 currentScenePath;
        std::function<void(const std::string&)>      onOpenScene;
        std::function<void(Actor*)>                  onSelectOnly;
        std::function<void(const std::vector<Actor*>&)> onRecordCreation;
        std::function<bool(const Actor*)>            isEditorCam;
        std::function<void()>                        onDetailsDirty;
    };

    void init(Deps deps);
    void scanAll();
    void scanScenes();
    void refreshPrefabs();

    const std::vector<std::string>& sceneFiles() const { return sceneFiles_; }
    void buildTab(SVerticalBox& content) const;

private:
    float s(float x) const { return deps_.ui->s(x); }
    void buildScenesSection(SVerticalBox& content) const;
    void buildModelsSection(SVerticalBox& content) const;
    void buildAudioSection(SVerticalBox& content) const;
    void buildPrefabsSection(SVerticalBox& content) const;

    Deps deps_;
    std::vector<std::string> sceneFiles_;    // assets/scenes 아래 .hscene 논리 경로(정렬)
    std::vector<std::string> modelFiles_;
    std::vector<std::string> audioFiles_;
    std::vector<std::string> prefabFiles_;
};
