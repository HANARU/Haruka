cmake_minimum_required(VERSION 3.20)
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
file(MAKE_DIRECTORY ${SHADER_BIN_DIR})

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

foreach(SHADER ${GLSL_SHADERS})
    set(IN  ${SHADER_SRC_DIR}/${SHADER})
    set(OUT ${SHADER_BIN_DIR}/${SHADER}.spv)
    add_custom_command(
        OUTPUT  ${OUT}
        COMMAND ${GLSLC} ${IN} -I ${SHADER_SRC_DIR}/include -o ${OUT}
        DEPENDS ${IN}
        COMMENT "glslc -> ${SHADER}.spv"
    )
    list(APPEND SPIRV_OUTPUTS ${OUT})
endforeach()

# Compute shaders (no -fshader-stage flag needed, glslc detects .comp)
set(COMP_SHADERS cull.comp)
foreach(SHADER ${COMP_SHADERS})
    set(IN  ${SHADER_SRC_DIR}/${SHADER})
    set(OUT ${SHADER_BIN_DIR}/${SHADER}.spv)
    add_custom_command(
        OUTPUT  ${OUT}
        COMMAND ${GLSLC} ${IN} -I ${SHADER_SRC_DIR}/include -o ${OUT}
        DEPENDS ${IN}
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
    src/components/CameraComponent.cpp
    src/components/LightComponent.cpp
    src/components/StaticMeshComponent.cpp
    src/components/AudioComponent.cpp
    src/components/RigidBodyComponent.cpp
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
    src/renderer/passes/CullPass.cpp
    src/geometry/PrimitiveDesc.cpp
    src/geometry/PrimitiveBuilder.cpp
    src/assets/GltfLoader.cpp
    src/assets/AssetManager.cpp
    src/assets/GassetCache.cpp
    src/assets/PrefabManager.cpp
    src/assets/SceneSerializer.cpp
    src/audio/AudioSystem.cpp
    src/physics/PhysicsSystem.cpp
    src/ui/slate/HPaintContext.cpp
    src/ui/slate/HFontAtlas.cpp
    src/ui/slate/HSlateRenderer.cpp
    src/editor/EditorApp.cpp
    src/editor/EditorContext.cpp
    src/editor/EditorGizmo.cpp
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

    list(APPEND CORE_SOURCES
        src/platform/glfw/GlfwWindow.cpp
    )
    add_executable(HarukaEngine ${CORE_SOURCES})
    target_link_libraries(HarukaEngine PRIVATE Vulkan::Vulkan glfw glm::glm tinygltf Jolt)
    target_include_directories(HarukaEngine PRIVATE ${stb_SOURCE_DIR} ${miniaudio_SOURCE_DIR})
endif()

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
    )
    target_link_libraries(HarukaActorTest PRIVATE glm::glm)
    target_include_directories(HarukaActorTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaActorTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaActorTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    enable_testing()
    add_test(NAME geometry_conventions COMMAND HarukaGeometryTest)
    add_test(NAME actor_lifecycle      COMMAND HarukaActorTest)
endif()
