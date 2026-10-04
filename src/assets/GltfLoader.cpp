#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"

#include "GltfLoader.hpp"
#include "AssetLog.hpp"
#include <stdexcept>
#include <numeric>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <set>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

// ─── accessor helpers ────────────────────────────────────────────────────────

static float readF(const uint8_t* base, size_t idx, int stride, int comp) {
    return reinterpret_cast<const float*>(base + idx * stride)[comp];
}

static std::tuple<const uint8_t*, int, size_t>
accessorInfo(const tinygltf::Model& model, int accIdx) {
    const auto& acc  = model.accessors[accIdx];
    const auto& bv   = model.bufferViews[acc.bufferView];
    const auto& buf  = model.buffers[bv.buffer];
    int cSz    = tinygltf::GetComponentSizeInBytes(acc.componentType);
    int nComp  = tinygltf::GetNumComponentsInType(acc.type);
    int stride = bv.byteStride > 0 ? (int)bv.byteStride : cSz * nComp;
    return { buf.data.data() + bv.byteOffset + acc.byteOffset, stride, acc.count };
}

static TexSourceData extractTexture(const tinygltf::Model& model, int texIdx) {
    TexSourceData out;
    if (texIdx < 0 || texIdx >= (int)model.textures.size()) return out;
    int imgIdx = model.textures[texIdx].source;
    if (imgIdx < 0 || imgIdx >= (int)model.images.size()) return out;
    const auto& img = model.images[imgIdx];
    if (img.image.empty()) return out;
    out.data    = img.image;
    out.w       = img.width;
    out.h       = img.height;
    out.hasData = true;
    return out;
}

// ─── glTF 머티리얼 팩터 적용 ─────────────────────────────────────────────────
// glTF는 색을 텍스처와 스칼라 팩터 두 가지로 표현하고, 최종값은 둘의 곱이다.
// 텍스처 없이 팩터만 지정한 머티리얼이 매우 흔한데, 예전에는 팩터를 아예 읽지 않아
// 그런 머티리얼이 전부 1x1 흰색 폴백으로 렌더링되었다. (DEF-47)
//
// 셰이더나 디스크립터를 바꾸지 않고 로드 시점에 픽셀로 구워 넣는다. 팩터는 머티리얼당
// 상수이므로 이 방식이 수학적으로 정확하다.
//
// 색 공간 주의: albedo와 emissive는 VK_FORMAT_R8G8B8A8_SRGB로 업로드되므로 저장 바이트가
// sRGB 인코딩 상태여야 한다. 반면 팩터는 선형이다. 따라서 디코드 → 곱 → 인코드 순서로
// 처리한다. 알파는 어느 쪽에서도 sRGB가 아니므로 그대로 곱한다.

