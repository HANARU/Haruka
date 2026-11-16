# ── Steam Audio (HRTF, 2026-11-06, Docs/Audio_Design.md 9-6) ─────────────────────────────────────────────────────
# HRTF 를 Steam Audio 4.8.1(Valve, Apache-2.0)의 바이노럴 효과로 냅니다. 소스 빌드는 의존성을 Python 스크립트로 받고 호스트 도구를
# 따로 빌드해야 해서, 공식 릴리스 ZIP(모든 플랫폼 합계 약 181 MB)을 받아 필요한 파일만 풉니다:
#   include/phonon.h 등   SteamAudioHrtf.cpp 가 함수 형 · 구조체를 씀(라이브러리는 링크하지 않고 실행 중에 엶)
#   phonon.dll            Windows x64(약 53 MB, Embree · IPP 등이 들어 있음). 실행 폴더 · 배포 ZIP 에 복사
#   libphonon.so          Android arm64-v8a(strip 뒤 약 7 MB). package_apk.py 가 APK 에 넣음
#   THIRDPARTY.md         Steam Audio 안에 든 서드파티의 라이선스(고지 파일에 넣음)
# ZIP 은 SHA-256 으로 확인하고, 직접 받은 경우 푼 뒤 지웁니다. 빌드 폴더마다 한 번 받으므로, 이미 받은 ZIP 이 있으면
# -DHARUKA_STEAM_AUDIO_ZIP=<경로> 로 넘겨 다시 받지 않게 할 수 있습니다(해시는 그래도 확인). 받고 싶지 않으면
# -DHARUKA_STEAM_AUDIO=OFF: 엔진은 그대로 빌드되고 HRTF 만 없습니다(3D 소리는 스테레오 패닝).
#
# 결과 변수: HARUKA_STEAM_AUDIO_FOUND, HARUKA_STEAM_AUDIO_INCLUDE, HARUKA_STEAM_AUDIO_LIBRARY, HARUKA_STEAM_AUDIO_THIRDPARTY

option(HARUKA_STEAM_AUDIO "HRTF through Steam Audio (downloads the 181 MB release ZIP once per build folder)" ON)
set(HARUKA_STEAM_AUDIO_ZIP "" CACHE FILEPATH "steamaudio_4.8.1.zip downloaded beforehand (skips the download; the hash is still checked)")
set(HARUKA_STEAM_AUDIO_FOUND OFF)

if(HARUKA_STEAM_AUDIO)
    set(_sa_version 4.8.1)
    set(_sa_sha256  4a0aa5ec1176f38f0b0993a37c2259d9e86f27e22d5e24f83ec4c3cb9a1d5449)
    set(_sa_url     https://github.com/ValveSoftware/steam-audio/releases/download/v${_sa_version}/steamaudio_${_sa_version}.zip)
    set(_sa_dir     ${CMAKE_BINARY_DIR}/_deps/steamaudio-${_sa_version})

    set(_sa_lib_rel "")
    if(ANDROID)
        if(ANDROID_ABI STREQUAL "arm64-v8a")
            set(_sa_lib_rel steamaudio/lib/android-armv8/libphonon.so)
        endif()
    elseif(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_sa_lib_rel steamaudio/lib/windows-x64/phonon.dll)
    endif()

    if(NOT _sa_lib_rel)
        message(WARNING "Steam Audio: no library for this platform in the Haruka build rules; HRTF is disabled")
    else()
        if(NOT EXISTS ${_sa_dir}/${_sa_lib_rel} OR NOT EXISTS ${_sa_dir}/steamaudio/include/phonon.h
           OR NOT EXISTS ${_sa_dir}/steamaudio/THIRDPARTY.md)
            set(_sa_downloaded OFF)
            if(HARUKA_STEAM_AUDIO_ZIP)
                set(_sa_zip ${HARUKA_STEAM_AUDIO_ZIP})
                if(NOT EXISTS ${_sa_zip})
                    message(FATAL_ERROR "HARUKA_STEAM_AUDIO_ZIP does not exist: ${_sa_zip}")
                endif()
            else()
                set(_sa_zip ${_sa_dir}/steamaudio_${_sa_version}.zip)
                message(STATUS "Steam Audio ${_sa_version}: downloading the release ZIP (181 MB, once per build folder)")
                file(DOWNLOAD ${_sa_url} ${_sa_zip} STATUS _sa_status TLS_VERIFY ON)
                list(GET _sa_status 0 _sa_code)
                if(NOT _sa_code EQUAL 0)
                    file(REMOVE ${_sa_zip})
                    list(GET _sa_status 1 _sa_msg)
                    message(FATAL_ERROR "Steam Audio download failed (${_sa_msg}). Pass -DHARUKA_STEAM_AUDIO_ZIP=<zip> or "
                                        "build without HRTF: -DHARUKA_STEAM_AUDIO=OFF")
                endif()
                set(_sa_downloaded ON)
            endif()
            file(SHA256 ${_sa_zip} _sa_hash)
            if(NOT _sa_hash STREQUAL _sa_sha256)
                if(_sa_downloaded)
                    file(REMOVE ${_sa_zip})
                endif()
                message(FATAL_ERROR "Steam Audio ZIP hash mismatch: ${_sa_hash} (expected ${_sa_sha256}): ${_sa_zip}")
            endif()
            file(ARCHIVE_EXTRACT INPUT ${_sa_zip} DESTINATION ${_sa_dir}
                 PATTERNS "steamaudio/include/*" steamaudio/THIRDPARTY.md ${_sa_lib_rel})
            if(_sa_downloaded)
                file(REMOVE ${_sa_zip})   # 필요한 파일은 풀었으므로 181 MB 를 빌드 폴더에 남기지 않습니다
            endif()
            if(NOT EXISTS ${_sa_dir}/${_sa_lib_rel} OR NOT EXISTS ${_sa_dir}/steamaudio/include/phonon.h)
                message(FATAL_ERROR "Steam Audio ZIP did not contain ${_sa_lib_rel} / include/phonon.h")
            endif()
            message(STATUS "Steam Audio ${_sa_version}: extracted to ${_sa_dir}")
        endif()
        set(HARUKA_STEAM_AUDIO_INCLUDE    ${_sa_dir}/steamaudio/include)
        set(HARUKA_STEAM_AUDIO_LIBRARY    ${_sa_dir}/${_sa_lib_rel})
        set(HARUKA_STEAM_AUDIO_THIRDPARTY ${_sa_dir}/steamaudio/THIRDPARTY.md)
        set(HARUKA_STEAM_AUDIO_FOUND ON)
    endif()
endif()

# SteamAudioHrtf.cpp 만 phonon.h 를 씁니다. 같은 파일을 엔진 라이브러리와 오디오 시험이 따로 컴파일하므로 소스 속성으로 둡니다.
# 끄면 그 파일은 "Steam Audio 없이 빌드됨" 을 돌려주는 대체 코드로 컴파일됩니다.
if(HARUKA_STEAM_AUDIO_FOUND)
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/audio/SteamAudioHrtf.cpp PROPERTIES
        COMPILE_DEFINITIONS HARUKA_HAS_STEAM_AUDIO=1
        INCLUDE_DIRECTORIES ${HARUKA_STEAM_AUDIO_INCLUDE})
endif()
