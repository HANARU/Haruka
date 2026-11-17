#pragma once
#include "../vfs/ShaderManifest.hpp"

#include <cstddef>

// 이 실행 파일을 빌드할 때 컴파일한 셰이더 목록(2026-11-01, DEF-149). 빌드가 HarukaPak shader-manifest 로 만든
// haruka_shader_manifest.inc 를 ShaderManifest.cpp 가 넣습니다. 엔진이 마운트 뒤 실제로 읽힐 셰이더와 비교합니다.
namespace hbuild {

extern const hpak::ShaderManifestEntry kShaderManifest[];
extern const size_t                    kShaderManifestCount;

} // namespace hbuild