static float srgbToLinear(float c) {
    return (c <= 0.04045f) ? (c / 12.92f)
                           : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
static float linearToSrgb(float c) {
    return (c <= 0.0031308f) ? (c * 12.92f)
                             : (1.055f * std::pow(c, 1.f / 2.4f) - 0.055f);
}
static uint8_t toByte(float v) {
    return (uint8_t)std::lround(std::clamp(v, 0.f, 1.f) * 255.f);
}

// RGBA 선형 팩터를 텍스처에 곱한다. 텍스처가 없으면 팩터만으로 1x1을 만든다.
// srgbEncoded=true 면 저장 바이트가 sRGB이므로 디코드/인코드를 거친다.
static void applyColorFactor(TexSourceData& tex, const std::vector<double>& f,
                             bool srgbEncoded) {
    float fr = f.size() > 0 ? (float)f[0] : 1.f;
    float fg = f.size() > 1 ? (float)f[1] : 1.f;
    float fb = f.size() > 2 ? (float)f[2] : 1.f;
    float fa = f.size() > 3 ? (float)f[3] : 1.f;

    if (!tex.hasData) {
        // 팩터만으로 1x1 텍스처를 만든다. 항등 팩터라도 만들어 두면 GpuScene의
        // 폴백 경로와 결과가 같으므로 분기를 늘리지 않는다.
        tex.w = tex.h = 1;
        tex.data.resize(4);
        tex.data[0] = toByte(srgbEncoded ? linearToSrgb(fr) : fr);
        tex.data[1] = toByte(srgbEncoded ? linearToSrgb(fg) : fg);
        tex.data[2] = toByte(srgbEncoded ? linearToSrgb(fb) : fb);
        tex.data[3] = toByte(fa);
        tex.hasData = true;
        return;
    }

    // 항등 팩터면 픽셀을 훑을 필요가 없다.
    if (fr == 1.f && fg == 1.f && fb == 1.f && fa == 1.f) return;

    for (size_t i = 0; i + 3 < tex.data.size(); i += 4) {
        if (srgbEncoded) {
            float r = srgbToLinear(tex.data[i + 0] / 255.f) * fr;
            float g = srgbToLinear(tex.data[i + 1] / 255.f) * fg;
            float b = srgbToLinear(tex.data[i + 2] / 255.f) * fb;
            tex.data[i + 0] = toByte(linearToSrgb(r));
            tex.data[i + 1] = toByte(linearToSrgb(g));
            tex.data[i + 2] = toByte(linearToSrgb(b));
        } else {
            tex.data[i + 0] = toByte(tex.data[i + 0] / 255.f * fr);
            tex.data[i + 1] = toByte(tex.data[i + 1] / 255.f * fg);
            tex.data[i + 2] = toByte(tex.data[i + 2] / 255.f * fb);
        }
        tex.data[i + 3] = toByte(tex.data[i + 3] / 255.f * fa);
    }
}

// metallicRoughness 텍스처는 선형(UNORM)이고 glTF 규약상 B=metallic, G=roughness 이다.
static void applyMetalRoughFactor(TexSourceData& tex, float metallic, float roughness) {
    if (!tex.hasData) {
        tex.w = tex.h = 1;
        tex.data.assign(4, 0);
        tex.data[1] = toByte(roughness);   // G
        tex.data[2] = toByte(metallic);    // B
        tex.data[3] = 255;
        tex.hasData = true;
        return;
    }
    if (metallic == 1.f && roughness == 1.f) return;
    for (size_t i = 0; i + 3 < tex.data.size(); i += 4) {
        tex.data[i + 1] = toByte(tex.data[i + 1] / 255.f * roughness);
        tex.data[i + 2] = toByte(tex.data[i + 2] / 255.f * metallic);
    }
}

// ─── tangent generation ──────────────────────────────────────────────────────
// Per-triangle TB accumulation → Gram-Schmidt orthogonalization per vertex.
// Used when the GLTF file does not supply a TANGENT attribute.

static void generateTangents(std::vector<MeshSourceVertex>& verts,
                              const std::vector<uint32_t>& indices) {
    std::vector<glm::vec3> tanAcc(verts.size(), glm::vec3(0));
    std::vector<glm::vec3> bitAcc(verts.size(), glm::vec3(0));

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];

        glm::vec3 p0(verts[i0].pos[0], verts[i0].pos[1], verts[i0].pos[2]);
        glm::vec3 p1(verts[i1].pos[0], verts[i1].pos[1], verts[i1].pos[2]);
        glm::vec3 p2(verts[i2].pos[0], verts[i2].pos[1], verts[i2].pos[2]);
        glm::vec2 u0(verts[i0].uv[0], verts[i0].uv[1]);
        glm::vec2 u1(verts[i1].uv[0], verts[i1].uv[1]);
        glm::vec2 u2(verts[i2].uv[0], verts[i2].uv[1]);

        glm::vec3 e1 = p1 - p0, e2 = p2 - p0;
        glm::vec2 d1 = u1 - u0, d2 = u2 - u0;
        float det = d1.x * d2.y - d2.x * d1.y;
        if (fabsf(det) < 1e-8f) continue;

        float r = 1.f / det;
        glm::vec3 T = (d2.y * e1 - d1.y * e2) * r;
        glm::vec3 B = (d1.x * e2 - d2.x * e1) * r;

        tanAcc[i0] += T; tanAcc[i1] += T; tanAcc[i2] += T;
        bitAcc[i0] += B; bitAcc[i1] += B; bitAcc[i2] += B;
    }

    for (size_t i = 0; i < verts.size(); ++i) {
        glm::vec3 N(verts[i].normal[0], verts[i].normal[1], verts[i].normal[2]);
        glm::vec3 T = tanAcc[i];

        if (glm::length(T) < 1e-6f) T = glm::vec3(1, 0, 0);
        glm::vec3 Torth = glm::normalize(T - N * glm::dot(N, T));
        if (glm::length(Torth) < 0.5f) Torth = glm::vec3(1, 0, 0);

        float hand = (glm::dot(glm::cross(N, T), bitAcc[i]) < 0.f) ? -1.f : 1.f;
        verts[i].tangent[0] = Torth.x;
        verts[i].tangent[1] = Torth.y;
        verts[i].tangent[2] = Torth.z;
        verts[i].tangent[3] = hand;
    }
}

