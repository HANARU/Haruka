# ── 프로젝트 설명 파일 읽기 (*.hproject, P4 2026-11-19) ──────────────────────
# src/core/ProjectDescriptor.hpp 와 같은 규칙으로 *.hproject 를 읽습니다.
function(haruka_read_project_descriptor PROJECT_DIR OUT_PREFIX)
    file(GLOB _hproj_files LIST_DIRECTORIES false CONFIGURE_DEPENDS "${PROJECT_DIR}/*.hproject")
    if(NOT _hproj_files)
        message(FATAL_ERROR "haruka_read_project_descriptor: no .hproject file in ${PROJECT_DIR}")
    endif()
    list(SORT _hproj_files)
    list(GET _hproj_files 0 _hproj_file)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_hproj_file}")

    set(_name "")
    set(_company "")
    set(_packageId "")
    set(_engineVersion "")

    file(STRINGS "${_hproj_file}" _lines ENCODING UTF-8)
    set(_line_no 0)
    foreach(_line IN LISTS _lines)
        math(EXPR _line_no "${_line_no} + 1")
        string(STRIP "${_line}" _t)
        if(_t STREQUAL "" OR _t MATCHES "^#")
            continue()
        endif()
        string(FIND "${_t}" "=" _eq)
        if(_eq EQUAL -1)
            message(WARNING "${_hproj_file}:${_line_no}: expected 'name = value'")
            continue()
        endif()
        string(SUBSTRING "${_t}" 0 ${_eq} _key)
        math(EXPR _val_start "${_eq} + 1")
        string(SUBSTRING "${_t}" ${_val_start} -1 _val)
        string(STRIP "${_key}" _key)
        string(STRIP "${_val}" _val)
        if(_key STREQUAL "name")
            set(_name "${_val}")
        elseif(_key STREQUAL "company")
            set(_company "${_val}")
        elseif(_key STREQUAL "packageId")
            set(_packageId "${_val}")
        elseif(_key STREQUAL "engineVersion")
            set(_engineVersion "${_val}")
        else()
            message(WARNING "${_hproj_file}:${_line_no}: unknown key '${_key}'")
        endif()
    endforeach()

    foreach(_req IN ITEMS name company packageId engineVersion)
        if("${_${_req}}" STREQUAL "")
            message(FATAL_ERROR "${_hproj_file}: missing required key '${_req}'")
        endif()
    endforeach()

    set(${OUT_PREFIX}_name          "${_name}"          PARENT_SCOPE)
    set(${OUT_PREFIX}_company       "${_company}"       PARENT_SCOPE)
    set(${OUT_PREFIX}_packageId     "${_packageId}"     PARENT_SCOPE)
    set(${OUT_PREFIX}_engineVersion "${_engineVersion}" PARENT_SCOPE)
    set(${OUT_PREFIX}_FILE          "${_hproj_file}"   PARENT_SCOPE)
endfunction()

