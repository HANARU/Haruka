# Android 구성이 쓰는 데스크톱 HarukaPak 이 그 소스보다 오래되었으면 빌드를 멈춥니다(2026-11-01, DEF-149 독립 검토).
# Android 트리에는 HarukaPak 을 다시 빌드하는 규칙이 없어, 묶음 형식 · 쓰기 규칙을 고친 뒤 데스크톱을 다시 빌드하지 않으면 새 .so 가
# 옛 도구로 만든 묶음과 목록을 받았습니다.
#   cmake -DTOOL=<HarukaPak 실행 파일> -DSOURCES=<a|b|...> -DSTAMP=<표지 파일> -P CheckHostPak.cmake
string(REPLACE "|" ";" _sources "${SOURCES}")
set(_newer)
foreach(S IN LISTS _sources)
    # IS_NEWER_THAN 은 시각이 같아도 참이므로, 거짓이면 소스가 도구보다 확실히 새것입니다.
    if(NOT "${TOOL}" IS_NEWER_THAN "${S}")
        list(APPEND _newer "${S}")
    endif()
endforeach()
if(_newer)
    # 안내는 실제로 쓰는 도구의 빌드 폴더 · 구성에 맞춥니다(Visual Studio 트리의 <빌드>/<구성>/HarukaPak.exe). 예전에는 늘 Release 를
    # 안내해, Debug 도구로 캐시된 구성에서는 안내대로 해도 풀리지 않았습니다(2026-11-01 재검토).
    get_filename_component(_cfg_dir "${TOOL}" DIRECTORY)
    get_filename_component(_cfg "${_cfg_dir}" NAME)
    get_filename_component(_build_dir "${_cfg_dir}" DIRECTORY)
    list(JOIN _newer "\n  " _list)
    message(FATAL_ERROR "The desktop HarukaPak (${TOOL}) is older than its sources:\n  ${_list}\n"
                        "Rebuild that tool first, for example: cmake --build ${_build_dir} --config ${_cfg} --target HarukaPak\n"
                        "(or pass -DHARUKA_HOST_PAK=<path to an up-to-date HarukaPak> and reconfigure)")
endif()
file(TOUCH "${STAMP}")
