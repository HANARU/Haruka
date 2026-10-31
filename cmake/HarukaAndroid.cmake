# ── Android(Quest) 빌드 (2026-10-31, 패키징 3단계) ──────────────────────────────────────────────
# CMakeLists.txt 의 if(ANDROID) 갈래가 이 파일을 include 합니다. NDK 툴체인으로 구성합니다(Gradle 없음):
#   cmake -S . -B build/android -G Ninja -DCMAKE_TOOLCHAIN_FILE=<NDK>/build/cmake/android.toolchain.cmake
#         -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-32 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=RelWithDebInfo
# 결과는 libHarukaGame.so 와 libopenxr_loader.so 이고, Packaging/android/package_apk.py 가 APK 로 묶습니다.
# 묶음(pack/game.hpak)과 엔진에 넣는 셰이더 목록은 데스크톱 빌드의 HarukaPak 으로 만듭니다(2026-11-01, DEF-149). 그래서 데스크톱
# 트리에서 HarukaPak 을 먼저 빌드해 두어야 합니다(없으면 구성이 멈추고 방법을 알림). 묶음 형식 · 쓰기 규칙의 소스(src/vfs 의 PakFormat ·
# PakWriter · PathUtf8, src/tools)가 그 도구보다 새로우면 빌드가 멈춥니다(cmake/CheckHostPak.cmake). 그 도구를 다시 빌드하면 풀립니다.
# RelWithDebInfo 는 NDEBUG 를 정의해 검증 레이어를 켜지 않습니다(기기에는 레이어가 없음).
#
# OpenXR 로더: 데스크톱과 같은 OpenXR-SDK 릴리스를 공유 라이브러리로 빌드합니다(DYNAMIC_LOADER ON, Android 의 기본).
# Khronos hello_xr · Meta 예제와 같은 형태이고, 로더가 런타임 브로커를 JNI 로 찾습니다. API 는 C 라서 로더와 엔진이 각자
# libc++ 를 정적으로 가져도 됩니다(Khronos 의 Android AAR 도 c++_static 으로 빌드).

FetchContent_Declare(
    openxr
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG        release-1.1.63
)
set(DYNAMIC_LOADER ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(openxr)

list(APPEND RUNTIME_SOURCES
    src/platform/android/AndroidWindow.cpp
    src/xr/XrSystem.cpp
)
add_library(HarukaRuntime STATIC ${RUNTIME_SOURCES})
target_link_libraries(HarukaRuntime PUBLIC Vulkan::Vulkan android log glm::glm tinygltf Jolt openxr_loader HarukaVfs)
target_include_directories(HarukaRuntime PUBLIC ${stb_SOURCE_DIR} ${miniaudio_SOURCE_DIR})
# AndroidWindow.cpp 가 android_native_app_glue.h 를 씁니다.
set(HARUKA_APP_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)
target_include_directories(HarukaRuntime PUBLIC ${HARUKA_APP_GLUE_DIR})

# NativeActivity 의 진입점(ANativeActivity_onCreate)은 android_native_app_glue.c 에 있습니다. 정적 라이브러리로 링크하면 아무도
# 부르지 않는 기호라 링커가 버리므로, 오브젝트로 바로 넣습니다(hello_xr 과 같음).
add_library(haruka_app_glue OBJECT ${HARUKA_APP_GLUE_DIR}/android_native_app_glue.c)
target_include_directories(haruka_app_glue PUBLIC ${HARUKA_APP_GLUE_DIR})
target_compile_options(haruka_app_glue PRIVATE -Wno-unused-parameter)

add_library(HarukaGame SHARED src/platform/android/AndroidMain.cpp ${GAME_SOURCES} $<TARGET_OBJECTS:haruka_app_glue>)
target_link_libraries(HarukaGame PRIVATE HarukaRuntime)

# Jolt 의 기본 LTO 는 NDK 링커의 --fatal-warnings 와 겹치면 막힐 수 있어 끕니다(첫 실행 우선).
if(TARGET Jolt)
    set_target_properties(Jolt PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
endif()
