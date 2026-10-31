#include "ShaderManifest.hpp"

namespace hbuild {

// 빌드 폴더의 generated/ 에 있습니다(CMakeLists.txt "셰이더 목록"). 셰이더를 다시 컴파일할 때마다 새로 만들어집니다.
const hpak::ShaderManifestEntry kShaderManifest[] = {
#include "haruka_shader_manifest.inc"
};
const size_t kShaderManifestCount = sizeof(kShaderManifest) / sizeof(kShaderManifest[0]);

} // namespace hbuild
