cmake_minimum_required(VERSION 3.21)
# C 는 LZ4(lz4.c, 2026-10-24) 때문에 켭니다. 켜지 않으면 .c 파일이 C 런타임 설정 없이(정적 런타임) 컴파일되어 엔진의
# 동적 런타임과 충돌합니다(LNK4098).
project(HarukaEngine VERSION 0.1.0 LANGUAGES C CXX)

option(HARUKA_BUILD_SAMPLES "Build the bundled Samples project" ${PROJECT_IS_TOP_LEVEL})
option(HARUKA_BUILD_TESTS   "Build Haruka unit tests"           ${PROJECT_IS_TOP_LEVEL})
option(HARUKA_PACKAGE_GAME  "Enable CPack Windows ZIP packaging for Samples" ${PROJECT_IS_TOP_LEVEL})
# CPU 구간 계측 스코프(HPROF_SCOPE, Docs/CPU_Profiling.md D2). 끄면 매크로가 비어 비용이 없습니다. 기본은 모든 구성에서 켬
# (Quest 실측은 Release 로 하므로). 기록은 --trace · trace.start 로 켤 때만 합니다.
option(HARUKA_PROFILER      "Compile CPU profiling scopes (HPROF_SCOPE)" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release 도 PDB(디버그 기호)를 만듭니다(크래시 보고 D4, Docs/Crash_Reporting.md). 최적화는 그대로이고(/OPT:REF · /OPT:ICF 를
# 다시 켬), 실행 파일에는 PDB 파일 이름만 넣어(MSBuild 의 $(TargetName), CMake 가 %_PDB% 의 % 를 두 겹으로 써서 쓰지 않음) 빌드 PC 의 경로가 배포 파일에 남지 않게 합니다. PDB 는 배포 ZIP 에 넣지 않습니다.
if(MSVC)
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " /Zi")
    string(APPEND CMAKE_C_FLAGS_RELEASE " /Zi")
    foreach(kind EXE SHARED MODULE)
        string(APPEND CMAKE_${kind}_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF /PDBALTPATH:$(TargetName).pdb")
    endforeach()
endif()

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

# ── Steam Audio (HRTF, 2026-11-06) ────────────────────────────────────────────
# 릴리스 ZIP 을 받아 헤더와 이 플랫폼의 라이브러리만 풉니다. 끄려면 -DHARUKA_STEAM_AUDIO=OFF(엔진은 패닝으로 동작).
include(cmake/SteamAudio.cmake)

# ── LZ4 · xxHash (묶음 파일 .hpak, 2026-10-24) ─────────────────────────────────
# 두 라이브러리 모두 BSD-2 입니다(LZ4 는 lib/ 아래). 저장소의 CMake 를 쓰지 않고 소스만 받습니다(SOURCE_SUBDIR 에
# CMakeLists.txt 가 없으면 받기만 함). LZ4 는 lz4.c · lz4hc.c 두 파일을 정적 라이브러리로, xxHash 는 헤더 하나로 씁니다.
FetchContent_Declare(
    lz4
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    GIT_TAG        v1.10.0
    SOURCE_SUBDIR  _haruka_sources_only
)
FetchContent_Declare(
    xxhash
    GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
    GIT_TAG        v0.8.3
    SOURCE_SUBDIR  _haruka_sources_only
)
FetchContent_MakeAvailable(lz4 xxhash)
add_library(haruka_lz4 STATIC ${lz4_SOURCE_DIR}/lib/lz4.c ${lz4_SOURCE_DIR}/lib/lz4hc.c)
target_include_directories(haruka_lz4 PUBLIC ${lz4_SOURCE_DIR}/lib)

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
# Jolt 은 기본값(꺼짐)이면 MSVC 에서 _HAS_EXCEPTIONS=0 을 PUBLIC 정의로 걸어, Jolt 를 링크하는 엔진 전체가 "예외 없는 STL" 로
# 컴파일되었습니다(2026-11-01, DEF-157). 그 모드의 std::exception 은 메시지를 복사하지 않고 포인터만 쥐므로, std::string 으로 만든
# 예외(핵심 힙 실패 · GPU 를 고르지 못한 이유 · 셰이더 열기 실패 등)의 FATAL 문구가 Windows 에서 해제된 메모리를 읽었습니다.
# 엔진은 예외를 쓰므로 켭니다(Jolt 자신은 예외를 던지지 않음). src/runtime/Engine.cpp 가 다시 새어 들어오면 컴파일을 멈춥니다.
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
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

# ── meshoptimizer (메시 LOD 자동 생성, O13) ───────────────────────────────────
FetchContent_Declare(
    meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG        v1.3
)
set(MESHOPT_BUILD_DEMO     OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(meshoptimizer)

# ── nanosvg (에디터 아이콘 SVG 래스터화, D3 2026-11-19) ─────────────────────────
# zlib 라이선스, 헤더 두 개(nanosvg.h · nanosvgrast.h)만 씁니다. 판이 없는 저장소라 커밋으로 고정하고, 저장소의 CMake 는
# 쓰지 않습니다(SOURCE_SUBDIR 에 CMakeLists.txt 가 없으면 받기만 함). 구현은 src/editor/EditorIcons.cpp 한 곳에서 켭니다.
FetchContent_Declare(
    nanosvg
    GIT_REPOSITORY https://github.com/memononen/nanosvg.git
    GIT_TAG        239e102ec2c691f2902e20ace2ed36ee4a35cfe6
    SOURCE_SUBDIR  _haruka_sources_only
)
FetchContent_MakeAvailable(nanosvg)
add_library(haruka_nanosvg INTERFACE)
target_include_directories(haruka_nanosvg SYSTEM INTERFACE ${nanosvg_SOURCE_DIR}/src)

# ── recastnavigation (내비 메시 굽기 · 경로 찾기, 2026-11-21, Docs/Navigation_AI.md D1) ─────────────────────
# zlib 라이선스. 판이 고정된 압축 파일을 SHA256 으로 확인해 받고, 저장소의 CMake(데모 · 시험 · 설치)는 쓰지 않습니다(SOURCE_SUBDIR 에
# CMakeLists.txt 가 없으면 받기만 함). Recast(굽기) · Detour(질의) 소스만 정적 라이브러리로 묶어 데스크톱 · Android 엔진에 링크합니다.
# DetourCrowd · DetourTileCache · DebugUtils 는 넣지 않습니다. Recast · Detour 형은 src/nav 의 .cpp 안에서만 씁니다.
FetchContent_Declare(
    recastnavigation
    URL      https://github.com/recastnavigation/recastnavigation/archive/refs/tags/v1.6.0.tar.gz
    URL_HASH SHA256=d48ca0121962fa0639502c0f56c4e3ae72f98e55d88727225444f500775c0074
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR _haruka_sources_only
)
FetchContent_MakeAvailable(recastnavigation)
file(GLOB _haruka_recast_sources ${recastnavigation_SOURCE_DIR}/Recast/Source/*.cpp ${recastnavigation_SOURCE_DIR}/Detour/Source/*.cpp)
add_library(haruka_recast STATIC ${_haruka_recast_sources})
target_include_directories(haruka_recast SYSTEM PUBLIC
    ${recastnavigation_SOURCE_DIR}/Recast/Include ${recastnavigation_SOURCE_DIR}/Detour/Include)
if(MSVC)
    target_compile_options(haruka_recast PRIVATE /W0 /utf-8)
else()
    target_compile_options(haruka_recast PRIVATE -w)
endif()

# ── 텍스처 인코더 (묶음 도구 전용, 2026-11-21, Docs/Texture_Compression.md D7) ─────────────────
# 묶을 때 텍스처를 BC(PC) · ASTC(Android)로 압축합니다. HarukaPak 과 시험에만 링크하고, 게임 런타임 · Android 실행 파일에는
# 넣지 않습니다(런타임은 미리 압축한 KTX2 만 읽음). 판이 고정된 압축 파일을 SHA256 으로 확인해 받고, 저장소의 CMake 는 쓰지
# 않습니다(SOURCE_SUBDIR 에 CMakeLists.txt 가 없으면 받기만 함). 내려받기: astc-encoder 약 35 MB, bc7enc_rdo 약 15 MB.
#   astc-encoder 5.7.1 (ARM, Apache-2.0): Source/astcenc_*.cpp(CLI 제외)를 SSE4.1 · POPCNT 정의로 직접 컴파일합니다.
#   bc7enc_rdo (Richard Geldreich, MIT 또는 공개 도메인): bc7enc.cpp(BC7) · bc7decomp.cpp(BC7 풀기) · rgbcx.cpp(BC5) 세 파일만.
if(NOT ANDROID)
    FetchContent_Declare(
        astcenc
        URL      https://github.com/ARM-software/astc-encoder/archive/refs/tags/5.7.1.tar.gz
        URL_HASH SHA256=37ff3928c4c156fb3ac0d1adb5df144ff9c5cdbc8a8e1ffeb20229cc7f35c767
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR _haruka_sources_only
    )
    FetchContent_Declare(
        bc7enc_rdo
        URL      https://github.com/richgel999/bc7enc_rdo/archive/b9438627eef73a1157e84201b6fa6eb2ffd6d9f0.tar.gz
        URL_HASH SHA256=bbb33d1dbb6178a3a2c044956c3cc0b8ea9fc2f2903824379dd86f87d53b64c6
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR _haruka_sources_only
    )
    FetchContent_MakeAvailable(astcenc bc7enc_rdo)
    file(GLOB _haruka_astcenc_sources ${astcenc_SOURCE_DIR}/Source/astcenc_*.cpp)
    add_library(haruka_astcenc STATIC ${_haruka_astcenc_sources})
    target_include_directories(haruka_astcenc SYSTEM PUBLIC ${astcenc_SOURCE_DIR}/Source)
    target_compile_definitions(haruka_astcenc PRIVATE
        ASTCENC_NEON=0 ASTCENC_SVE=0 ASTCENC_SSE=41 ASTCENC_AVX=0 ASTCENC_POPCNT=1 ASTCENC_F16C=0 ASTCENC_X86_GATHERS=0)
    add_library(haruka_bc7enc STATIC
        ${bc7enc_rdo_SOURCE_DIR}/bc7enc.cpp
        ${bc7enc_rdo_SOURCE_DIR}/bc7decomp.cpp
        ${bc7enc_rdo_SOURCE_DIR}/rgbcx.cpp)
    target_include_directories(haruka_bc7enc SYSTEM PUBLIC ${bc7enc_rdo_SOURCE_DIR})
    if(MSVC)
        target_compile_options(haruka_astcenc PRIVATE /W0 /utf-8)
        target_compile_options(haruka_bc7enc  PRIVATE /W0 /utf-8)
    else()
        target_compile_options(haruka_astcenc PRIVATE -w -msse4.1 -mpopcnt)
        target_compile_options(haruka_bc7enc  PRIVATE -w)
    endif()
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
# Shaders 타깃만 빌드해도 실행 중인 에디터가 다시 읽습니다. 에디터와 게임 실행 파일은 같은 폴더에 생기며,
# Android 에도 있는 게임 대상으로 그 폴더를 가리킵니다(2026-10-19).
if(HARUKA_BUILD_SAMPLES)
    set(SHADER_RUN_DIR  $<TARGET_FILE_DIR:HarukaGame>/shaders)
else()
    set(SHADER_RUN_DIR "")
endif()
file(MAKE_DIRECTORY ${SHADER_BIN_DIR} ${SHADER_DEP_DIR})

# 셰이더 목록은 직접 적습니다. 목록에 없는 셰이더 파일은 컴파일되지 않으므로 구성 단계에서 경고합니다.
set(GLSL_SHADERS
    shadow.vert shadow.frag
    depth.vert  depth.frag
    pbr.vert    pbr.frag
    post.vert
    brdf_lut.frag
    linDepth.frag
    ssao.frag   ssao_blur.frag
    bloom_threshold.frag
    bloom_downsample.frag
    bloom_upsample.frag
    post.frag
    ui.vert     ui.frag
    line.vert   line.frag
    ui_panel.vert ui_panel.frag
    particle.vert particle.frag
)

file(GLOB SHADER_FILES_ON_DISK RELATIVE ${SHADER_SRC_DIR}
    ${SHADER_SRC_DIR}/*.vert ${SHADER_SRC_DIR}/*.frag ${SHADER_SRC_DIR}/*.comp)
foreach(F ${SHADER_FILES_ON_DISK})
    if(NOT F IN_LIST GLSL_SHADERS)
        message(WARNING "shaders/${F} is not in GLSL_SHADERS and will not be compiled")
    endif()
endforeach()

# 목록에서 빠진 셰이더의 옛 결과물(예: 2026-10-04 에 지운 cull.comp.spv)을 지웁니다. 남아 있으면 게임 묶음(game.hpak)에
# 따라 들어갑니다(2026-10-24). 지웠으면 묶음도 지워 다음 빌드가 다시 만들게 합니다(2026-11-01). 묶음의 입력에서 파일이 빠지는 것만으로는
# 다시 만들 이유가 되지 않아, 지운 셰이더가 다음에 묶음을 만들 때까지 남았습니다.
set(GAME_PACK ${CMAKE_CURRENT_BINARY_DIR}/pack/game.hpak)
file(GLOB _spv_in_bin RELATIVE ${SHADER_BIN_DIR} ${SHADER_BIN_DIR}/*.spv)
foreach(F ${_spv_in_bin})
    string(REGEX REPLACE "\\.spv$" "" _src "${F}")
    if(NOT _src IN_LIST GLSL_SHADERS)
        file(REMOVE ${SHADER_BIN_DIR}/${F} ${GAME_PACK})
        message(STATUS "Removed stale shader binary ${F}")
    endif()
endforeach()

# DEPFILE: glslc -MD 가 셰이더가 #include 한 파일 목록을 쓰고, CMake 가 그 목록으로 다시
# 컴파일 여부를 정합니다(Visual Studio 생성기는 CMake 3.21 부터 지원). 이것이 없으면 include
# 파일만 고쳤을 때 아무것도 다시 컴파일되지 않아 낡은 SPIR-V 가 배포됩니다(DEF-11).
# --target-env=vulkan1.0: SPIR-V 1.0 으로 고정합니다(Vulkan 1.1 기준선, 2026-10-31 DEF-97). 지금 glslc 기본값과 같아
# 결과물은 바뀌지 않지만, 도구 버전이나 플래그가 바뀌어 1.1 장치(Quest 2 · Pro)가 받을 수 없는 SPIR-V 1.4 이상이 나오는 것을 막습니다.
foreach(SHADER ${GLSL_SHADERS})
    set(IN  ${SHADER_SRC_DIR}/${SHADER})
    set(OUT ${SHADER_BIN_DIR}/${SHADER}.spv)
    set(DEP ${SHADER_DEP_DIR}/${SHADER}.d)
    if(SHADER_RUN_DIR)
        add_custom_command(
            OUTPUT  ${OUT}
            COMMAND ${GLSLC} --target-env=vulkan1.0 -MD -MF ${DEP} ${IN} -I ${SHADER_SRC_DIR}/include -o ${OUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_RUN_DIR}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT} ${SHADER_RUN_DIR}/${SHADER}.spv
            DEPENDS ${IN}
            DEPFILE ${DEP}
            COMMENT "glslc -> ${SHADER}.spv"
        )
    else()
        add_custom_command(
            OUTPUT  ${OUT}
            COMMAND ${GLSLC} --target-env=vulkan1.0 -MD -MF ${DEP} ${IN} -I ${SHADER_SRC_DIR}/include -o ${OUT}
            DEPENDS ${IN}
            DEPFILE ${DEP}
            COMMENT "glslc -> ${SHADER}.spv"
        )
    endif()
    list(APPEND SPIRV_OUTPUTS ${OUT})
endforeach()

# ── 묶음 도구와 셰이더 목록 (2026-11-01, DEF-149) ───────────────────────────────
# 묶음(game.hpak)의 셰이더가 실행 파일과 같은 빌드의 것인지 엔진이 확인하도록, 컴파일한 SPIR-V 의 논리 경로와 내용 해시를
# HarukaPak 이 목록 파일(generated/haruka_shader_manifest.inc)로 적고 엔진(src/runtime/ShaderManifest.cpp)에 넣습니다. 해시는 묶음
# 목차와 같은 XXH3-64 라 호스트 도구가 필요합니다. Android 구성은 HarukaPak 을 만들지 않으므로(기기용 실행 파일만 만듦) 데스크톱
# 빌드의 HarukaPak 을 씁니다. 다른 위치의 도구는 -DHARUKA_HOST_PAK=<경로> 로 줍니다.
set(HARUKA_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(SHADER_MANIFEST_INC  ${HARUKA_GENERATED_DIR}/haruka_shader_manifest.inc)
if(ANDROID)
    # 캐시된 경로가 사라졌으면(빌드 폴더를 지움) 다시 찾습니다. find_program 은 캐시 값이 있으면 찾지 않습니다. 처음 구성할 때
    # Release 가 없어 Debug 도구가 골라졌다면, Release 도구가 생긴 뒤에는 그것을 다시 고릅니다(build.bat 의 기본이 Release).
    set(_host_pak_debug   ${CMAKE_CURRENT_SOURCE_DIR}/build/Debug/HarukaPak${CMAKE_HOST_EXECUTABLE_SUFFIX})
    set(_host_pak_release ${CMAKE_CURRENT_SOURCE_DIR}/build/Release/HarukaPak${CMAKE_HOST_EXECUTABLE_SUFFIX})
    if(HARUKA_HOST_PAK AND (NOT EXISTS "${HARUKA_HOST_PAK}" OR
       (HARUKA_HOST_PAK STREQUAL _host_pak_debug AND EXISTS "${_host_pak_release}")))
        unset(HARUKA_HOST_PAK CACHE)
    endif()
    find_program(HARUKA_HOST_PAK NAMES HarukaPak
        HINTS ${CMAKE_CURRENT_SOURCE_DIR}/build/Release ${CMAKE_CURRENT_SOURCE_DIR}/build/Debug
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    if(NOT HARUKA_HOST_PAK OR NOT EXISTS "${HARUKA_HOST_PAK}")
        message(FATAL_ERROR "The Android build packs shaders and assets with the desktop HarukaPak tool. Build it first "
                            "(cmake --build build --config Release --target HarukaPak) or pass -DHARUKA_HOST_PAK=<path>. "
                            "Now: '${HARUKA_HOST_PAK}'")
    endif()
    set(HARUKA_PAK_TOOL ${HARUKA_HOST_PAK})
    # 도구가 묶음 · 목록의 바이트를 정하는 소스(묶음 형식 · 쓰기 · 목록 규칙)보다 오래되었으면 빌드를 멈춥니다(cmake/CheckHostPak.cmake).
    # 목록 · 묶음 명령이 이 표지에 기대므로 소스나 도구가 바뀔 때마다 다시 확인합니다. 출력에 영향이 없는 것은 넣지 않습니다:
    # 도구에 들어가지 않는 Vfs · ShaderManifest(바꿔도 도구가 다시 링크되지 않아 언제나 오래된 것으로 보임)와, 읽기 전용인 PakReader
    # (Android 의 읽기 코드만 고쳐도 멈추던 거짓 경보, 2026-11-01 재검토).
    set(HOST_PAK_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/tools/HarukaPak.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/tools/PakGlob.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vfs/PakFormat.hpp   ${CMAKE_CURRENT_SOURCE_DIR}/src/vfs/PakFormat.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vfs/PakWriter.hpp   ${CMAKE_CURRENT_SOURCE_DIR}/src/vfs/PakWriter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vfs/PathUtf8.hpp)
    string(REPLACE ";" "|" _host_pak_sources "${HOST_PAK_SOURCES}")
    set(HOST_PAK_STAMP ${HARUKA_GENERATED_DIR}/host_pak_checked.stamp)
    add_custom_command(
        OUTPUT  ${HOST_PAK_STAMP}
        COMMAND ${CMAKE_COMMAND} -DTOOL=${HARUKA_HOST_PAK} -DSOURCES=${_host_pak_sources} -DSTAMP=${HOST_PAK_STAMP}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckHostPak.cmake
        DEPENDS ${HARUKA_HOST_PAK} ${HOST_PAK_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckHostPak.cmake
        COMMENT "Checking that the desktop HarukaPak is up to date"
        VERBATIM
    )
    set(HARUKA_PAK_DEPENDS ${HARUKA_HOST_PAK} ${HOST_PAK_STAMP})
else()
    set(HARUKA_PAK_TOOL    HarukaPak)   # 아래 "묶음 도구와 게임 묶음" 의 대상(소스가 바뀌면 함께 다시 빌드됨)
    set(HARUKA_PAK_DEPENDS HarukaPak)
endif()
# 셰이더 파일 목록을 그대로 넘깁니다. 폴더를 훑게 하면 GLSL_SHADERS 에서 셰이더를 빼도 명령이 바뀌지 않아 목록 파일이 다시
# 만들어지지 않았고, 배포 묶음에서 그 셰이더를 찾지 못해 거짓 FATAL 이 났습니다(2026-11-01 독립 검토).
add_custom_command(
    OUTPUT  ${SHADER_MANIFEST_INC}
    COMMAND ${HARUKA_PAK_TOOL} shader-manifest -o ${SHADER_MANIFEST_INC} ${SPIRV_OUTPUTS}
    DEPENDS ${HARUKA_PAK_DEPENDS} ${SPIRV_OUTPUTS}
    COMMENT "HarukaPak -> shader manifest"
    VERBATIM
)

# 목록 파일도 Shaders 대상이 만듭니다. 엔진 라이브러리는 Shaders 에 기대어(아래) 목록이 먼저 생기고, 셰이더가 바뀌면 그
# 목록을 넣는 파일 하나만 다시 컴파일됩니다.
add_custom_target(Shaders DEPENDS ${SPIRV_OUTPUTS} ${SHADER_MANIFEST_INC})

# ── 에셋 디렉토리 ─────────────────────────────────────────────────────────────
if(HARUKA_BUILD_SAMPLES)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Projects/Samples/Content/models)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Projects/Samples/Content/prefabs)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Projects/Samples/Content/audio)
endif()
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/cache)

# ── 엔진 런타임 라이브러리와 실행 파일 (2026-10-19, 런타임 분리 DEF-12) ──────────
# 에디터를 뺀 엔진 전부를 HarukaRuntime 정적 라이브러리로 한 번만 빌드하고, 실행 파일 둘이 각각 링크합니다.
#   HarukaEngine.exe  에디터(EDITOR_SOURCES + editor/EditorFrontend.cpp). 프로젝트 폴더를 직접 읽고 씀.
#   HarukaGame.exe    게임만(game/GameFrontend.cpp). 시작 씬을 열고 처음부터 Play. 에디터 코드는 들어가지 않습니다.
# UE 의 Editor · Game 타깃, O3DE 의 Editor · GameLauncher 와 같은 구분입니다. 런타임 코드가 에디터에 기대면 게임
# 실행 파일의 링크가 실패하므로, 분리가 규칙이 아니라 구조로 지켜집니다. 엔진은 앞단(core/IEngineFrontend.hpp)으로만
# 에디터 · 게임을 부르고, 어느 앞단인지는 실행 파일마다 링크한 createEngineFrontend 가 정합니다.
set(RUNTIME_SOURCES
    src/core/AppPaths.cpp
    src/runtime/Engine.cpp
    src/runtime/SceneTransition.cpp
    src/runtime/FrameStats.cpp
    src/runtime/WorldUiDriver.cpp
    src/runtime/XrSessionController.cpp
    src/runtime/XrViewOrigin.cpp
    src/runtime/EngineOptions.cpp
    src/runtime/ShaderManifest.cpp
    src/scene/World.cpp
    src/scene/TimerManager.cpp
    src/scene/Actor.cpp
    src/scene/Component.cpp
    src/core/HLog.cpp
    src/core/HText.cpp
    src/core/AssetRefs.cpp
    src/core/SaveGame.cpp
    src/core/CVar.cpp
    src/core/Profiler.cpp
    src/core/TraceRecorder.cpp
    src/core/UserSettings.cpp
    src/core/Accessibility.cpp
    src/core/Subtitles.cpp
    src/core/CrashHandler.cpp
    src/components/CameraComponent.cpp
    src/components/LightComponent.cpp
    src/components/RenderExtract.cpp
    src/components/StaticMeshComponent.cpp
    src/components/SubtitlePanel.cpp
    src/components/AssetRebind.cpp
    src/audio/AudioComponent.cpp
    src/audio/ReverbZoneComponent.cpp
    src/physics/RigidBodyComponent.cpp
    src/physics/JointComponent.cpp
    src/components/CoreComponents.cpp
    src/components/XrComponents.cpp
    src/components/SkinnedMeshComponent.cpp
    src/components/AnimationComponent.cpp
    src/components/UIPanelComponent.cpp
    src/components/UIPanelPointer.cpp
    src/components/HandPoseComponent.cpp
    src/components/HeightFogComponent.cpp
    src/components/ReflectionCaptureComponent.cpp
    src/components/ParticleSystemComponent.cpp
    src/components/DecalComponent.cpp
    src/vfx/ParticleSim.cpp
    src/xr/GraspSolver.cpp
    src/renderer/Renderer.cpp
    src/renderer/Renderer_swap.cpp
    src/renderer/Renderer_render.cpp
    src/renderer/Renderer_window.cpp
    src/renderer/Renderer_capture.cpp
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
    src/renderer/passes/GeometryPass_particles.cpp
    src/renderer/passes/UIPanelPass.cpp
    src/geometry/PrimitiveDesc.cpp
    src/geometry/PrimitiveBuilder.cpp
    src/geometry/MeshLod.cpp
    src/geometry/Ktx2.cpp
    src/assets/GltfLoader.cpp
    src/assets/AssetManager.cpp
    src/assets/AssetStreamer.cpp
    src/assets/GassetCache.cpp
    src/assets/ImportSettings.cpp
    src/assets/TextureCompression.cpp
    src/assets/Skin.cpp
    src/assets/Animation.cpp
    src/assets/AnimStateMachine.cpp
    src/sceneio/PrefabManager.cpp
    src/sceneio/SceneSerializer.cpp
    src/sceneio/PrefabLink.cpp
    src/sceneio/ComponentSerializer.cpp
    src/audio/AudioSystem.cpp
    src/audio/AudioMix.cpp
    src/audio/AudioAttenuation.cpp
    src/audio/MiniaudioImpl.cpp
    src/audio/SteamAudioHrtf.cpp
    src/physics/PhysicsSystem.cpp
    src/nav/NavMeshBuild.cpp
    src/nav/NavMesh.cpp
    src/nav/NavGeometry.cpp
    src/nav/NavSystem.cpp
    src/nav/NavMeshVolumeComponent.cpp
    src/nav/NavAgentComponent.cpp
    src/nav/AIBrainComponent.cpp
    src/ui/slate/HPaintContext.cpp
    src/ui/slate/HFontAtlas.cpp
    src/ui/slate/HSlateRenderer.cpp
# 게임 소스는 프로젝트(Projects/Samples/Source/SamplesGameModule.cpp 등)로 옮겼고(P3b),
# HarukaRuntime 은 게임 코드를 포함하지 않습니다.
    src/input/HInputSystem.cpp
)
set(EDITOR_SOURCES
    src/editor/EditorApp.cpp
    src/editor/EditorApp_dump.cpp
    src/editor/EditorApp_windows.cpp
    src/editor/EditorFloatWindows.cpp
    src/editor/EditorUiKit.cpp
    src/editor/EditorIcons.cpp
    src/editor/EditorViewportController.cpp
    src/editor/dock/DockTree.cpp
    src/editor/EditorDock.cpp
    src/editor/EditorMenus.cpp
    src/editor/EditorChromePanels.cpp
    src/editor/ViewportToolbar.cpp
    src/editor/TextInputController.cpp
    src/editor/OutlinerPanel.cpp
    src/editor/DetailsPanel.cpp
    src/editor/ContentBrowserPanel.cpp
    src/editor/GpuStatsPanel.cpp
    src/editor/OutputLogPanel.cpp
    src/editor/ComponentPicker.cpp
    src/editor/AssetPicker.cpp
    src/editor/ColorPickerPopup.cpp
    src/editor/EditorContextMenu.cpp
    src/editor/EditorMenuView.cpp
    src/editor/ComponentVisualizers.cpp
    src/editor/EditorApp_scene.cpp
    src/editor/EditorApp_edit.cpp
    src/editor/EditorGizmo.cpp
    src/editor/ActorSnapshot.cpp
    src/editor/HotReloader.cpp
    src/editor/AssetWatcher.cpp
    src/editor/EditorApp_reload.cpp
    src/editor/EditorFrontend.cpp
)
set(GAME_SOURCES
    src/game/GameFrontend.cpp
)

# 엔진 대상(라이브러리 · 실행 파일)이 함께 쓰는 컴파일 설정. 위의 add_compile_options(/wd4100 …)는 이 폴더 범위라, 엔진을
# 하위 프로젝트로 쓰는 바깥 프로젝트의 실행 파일(haruka_add_project)에는 닿지 않으므로 여기에도 적습니다(D3, 2026-11-19).
function(haruka_target_defaults tgt)
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /W4 /utf-8 /wd4865 /wd4100 /Wv:18)
    else()
        target_compile_options(${tgt} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# ── 묶음 파일 · 가상 파일 시스템 (2026-10-24, src/vfs) ──────────────────────────
# 엔진(HarukaRuntime) · 묶음 도구(HarukaPak) · 시험이 함께 씁니다. Vulkan · 창 없이 빌드됩니다.
add_library(HarukaVfs STATIC
    src/vfs/PakFormat.cpp
    src/vfs/PakReader.cpp
    src/vfs/PakWriter.cpp
    src/vfs/Vfs.cpp
)
target_include_directories(HarukaVfs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src PRIVATE ${xxhash_SOURCE_DIR})
target_link_libraries(HarukaVfs PRIVATE haruka_lz4)
haruka_target_defaults(HarukaVfs)

if(ANDROID)
    # Quest 단독 실행(2026-10-31, 패키징 3단계): NativeActivity 진입점 · OpenXR 로더(공유) · APK 포장은 이 파일에 있습니다.
    include(cmake/HarukaAndroid.cmake)
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

    list(APPEND RUNTIME_SOURCES
        src/platform/glfw/GlfwWindow.cpp
        src/platform/glfw/GlfwPlatform.cpp
        src/xr/XrSystem.cpp
    )
    add_library(HarukaRuntime STATIC ${RUNTIME_SOURCES})
    if(NOT HARUKA_PROFILER)
        target_compile_definitions(HarukaRuntime PUBLIC HARUKA_PROFILER=0)
    endif()
    target_link_libraries(HarukaRuntime PUBLIC Vulkan::Vulkan glfw glm::glm tinygltf Jolt openxr_loader HarukaVfs meshoptimizer)
    target_link_libraries(HarukaRuntime PRIVATE haruka_recast)   # 내비 메시(Docs/Navigation_AI.md D1)
    target_include_directories(HarukaRuntime PUBLIC ${stb_SOURCE_DIR} ${miniaudio_SOURCE_DIR})
    if(WIN32)
        # 쓰기 루트(%LOCALAPPDATA%)를 구하는 SHGetKnownFolderPath · CoTaskMemFree(core/AppPaths.cpp).
        target_link_libraries(HarukaRuntime PUBLIC shell32 ole32)
    endif()

    # ── Windows 실행 파일·창 아이콘 ────────────────────────────────────────────
    # res/Haruka.rc 가 다중 크기 .ico 하나를 실행 파일 안에 넣습니다. GLFW 는 창
    # 클래스를 등록할 때 GLFW_ICON 이라는 이름의 아이콘 리소스를 직접 찾아 가므로
    # (win32_window.c) 런타임에서 glfwSetWindowIcon 을 호출할 필요가 없습니다.
    # 어느 크기를 쓸지는 표시 위치와 배율을 보고 Windows 가 고릅니다.
    # res/Haruka.manifest 는 프로세스 코드 페이지를 UTF-8 로 둡니다(2026-10-19). 엔진은 경로를 std::string 으로 다루므로,
    # 이것이 없으면 사용자 이름이 한글인 %LOCALAPPDATA% 경로가 시스템 코드 페이지(CP949)로 해석됩니다. CMake 가 .manifest
    # 소스를 링커의 /MANIFESTINPUT 으로 넘겨, 링커가 만드는 매니페스트와 합칩니다.
    set(EXE_RESOURCES)
    if(WIN32)
        enable_language(RC)
        list(APPEND EXE_RESOURCES res/Haruka.rc res/Haruka.manifest)
    endif()
endif()

# ── GLM 설정 ──────────────────────────────────────────────────────────────────
# 투영 행렬의 깊이 범위를 Vulkan 규약 [0,1] 로 둡니다. 예전에는 헤더마다 #define 했는데, 매크로 없이
# GLM 을 먼저 포함한 헤더(Reflect.hpp)가 있으면 그 번역 단위는 OpenGL 규약 [-1,1] 로 굳었습니다.
# 그래서 카메라 투영만 [-1,1] 이 되어 실제 근평면이 두 배(0.2m)였습니다(DEF-95). 소스의 개별 정의는
# 두지 않습니다. 이 디렉터리의 대상(엔진·테스트)에는 위치와 상관없이 붙고, 이 줄보다 먼저 추가한
# 외부 라이브러리 하위 디렉터리(GLFW 등)에는 붙지 않도록 외부 라이브러리를 다 불러온 뒤에 둡니다.
add_compile_definitions(GLM_FORCE_DEPTH_ZERO_TO_ONE)

target_include_directories(HarukaRuntime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# 셰이더 목록(haruka_shader_manifest.inc, DEF-149)은 Shaders 대상이 만듭니다.
target_include_directories(HarukaRuntime PRIVATE ${HARUKA_GENERATED_DIR})
add_dependencies(HarukaRuntime Shaders)
# Android 는 NDK sysroot 의 Vulkan 헤더를 기본 경로로 씁니다. sysroot 를 -I 로 다시 넣으면 libc++ 의 #include_next 순서가
# 깨질 수 있어 데스크톱(SDK 헤더)만 넣습니다.
if(NOT ANDROID)
    target_include_directories(HarukaRuntime PUBLIC ${Vulkan_INCLUDE_DIRS})
endif()
haruka_target_defaults(HarukaRuntime)

if(NOT ANDROID)
    option(HARUKA_CONSOLE "Open a console window alongside the engine" OFF)
    set(UTF8_MANIFEST)
    if(WIN32)
        set(UTF8_MANIFEST res/Haruka.manifest)
    endif()
    # 묶을 때 텍스처 압축(Docs/Texture_Compression.md D6)은 런타임과 같은 glTF 로더로 최종 화소를 얻습니다. 로더 · 가져오기 설정 ·
    # KTX2 · 형식 표는 소스를 직접 넣습니다(HarukaRuntime 은 셰이더 목록 때문에 이 도구에 기대므로 링크할 수 없음).
    set(HARUKA_TEXTURE_COOK_SOURCES
        src/tools/TextureEncode.cpp
        src/tools/TextureCook.cpp
        src/geometry/Ktx2.cpp
        src/assets/TextureCompression.cpp
        src/assets/GltfLoader.cpp
        src/assets/ImportSettings.cpp
        src/assets/Animation.cpp
        src/assets/Skin.cpp
        src/geometry/MeshLod.cpp
        src/core/HLog.cpp)
    add_executable(HarukaPak src/tools/HarukaPak.cpp src/core/HText.cpp src/core/AssetRefs.cpp ${HARUKA_TEXTURE_COOK_SOURCES}
                   ${UTF8_MANIFEST})
    target_link_libraries(HarukaPak PRIVATE HarukaVfs tinygltf glm::glm meshoptimizer haruka_astcenc haruka_bc7enc)
    target_include_directories(HarukaPak PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIRS})
    haruka_target_defaults(HarukaPak)
    if(MSVC)
        set_target_properties(HarukaPak PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()
endif()

# ── 프로젝트 등록 (P3b: haruka_add_project / P4: 하위 프로젝트 지원) ──────────
set(HARUKA_ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "")
list(TRANSFORM EDITOR_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/" OUTPUT_VARIABLE _haruka_editor_sources)
list(TRANSFORM GAME_SOURCES   PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/" OUTPUT_VARIABLE _haruka_game_sources)
set(_haruka_exe_resources)
foreach(_res ${EXE_RESOURCES})
    list(APPEND _haruka_exe_resources "${CMAKE_CURRENT_SOURCE_DIR}/${_res}")
endforeach()
set(HARUKA_EDITOR_SOURCES      "${_haruka_editor_sources}"      CACHE INTERNAL "")
set(HARUKA_GAME_SOURCES        "${_haruka_game_sources}"        CACHE INTERNAL "")
set(HARUKA_EXE_RESOURCES       "${_haruka_exe_resources}"       CACHE INTERNAL "")
set(HARUKA_SHADER_BIN_DIR      "${SHADER_BIN_DIR}"              CACHE INTERNAL "")
set(HARUKA_SPIRV_OUTPUTS       "${SPIRV_OUTPUTS}"               CACHE INTERNAL "")
set(HARUKA_PAK_TOOL            "${HARUKA_PAK_TOOL}"             CACHE INTERNAL "")
set(HARUKA_PAK_DEPENDS         "${HARUKA_PAK_DEPENDS}"          CACHE INTERNAL "")
set(HARUKA_STEAM_AUDIO_FOUND   "${HARUKA_STEAM_AUDIO_FOUND}"    CACHE INTERNAL "")
set(HARUKA_STEAM_AUDIO_LIBRARY "${HARUKA_STEAM_AUDIO_LIBRARY}"  CACHE INTERNAL "")
include(cmake/HarukaProject.cmake)
if(HARUKA_BUILD_SAMPLES)
    add_subdirectory(Projects/Samples)
endif()

# ── 서드파티 라이선스 고지 (2026-10-24) ─────────────────────────────────────────
# 게임 실행 파일에 들어가는 라이브러리의 라이선스 문구를 한 파일로 모아 배포물에 넣습니다. MIT · BSD-2 · Apache 2.0 은
# 실행 파일로 배포할 때 문구를 함께 넣으라고 요구합니다. GLFW(zlib) · miniaudio · stb(퍼블릭 도메인 선택 가능)는 요구하지
# 않지만 함께 적습니다. OpenXR 로더는 "Apache-2.0 OR MIT" 라 MIT 를 고릅니다. 구성할 때 받은 소스에서 다시 만듭니다.
set(THIRD_PARTY_NOTICES ${CMAKE_CURRENT_BINARY_DIR}/THIRD_PARTY_NOTICES.txt)
file(WRITE ${THIRD_PARTY_NOTICES}
    "Haruka - third-party notices\n\n"
    "The Haruka game executable includes the following third-party software.\n"
    "Each section reproduces the license under which it is used.\n")
function(haruka_notice title text)
    file(APPEND ${THIRD_PARTY_NOTICES}
        "\n==============================================================================\n"
        "${title}\n"
        "==============================================================================\n\n"
        "${text}\n")
endfunction()
function(haruka_notice_file title path)
    file(READ ${path} _text)
    haruka_notice("${title}" "${_text}")
endfunction()
# MIT 원문(OpenXR-SDK 의 LICENSES/MIT.txt 와 같은 글). 저작권 줄의 자리표시를 라이브러리마다 바꿔 씁니다. 예전에는 OpenXR 소스에서
# 읽어, OpenXR 을 받지 않는 Android 구성이 이 줄에서 멈췄습니다(2026-10-31 검토). 원문을 저장소의 파일로 두지 않는 것은 프로젝트
# 자체의 라이선스로 오해되지 않게 하려는 것입니다.
set(_mit [=[MIT License Copyright (c) <year> <copyright holders>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
]=])
haruka_notice_file("Jolt Physics (MIT License)" ${joltphysics_SOURCE_DIR}/LICENSE)
haruka_notice_file("tinygltf (MIT License)" ${tinygltf_SOURCE_DIR}/LICENSE)
string(REPLACE "<year> <copyright holders>" "2013-2019 Niels Lohmann <http://nlohmann.me>" _json "${_mit}")
haruka_notice("JSON for Modern C++, bundled with tinygltf (MIT License)" "${_json}")
haruka_notice_file("OpenGL Mathematics (GLM) (MIT License, chosen from The Happy Bunny License or MIT)" ${glm_SOURCE_DIR}/copying.txt)
# OpenXR 로더는 두 플랫폼 모두 들어갑니다(데스크톱은 정적 링크, Android 는 libopenxr_loader.so, 2026-10-31). 로더 · common 소스의
# 저작권자는 Khronos · Valve · LunarG 와 Collabora(object_info · hex_and_handles, Android 의 android_utilities, 2026-11-01 더함)입니다.
string(REPLACE "<year> <copyright holders>"
    "2017-2026 The Khronos Group Inc.\nCopyright (c) 2017-2019 Valve Corporation\nCopyright (c) 2017-2019 LunarG, Inc.\nCopyright (c) 2019-2021 Collabora, Ltd." _oxr "${_mit}")
haruka_notice("OpenXR loader (MIT License, chosen from Apache-2.0 OR MIT)" "${_oxr}")
haruka_notice_file("LZ4 library (BSD 2-Clause License)" ${lz4_SOURCE_DIR}/lib/LICENSE)
haruka_notice_file("xxHash (BSD 2-Clause License)" ${xxhash_SOURCE_DIR}/LICENSE)
haruka_notice_file("recastnavigation 1.6.0 (Recast · Detour) (zlib License)" ${recastnavigation_SOURCE_DIR}/License.txt)
if(NOT ANDROID)
    haruka_notice_file("GLFW (zlib License)" ${glfw_SOURCE_DIR}/LICENSE.md)
else()
    # Android 에서만 들어가는 것(2026-11-01, DEF-151): OpenXR 로더가 Android 에서 함께 컴파일하는 jnipp(MIT)와, 엔진 .so 에
    # 넣는 NDK 의 android_native_app_glue(Apache-2.0, §4 에 따라 NDK 의 NOTICE 와 라이선스 원문). 로더의 android-jni-wrappers 는
    # BSL-1.0 이라 기계어로 배포할 때 고지가 필요 없고, libc++ · compiler-rt 는 LLVM 예외라 넣지 않습니다.
    haruka_notice_file("jnipp, bundled with the OpenXR loader on Android (MIT License)"
        ${openxr_SOURCE_DIR}/src/external/jnipp/LICENSE)
    file(READ ${ANDROID_NDK}/sources/android/native_app_glue/NOTICE _glue_notice)
    file(READ ${openxr_SOURCE_DIR}/LICENSES/Apache-2.0.txt _apache2)
    haruka_notice("android_native_app_glue from the Android NDK (Apache License 2.0)" "${_glue_notice}\n${_apache2}")
endif()
# OpenXR 로더는 시스템 JsonCpp 가 없으면 동봉한 jsoncpp 를 함께 컴파일해 넣습니다(DEF-128). 시스템 JsonCpp 로 빌드해도 정적
# 링크라 결과물에 들어가므로 조건 없이 적습니다. 퍼블릭 도메인을 인정하지 않는 곳을 위해 MIT 문구를 넣습니다.
haruka_notice_file("JsonCpp, bundled with the OpenXR loader (Public Domain or MIT License)"
    ${openxr_SOURCE_DIR}/src/external/jsoncpp/LICENSE)
haruka_notice_file("miniaudio (Public Domain or MIT No Attribution)" ${miniaudio_SOURCE_DIR}/LICENSE)
# verblib(잔향, 2026-11-06): miniaudio 의 extras 에 든 Freeverb 기반 리버브입니다. MIT No Attribution 또는 퍼블릭 도메인이라 고지 의무는
# 없지만 출처를 밝혀 둡니다. 원문은 verblib.h 끝의 LICENSE 주석입니다.
file(READ ${miniaudio_SOURCE_DIR}/extras/nodes/ma_reverb_node/verblib.h _verblib)
string(FIND "${_verblib}" "/* LICENSE" _verblib_at)
if(_verblib_at GREATER -1)
    string(SUBSTRING "${_verblib}" ${_verblib_at} -1 _verblib_license)
    haruka_notice("verblib by Philip Bennefall, bundled with miniaudio, based on Freeverb by Jezar at Dreampoint (Public Domain or MIT No Attribution)"
        "${_verblib_license}")
else()
    message(FATAL_ERROR "verblib.h license block not found (miniaudio extras changed?)")
endif()
haruka_notice_file("stb, including stb_image bundled with tinygltf (Public Domain or MIT License)" ${stb_SOURCE_DIR}/LICENSE)
# 텍스처 인코더(2026-11-21): 게임 실행 파일에는 들어가지 않고 묶음 도구(HarukaPak)만 씁니다. 묶음 안의 압축 텍스처는 그 도구의
# 출력이라 고지 대상은 아니지만, 도구를 함께 배포할 때를 위해 적습니다.
if(NOT ANDROID)
    haruka_notice_file("astc-encoder 5.7.1, used by the HarukaPak packaging tool only (Apache License 2.0)" ${astcenc_SOURCE_DIR}/LICENSE.txt)
    haruka_notice_file("bc7enc_rdo (bc7enc · bc7decomp · rgbcx), used by the HarukaPak packaging tool only (MIT License, chosen from MIT or Public Domain)"
        ${bc7enc_rdo_SOURCE_DIR}/LICENSE)
endif()
# Steam Audio(HRTF, 2026-11-06): 라이브러리를 그대로 함께 배포합니다(Windows phonon.dll, Android libphonon.so). Apache-2.0 §4 에 따라
# 라이선스 원문과 저작권 표시(phonon.h 머리말)를 넣습니다. 저장소에 NOTICE 파일은 없습니다. 라이브러리 안에 든 서드파티(IPP · FFTS ·
# PFFFT · MySOFA · Embree · CIPIC HRTF 등)의 문구는 Valve 가 ZIP 에 넣은 THIRDPARTY.md 를 그대로 붙입니다.
if(HARUKA_STEAM_AUDIO_FOUND)
    file(READ ${openxr_SOURCE_DIR}/LICENSES/Apache-2.0.txt _sa_apache2)
    file(READ ${HARUKA_STEAM_AUDIO_THIRDPARTY} _sa_thirdparty)
    haruka_notice("Steam Audio 4.8.1 (Apache License 2.0)" "Copyright 2017-2023 Valve Corporation.\n\n${_sa_apache2}")
    haruka_notice("Third-party software included in the Steam Audio library (THIRDPARTY.md from the Steam Audio release)"
        "${_sa_thirdparty}")
endif()

# ── 게임 패키지 (2026-10-19, Windows) ─────────────────────────────────────────
# 배포 폴더 규칙입니다. 빌드한 뒤 "cmake --build build --config Release --target PACKAGE" 로 build/ 아래에
# HarukaGame-<버전>-win64.zip 을 만듭니다. 압축을 풀면 바로 실행되고 관리자 권한이 필요 없습니다.
#   HarukaGame.exe      게임 실행 파일(에디터 코드 없음). OpenXR 로더는 정적 링크라 DLL 이 없습니다.
#   VC++ 런타임 DLL     실행 파일 옆에 둡니다(사용자 결정 7, InstallRequiredSystemLibraries). UCRT 는 Windows 10 부터
#                       운영체제에 들어 있어 넣지 않습니다.
#   phonon.dll          Steam Audio(HRTF, 2026-11-06, 약 53 MB). 엔진이 실행 중에 열며, 없으면 3D 소리는 패닝으로 동작합니다.
#   game.hpak           셰이더와 에셋의 묶음(2026-10-24, 사용자 결정 6). 낱개 shaders/ · assets/ 는 넣지 않습니다.
#                       glTF 캐시는 기기에서 처음 실행할 때 쓰기 루트에 만듭니다.
#   THIRD_PARTY_NOTICES.txt  서드파티 라이선스 고지
# 설정 · 로그 · 캐시는 실행할 때 %LOCALAPPDATA%\Haruka\HarukaGame 에 생깁니다(core/AppPaths.hpp). 실행 폴더에
# Config/engine.ini 를 두면 패키지 기본값으로 읽습니다(없으면 엔진 기본값).
# 규칙은 모두 Game 구성 요소로 둡니다. 외부 라이브러리(GLFW · Jolt · OpenXR 등)가 자기 헤더 · 라이브러리 설치 규칙을
# 갖고 있어, 구성 요소를 가리지 않으면 그것까지 ZIP 에 들어갑니다(처음 시험에서 include/ · lib/ 300여 개가 들어감).
if(WIN32 AND NOT ANDROID AND HARUKA_BUILD_SAMPLES AND HARUKA_PACKAGE_GAME)
    install(TARGETS HarukaGame RUNTIME DESTINATION . COMPONENT Game)
    install(FILES ${GAME_PACK} ${THIRD_PARTY_NOTICES} DESTINATION . COMPONENT Game)
    if(HARUKA_STEAM_AUDIO_FOUND)
        install(FILES ${HARUKA_STEAM_AUDIO_LIBRARY} DESTINATION . COMPONENT Game)   # phonon.dll(HRTF, 2026-11-06)
    endif()

    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT Game)
    set(CMAKE_INSTALL_UCRT_LIBRARIES OFF)
    # 재배포 DLL 폴더는 캐시에 남습니다. Visual Studio 가 업데이트되면 버전 폴더가 바뀌어(2026-10-20, 14.51 → 14.52) 없는
    # 경로를 가리키므로, 사라졌으면 다시 찾게 합니다. 그대로 두면 패키지 생성이 "file INSTALL cannot find" 로 실패합니다.
    if(MSVC_REDIST_DIR AND NOT EXISTS "${MSVC_REDIST_DIR}")
        unset(MSVC_REDIST_DIR CACHE)
    endif()
    include(InstallRequiredSystemLibraries)

    get_property(_haruka_pkg_name GLOBAL PROPERTY HARUKA_PACKAGING_NAME)
    if(NOT _haruka_pkg_name)
        set(_haruka_pkg_name HarukaGame)
    endif()
    set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_CURRENT_BINARY_DIR};${PROJECT_NAME};Game;/")
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_NAME "${_haruka_pkg_name}")
    set(CPACK_PACKAGE_FILE_NAME "${_haruka_pkg_name}-${PROJECT_VERSION}-win64")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
    include(CPack)
endif()

# ── 지오메트리 규약 테스트 ────────────────────────────────────────────────────
# Vulkan 도 디바이스도 창도 필요 없습니다. 생성과 업로드를 분리해 둔 덕분이며,
# 탄젠트 handedness 나 와인딩 같은 결함은 실행이 아니라 검산으로만 잡힙니다.
if(NOT ANDROID AND HARUKA_BUILD_TESTS)
    add_executable(HarukaGeometryTest
        tests/GeometryTest.cpp
        src/geometry/PrimitiveDesc.cpp
        src/geometry/PrimitiveBuilder.cpp
    src/geometry/MeshLod.cpp
        src/core/HLog.cpp
    )
    target_link_libraries(HarukaGeometryTest PRIVATE glm::glm meshoptimizer)
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
        src/scene/Actor.cpp
        src/scene/Component.cpp
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
        src/scene/Actor.cpp
        src/scene/Component.cpp
        src/core/HLog.cpp
    )
    target_link_libraries(HarukaStereoTest PRIVATE glm::glm)
    # 합성 눈(xr/SyntheticEyes.hpp)이 RenderTypes.hpp 의 Vulkan 형을 쓰므로 헤더 경로만 둡니다(링크는 필요 없음).
    target_include_directories(HarukaStereoTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(HarukaStereoTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaStereoTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 모듈 의존 방향 시험(엔진 구조 안정화 S1, 2026-11-18) -------------------
    # src 아래 모든 #include "..." 가 tests/ArchitectureRules.txt 의 허용 방향 안인지 봅니다. 정본 Docs/Engine_Structure_Design.md.
    add_executable(HarukaArchTest tests/ArchitectureTest.cpp)
    target_compile_definitions(HarukaArchTest PRIVATE HARUKA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(HarukaArchTest PRIVATE /W4 /utf-8)
        set_target_properties(HarukaArchTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 공통 수학 테스트(src/math, 2026-11-17) -----------------------------
    # 광선 · 도형 판정, 감쇠 · 스프링, 곡선, 시드 난수 · 노이즈, 포즈 필터, 두 뼈 IK 를 화면 없이 검산합니다. 헤더만 씁니다.
    add_executable(HarukaMathTest tests/MathTest.cpp)
    target_link_libraries(HarukaMathTest PRIVATE glm::glm)
    target_include_directories(HarukaMathTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaMathTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaMathTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
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
        src/sceneio/ComponentSerializer.cpp
        src/scene/Actor.cpp
        src/scene/Component.cpp
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
    add_executable(HarukaLightTest
        tests/LightSelectionTest.cpp
        src/components/RenderExtract.cpp
        src/components/CameraComponent.cpp
        src/components/HeightFogComponent.cpp
        src/components/LightComponent.cpp
        src/components/ReflectionCaptureComponent.cpp
        src/components/SkinnedMeshComponent.cpp
        src/components/StaticMeshComponent.cpp
        src/components/UIPanelComponent.cpp
        src/components/ParticleSystemComponent.cpp
        src/components/DecalComponent.cpp
        src/vfx/ParticleSim.cpp
        src/core/Profiler.cpp
        src/ui/slate/HPaintContext.cpp
        src/ui/slate/HFontAtlas.cpp
        src/assets/Skin.cpp
        src/geometry/PrimitiveDesc.cpp
        src/scene/World.cpp
        src/scene/TimerManager.cpp
        src/scene/Actor.cpp
        src/scene/Component.cpp
        src/core/HLog.cpp
    )
    target_link_libraries(HarukaLightTest PRIVATE glm::glm HarukaVfs Vulkan::Vulkan)
    target_include_directories(HarukaLightTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${stb_SOURCE_DIR} ${Vulkan_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(HarukaLightTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaLightTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- GLTF 캐시 유효성 테스트 ---------------------------------------------
    # 수정 시각만 바뀐 원본(빌드의 에셋 복사)에는 캐시를 쓰고, 내용이 바뀌면 다시 읽는지(DEF-105) 검산합니다.
    # 2026-10-24 부터 원본을 가상 파일 시스템으로 찾고 키가 논리 경로라, 묶음 안 원본과 키 안정성도 봅니다.
    add_executable(HarukaAssetCacheTest
        tests/AssetCacheTest.cpp
        src/assets/GassetCache.cpp
        src/assets/ImportSettings.cpp
        src/assets/Skin.cpp
        src/core/HLog.cpp
    )
    # 캐시 버전 7 부터 스킨을 담아(2026-11-07) Skin.cpp · glm 이 필요합니다.
    target_link_libraries(HarukaAssetCacheTest PRIVATE HarukaVfs glm::glm)
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

    # -- Vulkan 기준선 테스트 -------------------------------------------------
    # 장치 계획(1.1 · push descriptor 필수 · sync2 선택) · 기능 체인 · 함수 이름 · 큐 계열 · XR 범위를 창 · 장치 없이
    # 검산합니다(패키징 2단계, DEF-97). 헤더 전용이고 장치 함수는 가짜로 넘기므로 Vulkan 로더에 링크하지 않습니다.
    add_executable(HarukaVulkanBaselineTest tests/VulkanBaselineTest.cpp)
    target_include_directories(HarukaVulkanBaselineTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(HarukaVulkanBaselineTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaVulkanBaselineTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 배리어 기록 분기 테스트 ----------------------------------------------
    # sync2 배리어의 1.0 변환표, 엔진이 쓰는 값 전수, 거부, KHR · 1.0 발행 분기를 검산합니다(DEF-04). 헤더 전용입니다.
    add_executable(HarukaBarrierTest tests/BarrierCompatTest.cpp)
    target_include_directories(HarukaBarrierTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(HarukaBarrierTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaBarrierTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 메모리 타입 선택 테스트 ----------------------------------------------
    # 핵심 힙과 지연 할당 이미지의 메모리 타입 선택, 실패 문구를 Quest Pro · ReBAR 없는 PC 같은 메모리 표로 검산합니다
    # (DEF-42 · 06). 헤더 전용입니다.
    add_executable(HarukaMemoryTest tests/MemoryTypeTest.cpp)
    target_include_directories(HarukaMemoryTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(HarukaMemoryTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaMemoryTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 창 없는 렌더 경로 테스트 ---------------------------------------------
    # 빈 스왑체인 · 이미지별 세마포어(DEF-118) · 표면 없는 창(HeadlessWindow · AndroidWindow, DEF-103) · 자동 확인 명령줄과
    # 종료 코드, 파이프라인 캐시 파일의 거르기(DEF-153)를 창 · GPU 없이 검산합니다(패키징 2단계). AndroidWindow.cpp 는 NDK 헤더를
    # 쓰지 않아 PC 에서도 컴파일됩니다.
    add_executable(HarukaHeadlessTest
        tests/HeadlessRenderTest.cpp
        src/platform/android/AndroidWindow.cpp
        src/runtime/EngineOptions.cpp
        src/core/HLog.cpp
    )
    target_link_libraries(HarukaHeadlessTest PRIVATE glm::glm HarukaVfs)   # 파이프라인 캐시 파일의 해시(DEF-153)
    target_include_directories(HarukaHeadlessTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(HarukaHeadlessTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaHeadlessTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- Vulkan 가져오기 표 테스트 (Windows) -----------------------------------
    # 에디터 · 게임 실행 파일이 vulkan-1.dll 에서 1.0 · 1.1 코어와 표면 · 스왑체인 함수만 가져오는지 PE 를 읽어 검산합니다
    # (DEF-97). Android(API 34)의 libvulkan 에 없는 이름을 PC 에서 미리 잡습니다. 허용 목록은 SDK 의 vulkan_core.h 에서 뽑습니다.
    if(WIN32)
        add_executable(HarukaVkImportTest tests/VulkanImportTest.cpp)
        if(MSVC)
            target_compile_options(HarukaVkImportTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
            set_target_properties(HarukaVkImportTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
        endif()
    endif()

    # -- VR 입력 테스트 -------------------------------------------------------
    # XR 모듈이 넣는 컨트롤러 상태가 축 · 디지털 액션으로 나오는지 검산합니다(DEF-20).
    # 게임패드 · 수정자 · 컨텍스트 · 재지정(2026-11-21, Docs/Input_Extensions.md)도 검산합니다. 재지정이 사용자 설정(core)을 쓰므로
    # 런타임 라이브러리에 링크합니다.
    add_executable(HarukaInputTest tests/InputTest.cpp)
    target_link_libraries(HarukaInputTest PRIVATE HarukaRuntime)
    target_include_directories(HarukaInputTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaInputTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaInputTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- GLFW 플랫폼 수명 · 펌프 테스트 ---------------------------------------
    # 숨은 창 둘로 GLFW 참조 수 수명(보조 창을 닫아도 메인 창이 삶)과 펌프 한 번에 모든 창의 걸쇠를 비우는지(DEF-82),
    # 창 관리 함수를 확인합니다(다중 OS 창 H0, Docs/Editor_MultiWindow_Design.md). 창 시스템이 없으면 건너뜁니다.
    add_executable(HarukaWindowTest tests/WindowPlatformTest.cpp)
    target_link_libraries(HarukaWindowTest PRIVATE HarukaRuntime)
    target_include_directories(HarukaWindowTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaWindowTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaWindowTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
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

    # -- 씬 문서 테스트 -----------------------------------------------------
    # 헤더 전용입니다. Undo 상태 표지와 저장 여부 판정(Play 포함), 프로젝트 설정(assets/project.ini) 읽기,
    # 임시 파일에 쓴 뒤 바꾸는 저장, 손으로 만든 설정 파일의 인코딩(DEF-152)을 화면 없이 검산합니다(2026-10-15).
    add_executable(HarukaSceneDocTest tests/SceneDocumentTest.cpp)
    target_include_directories(HarukaSceneDocTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaSceneDocTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaSceneDocTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 편집 기본기 테스트 ---------------------------------------------------
    # ID 로 액터를 기억하는 커맨드(이름 · 켜기 · 부모 바꾸기 · 컴포넌트 추가/제거 · 속성), 부모를 바꿔도 월드 자세 유지,
    # 여러 액터 함께 움직이기, 클립보드 형식, 화면 맞추기 거리를 화면 없이 검산합니다(2026-10-15).
    add_executable(HarukaEditCmdTest
        tests/EditorCommandTest.cpp
        src/core/HText.cpp
        src/editor/ActorSnapshot.cpp
        src/scene/World.cpp
        src/scene/TimerManager.cpp
        src/scene/Actor.cpp
        src/scene/Component.cpp
        src/core/HLog.cpp
    )
    target_link_libraries(HarukaEditCmdTest PRIVATE glm::glm HarukaVfs)   # HText(H2b 명령 표의 글)이 번역 표 파일을 vfs 로 읽음
    target_include_directories(HarukaEditCmdTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaEditCmdTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaEditCmdTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    add_executable(HarukaDockTest
        tests/DockTreeTest.cpp
        src/editor/dock/DockTree.cpp
    )
    target_include_directories(HarukaDockTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaDockTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaDockTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 액터 생성 · 삭제 Undo 테스트 -----------------------------------------
    # 서브트리를 떠 두고 원래 ID · 원래 부모로 되살리는지, World 의 ID 순서와 다음 ID 가 지켜지는지,
    # 커맨드 스택과 purge 사이의 규칙을 검산합니다. 씬 직렬화 대신 테스트용 글 형식을 넘깁니다.
    add_executable(HarukaActorUndoTest
        tests/ActorUndoTest.cpp
        src/editor/ActorSnapshot.cpp
        src/scene/World.cpp
        src/scene/TimerManager.cpp
        src/scene/Actor.cpp
        src/scene/Component.cpp
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

    # -- 글자 체계 테스트(2026-11-19, Docs/Text_System.md) -------------------
    # UTF-8 · 글자 경계 · 편집 상태 · 입력 처리기(가짜 창, IME 조합) · 표시 글 HText 를 창 없이 검산합니다.
    add_executable(HarukaTextTest
        tests/TextTest.cpp
        src/editor/TextInputController.cpp
        src/core/HText.cpp
        src/ui/slate/HFontAtlas.cpp
        src/ui/slate/HPaintContext.cpp
        src/core/HLog.cpp
    )
    target_link_libraries(HarukaTextTest PRIVATE Vulkan::Vulkan glm::glm HarukaVfs)
    target_include_directories(HarukaTextTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${stb_SOURCE_DIR} ${Vulkan_INCLUDE_DIRS})
    # 번역 표가 소스의 HTEXT 키를 모두 덮는지 맞대어 봅니다(T3-8).
    target_compile_definitions(HarukaTextTest PRIVATE HARUKA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src"
                                                      HARUKA_ENGINE_CONTENT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Engine/Content")
    if(MSVC)
        target_compile_options(HarukaTextTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaTextTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 참조 체계 테스트(Docs/Object_References.md) ---------------------------
    # 액터 GUID · 액터 참조 · GUID 정책(Keep · Fresh) · 디테일 칸 · 에셋 참조 색인 · 검사 · 옮기기를 창 · GPU 없이 검산합니다.
    # 씬 읽기 · 쓰기(SceneSerializer)를 그대로 쓰려고 엔진 정적 라이브러리를 링크합니다(메시 없는 씬만 읽어 렌더러를 부르지 않음).
    # 샘플 프로젝트에 끊긴 참조가 없는지도 봅니다.
    add_executable(HarukaRefTest
        tests/ReferenceTest.cpp
        src/editor/ActorSnapshot.cpp
    )
    target_link_libraries(HarukaRefTest PRIVATE HarukaRuntime)
    target_include_directories(HarukaRefTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(HarukaRefTest PRIVATE HARUKA_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Projects/Samples")
    if(MSVC)
        target_compile_options(HarukaRefTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaRefTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 링크된 프리팹 테스트(2026-11-21, Docs/Linked_Prefabs.md) -----------------
    # 인스턴스 블록 펼치기 · 결정적 GUID · 비교로 얻는 덮어쓰기 · 다시 펼침 · 중첩 2단 왕복 · 순환 거부 · 추가 · 삭제 · 링크 끊기 ·
    # 되돌리기 · 원본에 적용 · 옛 씬 · 프리팹을 창 · GPU 없이 검산합니다(메시 없는 액터만, 프리팹 글은 캐시 서비스에 넣음).
    add_executable(HarukaPrefabTest
        tests/PrefabLinkTest.cpp
        src/editor/ActorSnapshot.cpp
    )
    target_link_libraries(HarukaPrefabTest PRIVATE HarukaRuntime)
    target_include_directories(HarukaPrefabTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaPrefabTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaPrefabTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 애니메이션 테스트(2026-11-20, Docs/Animation_System.md) ------------------
    # 트랙 샘플링 · 자세 섞기 · glTF 클립 가져오기 · 캐시 v10 · 재생기(크로스페이드 · 가산 · 이벤트 · 루트 모션) · 상태 기계를
    # 화면 없이 검산합니다. 시험 모델 Projects/Samples/Content/models/anim/TestArm.glb 를 가상 파일 시스템으로 읽습니다.
    add_executable(HarukaAnimTest
        tests/AnimationTest.cpp
        src/assets/Animation.cpp
        src/assets/AnimStateMachine.cpp
        src/assets/GltfLoader.cpp
        src/assets/GassetCache.cpp
        src/assets/ImportSettings.cpp
        src/assets/Skin.cpp
        src/geometry/MeshLod.cpp
        src/components/AnimationComponent.cpp
        src/components/SkinnedMeshComponent.cpp
        src/scene/World.cpp
        src/scene/TimerManager.cpp
        src/scene/Actor.cpp
        src/scene/Component.cpp
        src/core/HLog.cpp
    )
    target_link_libraries(HarukaAnimTest PRIVATE HarukaVfs tinygltf glm::glm meshoptimizer)
    target_include_directories(HarukaAnimTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIRS})
    target_compile_definitions(HarukaAnimTest PRIVATE HARUKA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(HarukaAnimTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaAnimTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 비동기 로딩 테스트(2026-11-20, Docs/Async_Loading.md) -------------------
    # 메가 버퍼 구간 할당자 · 백그라운드 모델 읽기(워커 스레드 · 중복 요청 · 실패 · 동시 요청 · 동기 결과와 같음) · 씬의 모델 참조
    # 모으기를 GPU 없이 검산합니다. 씬 · 에셋 코드를 그대로 쓰려고 엔진 정적 라이브러리를 링크합니다.
    # -- 저장 게임 · 사용자 설정 테스트(Docs/Save_And_Settings.md) ---------------------
    # 저장 파일 형식 · 슬롯(원자적 쓰기 · 이전 저장본) · 씬 GUID 참조 왕복 · 사용자 설정 층 · 편안함 서비스를 창 · GPU 없이 검산합니다.
    add_executable(HarukaSaveTest tests/SaveTest.cpp)
    target_link_libraries(HarukaSaveTest PRIVATE HarukaRuntime)
    target_include_directories(HarukaSaveTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaSaveTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaSaveTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 접근성 테스트(Docs/Accessibility.md) -----------------------------------------------
    # 자막 큐(시간 · 겹침 · 우선순위) · 자막 판 크기와 자리 · 글자 배율 레이아웃 · 비네트 세기 · 앉은 높이 · 손 바꾸기 · 색 보정 행렬 ·
    # 설정 왕복을 창 · GPU 없이 검산합니다.
    add_executable(HarukaAccessTest tests/AccessibilityTest.cpp)
    target_link_libraries(HarukaAccessTest PRIVATE HarukaRuntime)
    target_include_directories(HarukaAccessTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaAccessTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaAccessTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 오디오 버스 그래프 · 스냅숏 · 더킹 · 리미터 · 사운드 큐 테스트(Docs/Audio_Design.md 12절) --------------
    # 장치 없는 소리 엔진에서 출력을 당겨 더킹 곡선 · 리미터 피크 · 스냅숏 섞기 · 큐의 동시 재생 규칙과 시드를 숫자로 검산합니다.
    add_executable(HarukaAudioMixTest tests/AudioMixTest.cpp)
    target_link_libraries(HarukaAudioMixTest PRIVATE HarukaRuntime)
    target_include_directories(HarukaAudioMixTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaAudioMixTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaAudioMixTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 게임플레이 기본 도구 · CVar · 콘솔 테스트(Docs/Gameplay_Tools.md) --------------
    # 게임 시간(배율) · 타이머 · 물리 뒤 틱 · 태그 · 이벤트 버스 · CVar · 콘솔 명령을 창 · GPU 없이 검산합니다.
    add_executable(HarukaGameplayTest tests/GameplayTest.cpp)
    target_link_libraries(HarukaGameplayTest PRIVATE HarukaRuntime)
    target_include_directories(HarukaGameplayTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaGameplayTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaGameplayTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 파티클 · 데칼 테스트(Docs/Particles_And_Decals.md) ---------------------------
    # 이미터 시뮬레이션(결정적 시드 · 수명 · 상한 · 곡선 · 공간 · 시간 배율), 그리기 순서(이미터끼리 · 이미터 안 정렬),
    # 씬 파일 왕복, 데칼 상자 · 모양 · 각도 계산, 확인 장면을 창 · GPU 없이 봅니다.
    add_executable(HarukaParticleTest tests/ParticleTest.cpp)
    target_link_libraries(HarukaParticleTest PRIVATE HarukaRuntime)
    target_include_directories(HarukaParticleTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(HarukaParticleTest PRIVATE HARUKA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(HarukaParticleTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaParticleTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 텍스처 압축 테스트(2026-11-21, Docs/Texture_Compression.md) ---------------------------
    # KTX2 쓰기 · 읽기(정상 · 손상), 형식 표, 장치 지원 판단(대체 경로), 밉 생성, 압축 → 풀기 PSNR 하한, 쿠킹 캐시, 시험 모델을
    # 쿠킹한 뒤 런타임 찾기가 모두 붙는지(열쇠 일치)를 GPU 없이 봅니다. 인코더는 묶음 도구와 같은 소스입니다.
    add_executable(HarukaTexTest tests/TextureCompressionTest.cpp ${HARUKA_TEXTURE_COOK_SOURCES})
    target_link_libraries(HarukaTexTest PRIVATE HarukaVfs tinygltf glm::glm meshoptimizer haruka_astcenc haruka_bc7enc)
    target_include_directories(HarukaTexTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIRS})
    target_compile_definitions(HarukaTexTest PRIVATE HARUKA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(HarukaTexTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaTexTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- CPU 구간 계측 테스트(Docs/CPU_Profiling.md) --------------------------------
    # 중첩 · 스레드 구분 · 시작 / 멈춤 · 상한 · Chrome trace JSON(다시 읽어 검사) · 끈 번역 단위(ProfilerOffTest.cpp, 매크로 비용 0) ·
    # FrameTimings 공유 측정 지점 · 추적 경로 규칙을 창 · GPU 없이 봅니다.
    add_executable(HarukaProfilerTest tests/ProfilerTest.cpp tests/ProfilerOffTest.cpp)
    target_link_libraries(HarukaProfilerTest PRIVATE HarukaRuntime)
    target_include_directories(HarukaProfilerTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaProfilerTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaProfilerTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 내비게이션 · AI 테스트(Docs/Navigation_AI.md) ------------------------------------------------
    # 시험 지형(바닥 · 벽 · 경사 · 섬)의 굽기 · 경로 길이 · 부분 경로 · 없음, 생성 시간, 결정성, .hnav 왕복 · 손상, 에이전트 이동 · 회피,
    # AI 상태 기계, 확인 장면(NavProbe.hscene + 구운 NavProbe.hnav)을 창 · GPU 없이 봅니다.
    add_executable(HarukaNavTest tests/NavigationTest.cpp)
    target_link_libraries(HarukaNavTest PRIVATE HarukaRuntime)
    target_include_directories(HarukaNavTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(HarukaNavTest PRIVATE HARUKA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(HarukaNavTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaNavTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 물리 조인트 · 충돌 이벤트 · 레이어 테스트(Docs/Physics_Joints_And_Events.md) ------------------
    # 레이어 표 · 무시한 쌍 · 트리거 들어감 · 나감 · 접촉 알림 · 알림 중 삭제 · 조인트 네 종류 · 상대 삭제 · 셰이프 캐스트 · 확인 장면을
    # 창 · GPU 없이 봅니다. 확인 장면을 저장소에서 읽도록 소스 경로를 넘깁니다.
    add_executable(HarukaJointTest tests/PhysicsJointsTest.cpp)
    target_link_libraries(HarukaJointTest PRIVATE HarukaRuntime)
    target_include_directories(HarukaJointTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(HarukaJointTest PRIVATE HARUKA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(HarukaJointTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaJointTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    add_executable(HarukaLoadingTest tests/LoadingTest.cpp)
    target_link_libraries(HarukaLoadingTest PRIVATE HarukaRuntime)
    target_include_directories(HarukaLoadingTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(HarukaLoadingTest PRIVATE HARUKA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(HarukaLoadingTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaLoadingTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 에셋 핫리로드 · 가져오기 설정 테스트(Docs/Asset_Hot_Reload.md) ------------
    # import.ini 패턴 · 설정 해시, 바뀐 파일 모으기, 의존 텍스처가 바뀌면 캐시 무효(DEF-174), 다시 묶기, 옛 자원 은퇴 수, 임시 폴더 감시를
    # 창 · GPU 없이 봅니다. 감시자는 에디터 소스라 직접 넣습니다.
    add_executable(HarukaHotReloadTest tests/HotReloadTest.cpp src/editor/AssetWatcher.cpp)
    target_link_libraries(HarukaHotReloadTest PRIVATE HarukaRuntime)
    target_include_directories(HarukaHotReloadTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaHotReloadTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaHotReloadTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 크래시 보고 테스트(Docs/Crash_Reporting.md) ---------------------------
    # 자기 자신을 자식 프로세스로 띄워 종류마다 일부러 크래시시키고, 보고서 폴더 · crash.txt · crash.dmp · 다음 실행 알림 · 보관 수를 봅니다.
    add_executable(HarukaCrashTest
        tests/CrashTest.cpp
        src/core/CrashHandler.cpp
        src/core/HLog.cpp
    )
    target_include_directories(HarukaCrashTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaCrashTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaCrashTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 오디오 테스트 ------------------------------------------------------
    # 출력 장치 없는 miniaudio 엔진(noDevice)에서 출력 샘플을 직접 당겨, 청취자 방향·거리
    # 감쇠·일시정지·비활성 정지·OGG 디코딩을 귀가 아니라 숫자로 검산합니다. Vulkan 도 창도
    # 필요 없습니다. OGG 확인에는 저장소의 시험용 음원을 쓰므로 소스 경로를 넘깁니다.
    # 차폐 절(2026-11-06)은 실제 물리(Jolt)로 정적 상자 벽을 세우고 레이 판정까지 검산하므로 물리 파일도 넣습니다.
    add_executable(HarukaAudioTest
        tests/AudioTest.cpp
        src/audio/AudioSystem.cpp
        src/audio/AudioMix.cpp
        src/core/CVar.cpp
        src/audio/AudioAttenuation.cpp
        src/audio/MiniaudioImpl.cpp
        src/audio/SteamAudioHrtf.cpp
        src/audio/AudioComponent.cpp
        src/core/Subtitles.cpp
        src/core/Accessibility.cpp
        src/core/UserSettings.cpp
        src/core/HText.cpp
        src/components/CameraComponent.cpp
        src/audio/ReverbZoneComponent.cpp
        src/physics/RigidBodyComponent.cpp
        src/physics/JointComponent.cpp
        src/physics/PhysicsSystem.cpp
        src/core/Profiler.cpp
        src/scene/World.cpp
        src/scene/TimerManager.cpp
        src/scene/Actor.cpp
        src/scene/Component.cpp
        src/core/HLog.cpp
    )
    target_link_libraries(HarukaAudioTest PRIVATE glm::glm HarukaVfs Jolt)
    target_include_directories(HarukaAudioTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${miniaudio_SOURCE_DIR})
    target_compile_definitions(HarukaAudioTest PRIVATE
        HARUKA_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Projects/Samples/Content")
    # HRTF 절(17절)은 Steam Audio 로 빌드했으면 실제 라이브러리도 확인합니다(실행 폴더의 phonon.dll).
    if(HARUKA_STEAM_AUDIO_FOUND)
        target_compile_definitions(HarukaAudioTest PRIVATE HARUKA_EXPECT_STEAM_AUDIO=1)
        add_dependencies(HarukaAudioTest SteamAudioRuntime)
    endif()
    if(MSVC)
        target_compile_options(HarukaAudioTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaAudioTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- HSlate 입력 테스트 --------------------------------------------------
    # 포인터 캡처와 숫자 칸의 프레스 추적을 화면 없이 검산합니다. 렌더러의 입력 경로는
    # Vulkan 을 부르지 않지만, 같은 파일의 렌더 코드 때문에 Vulkan 로더에 링크합니다.
    add_executable(HarukaSlateTest
        tests/SlateInputTest.cpp
        src/core/HText.cpp
        src/ui/slate/HSlateRenderer.cpp
        src/ui/slate/HFontAtlas.cpp
        src/ui/slate/HPaintContext.cpp
        src/core/HLog.cpp
        # 3D UI 패널 P1b: 컴포넌트(텍스처 크기 · 직렬화 · 내용). 깨진 값 알림이 액터 이름을 씀
        src/components/UIPanelComponent.cpp
        src/components/UIPanelPointer.cpp
        src/scene/Component.cpp
        src/scene/Actor.cpp
        # 에디터 아이콘(D3): SVG 읽기 · 래스터화
        src/editor/EditorIcons.cpp
        # 다중 OS 창 M3a: 도크 창 단위(패널이 한 루트에만) · 떠 있는 창 수명(가짜 창 · 렌더러 호출 순서)
        src/editor/EditorDock.cpp
        src/editor/EditorUiKit.cpp
        src/editor/EditorFloatWindows.cpp
        src/editor/dock/DockTree.cpp
        # H3a: 열거 콤보 = 오른쪽 메뉴와 같은 메뉴 위젯(키 · 체크 · 바깥 닫기)
        src/editor/EditorContextMenu.cpp
        src/editor/EditorMenuView.cpp
    )
    # 셰이더 · 글꼴을 가상 파일 시스템으로 읽습니다(2026-10-24).
    target_link_libraries(HarukaSlateTest PRIVATE Vulkan::Vulkan glm::glm HarukaVfs haruka_nanosvg)
    target_compile_definitions(HarukaSlateTest PRIVATE HARUKA_ENGINE_CONTENT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Engine/Content")
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
        src/core/Profiler.cpp
        src/physics/RigidBodyComponent.cpp
        src/physics/JointComponent.cpp
        src/components/XrComponents.cpp
        src/components/CameraComponent.cpp
        src/components/HandPoseComponent.cpp
        src/components/SkinnedMeshComponent.cpp
        src/assets/Skin.cpp
        src/xr/GraspSolver.cpp
        src/input/HInputSystem.cpp
        src/core/UserSettings.cpp
        src/core/Accessibility.cpp
        src/scene/World.cpp
        src/scene/TimerManager.cpp
        src/scene/Actor.cpp
        src/scene/Component.cpp
        src/core/HLog.cpp
    )
    target_link_libraries(HarukaPhysicsTest PRIVATE glm::glm Jolt)
    # 리그 · 잡기 검사가 입력 시스템을 씁니다. IWindow.hpp 가 Vulkan 형을 쓰므로 헤더 경로만 둡니다.
    target_include_directories(HarukaPhysicsTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(HarukaPhysicsTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaPhysicsTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 묶음 파일 테스트 -----------------------------------------------------
    # .hpak v1 의 왕복 · 범위 읽기 · 손상 감지 · 정렬 · 결정적 출력 · 경로 정규화 · 버전 규칙 · 우선순위 · 동시 읽기를
    # 화면 없이 검산합니다(2026-10-24). 2026-10-31 부터 조작한 목차 · UTF-8 경로 · 목록 패턴 · --loose 우선순위도 봅니다.
    add_executable(HarukaPakTest tests/PakTest.cpp)
    target_link_libraries(HarukaPakTest PRIVATE HarukaVfs)
    target_compile_definitions(HarukaPakTest PRIVATE HARUKA_PACK_MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/Packaging/game.pack.txt")
    if(MSVC)
        target_compile_options(HarukaPakTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaPakTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- glTF 외부 파일 경로 테스트 -------------------------------------------
    # .gltf 의 uri 에 "../" · "./" 가 든 외부 버퍼 · 텍스처를 낱개 폴더와 묶음에서 찾는지, 프로젝트 밖으로 올라가는 uri 는
    # 거부하는지 검산합니다(2026-10-31, DEF-125).
    add_executable(HarukaGltfTest
        tests/GltfLoaderTest.cpp
        src/assets/GltfLoader.cpp
        src/assets/ImportSettings.cpp
        src/assets/Animation.cpp
        src/assets/Skin.cpp
        src/geometry/MeshLod.cpp
        src/core/HLog.cpp
    )
    target_link_libraries(HarukaGltfTest PRIVATE HarukaVfs tinygltf glm::glm meshoptimizer)
    target_include_directories(HarukaGltfTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(HarukaGltfTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaGltfTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # -- 손 쥐기 A 단계 테스트(2026-11-07, Docs/Hand_Grasp_Design.md 6) ---------
    # 손 뼈대 표 · CPU 스키닝 수식 · 시험 장갑 glb 읽기 · 캐시 7 왕복 · 손 자세(굽힘 방향 · 축 규약 경고)를 화면 없이 검산합니다.
    # 시험 장갑을 저장소의 assets/models/hands 에서 읽도록 소스 폴더를 가상 파일 시스템 뿌리로 씁니다.
    add_executable(HarukaHandTest
        tests/HandTest.cpp
        src/assets/GltfLoader.cpp
        src/assets/Animation.cpp
        src/assets/GassetCache.cpp
        src/assets/ImportSettings.cpp
        src/assets/Skin.cpp
        src/geometry/MeshLod.cpp
        src/components/SkinnedMeshComponent.cpp
        src/components/HandPoseComponent.cpp
        src/components/CameraComponent.cpp
        src/components/XrComponents.cpp
        src/physics/RigidBodyComponent.cpp
        src/physics/JointComponent.cpp
        src/physics/PhysicsSystem.cpp
        src/core/Profiler.cpp
        src/xr/GraspSolver.cpp
        src/input/HInputSystem.cpp
        src/core/UserSettings.cpp
        src/core/Accessibility.cpp
        src/scene/World.cpp
        src/scene/TimerManager.cpp
        src/scene/Actor.cpp
        src/scene/Component.cpp
        src/core/HLog.cpp
    )
    # 자동 맞춤 쥐기(손 쥐기 D)는 잡기(XrComponents) · Jolt 바디 판정을 함께 시험합니다.
    target_link_libraries(HarukaHandTest PRIVATE HarukaVfs tinygltf glm::glm Jolt meshoptimizer)
    target_include_directories(HarukaHandTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIRS})
    target_compile_definitions(HarukaHandTest PRIVATE HARUKA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(HarukaHandTest PRIVATE /W4 /utf-8 /wd4865 /Wv:18)
        set_target_properties(HarukaHandTest PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()

    # 모든 시험 실행 파일이 엔진 · 도구와 같은 코드 페이지(UTF-8)에서 돕니다(DEF-121, 2026-10-31 검토). 예를 들어 소리 시험은
    # 임시 폴더의 경로를 std::string 으로 넘기는데, 매니페스트가 없으면 CP949 바이트가 되어 가상 파일 시스템이 찾지 못합니다.
    set(HARUKA_TESTS HarukaPakTest HarukaGltfTest HarukaGeometryTest HarukaActorTest HarukaCommandTest HarukaActorUndoTest
        HarukaLogTest HarukaAudioTest HarukaSlateTest HarukaPhysicsTest HarukaStereoTest HarukaInputTest HarukaBatchTest
        HarukaRenderExtTest HarukaSceneTest HarukaLightTest HarukaAssetCacheTest HarukaSceneDocTest HarukaEditCmdTest HarukaDockTest
        HarukaVulkanBaselineTest HarukaBarrierTest HarukaMemoryTest HarukaHeadlessTest HarukaHandTest HarukaMathTest HarukaArchTest
        HarukaWindowTest)
    if(WIN32)
        list(APPEND HARUKA_TESTS HarukaVkImportTest)
    endif()
    if(UTF8_MANIFEST)
        foreach(T ${HARUKA_TESTS})
            target_sources(${T} PRIVATE ${UTF8_MANIFEST})
        endforeach()
    endif()

    enable_testing()
    add_test(NAME pak_format           COMMAND HarukaPakTest)
    add_test(NAME gltf_external_files  COMMAND HarukaGltfTest)
    add_test(NAME geometry_conventions COMMAND HarukaGeometryTest)
    add_test(NAME actor_lifecycle      COMMAND HarukaActorTest)
    add_test(NAME command_stack_purge  COMMAND HarukaCommandTest)
    add_test(NAME actor_undo           COMMAND HarukaActorUndoTest)
    add_test(NAME log_category         COMMAND HarukaLogTest)
    add_test(NAME text_system          COMMAND HarukaTextTest)
    add_test(NAME object_refs          COMMAND HarukaRefTest)
    add_test(NAME prefab_links         COMMAND HarukaPrefabTest)
    add_test(NAME crash_reports        COMMAND HarukaCrashTest)
    add_test(NAME animation            COMMAND HarukaAnimTest)
    add_test(NAME async_loading        COMMAND HarukaLoadingTest)
    add_test(NAME asset_hot_reload     COMMAND HarukaHotReloadTest)
    add_test(NAME save_settings        COMMAND HarukaSaveTest)
    add_test(NAME accessibility        COMMAND HarukaAccessTest)
    add_test(NAME gameplay_tools       COMMAND HarukaGameplayTest)
    add_test(NAME cpu_profiling        COMMAND HarukaProfilerTest)
    add_test(NAME audio_system         COMMAND HarukaAudioTest)
    add_test(NAME audio_mix            COMMAND HarukaAudioMixTest)
    add_test(NAME slate_input          COMMAND HarukaSlateTest)
    add_test(NAME physics_lifecycle    COMMAND HarukaPhysicsTest)
    add_test(NAME physics_joints       COMMAND HarukaJointTest)
    add_test(NAME navigation           COMMAND HarukaNavTest)
    add_test(NAME particles_decals     COMMAND HarukaParticleTest)
    add_test(NAME texture_compression  COMMAND HarukaTexTest)
    add_test(NAME stereo_views         COMMAND HarukaStereoTest)
    add_test(NAME math_common          COMMAND HarukaMathTest)
    add_test(NAME architecture_deps    COMMAND HarukaArchTest)
    add_test(NAME xr_input             COMMAND HarukaInputTest)
    add_test(NAME window_platform      COMMAND HarukaWindowTest)
    add_test(NAME draw_batching        COMMAND HarukaBatchTest)
    add_test(NAME render_extensions    COMMAND HarukaRenderExtTest)
    add_test(NAME scene_components     COMMAND HarukaSceneTest)
    add_test(NAME light_selection      COMMAND HarukaLightTest)
    add_test(NAME asset_cache          COMMAND HarukaAssetCacheTest)
    add_test(NAME scene_document       COMMAND HarukaSceneDocTest)
    add_test(NAME editor_commands      COMMAND HarukaEditCmdTest)
add_test(NAME dock_tree            COMMAND HarukaDockTest)
    add_test(NAME vulkan_baseline      COMMAND HarukaVulkanBaselineTest)
    add_test(NAME barrier_compat       COMMAND HarukaBarrierTest)
    add_test(NAME memory_types         COMMAND HarukaMemoryTest)
    add_test(NAME headless_render      COMMAND HarukaHeadlessTest)
add_test(NAME hand_skinning        COMMAND HarukaHandTest)
    if(WIN32)
        add_test(NAME vulkan_imports   COMMAND HarukaVkImportTest $<TARGET_FILE:HarukaEngine> $<TARGET_FILE:HarukaGame>
                                               ${Vulkan_INCLUDE_DIRS}/vulkan/vulkan_core.h)
    endif()
endif()