// ─── node hierarchy ──────────────────────────────────────────────────────────

struct NodeRef { glm::mat4 world; int meshIdx; };

static glm::mat4 nodeLocalMatrix(const tinygltf::Node& n) {
    if (!n.matrix.empty()) {
        glm::mat4 m(1.f);
        // GLTF matrix is 16 doubles, column-major — same layout as GLM value_ptr
        for (int i = 0; i < 16; ++i) glm::value_ptr(m)[i] = (float)n.matrix[i];
        return m;
    }
    glm::mat4 T(1.f), R(1.f), S(1.f);
    if (!n.translation.empty())
        T = glm::translate(glm::mat4(1.f),
                glm::vec3((float)n.translation[0],
                          (float)n.translation[1],
                          (float)n.translation[2]));
    if (!n.rotation.empty()) {
        // GLTF quat = [x, y, z, w]; GLM quat ctor = (w, x, y, z)
        glm::quat q((float)n.rotation[3], (float)n.rotation[0],
                    (float)n.rotation[1], (float)n.rotation[2]);
        R = glm::mat4_cast(q);
    }
    if (!n.scale.empty())
        S = glm::scale(glm::mat4(1.f),
                glm::vec3((float)n.scale[0],
                          (float)n.scale[1],
                          (float)n.scale[2]));
    return T * R * S;
}

static void flattenNodes(const tinygltf::Model& model, int nodeIdx,
                          const glm::mat4& parent, std::vector<NodeRef>& out) {
    const auto& n = model.nodes[nodeIdx];
    glm::mat4   w = parent * nodeLocalMatrix(n);
    if (n.mesh >= 0) out.push_back({ w, n.mesh });
    for (int c : n.children) flattenNodes(model, c, w, out);
}

// ─── loadAll ─────────────────────────────────────────────────────────────────

