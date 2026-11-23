// 에디터 실행 파일(HarukaEngine)의 엔진 앞단 선택(2026-10-19, 런타임 분리). 게임 실행 파일은 이 파일 대신
// game/GameFrontend.cpp 를 링크합니다.
#include "EditorApp.hpp"
#include "../core/AppPaths.hpp"
#include "../projectops/ProjectOps.hpp"
#include "../vfs/PathUtf8.hpp"

#include <filesystem>

void EditorApp::resolveRuntimePaths(const std::string& cliProjectArg) {
    resolvedProjectDir_.clear();
    resolvedEngineContentDir_.clear();
    projectResolveError_.clear();
    engineResolveError_.clear();
    startupLogs_.clear();

    const hprojops::ProjectResolveResult projRes =
        hprojops::resolveEditorProjectDir(cliProjectArg, happ::exeDir(), frontendConfig_.projectDir);
    if (!projRes.ok) {
        projectResolveError_ = projRes.errorMessage;
        if (!projRes.fixHint.empty()) {
            projectResolveError_ += "\n" + projRes.fixHint;
        }
        return;
    }

    resolvedProjectDir_ = hpak::utf8Of(projRes.projectDir);
    if (!projRes.sourceLog.empty()) startupLogs_.push_back(projRes.sourceLog);

    const hprojops::EngineResolveResult engRes =
        hprojops::resolveEngineForProject(projRes.projectDir);
    if (engRes.ok) {
        resolvedEngineContentDir_ = hpak::utf8Of(engRes.engineContentDir);
        if (!engRes.sourceLog.empty()) startupLogs_.push_back(engRes.sourceLog);
        return;
    }

    if (!frontendConfig_.engineContentDir.empty()) {
        std::error_code ec;
        std::filesystem::path fb;
        if (hpak::pathFromUtf8(frontendConfig_.engineContentDir, fb) &&
            std::filesystem::is_directory(fb, ec)) {
            resolvedEngineContentDir_ = hpak::utf8Of(fb);
            startupLogs_.push_back(
                "Engine content selected via compile-time fallback (HARUKA_ENGINE_CONTENT_DIR): " +
                resolvedEngineContentDir_);
            return;
        }
    }

    engineResolveError_ = engRes.errorMessage;
    if (!engRes.fixHint.empty()) {
        engineResolveError_ += "\n" + engRes.fixHint;
    }
}

IEngineFrontend::Policy EditorApp::policy() const {
    Policy p;
    // 에디터(E5): 프로젝트와 엔진 콘텐츠는 실행 때 찾으며, 못 찾으면 실행 폴더 복사본으로 넘어가지 않고 멈춥니다.
    p.requireProject       = true;
    p.requireEngineContent = true;
    p.projectDir           = resolvedProjectDir_;
    p.engineContentDir     = resolvedEngineContentDir_;
    p.projectResolveError  = projectResolveError_;
    p.engineResolveError   = engineResolveError_;
    p.startupLogs          = startupLogs_;
    // 나머지는 기본값입니다: VR 은 --xr 로만 준비하고 Play 동안만 헤드셋에 그림(2026-10-15), 헤드셋을 벗어도 Play 는 계속,
    // 런타임이 세션을 끝내면 데스크톱으로 계속하고 다음 Play 에서 다시 엶, 설정 · 로그는 실행 폴더.
    return p;
}

std::unique_ptr<IEngineFrontend> createEngineFrontend(const EngineFrontendConfig& config) {
    auto app = std::make_unique<EditorApp>();
    app->setFrontendConfig(config);
    return app;
}
