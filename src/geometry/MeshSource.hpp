#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

struct SkinSourceData;   // assets/Skin.hpp
namespace hktx2 { struct Image; }   // geometry/Ktx2.hpp

// ── 메시 소스 데이터 (CPU 측) ────────────────────────────────────────────────
// glTF 로더와 절차 도형 생성기가 공통으로 뱉는 중간 표현입니다.
// GPU 의존이 0이므로 디바이스 없이 단위 테스트할 수 있습니다.
//
// 예전 이름은 GltfMeshData / GltfVertex / GltfTexData 였습니다. 절차 생성기가
// 같은 구조체를 반환하므로 "Gltf" 라는 이름이 더는 사실과 맞지 않아 바꿨습니다.
// GassetCache 의 바이너리 포맷은 필드 구성이 그대로라 영향이 없습니다.

struct TexSourceData {
    std::vector<uint8_t> data;
    int  w       = 0;
    int  h       = 0;
    bool hasData = false;
    // 묶을 때 미리 압축한 같은 텍스처(KTX2, Docs/Texture_Compression.md D9). 워커가 붙이고, 장치가 형식을 지원하면 GpuScene 이
    // 이것을 올립니다(아니면 위 RGBA8 화소). .gasset 캐시에는 저장하지 않습니다.
    std::shared_ptr<const hktx2::Image> compressed;
};

struct MeshSourceVertex {
    float pos[3];
    float normal[3];
    float uv[2];
    float tangent[4];  // xyz=tangent direction, w=handedness (+1 or -1)
};

struct MeshLodSource {
    std::vector<uint32_t> indices;
    float                 screenSize = 0.f;
};

struct MeshSourceData {
    std::string                   name;
    bool                          isBlend = false;  // alphaMode == "BLEND"
    bool                          doubleSided = false;  // glTF material.doubleSided. 지금은 에셋 계층까지만 전달하고 렌더러는 아직 쓰지 않음
    bool                          alphaMask   = false;  // alphaMode == "MASK". 지금은 에셋 계층까지만 전달(컷아웃 미구현)
    float                         alphaCutoff = 0.5f;   // glTF material.alphaCutoff(MASK 일 때만 의미, 기본 0.5)
    std::vector<MeshSourceVertex> vertices;
    std::vector<uint32_t>         indices;
    std::vector<MeshLodSource>    lods;     // O13: 자동 생성 LOD (거친 단계 순, 비어 있을 수 있음)
    // 인덱스(본 · LOD)가 이미 정점 캐시 순서인지(2026-11-20, 비동기 로딩): 가져올 때 한 번 정렬해 캐시에 담고, 업로드는 건너뜁니다.
    // 예전에는 업로드(메인 스레드)마다 정렬해 큰 모델 하나에 수백 ms 가 걸렸습니다.
    bool                          cacheOrdered = false;
    TexSourceData albedo;
    TexSourceData normalMap;
    TexSourceData metalRough;
    TexSourceData ao;
    TexSourceData emissive;
    // 스킨 메시(2026-11-07)일 때만 있습니다. 그때 vertices 는 바인드 공간 그대로이고(노드 트랜스폼을 굽지 않음),
    // 관절 · 정점별 관절 번호와 무게가 여기 들어 있습니다. 프리미티브마다 따로 가집니다.
    std::shared_ptr<SkinSourceData> skin;
};