std::vector<MeshSourceData> GltfLoader::loadAll(const std::string& path) {
    tinygltf::Model    model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ok = (path.size() >= 4 && path.substr(path.size() - 4) == ".glb")
        ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
        : loader.LoadASCIIFromFile(&model, &err, &warn, path);

    if (!warn.empty()) HLOG(LogAsset, Warning) << "glTF warning: " << warn;
    if (!ok)           throw std::runtime_error("glTF load failed: " + err);
    if (model.meshes.empty())
        throw std::runtime_error("glTF has no meshes: " + path);

    // Collect root nodes
    std::vector<int> roots;
    if (model.defaultScene >= 0 && model.defaultScene < (int)model.scenes.size()) {
        roots = model.scenes[model.defaultScene].nodes;
    } else if (!model.scenes.empty()) {
        roots = model.scenes[0].nodes;
    } else {
        std::set<int> children;
        for (const auto& n : model.nodes)
            for (int c : n.children) children.insert(c);
        for (int i = 0; i < (int)model.nodes.size(); ++i)
            if (!children.count(i)) roots.push_back(i);
    }

    // Flatten node → mesh references with accumulated world transforms
    std::vector<NodeRef> refs;
    for (int r : roots) flattenNodes(model, r, glm::mat4(1.f), refs);

    // Fallback: if no scene hierarchy, use all meshes with identity
    if (refs.empty()) {
        for (int i = 0; i < (int)model.meshes.size(); ++i)
            refs.push_back({ glm::mat4(1.f), i });
    }

    std::vector<MeshSourceData> result;

    for (const auto& [worldMat, meshIdx] : refs) {
        const auto& mesh = model.meshes[meshIdx];
        // Inverse-transpose for transforming normals and tangents under non-uniform scale
        glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(worldMat)));

        for (const auto& prim : mesh.primitives) {
            if (!prim.attributes.count("POSITION")) continue;

            MeshSourceData d;
            d.name = mesh.name.empty()
                ? ("mesh" + std::to_string(meshIdx)) : mesh.name;

            // ── Positions (with baked world transform) ─────────────────────
            auto [posBase, posStride, posCount] =
                accessorInfo(model, prim.attributes.at("POSITION"));
            d.vertices.resize(posCount);

            for (size_t i = 0; i < posCount; ++i) {
                glm::vec4 lp(readF(posBase, i, posStride, 0),
                             readF(posBase, i, posStride, 1),
                             readF(posBase, i, posStride, 2), 1.f);
                glm::vec3 wp = glm::vec3(worldMat * lp);
                d.vertices[i].pos[0] = wp.x;
                d.vertices[i].pos[1] = wp.y;
                d.vertices[i].pos[2] = wp.z;
                // Defaults: up normal, +X tangent, +1 handedness
                d.vertices[i].normal[0] = 0.f; d.vertices[i].normal[1] = 1.f; d.vertices[i].normal[2] = 0.f;
                d.vertices[i].tangent[0] = 1.f; d.vertices[i].tangent[3] = 1.f;
            }

            // ── Normals ────────────────────────────────────────────────────
            if (prim.attributes.count("NORMAL")) {
                auto [base, stride, _] = accessorInfo(model, prim.attributes.at("NORMAL"));
                for (size_t i = 0; i < posCount; ++i) {
                    glm::vec3 ln(readF(base, i, stride, 0),
                                 readF(base, i, stride, 1),
                                 readF(base, i, stride, 2));
                    glm::vec3 wn = glm::normalize(normalMat * ln);
                    d.vertices[i].normal[0] = wn.x;
                    d.vertices[i].normal[1] = wn.y;
                    d.vertices[i].normal[2] = wn.z;
                }
            }

            // ── UVs ────────────────────────────────────────────────────────
            if (prim.attributes.count("TEXCOORD_0")) {
                auto [base, stride, _] = accessorInfo(model, prim.attributes.at("TEXCOORD_0"));
                for (size_t i = 0; i < posCount; ++i) {
                    d.vertices[i].uv[0] = readF(base, i, stride, 0);
                    d.vertices[i].uv[1] = readF(base, i, stride, 1);
                }
            }

            // ── Tangents ───────────────────────────────────────────────────
            bool hasTangent = prim.attributes.count("TANGENT") > 0;
            if (hasTangent) {
                auto [base, stride, _] = accessorInfo(model, prim.attributes.at("TANGENT"));
                for (size_t i = 0; i < posCount; ++i) {
                    glm::vec3 lt(readF(base, i, stride, 0),
                                 readF(base, i, stride, 1),
                                 readF(base, i, stride, 2));
                    glm::vec3 wt = glm::normalize(normalMat * lt);
                    d.vertices[i].tangent[0] = wt.x;
                    d.vertices[i].tangent[1] = wt.y;
                    d.vertices[i].tangent[2] = wt.z;
                    d.vertices[i].tangent[3] = readF(base, i, stride, 3); // handedness unchanged
                }
            }

            // ── Indices ────────────────────────────────────────────────────
            if (prim.indices >= 0) {
                const auto& acc  = model.accessors[prim.indices];
                const auto& bv   = model.bufferViews[acc.bufferView];
                const auto& buf  = model.buffers[bv.buffer];
                const uint8_t* base = buf.data.data() + bv.byteOffset + acc.byteOffset;
                d.indices.resize(acc.count);
                for (size_t i = 0; i < acc.count; ++i) {
                    switch (acc.componentType) {
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                            d.indices[i] = reinterpret_cast<const uint32_t*>(base)[i]; break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                            d.indices[i] = reinterpret_cast<const uint16_t*>(base)[i]; break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                            d.indices[i] = base[i]; break;
                        default: d.indices[i] = (uint32_t)i; break;
                    }
                }
            } else {
                d.indices.resize(posCount);
                std::iota(d.indices.begin(), d.indices.end(), 0u);
            }

            // Generate tangents from positions/UVs when not supplied by file
            if (!hasTangent) generateTangents(d.vertices, d.indices);

            // ── Material ───────────────────────────────────────────────────
            int matIdx = (prim.material >= 0 && prim.material < (int)model.materials.size())
                ? prim.material : (model.materials.empty() ? -1 : 0);
            if (matIdx >= 0) {
                const auto& mat = model.materials[matIdx];
                const auto& pbr = mat.pbrMetallicRoughness;
                d.albedo     = extractTexture(model, pbr.baseColorTexture.index);
                d.normalMap  = extractTexture(model, mat.normalTexture.index);
                d.metalRough = extractTexture(model, pbr.metallicRoughnessTexture.index);
                d.ao         = extractTexture(model, mat.occlusionTexture.index);
                d.emissive   = extractTexture(model, mat.emissiveTexture.index);
                d.isBlend    = (mat.alphaMode == "BLEND");

                // 스칼라 팩터를 픽셀에 적용한다. 텍스처가 없으면 팩터만으로 1x1을 만든다.
                // tinygltf가 파일에 값이 없을 때 glTF 규약 기본값을 채워 주므로 그대로 쓴다
                // (baseColor 1,1,1,1 / metallic 1 / roughness 1 / emissive 0,0,0).
                // 팩터를 적용하기 전에 파일이 실제로 무엇을 지정했는지 남긴다.
                // 텍스처 유무와 팩터 값을 함께 봐야 화면이 왜 그렇게 보이는지 판단할 수 있다.
                // (applyColorFactor 가 hasData 를 true 로 바꾸므로 반드시 그 전에 읽는다.)
                HLOG(LogAsset, Verbose) << "mat \"" << mat.name << "\""
                                        << "  baseColor(" << pbr.baseColorFactor[0] << ", "
                                        << pbr.baseColorFactor[1] << ", " << pbr.baseColorFactor[2]
                                        << ", " << pbr.baseColorFactor[3] << ")"
                                        << "  metallic=" << pbr.metallicFactor
                                        << "  roughness=" << pbr.roughnessFactor
                                        << "  tex[albedo=" << (d.albedo.hasData ? "Y" : "-")
                                        << " normal="      << (d.normalMap.hasData ? "Y" : "-")
                                        << " mr="          << (d.metalRough.hasData ? "Y" : "-")
                                        << " ao="          << (d.ao.hasData ? "Y" : "-")
                                        << " emis="        << (d.emissive.hasData ? "Y" : "-") << "]";

                // 머티리얼이 사실상 비어 있는지 판정한다. 텍스처가 하나도 없고 모든 팩터가
                // glTF 기본값이면, 제작 도구에서 머티리얼을 만들기만 하고 아무 값도 지정하지
                // 않은 상태다. Blender 기본 내보내기에서 흔히 나온다.
                //
                // 엔진은 이 경우에도 **파일이 말한 대로 그린다.** 규약 기본값이
                // metallic=1, roughness=1 이므로 완전 금속에 완전 거침이 되고, pbr.frag 에서
                // kD = (1-F)*(1-metallic) 이 0이 되어 확산광이 사라진다. IBL 반사도 가장 흐린
                // 밉을 쓰므로 방향성이 거의 남지 않아 음영 없는 평평한 표면으로 보인다.
                //
                // 엔진이 값을 몰래 바꾸지 않는 이유는, 그렇게 하면 제대로 만든 에셋을 넣었을 때
                // 왜 다르게 보이는지 추적할 수 없게 되기 때문이다. 대신 원인을 로그로 알린다.
                const bool noTextures =
                    !d.albedo.hasData && !d.normalMap.hasData && !d.metalRough.hasData &&
                    !d.ao.hasData && !d.emissive.hasData;
                const bool defaultFactors =
                    pbr.baseColorFactor.size() >= 4 &&
                    pbr.baseColorFactor[0] == 1.0 && pbr.baseColorFactor[1] == 1.0 &&
                    pbr.baseColorFactor[2] == 1.0 && pbr.baseColorFactor[3] == 1.0 &&
                    pbr.metallicFactor == 1.0 && pbr.roughnessFactor == 1.0 &&
                    (mat.emissiveFactor.size() < 3 ||
                     (mat.emissiveFactor[0] == 0.0 && mat.emissiveFactor[1] == 0.0 &&
                      mat.emissiveFactor[2] == 0.0));
                if (noTextures && defaultFactors) {
                    HLOG(LogAsset, Warning)
                        << "material \"" << mat.name
                        << "\" has no textures and every factor is at the glTF default.\n"
                        << "  Spec defaults are metallic=1, roughness=1, so this\n"
                        << "  renders as a fully metallic, fully rough surface:\n"
                        << "  diffuse becomes zero and the result looks flat with\n"
                        << "  no directional shading. The engine renders what the\n"
                        << "  file says and does not substitute its own defaults.\n"
                        << "  Set the material in the DCC tool before exporting.";
                }

                applyColorFactor(d.albedo, pbr.baseColorFactor, /*srgbEncoded=*/true);
                applyMetalRoughFactor(d.metalRough,
                                      (float)pbr.metallicFactor,
                                      (float)pbr.roughnessFactor);
                applyColorFactor(d.emissive, mat.emissiveFactor, /*srgbEncoded=*/true);
            }

            result.push_back(std::move(d));
        }
    }

    if (result.empty())
        throw std::runtime_error("glTF has no valid primitives: " + path);

    size_t totalV = 0, totalT = 0;
    for (const auto& r : result) { totalV += r.vertices.size(); totalT += r.indices.size() / 3; }
    HLOG(LogAsset, Log) << "Loaded: " << path
                        << "  meshes=" << result.size()
                        << "  verts=" << totalV << "  tris=" << totalT;
    return result;
}

