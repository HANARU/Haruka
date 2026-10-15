cmake_minimum_required(VERSION 3.21)
project(HarukaEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # Force English compiler diagnostics (suppress Korean locale output)
    set(ENV{VSLANG} 1033)
    add_compile_options(/wd4865 /wd4100 /Wv:18)
endif()

# ── Vulkan ────────────────────────────────────────────────────────────────────
find_package(Vulkan REQUIRED)

include(FetchContent)

# ── GLM (both platforms) ──────────────────────────────────────────────────────
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)
FetchContent_MakeAvailable(glm)

# ── tinygltf + stb_image (both platforms) ────────────────────────────────────
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        v2.9.3
)
set(TINYGLTF_BUILD_GL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_VALIDATOR   OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_BUILDER     OFF CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL           OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tinygltf)

# ── stb (stb_truetype용) ───────────────────────────────────────────────────────
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(stb)

# ── miniaudio (single-header audio) ──────────────────────────────────────────
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.21
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(miniaudio)

# ── JoltPhysics ───────────────────────────────────────────────────────────────
FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.1.0
    SOURCE_SUBDIR  Build
)
set(JPH_BUILD_SAMPLES    OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_TOOLS      OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(WARNINGS_AS_ERRORS   OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(JoltPhysics)

# VS 2026 (MSVC 19.51+): JoltPhysics hardcodes /WX which triggers C2220
# on Windows SDK 10.0.28000+ enum warnings (C4865). Strip /WX from Jolt.
if(MSVC AND TARGET Jolt)
    get_target_property(_jolt_opts Jolt COMPILE_OPTIONS)
    if(_jolt_opts)
        list(REMOVE_ITEM _jolt_opts "/WX")
        set_target_properties(Jolt PROPERTIES COMPILE_OPTIONS "${_jolt_opts}")
    endif()
    target_compile_options(Jolt PRIVATE /wd4865 /wd4100 /Wv:18)
endif()

# ── 셰이더 컴파일 (glslc → SPIR-V) ───────────────────────────────────────────
find_program(GLSLC glslc
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "$ENV{VULKAN_SDK}/bin"
        "C:/VulkanSDK/1.4.357.0/Bin"
    REQUIRED
)

set(SHADER_SRC_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BIN_DIR  ${CMAKE_CURRENT_BINARY_DIR}/shaders)
# glslc 의존성 파일(.d)은 실행 폴더로 복사되는 SHADER_BIN_DIR 밖에 둡니다.
set(SHADER_DEP_DIR  ${CMAKE_CURRENT_BINARY_DIR}/shader_deps)
# 실행 파일 옆 shaders/ (핫 리로드 감시 폴더). 셰이더 명령이 바뀐 .spv 를 바로 복사하므로
# Shaders 타깃만 빌드해도 실행 중인 에디터가 다시 읽습니다.
set(SHADER_RUN_DIR  $<TARGET_FILE_DIR:HarukaEngine>/shaders)
file(MAKE_DIRECTORY ${SHADER_BIN_DIR} ${SHADER_DEP_DIR})

# 셰이더 목록은 직접 적습니다. 목록에 없는 셰이더 파일은 컴파일되지 않으므로 구성 단계에서 경고합니다.
set(GLSL_SHADERS
    shadow.vert shadow.frag
    depth.vert  depth.frag
    pbr.vert    pbr.frag
    post.vert
    brdf_lut.frag
    linDepth.frag
    ssao.frag
    bloom_threshold.frag
    bloom_downsample.frag
    bloom_upsample.frag
    post.frag
    ui.vert     ui.frag
    line.vert   line.frag
)

file(GLOB SHADER_FILES_ON_DISK RELATIVE ${SHADER_SRC_DIR}
    ${SHADER_SRC_DIR}/*.vert ${SHADER_SRC_DIR}/*.frag ${SHADER_SRC_DIR}/*.comp)
foreach(F ${SHADER_FILES_ON_DISK})
    if(NOT F IN_LIST GLSL_SHADERS)
        message(WARNING "shaders/${F} is not in GLSL_SHADERS and will not be compiled")
    endif()
endforeach()

# DEPFILE: glslc -MD 가 셰이더가 #include 한 파일 목록을 쓰고, CMake 가 그 목록으로 다시
# 컴파일 여부를 정합니다(Visual Studio 생성기는 CMake 3.21 부터 지원). 이것이 없으면 include
# 파일만 고쳤을 때 아무것도 다시 컴파일되지 않아 낡은 SPIR-V 가 배포됩니다(DEF-11).
foreach(SHADER ${GLSL_SHADERS})
    set(IN  ${SHADER_SRC_DIR}/${SHADER})
    set(OUT ${SHADER_BIN_DIR}/${SHADER}.spv)
    set(DEP ${SHADER_DEP_DIR}/${SHADER}.d)
    add_custom_command(
        OUTPUT  ${OUT}
        COMMAND ${GLSLC} -MD -MF ${DEP} ${IN} -I ${SHADER_SRC_DIR}/include -o ${OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_RUN_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT} ${SHADER_RUN_DIR}/${SHADER}.spv
        DEPENDS ${IN}
        DEPFILE ${DEP}
        COMMENT "glslc -> ${SHADER}.spv"
    )
    list(APPEND SPIRV_OUTPUTS ${OUT})
endforeach()

add_custom_target(Shaders DEPENDS ${SPIRV_OUTPUTS})

# ── 에셋 디렉토리 ─────────────────────────────────────────────────────────────
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/models)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/cache)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/prefabs)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/audio)

# ── 실행 파일 및 플랫폼 소스 ──────────────────────────────────────────────────
set(CORE_SOURCES
    src/main.cpp
    src/core/Engine.cpp
    src/core/World.cpp
    src/core/Actor.cpp
    src/core/Component.cpp
    src/core/HLog.cpp
    src/components/CameraComponent.cpp
    src/components/LightComponent.cpp
    src/components/StaticMeshComponent.cpp
    src/components/AudioComponent.cpp
    src/components/RigidBodyComponent.cpp
    src/components/CoreComponents.cpp
    src/renderer/Renderer.cpp
    src/renderer/Renderer_swap.cpp
    src/renderer/Renderer_render.cpp
    src/renderer/GlfwSwapchain.cpp
    src/memory/HarukaAllocator.cpp
    src/renderer/RenderContext.cpp
    src/renderer/FrameResources.cpp
    src/renderer/GpuScene.cpp
    src/renderer/passes/BloomPass.cpp
    src/renderer/passes/AOPass.cpp
    src/renderer/passes/ShadowPass.cpp
    src/renderer/passes/PostPass.cpp
    src/renderer/passes/GeometryPass.cpp
    src/geometry/PrimitiveDesc.cpp
    src/geometry/PrimitiveBuilder.cpp
    src/assets/GltfLoader.cpp
    src/assets/AssetManager.cpp
    src/assets/GassetCache.cpp
    src/assets/PrefabManager.cpp
    src/assets/SceneSerializer.cpp
    src/assets/ComponentSerializer.cpp
    src/audio/AudioSystem.cpp
    src/audio/AudioAttenuation.cpp
    src/audio/MiniaudioImpl.cpp
    src/physics/PhysicsSystem.cpp
    src/ui/slate/HPaintContext.cpp
    src/ui/slate/HFontAtlas.cpp
    src/ui/slate/HSlateRenderer.cpp
    src/editor/EditorApp.cpp
    src/editor/EditorGizmo.cpp
    src/editor/ActorSnapshot.cpp
    src/editor/HotReloader.cpp
    src/game/DefaultGameModule.cpp
    src/input/HInputSystem.cpp
)

if(ANDROID)
    list(APPEND CORE_SOURCES
        src/platform/android/AndroidWindow.cpp
    )
    add_library(HarukaEngine SHARED ${CORE_SOURCES})
    target_link_libraries(HarukaEngine PRIVATE Vulkan::Vulkan android log glm::glm tinygltf Jolt)
    target_include_directories(HarukaEngine PRIVATE ${stb_SOURCE_DIR} ${miniaudio_SOURCE_DIR})
else()
    # ── GLFW (desktop only) ───────────────────────────────────────────────────
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
    )
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)

    # ── OpenXR (desktop only: PC VR, Quest Link) ──────────────────────────────
    # Khronos 공식 로더와 헤더입니다. 로더는 정적 라이브러리로 링크해 DLL 을 따로 옮기지 않습니다.
    # 런타임(Meta Quest Link · SteamVR 등)은 사용자 PC 에 설치된 것을 로더가 찾습니다. `--xr` 로 켜고,
    # 런타임이나 헤드셋이 없으면 엔진은 데스크톱으로 계속합니다. Quest 단독 실행은 Android 이식 때 붙입니다.
    FetchContent_Declare(
        openxr
        GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
        GIT_TAG        release-1.1.63
    )
    set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(openxr)

    list(APPEND CORE_SOURCES
        src/platform/glfw/GlfwWindow.cpp
        src/xr/XrSystem.cpp
    )

    # ── Windows 실행 파일·창 아이콘 ────────────────────────────────────────────
    # res/Haruka.rc 가 다중 크기 .ico 하나를 실행 파일 안에 넣습니다. GLFW 는 창
    # 클래스를 등록할 때 GLFW_ICON 이라는 이름의 아이콘 리소스를 직접 찾아 가므로
    # (win32_window.c) 런타임에서 glfwSetWindowIcon 을 호출할 필요가 없습니다.
    # 어느 크기를 쓸지는 표시 위치와 배율을 보고 Windows 가 고릅니다.
    if(WIN32)
        enable_language(RC)
        list(APPEND CORE_SOURCES res/Haruka.rc)
    endif()

    add_executable(HarukaEngine ${CORE_SOURCES})
    target_link_libraries(HarukaEngine PRIVATE Vulkan::Vulkan glfw glm::glm tinygltf Jolt openxr_loader)
    target_include_directories(HarukaEngine PRIVATE ${stb_SOURCE_DIR} ${miniaudio_SOURCE_DIR})
endif()

# ── GLM 설정 ──────────────────────────────────────────────────────────────────
# 투영 행렬의 깊이 범위를 Vulkan 규약 [0,1] 로 둡니다. 예전에는 헤더마다 #define 했는데, 매크로 없이
# GLM 을 먼저 포함한 헤더(Reflect.hpp)가 있으면 그 번역 단위는 OpenGL 규약 [-1,1] 로 굳었습니다.
# 그래서 카메라 투영만 [-1,1] 이 되어 실제 근평면이 두 배(0.2m)였습니다(DEF-95). 소스의 개별 정의는
# 두지 않습니다. 이 디렉터리의 대상(엔진·테스트)에는 위치와 상관없이 붙고, 이 줄보다 먼저 추가한
# 외부 라이브러리 하위 디렉터리(GLFW 등)에는 붙지 않도록 외부 라이브러리를 다 불러온 뒤에 둡니다.
add_compile_definitions(GLM_FORCE_DEPTH_ZERO_TO_ONE)

add_dependencies(HarukaEngine Shaders)
target_include_directories(HarukaEngine PRIVATE
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
if(NOT ANDROID)
    target_compile_definitions(HarukaEngine PRIVATE HARUKA_EDITOR)
endif()
if(MSVC)
    target_compile_options(HarukaEngine PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
    # /SUBSYSTEM:CONSOLE 이 콘솔 창을 띄우던 원인이다. WINDOWS 서브시스템으로 두되
    # 진입점을 mainCRTStartup 으로 지정하면 WinMain 을 새로 쓰지 않고 int main() 을
    # 그대로 쓰면서 창만 사라진다.
    # 콘솔이 없으면 stdout 이 갈 곳도 없어지므로 main.cpp 가 파일로 우회시킨다.
    # 디버깅 중 콘솔이 필요하면 -DHARUKA_CONSOLE=ON 으로 구성하면 된다.
    option(HARUKA_CONSOLE "Open a console window alongside the engine" OFF)
    if(HARUKA_CONSOLE)
        set_target_properties(HarukaEngine PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    else()
        set_target_properties(HarukaEngine PROPERTIES
            LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
    endif()
else()
    target_compile_options(HarukaEngine PRIVATE -Wall -Wextra)
endif()

# 셰이더 바이너리를 실행 파일 옆으로 복사
add_custom_command(TARGET HarukaEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SHADER_BIN_DIR}
        $<TARGET_FILE_DIR:HarukaEngine>/shaders
    COMMENT "Copying SPIR-V shaders to output dir"
)

# 에셋을 실행 파일 옆으로 복사
add_custom_command(TARGET HarukaEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:HarukaEngine>/assets
    COMMENT "Copying assets to output dir"
)

# ── 지오메트리 규약 테스트 ────────────────────────────────────────────────────
# Vulkan 도 디바이스도 창도 필요 없습니다. 생성과 업로드를 분리해 둔 덕분이며,
# 탄젠트 handedness 나 와인딩 같은 결함은 실행이 아니라 검산으로만 잡힙니다.
if(NOT ANDROID)
    add_executable(HarukaGeometryTest
        tests/GeometryTest.cpp
        src/geometry/PrimitiveDesc.cpp
        src/geometry/PrimitiveBuilder.cpp
        src/core/HLog.cpp
    )
    target_link_libraries(HarukaGeometryTest PRIVATE glm::glm)
    target_include_directories(HarukaGeometryTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaGeometryTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaGeometryTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # ── Actor 활성·컴포넌트 생명주기 테스트 ──────────────────────────────────
    # Vulkan 도 창도 World 도 없이 Actor + Component 만 링크합니다. setActive 의
    # 계층 전파와 onEnable/onDisable 엣지는 화면이 아니라 검산으로 잡습니다.
    add_executable(HarukaActorTest
        tests/ActorLifecycleTest.cpp
        src/core/Actor.cpp
        src/core/Component.cpp
        src/core/HLog.cpp
        src/components/LightComponent.cpp
        src/components/CameraComponent.cpp
    )
    target_link_libraries(HarukaActorTest PRIVATE glm::glm)
    target_include_directories(HarukaActorTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaActorTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaActorTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 두 눈 렌더링 수학 테스트 --------------------------------------------
    # 눈별 비대칭 투영(OpenXR 시야각)과 두 눈 컬링을 화면 없이 검산합니다(DEF-09 · 21 · 31).
    add_executable(HarukaStereoTest
        tests/StereoViewTest.cpp
        src/components/CameraComponent.cpp
        src/core/Actor.cpp
        src/core/Component.cpp
        src/core/HLog.cpp
    )
    target_link_libraries(HarukaStereoTest PRIVATE glm::glm)
    target_include_directories(HarukaStereoTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaStereoTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaStereoTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 자동 인스턴싱 테스트 ------------------------------------------------
    # 같은 지오메트리 · 머티리얼의 묶기와 transform 행 배정을 화면 없이 검산합니다(DEF-16).
    add_executable(HarukaBatchTest tests/DrawBatchingTest.cpp)
    target_include_directories(HarukaBatchTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaBatchTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaBatchTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 컴포넌트 직렬화 테스트 ---------------------------------------------
    # 타입 등록 · 이름 붙은 값의 왕복 · 원문 보존 · 옛 이름을 화면 없이 검산합니다(DEF-22).
    add_executable(HarukaSceneTest
        tests/SceneComponentTest.cpp
        src/assets/ComponentSerializer.cpp
        src/core/Actor.cpp
        src/core/Component.cpp
        src/core/HLog.cpp
        src/components/LightComponent.cpp
        src/components/CameraComponent.cpp
    )
    target_link_libraries(HarukaSceneTest PRIVATE glm::glm)
    target_include_directories(HarukaSceneTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaSceneTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaSceneTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 라이트 선택 · 상한 설정 테스트 ---------------------------------------
    # 화면 밖 라이트 거르기 · 상한에서 고르기(DEF-33)와 Config/engine.ini 읽기(DEF-10)를 화면 없이 검산합니다.
    add_executable(HarukaLightTest tests/LightSelectionTest.cpp)
    target_link_libraries(HarukaLightTest PRIVATE glm::glm)
    target_include_directories(HarukaLightTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaLightTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaLightTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- GLTF 캐시 유효성 테스트 ---------------------------------------------
    # 수정 시각만 바뀐 원본(빌드의 에셋 복사)에는 캐시를 쓰고, 내용이 바뀌면 다시 읽는지(DEF-105) 검산합니다.
    add_executable(HarukaAssetCacheTest
        tests/AssetCacheTest.cpp
        src/assets/GassetCache.cpp
        src/core/HLog.cpp
    )
    target_include_directories(HarukaAssetCacheTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaAssetCacheTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaAssetCacheTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 렌더 확장 지점 테스트 -----------------------------------------------
    # 패스 레지스트리의 수명주기 순서와 지점별 호출을 화면 없이 검산합니다(DEF-17 · 18).
    add_executable(HarukaRenderExtTest tests/RenderExtensionTest.cpp)
    target_link_libraries(HarukaRenderExtTest PRIVATE glm::glm)
    # RenderExtension.hpp 가 Vulkan 형을 쓰므로 헤더 경로만 둡니다(링크는 필요 없음).
    target_include_directories(HarukaRenderExtTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(HarukaRenderExtTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaRenderExtTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- VR 입력 테스트 -------------------------------------------------------
    # XR 모듈이 넣는 컨트롤러 상태가 축 · 디지털 액션으로 나오는지 검산합니다(DEF-20).
    add_executable(HarukaInputTest
        tests/InputTest.cpp
        src/input/HInputSystem.cpp
    )
    target_link_libraries(HarukaInputTest PRIVATE glm::glm)
    # IWindow.hpp 가 Vulkan 형(VkSurfaceKHR)을 쓰므로 헤더 경로만 둡니다(링크는 필요 없음).
    target_include_directories(HarukaInputTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(HarukaInputTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaInputTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 커맨드 스택 purge 테스트 --------------------------------------------
    # 헤더 전용이라 링크할 소스가 없습니다. 죽은 대상을 참조하는 커맨드가 undo/redo
    # 양쪽에서 걷히는지 검산합니다(컴포넌트만 제거되는 경로 포함).
    add_executable(HarukaCommandTest tests/CommandStackTest.cpp)
    target_include_directories(HarukaCommandTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaCommandTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaCommandTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 액터 생성 · 삭제 Undo 테스트 -----------------------------------------
    # 서브트리를 떠 두고 원래 ID · 원래 부모로 되살리는지, World 의 ID 순서와 다음 ID 가 지켜지는지,
    # 커맨드 스택과 purge 사이의 규칙을 검산합니다. 씬 직렬화 대신 테스트용 글 형식을 넘깁니다.
    add_executable(HarukaActorUndoTest
        tests/ActorUndoTest.cpp
        src/editor/ActorSnapshot.cpp
        src/core/World.cpp
        src/core/Actor.cpp
        src/core/Component.cpp
        src/core/HLog.cpp
    )
    target_link_libraries(HarukaActorUndoTest PRIVATE glm::glm)
    target_include_directories(HarukaActorUndoTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaActorUndoTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaActorUndoTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 로그 카테고리 테스트 ------------------------------------------------
    # HLog.cpp 하나만 링크합니다. 꺼진 로그의 인자 미평가, 같은 이름의 상태 공유, 링 버퍼
    # seq 연속성, 표준 출력 어댑터의 태그 해석, 다중 스레드 제출을 검산합니다.
    add_executable(HarukaLogTest tests/LogTest.cpp src/core/HLog.cpp)
    target_include_directories(HarukaLogTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaLogTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaLogTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 오디오 테스트 ------------------------------------------------------
    # 출력 장치 없는 miniaudio 엔진(noDevice)에서 출력 샘플을 직접 당겨, 청취자 방향·거리
    # 감쇠·일시정지·비활성 정지·OGG 디코딩을 귀가 아니라 숫자로 검산합니다. Vulkan 도 창도
    # 필요 없습니다. OGG 확인에는 저장소의 시험용 음원을 쓰므로 소스 경로를 넘깁니다.
    add_executable(HarukaAudioTest
        tests/AudioTest.cpp
        src/audio/AudioSystem.cpp
        src/audio/AudioAttenuation.cpp
        src/audio/MiniaudioImpl.cpp
        src/components/AudioComponent.cpp
        src/components/CameraComponent.cpp
        src/core/World.cpp
        src/core/Actor.cpp
        src/core/Component.cpp
        src/core/HLog.cpp
    )
    target_link_libraries(HarukaAudioTest PRIVATE glm::glm)
    target_include_directories(HarukaAudioTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${miniaudio_SOURCE_DIR})
    target_compile_definitions(HarukaAudioTest PRIVATE
        HARUKA_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
    if(MSVC)
        target_compile_options(HarukaAudioTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaAudioTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- HSlate 입력 테스트 --------------------------------------------------
    # 포인터 캡처와 숫자 칸의 프레스 추적을 화면 없이 검산합니다. 렌더러의 입력 경로는
    # Vulkan 을 부르지 않지만, 같은 파일의 렌더 코드 때문에 Vulkan 로더에 링크합니다.
    add_executable(HarukaSlateTest
        tests/SlateInputTest.cpp
        src/ui/slate/HSlateRenderer.cpp
        src/ui/slate/HFontAtlas.cpp
        src/ui/slate/HPaintContext.cpp
        src/core/HLog.cpp
    )
    target_link_libraries(HarukaSlateTest PRIVATE Vulkan::Vulkan glm::glm)
    target_include_directories(HarukaSlateTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src ${stb_SOURCE_DIR} ${Vulkan_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(HarukaSlateTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaSlateTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 물리 바디 생명주기 테스트 -------------------------------------------
    # World + Actor + RigidBodyComponent + PhysicsSystem(Jolt) 만 링크합니다. 끈 액터의 바디
    # 제거와 잠든 바디 깨우기, 편집 확정 때만 재생성, 마찰 즉시 반영, 이름 직렬화를 검산합니다.
    add_executable(HarukaPhysicsTest
        tests/PhysicsTest.cpp
        src/physics/PhysicsSystem.cpp
        src/components/RigidBodyComponent.cpp
        src/core/World.cpp
        src/core/Actor.cpp
        src/core/Component.cpp
        src/core/HLog.cpp
    )
    target_link_libraries(HarukaPhysicsTest PRIVATE glm::glm Jolt)
    target_include_directories(HarukaPhysicsTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaPhysicsTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaPhysicsTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    enable_testing()
    add_test(NAME geometry_conventions COMMAND HarukaGeometryTest)
    add_test(NAME actor_lifecycle      COMMAND HarukaActorTest)
    add_test(NAME command_stack_purge  COMMAND HarukaCommandTest)
    add_test(NAME actor_undo           COMMAND HarukaActorUndoTest)
    add_test(NAME log_category         COMMAND HarukaLogTest)
    add_test(NAME audio_system         COMMAND HarukaAudioTest)
    add_test(NAME slate_input          COMMAND HarukaSlateTest)
    add_test(NAME physics_lifecycle    COMMAND HarukaPhysicsTest)
    add_test(NAME stereo_views         COMMAND HarukaStereoTest)
    add_test(NAME xr_input             COMMAND HarukaInputTest)
    add_test(NAME draw_batching        COMMAND HarukaBatchTest)
    add_test(NAME render_extensions    COMMAND HarukaRenderExtTest)
    add_test(NAME scene_components     COMMAND HarukaSceneTest)
    add_test(NAME light_selection      COMMAND HarukaLightTest)
    add_test(NAME asset_cache          COMMAND HarukaAssetCacheTest)
endif()