# ── 프로젝트 대상 등록 함수 (P3b, 2026-11-19 / P4 2026-11-19) ─────────────────
# 프로젝트 폴더, 게임 모듈 소스(없으면 빈 기본 모듈), 에디터 · 게임 실행 파일 이름을 받아
# HarukaRuntime 에 정적 링크한 두 대상과 셰이더/콘텐츠 복사, 묶음 대상을 만듭니다.
# Android 빌드도 같은 함수로 SHARED 게임 라이브러리와 매니페스트, 묶음을 만듭니다.
function(haruka_add_project)
    cmake_parse_arguments(HP "" "NAME;PROJECT_DIR;EDITOR_TARGET;GAME_TARGET" "SOURCES" ${ARGN})
    if(NOT HP_NAME)
        message(FATAL_ERROR "haruka_add_project: NAME is required")
    endif()
    if(NOT HP_PROJECT_DIR)
        message(FATAL_ERROR "haruka_add_project(${HP_NAME}): PROJECT_DIR is required")
    endif()
    if(NOT HP_EDITOR_TARGET)
        set(HP_EDITOR_TARGET "${HP_NAME}Editor")
    endif()
    if(NOT HP_GAME_TARGET)
        set(HP_GAME_TARGET "${HP_NAME}Game")
    endif()
    if(NOT HP_SOURCES)
        set(HP_SOURCES "${HARUKA_ENGINE_ROOT}/src/runtime/EmptyGameModule.cpp")
    endif()

    haruka_read_project_descriptor("${HP_PROJECT_DIR}" HP_DESC)
    set_property(GLOBAL PROPERTY HARUKA_PACKAGING_NAME "${HP_DESC_name}")

    if(HP_GAME_TARGET STREQUAL "HarukaGame")
        set(PACK_TGT "GamePack")
        set(PACK_OUT "${CMAKE_BINARY_DIR}/pack/game.hpak")
        set(RT_TGT   "RuntimeFiles")
        set(SA_TGT   "SteamAudioRuntime")
    else()
        set(PACK_TGT "${HP_NAME}GamePack")
        set(PACK_OUT "${CMAKE_BINARY_DIR}/pack/${HP_NAME}/game.hpak")
        set(RT_TGT   "${HP_NAME}RuntimeFiles")
        set(SA_TGT   "${HP_NAME}SteamAudioRuntime")
    endif()

    # ── 게임 실행 파일 · 에디터 실행 파일 ────────────────────────────────────────
    if(ANDROID)
        add_library(${HP_GAME_TARGET} SHARED
            ${HARUKA_ENGINE_ROOT}/src/AndroidMain.cpp
            ${HARUKA_GAME_SOURCES}
            ${HP_SOURCES}
            $<TARGET_OBJECTS:haruka_app_glue>
        )
        target_link_libraries(${HP_GAME_TARGET} PRIVATE HarukaRuntime)
        target_compile_definitions(${HP_GAME_TARGET} PRIVATE
            HARUKA_PROJECT_NAME="${HP_DESC_name}"
            HARUKA_PROJECT_COMPANY="${HP_DESC_company}"
            HARUKA_PROJECT_PACKAGE_ID="${HP_DESC_packageId}"
            HARUKA_GAME_TARGET_NAME="${HP_GAME_TARGET}"
        )
        set_target_properties(${HP_GAME_TARGET} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        )
        add_dependencies(${HP_GAME_TARGET} Shaders)
        haruka_target_defaults(${HP_GAME_TARGET})

        set(HARUKA_ANDROID_PACKAGE_ID "${HP_DESC_packageId}")
        set(HARUKA_ANDROID_VERSION    "${HP_DESC_engineVersion}")
        set(HARUKA_ANDROID_APP_LABEL  "${HP_DESC_name}")
        set(HARUKA_ANDROID_LIB_NAME   "${HP_GAME_TARGET}")
        configure_file(
            "${HARUKA_ENGINE_ROOT}/Packaging/android/AndroidManifest.xml.in"
            "${CMAKE_BINARY_DIR}/AndroidManifest.xml"
            @ONLY
        )
    else()
        add_executable(${HP_EDITOR_TARGET}
            ${HARUKA_ENGINE_ROOT}/src/main.cpp
            ${HARUKA_EDITOR_SOURCES}
            ${HP_SOURCES}
            ${HARUKA_EXE_RESOURCES}
        )
        target_link_libraries(${HP_EDITOR_TARGET} PRIVATE HarukaRuntime haruka_nanosvg)
        target_compile_definitions(${HP_EDITOR_TARGET} PRIVATE
            HARUKA_EDITOR
            HARUKA_PROJECT_DIR="${HP_PROJECT_DIR}"
            HARUKA_ENGINE_CONTENT_DIR="${HARUKA_ENGINE_ROOT}/Engine/Content"
            HARUKA_PROJECT_NAME="${HP_DESC_name}"
            HARUKA_PROJECT_COMPANY="${HP_DESC_company}"
            HARUKA_PROJECT_PACKAGE_ID="${HP_DESC_packageId}"
        )

        add_executable(${HP_GAME_TARGET}
            ${HARUKA_ENGINE_ROOT}/src/main.cpp
            ${HARUKA_GAME_SOURCES}
            ${HP_SOURCES}
            ${HARUKA_EXE_RESOURCES}
        )
        target_link_libraries(${HP_GAME_TARGET} PRIVATE HarukaRuntime)
        target_compile_definitions(${HP_GAME_TARGET} PRIVATE
            HARUKA_PROJECT_NAME="${HP_DESC_name}"
            HARUKA_PROJECT_COMPANY="${HP_DESC_company}"
            HARUKA_PROJECT_PACKAGE_ID="${HP_DESC_packageId}"
        )

        set(PROJECT_EXES ${HP_EDITOR_TARGET} ${HP_GAME_TARGET})
        foreach(EXE ${PROJECT_EXES})
            add_dependencies(${EXE} Shaders)
            haruka_target_defaults(${EXE})
            set_target_properties(${EXE} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
            )
            if(MSVC)
                if(HARUKA_CONSOLE)
                    set_target_properties(${EXE} PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
                else()
                    set_target_properties(${EXE} PROPERTIES
                        LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
                endif()
            endif()
        endforeach()

        add_custom_target(${RT_TGT} ALL
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${HARUKA_SHADER_BIN_DIR} $<TARGET_FILE_DIR:${HP_GAME_TARGET}>/shaders
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${HP_PROJECT_DIR}/Content $<TARGET_FILE_DIR:${HP_GAME_TARGET}>/Content
            # 엔진 콘텐츠(글꼴 · 아이콘 · 번역, 글자 체계 T4-4): 게임 실행 파일이 실행 폴더의 Engine/Content 를 /Engine/ 으로 씀
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${HARUKA_ENGINE_ROOT}/Engine/Content $<TARGET_FILE_DIR:${HP_GAME_TARGET}>/Engine/Content
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${HP_PROJECT_DIR}/Config/project.ini $<TARGET_FILE_DIR:${HP_GAME_TARGET}>/Config/project.ini
            COMMENT "Copying SPIR-V shaders and project files to the output dir (${HP_NAME})"
        )
        add_dependencies(${RT_TGT} Shaders)
        foreach(EXE ${PROJECT_EXES})
            add_dependencies(${EXE} ${RT_TGT})
        endforeach()

        if(HARUKA_STEAM_AUDIO_FOUND)
            add_custom_target(${SA_TGT} ALL
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${HARUKA_STEAM_AUDIO_LIBRARY} $<TARGET_FILE_DIR:${HP_GAME_TARGET}>
                COMMENT "Copying the Steam Audio library to the output dir (${HP_NAME})"
            )
            foreach(EXE ${PROJECT_EXES})
                add_dependencies(${EXE} ${SA_TGT})
            endforeach()
        endif()
    endif()

    # ── 묶음 파일 (game.hpak) ────────────────────────────────────────────────────
    file(GLOB_RECURSE PROJECT_PACK_ASSETS CONFIGURE_DEPENDS
        ${HP_PROJECT_DIR}/Content/*
        ${HP_PROJECT_DIR}/Config/*
        ${HARUKA_ENGINE_ROOT}/Engine/Content/*)
    # 텍스처 압축 대상(Docs/Texture_Compression.md D6): Windows 묶음은 BC, Android 묶음은 ASTC. 압축 결과는 빌드 폴더에 캐시합니다.
    if(ANDROID)
        set(_hp_tex_target astc)
    else()
        set(_hp_tex_target bc)
    endif()
    add_custom_command(
        OUTPUT  ${PACK_OUT}
        COMMAND ${HARUKA_PAK_TOOL} build ${HARUKA_ENGINE_ROOT}/Packaging/game.pack.txt -o ${PACK_OUT}
                -D PROJECT_DIR=${HP_PROJECT_DIR} -D SHADER_DIR=${HARUKA_SHADER_BIN_DIR}
                -D ENGINE_CONTENT_DIR=${HARUKA_ENGINE_ROOT}/Engine/Content
                -D TEXTURE_TARGET=${_hp_tex_target} -D TEXTURE_CACHE=${CMAKE_BINARY_DIR}/texture_cache
        DEPENDS ${HARUKA_PAK_DEPENDS} ${HARUKA_SPIRV_OUTPUTS} ${PROJECT_PACK_ASSETS} ${HARUKA_ENGINE_ROOT}/Packaging/game.pack.txt
        COMMENT "HarukaPak -> game.hpak (${HP_NAME})"
        VERBATIM
    )
    add_custom_target(${PACK_TGT} ALL DEPENDS ${PACK_OUT})
    add_dependencies(${PACK_TGT} Shaders)
    if(ANDROID)
        add_dependencies(${HP_GAME_TARGET} ${PACK_TGT})
    endif()
endfunction()