MeshSourceData GltfLoader::load(const std::string& path) {
    try {
        auto all = loadAll(path);
        if (!all.empty()) return std::move(all[0]);
    } catch (const std::exception& e) {
        HLOG(LogAsset, Error) << "Load failed (" << path << "): " << e.what() << " -> defaultCube";
    }
    return defaultCube();
}

// ─── defaultCube ─────────────────────────────────────────────────────────────

MeshSourceData GltfLoader::defaultCube() {
    MeshSourceData d;
    d.name = "default_cube";
    // clang-format off
    d.vertices = {
        // front (+z): N=(0,0,1), T=(1,0,0)
        {{-0.5f,-0.5f, 0.5f},{0,0,1},{0,1},{1,0,0,-1}}, {{ 0.5f,-0.5f, 0.5f},{0,0,1},{1,1},{1,0,0,-1}},
        {{ 0.5f, 0.5f, 0.5f},{0,0,1},{1,0},{1,0,0,-1}}, {{-0.5f, 0.5f, 0.5f},{0,0,1},{0,0},{1,0,0,-1}},
        // back (-z): N=(0,0,-1), T=(-1,0,0)
        {{ 0.5f,-0.5f,-0.5f},{0,0,-1},{0,1},{-1,0,0,-1}}, {{-0.5f,-0.5f,-0.5f},{0,0,-1},{1,1},{-1,0,0,-1}},
        {{-0.5f, 0.5f,-0.5f},{0,0,-1},{1,0},{-1,0,0,-1}}, {{ 0.5f, 0.5f,-0.5f},{0,0,-1},{0,0},{-1,0,0,-1}},
        // right (+x): N=(1,0,0), T=(0,0,-1)
        {{ 0.5f,-0.5f, 0.5f},{1,0,0},{0,1},{0,0,-1,-1}}, {{ 0.5f,-0.5f,-0.5f},{1,0,0},{1,1},{0,0,-1,-1}},
        {{ 0.5f, 0.5f,-0.5f},{1,0,0},{1,0},{0,0,-1,-1}}, {{ 0.5f, 0.5f, 0.5f},{1,0,0},{0,0},{0,0,-1,-1}},
        // left (-x): N=(-1,0,0), T=(0,0,1)
        {{-0.5f,-0.5f,-0.5f},{-1,0,0},{0,1},{0,0,1,-1}}, {{-0.5f,-0.5f, 0.5f},{-1,0,0},{1,1},{0,0,1,-1}},
        {{-0.5f, 0.5f, 0.5f},{-1,0,0},{1,0},{0,0,1,-1}}, {{-0.5f, 0.5f,-0.5f},{-1,0,0},{0,0},{0,0,1,-1}},
        // top (+y): N=(0,1,0), T=(1,0,0)
        {{-0.5f, 0.5f, 0.5f},{0,1,0},{0,1},{1,0,0,-1}}, {{ 0.5f, 0.5f, 0.5f},{0,1,0},{1,1},{1,0,0,-1}},
        {{ 0.5f, 0.5f,-0.5f},{0,1,0},{1,0},{1,0,0,-1}}, {{-0.5f, 0.5f,-0.5f},{0,1,0},{0,0},{1,0,0,-1}},
        // bottom (-y): N=(0,-1,0), T=(1,0,0)
        {{-0.5f,-0.5f,-0.5f},{0,-1,0},{0,1},{1,0,0,-1}}, {{ 0.5f,-0.5f,-0.5f},{0,-1,0},{1,1},{1,0,0,-1}},
        {{ 0.5f,-0.5f, 0.5f},{0,-1,0},{1,0},{1,0,0,-1}}, {{-0.5f,-0.5f, 0.5f},{0,-1,0},{0,0},{1,0,0,-1}},
    };
    d.indices = {
         0, 1, 2,  0, 2, 3,   4, 5, 6,  4, 6, 7,
         8, 9,10,  8,10,11,  12,13,14, 12,14,15,
        16,17,18, 16,18,19,  20,21,22, 20,22,23,
    };
    // clang-format on

    constexpr int W = 256, H = 256;
    d.albedo.w = W; d.albedo.h = H; d.albedo.hasData = true;
    d.albedo.data.resize(W * H * 4);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            uint8_t c = (((x / 32) + (y / 32)) % 2 == 0) ? 230 : 60;
            uint8_t* p = d.albedo.data.data() + (y * W + x) * 4;
            p[0] = c; p[1] = c; p[2] = c; p[3] = 255;
        }

    HLOG(LogAsset, Log) << "Using built-in default cube";
    return d;
}
