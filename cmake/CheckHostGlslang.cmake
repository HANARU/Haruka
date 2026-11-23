# Android 구성이 쓰는 데스크톱 glslang 의 판(버전)이 기대 판과 다르면 빌드를 멈춥니다(E0b2).
# 수정 시각 대신 `glslang --version` 출력을 비교하므로, CMakeLists.txt 를 고쳐도 같은 판의 glslang 이면 통과합니다.
#   cmake -DTOOL=<glslang 실행 파일> -DEXPECTED_VERSION=<16.4.0> -DSTAMP=<표지 파일> -P CheckHostGlslang.cmake
execute_process(
    COMMAND "${TOOL}" --version
    OUTPUT_VARIABLE _ver_out
    ERROR_VARIABLE  _ver_err
    RESULT_VARIABLE _ver_rc
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
get_filename_component(_cfg_dir "${TOOL}" DIRECTORY)
get_filename_component(_cfg "${_cfg_dir}" NAME)
get_filename_component(_build_dir "${_cfg_dir}" DIRECTORY)

if(NOT _ver_rc EQUAL 0)
    message(FATAL_ERROR "Failed to run desktop glslang (${TOOL} --version, rc=${_ver_rc}): ${_ver_err}\n"
                        "Rebuild that tool first, for example: cmake --build ${_build_dir} --config ${_cfg} --target glslang-standalone\n"
                        "(or pass -DHARUKA_HOST_GLSLANG=<path to an up-to-date glslang> and reconfigure)")
endif()

set(_actual_ver "")
if(_ver_out MATCHES "Glslang Version: ([^\r\n]+)")
    set(_actual_ver "${CMAKE_MATCH_1}")
endif()

if(NOT _actual_ver MATCHES "${EXPECTED_VERSION}")
    message(FATAL_ERROR "The desktop glslang (${TOOL}) version is '${_actual_ver}', expected '${EXPECTED_VERSION}' (vulkan-sdk-1.4.357.0).\n"
                        "Rebuild that tool first, for example: cmake --build ${_build_dir} --config ${_cfg} --target glslang-standalone\n"
                        "(or pass -DHARUKA_HOST_GLSLANG=<path to an up-to-date glslang> and reconfigure)")
endif()

file(TOUCH "${STAMP}")
