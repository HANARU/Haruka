#pragma once
#include "MeshSource.hpp"
#include <string>
#include <vector>

// Binary cache for pre-processed GLTF mesh data.
// Eliminates repeated GLTF parsing on startup.
// Format: magic(u32) version(u32) srcMtime(u64) srcSize(u64) srcHash(u64) meshCount(u32) [mesh entries...]
// Valid when size matches and either mtime or the content hash matches (DEF-105).
// Cache files live in the cache directory keyed by a stable hash of the logical source path (2026-10-24).
// srcPath 는 논리 경로("assets/models/model.glb")이고 원본은 가상 파일 시스템(vfs/Vfs.hpp)으로 찾습니다. 원본이 묶음
// 안이면 목차의 내용 해시(XXH3-64)로 판정합니다.
// 캐시 폴더는 엔진이 부팅할 때 정합니다(2026-10-19): 에디터는 실행 폴더의 assets/cache, 게임 실행 파일은 쓰기 루트의
// Cache/models. 정하지 않으면 작업 폴더의 assets/cache 입니다(테스트).

namespace GassetCache {
    void                      setDirectory(const std::string& dir);
    const std::string&        directory();
    bool                      isValid(const std::string& srcPath);
    std::vector<MeshSourceData> load(const std::string& srcPath);
    void                      save(const std::string& srcPath,
                                   const std::vector<MeshSourceData>& meshes);
    std::string               cachePath(const std::string& srcPath);
}
