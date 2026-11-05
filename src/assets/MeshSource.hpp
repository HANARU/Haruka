#pragma once
#include <vector>
#include <string>
#include <cstdint>

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
};

struct MeshSourceVertex {
    float pos[3];
    float normal[3];
    float uv[2];
    float tangent[4];  // xyz=tangent direction, w=handedness (+1 or -1)
};

struct MeshSourceData {
    std::string                   name;
    bool                          isBlend = false;  // alphaMode == "BLEND"
    bool                          doubleSided = false;  // glTF material.doubleSided. 지금은 에셋 계층까지만 전달하고 렌더러는 아직 쓰지 않음
    bool                          alphaMask   = false;  // alphaMode == "MASK". 지금은 에셋 계층까지만 전달(컷아웃 미구현)
    float                         alphaCutoff = 0.5f;   // glTF material.alphaCutoff(MASK 일 때만 의미, 기본 0.5)
    std::vector<MeshSourceVertex> vertices;
    std::vector<uint32_t>         indices;
    TexSourceData albedo;
    TexSourceData normalMap;
    TexSourceData metalRough;
    TexSourceData ao;
    TexSourceData emissive;
};
