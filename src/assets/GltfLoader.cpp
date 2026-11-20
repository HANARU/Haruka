#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"

#include "GltfLoader.hpp"
#include "AssetLog.hpp"
#include "Skin.hpp"
#include "Animation.hpp"
#include "../geometry/MeshLod.hpp"
#include "../geometry/MeshOptimize.hpp"
#include "../vfs/LogicalPath.hpp"
#include "../vfs/PakFormat.hpp"
#include "../vfs/PathUtf8.hpp"
#include "../vfs/Vfs.hpp"
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <numeric>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <set>
#include <unordered_map>
#include <chrono>
#include <cstdio>

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

struct NodeRef { glm::mat4 world; int meshIdx; int skin = -1; int node = -1; };

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
    if (n.mesh >= 0) out.push_back({ w, n.mesh, n.skin, nodeIdx });
    for (int c : n.children) flattenNodes(model, c, w, out);
}

// ─── 스킨 (2026-11-07, Docs/Hand_Grasp_Design.md 4-1) ────────────────────────
// glTF 규약: 스킨 메시의 정점은 바인드 공간에 있고, 스킨 메시 노드 자신의 트랜스폼은 무시합니다. 정점 = Σ 무게 x
// (관절 전역 행렬 x inverseBind) x 바인드 위치입니다. 관절 노드의 쉬는 자세 TRS 와 부모를 읽어 SkinSourceData 에 담고,
// 관절이 아닌 조상(Armature 노드 등)의 전역 행렬은 뿌리 관절의 rootParent 로 굳힙니다.

// 노드마다 부모(-1 = 없음)와 전역 행렬. 장면 밖 노드도 부모 사슬로 계산합니다. 순환이 있으면 그 노드는 단위 행렬입니다.
static void nodeGlobals(const tinygltf::Model& model, std::vector<glm::mat4>& global, std::vector<int>& parent) {
    const int n = (int)model.nodes.size();
    parent.assign(n, -1);
    for (int i = 0; i < n; ++i)
        for (int c : model.nodes[i].children)
            if (c >= 0 && c < n) parent[c] = i;
    global.assign(n, glm::mat4(1.f));
    std::vector<uint8_t> done(n, 0);
    for (int i = 0; i < n; ++i) {
        std::vector<int> chain;
        int j = i;
        while (j >= 0 && !done[j] && (int)chain.size() <= n) { chain.push_back(j); j = parent[j]; }
        glm::mat4 acc = (j >= 0 && done[j]) ? global[j] : glm::mat4(1.f);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            acc = acc * nodeLocalMatrix(model.nodes[*it]);
            global[*it] = acc;
            done[*it] = 1;
        }
    }
}

// 정수 · 정규화 정수 · 실수 성분 하나를 읽습니다(JOINTS_0 · WEIGHTS_0).
static float readComponent(const uint8_t* base, size_t idx, int stride, int comp, int componentType, bool normalized) {
    const uint8_t* p = base + idx * stride;
    switch (componentType) {
        case TINYGLTF_COMPONENT_TYPE_FLOAT:          return reinterpret_cast<const float*>(p)[comp];
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  { const float v = p[comp]; return normalized ? v / 255.f : v; }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: { uint16_t v; std::memcpy(&v, p + comp * 2, 2);
                                                       return normalized ? v / 65535.f : (float)v; }
        default: return 0.f;
    }
}

// ─── 애니메이션 클립 (2026-11-20, Docs/Animation_System.md A2) ─────────────────────
// 접근자 값 하나를 float 로 읽습니다. 회전 · 무게는 정규화 정수(i8 · u8 · i16 · u16)도 허용합니다(glTF 규약).
static bool readAnimFloat(const tinygltf::Accessor& acc, const uint8_t* p, int comp, float& out) {
    switch (acc.componentType) {
        case TINYGLTF_COMPONENT_TYPE_FLOAT:          { float v; std::memcpy(&v, p + comp * 4, 4); out = v; return true; }
        case TINYGLTF_COMPONENT_TYPE_BYTE:           { int8_t v; std::memcpy(&v, p + comp, 1); out = std::max(v / 127.f, -1.f); return acc.normalized; }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  { out = p[comp] / 255.f; return acc.normalized; }
        case TINYGLTF_COMPONENT_TYPE_SHORT:          { int16_t v; std::memcpy(&v, p + comp * 2, 2); out = std::max(v / 32767.f, -1.f); return acc.normalized; }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: { uint16_t v; std::memcpy(&v, p + comp * 2, 2); out = v / 65535.f; return acc.normalized; }
        default: return false;
    }
}

// 접근자 전체를 float 로 읽습니다(성분 nComp 개씩). 범위 · 형식이 틀리면 false.
static bool readAnimAccessor(const tinygltf::Model& model, int accIdx, int nComp, std::vector<float>& out) {
    if (accIdx < 0 || accIdx >= (int)model.accessors.size()) return false;
    const auto& acc = model.accessors[accIdx];
    if (tinygltf::GetNumComponentsInType(acc.type) != nComp) return false;
    if (acc.bufferView < 0 || acc.bufferView >= (int)model.bufferViews.size()) return false;
    const auto& bv = model.bufferViews[acc.bufferView];
    if (bv.buffer < 0 || bv.buffer >= (int)model.buffers.size()) return false;
    const auto& buf = model.buffers[bv.buffer];
    const int cSz = tinygltf::GetComponentSizeInBytes(acc.componentType);
    if (cSz <= 0) return false;
    const size_t stride = bv.byteStride > 0 ? bv.byteStride : (size_t)cSz * nComp;
    const size_t begin = bv.byteOffset + acc.byteOffset;
    if (acc.count && begin + (acc.count - 1) * stride + (size_t)cSz * nComp > buf.data.size()) return false;
    out.resize(acc.count * (size_t)nComp);
    for (size_t i = 0; i < acc.count; ++i)
        for (int c = 0; c < nComp; ++c)
            if (!readAnimFloat(acc, buf.data.data() + begin + i * stride, c, out[i * nComp + c])) return false;
    return true;
}

// nodeToJoint(glTF 노드 번호 → 관절 번호)의 관절을 움직이는 채널을 클립으로 모읍니다. 관절이 아닌 노드를 가리키는 채널은
// 건너뛰고 클립마다 한 번 알립니다. 깨진 클립은 그 클립만 버립니다. 무게 채널은 morphCount 가 0 이 아니고 이 메시가 붙은
// 노드(meshNode)를 가리킬 때만 받습니다.
static std::vector<hanim::Clip> readClips(const tinygltf::Model& model, const std::unordered_map<int, int>& nodeToJoint,
                                          int meshNode, int morphCount, int jointCount, const std::string& path) {
    std::vector<hanim::Clip> clips;
    for (size_t ai = 0; ai < model.animations.size(); ++ai) {
        const auto& anim = model.animations[ai];
        hanim::Clip clip;
        clip.name = anim.name.empty() ? ("Animation" + std::to_string(ai)) : anim.name;
        size_t skipped = 0;
        std::string bad;
        for (const auto& ch : anim.channels) {
            hanim::Track tr;
            if      (ch.target_path == "translation") { tr.path = hanim::Path::Translation; tr.comps = 3; }
            else if (ch.target_path == "rotation")    { tr.path = hanim::Path::Rotation;    tr.comps = 4; }
            else if (ch.target_path == "scale")       { tr.path = hanim::Path::Scale;       tr.comps = 3; }
            else if (ch.target_path == "weights")     { tr.path = hanim::Path::Weights;     tr.comps = (uint32_t)std::max(morphCount, 0); }
            else { ++skipped; continue; }
            if (tr.path == hanim::Path::Weights) {
                if (morphCount <= 0 || ch.target_node != meshNode) { ++skipped; continue; }
            } else {
                const auto it = nodeToJoint.find(ch.target_node);
                if (it == nodeToJoint.end()) { ++skipped; continue; }
                tr.target = it->second;
            }
            if (ch.sampler < 0 || ch.sampler >= (int)anim.samplers.size()) { bad = "channel sampler out of range"; break; }
            const auto& sm = anim.samplers[ch.sampler];
            if      (sm.interpolation == "STEP")        tr.interp = hanim::Interp::Step;
            else if (sm.interpolation == "CUBICSPLINE") tr.interp = hanim::Interp::CubicSpline;
            else                                        tr.interp = hanim::Interp::Linear;
            if (!readAnimAccessor(model, sm.input, 1, tr.times)) { bad = "sampler input is not a float SCALAR"; break; }
            // 무게는 SCALAR 접근자에 키마다 morphCount 개가 이어져 있습니다.
            const int accComps = tr.path == hanim::Path::Weights ? 1 : (int)tr.comps;
            if (!readAnimAccessor(model, sm.output, accComps, tr.values)) { bad = "sampler output has an unsupported type"; break; }
            clip.tracks.push_back(std::move(tr));
        }
        if (bad.empty() && clip.tracks.empty()) continue;   // 이 스킨과 관계없는 애니메이션
        if (bad.empty()) {
            clip.duration = hanim::computeDuration(clip);
            std::string err;
            if (!hanim::validate(clip, jointCount, morphCount, &err)) bad = err;
        }
        if (!bad.empty()) {
            HLOG(LogAsset, Warning) << path << ": animation '" << clip.name << "' skipped (" << bad << ")";
            continue;
        }
        if (skipped) {
            HLOG(LogAsset, Log) << path << ": animation '" << clip.name << "': " << skipped
                                << " channel(s) target nodes outside this skin and are ignored";
        }
        clips.push_back(std::move(clip));
    }
    return clips;
}

// ─── 모프 타깃 (2026-11-20, Docs/Animation_System.md A5 · D7) ─────────────────────
// prim.targets 의 POSITION · NORMAL 차이와 mesh.weights 를 out 에 채웁니다. 타깃이 없으면 true(모프 없음). 형식이 틀리면
// 경고하고 false(모프를 버림).
static bool readMorph(const tinygltf::Model& model, const tinygltf::Primitive& prim, size_t vertexCount,
                      const std::vector<double>& meshWeights, SkinSourceData& out, const std::string& path) {
    out.morphCount = 0;
    out.morphPos.clear();
    out.morphNrm.clear();
    out.defaultWeights.clear();
    if (prim.targets.empty()) return true;
    if (prim.targets.size() > 256) {
        HLOG(LogAsset, Warning) << path << ": " << prim.targets.size() << " morph targets (max 256); morphs ignored";
        return false;
    }
    const size_t tc = prim.targets.size();
    bool normals = true;
    for (const auto& t : prim.targets) normals = normals && t.count("NORMAL");
    out.morphPos.assign(tc * vertexCount * 3, 0.f);
    if (normals) out.morphNrm.assign(tc * vertexCount * 3, 0.f);
    std::vector<float> tmp;
    for (size_t t = 0; t < tc; ++t) {
        const auto& tgt = prim.targets[t];
        const auto pit = tgt.find("POSITION");
        if (pit != tgt.end()) {
            if (!readAnimAccessor(model, pit->second, 3, tmp) || tmp.size() != vertexCount * 3) {
                HLOG(LogAsset, Warning) << path << ": morph target " << t << " POSITION is not a VEC3 per vertex; morphs ignored";
                out.morphPos.clear(); out.morphNrm.clear();
                return false;
            }
            std::copy(tmp.begin(), tmp.end(), out.morphPos.begin() + (ptrdiff_t)(t * vertexCount * 3));
        }
        if (normals) {
            if (!readAnimAccessor(model, tgt.at("NORMAL"), 3, tmp) || tmp.size() != vertexCount * 3) {
                HLOG(LogAsset, Warning) << path << ": morph target " << t << " NORMAL is not a VEC3 per vertex; normal deltas ignored";
                out.morphNrm.clear();
                normals = false;
            } else {
                std::copy(tmp.begin(), tmp.end(), out.morphNrm.begin() + (ptrdiff_t)(t * vertexCount * 3));
            }
        }
    }
    out.morphCount = (uint32_t)tc;
    out.defaultWeights.assign(tc, 0.f);
    for (size_t t = 0; t < tc && t < meshWeights.size(); ++t) out.defaultWeights[t] = (float)meshWeights[t];
    return true;
}

// 스킨이 없는 모프 메시: 관절 하나(쉬는 자세 단위, rootParent = 노드 전역 행렬)짜리 스킨으로 만들어 스킨 경로로 그립니다(D7).
// 정점은 메시 로컬 공간 그대로이고 노드 행렬은 rootParent 가 곱합니다. 모프를 읽지 못하면 nullptr(보통 메시).
static std::shared_ptr<SkinSourceData> makeMorphSkin(const tinygltf::Model& model, const tinygltf::Primitive& prim,
                                                     size_t vertexCount, const std::vector<double>& meshWeights,
                                                     const glm::mat4& nodeWorld, int meshNode, const std::string& path) {
    auto out = std::make_shared<SkinSourceData>();
    if (!readMorph(model, prim, vertexCount, meshWeights, *out, path) || out->morphCount == 0) return nullptr;
    out->joints.resize(1);
    out->joints[0].name       = "root";
    out->joints[0].rootParent = nodeWorld;
    out->jointIndex.assign(vertexCount * hskin::kInfluences, 0);
    out->jointWeight.assign(vertexCount * hskin::kInfluences, 0.f);
    for (size_t i = 0; i < vertexCount; ++i) out->jointWeight[i * hskin::kInfluences] = 1.f;
    hskin::buildOrder(*out);
    out->clips = readClips(model, {}, meshNode, (int)out->morphCount, 1, path);
    return out;
}

static std::shared_ptr<SkinSourceData> readSkin(const tinygltf::Model& model, int skinIdx, const tinygltf::Primitive& prim,
                                                size_t vertexCount, const std::vector<glm::mat4>& global,
                                                const std::vector<int>& parent, int meshNode,
                                                const std::vector<double>& meshWeights, const std::string& path) {
    if (skinIdx < 0 || skinIdx >= (int)model.skins.size()) return nullptr;
    const auto& sk = model.skins[skinIdx];
    if (!prim.attributes.count("JOINTS_0") || !prim.attributes.count("WEIGHTS_0")) {
        HLOG(LogAsset, Warning) << path << ": skinned node has a primitive without JOINTS_0/WEIGHTS_0; drawn without skinning";
        return nullptr;
    }
    if (sk.joints.empty() || sk.joints.size() > 65535) {
        HLOG(LogAsset, Warning) << path << ": skin " << skinIdx << " has " << sk.joints.size() << " joints; drawn without skinning";
        return nullptr;
    }
    auto out = std::make_shared<SkinSourceData>();
    const int jc = (int)sk.joints.size();
    std::unordered_map<int, int> nodeToJoint;
    for (int j = 0; j < jc; ++j) nodeToJoint[sk.joints[j]] = j;
    out->joints.resize(jc);
    for (int j = 0; j < jc; ++j) {
        const int nj = sk.joints[j];
        if (nj < 0 || nj >= (int)model.nodes.size()) {
            HLOG(LogAsset, Warning) << path << ": skin joint node " << nj << " is out of range; drawn without skinning";
            return nullptr;
        }
        const auto& node = model.nodes[nj];
        SkinJoint& jt = out->joints[j];
        jt.name = node.name.empty() ? ("joint" + std::to_string(j)) : node.name;
        if (!node.matrix.empty()) {
            // 행렬로 준 관절: 이동 = 4열, 배율 = 각 열의 길이, 회전 = 배율을 나눈 3x3(전단 · 원근은 버림).
            const glm::mat4 m = nodeLocalMatrix(node);
            jt.restT = glm::vec3(m[3]);
            jt.restS = glm::vec3(glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])));
            const glm::mat3 r(glm::vec3(m[0]) / std::max(jt.restS.x, 1e-12f),
                              glm::vec3(m[1]) / std::max(jt.restS.y, 1e-12f),
                              glm::vec3(m[2]) / std::max(jt.restS.z, 1e-12f));
            jt.restR = glm::normalize(glm::quat_cast(r));
        } else {
            if (node.translation.size() == 3)
                jt.restT = glm::vec3((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]);
            if (node.rotation.size() == 4)
                jt.restR = glm::normalize(glm::quat((float)node.rotation[3], (float)node.rotation[0],
                                                    (float)node.rotation[1], (float)node.rotation[2]));
            if (node.scale.size() == 3)
                jt.restS = glm::vec3((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]);
        }
        const int pn = parent[nj];
        const auto it = pn >= 0 ? nodeToJoint.find(pn) : nodeToJoint.end();
        if (it != nodeToJoint.end()) jt.parent = it->second;
        else { jt.parent = -1; jt.rootParent = pn >= 0 ? global[pn] : glm::mat4(1.f); }
    }
    if (sk.inverseBindMatrices >= 0) {
        const auto& acc = model.accessors[sk.inverseBindMatrices];
        if (acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || acc.type != TINYGLTF_TYPE_MAT4 || (int)acc.count < jc) {
            HLOG(LogAsset, Warning) << path << ": skin " << skinIdx << " inverseBindMatrices is not " << jc
                                    << " float MAT4; drawn without skinning";
            return nullptr;
        }
        auto [base, stride, cnt] = accessorInfo(model, sk.inverseBindMatrices);
        (void)cnt;
        for (int j = 0; j < jc; ++j)
            std::memcpy(glm::value_ptr(out->joints[j].inverseBind), base + (size_t)j * stride, 64);
    }

    const auto& jAcc = model.accessors[prim.attributes.at("JOINTS_0")];
    const auto& wAcc = model.accessors[prim.attributes.at("WEIGHTS_0")];
    if (jAcc.type != TINYGLTF_TYPE_VEC4 || wAcc.type != TINYGLTF_TYPE_VEC4 ||
        jAcc.count < vertexCount || wAcc.count < vertexCount) {
        HLOG(LogAsset, Warning) << path << ": JOINTS_0/WEIGHTS_0 are not VEC4 per vertex; drawn without skinning";
        return nullptr;
    }
    auto [jBase, jStride, jCnt] = accessorInfo(model, prim.attributes.at("JOINTS_0"));
    auto [wBase, wStride, wCnt] = accessorInfo(model, prim.attributes.at("WEIGHTS_0"));
    (void)jCnt; (void)wCnt;
    out->jointIndex.assign(vertexCount * hskin::kInfluences, 0);
    out->jointWeight.assign(vertexCount * hskin::kInfluences, 0.f);
    // JOINTS_1 · WEIGHTS_1(영향 5 ~ 8)이 있으면 8개 중 무게가 큰 4개를 남기고 다시 정규화합니다(2026-11-20, 애니메이션 A2-4).
    const tinygltf::Accessor* j1Acc = nullptr;
    const tinygltf::Accessor* w1Acc = nullptr;
    const uint8_t* j1Base = nullptr;
    const uint8_t* w1Base = nullptr;
    int j1Stride = 0, w1Stride = 0;
    if (prim.attributes.count("JOINTS_1") && prim.attributes.count("WEIGHTS_1")) {
        const auto& ja = model.accessors[prim.attributes.at("JOINTS_1")];
        const auto& wa = model.accessors[prim.attributes.at("WEIGHTS_1")];
        if (ja.type == TINYGLTF_TYPE_VEC4 && wa.type == TINYGLTF_TYPE_VEC4 && ja.count >= vertexCount && wa.count >= vertexCount) {
            j1Acc = &ja;
            w1Acc = &wa;
            auto [b1, s1, c1] = accessorInfo(model, prim.attributes.at("JOINTS_1"));
            auto [b2, s2, c2] = accessorInfo(model, prim.attributes.at("WEIGHTS_1"));
            (void)c1; (void)c2;
            j1Base = b1; j1Stride = s1; w1Base = b2; w1Stride = s2;
        } else {
            HLOG(LogAsset, Warning) << path << ": JOINTS_1/WEIGHTS_1 are not VEC4 per vertex; only the first 4 influences are used";
        }
    }
    size_t badIndex = 0, noWeight = 0, trimmed = 0;
    for (size_t i = 0; i < vertexCount; ++i) {
        int   idx8[8] = {};
        float w8[8]   = {};
        const int count = j1Acc ? 8 : hskin::kInfluences;
        for (int k = 0; k < count; ++k) {
            const bool second = k >= hskin::kInfluences;
            const int  kk = second ? k - hskin::kInfluences : k;
            const float ji = second ? readComponent(j1Base, i, j1Stride, kk, j1Acc->componentType, false)
                                    : readComponent(jBase, i, jStride, kk, jAcc.componentType, false);
            float w = second ? readComponent(w1Base, i, w1Stride, kk, w1Acc->componentType, w1Acc->normalized)
                             : readComponent(wBase, i, wStride, kk, wAcc.componentType, wAcc.normalized);
            int idx = (int)ji;
            if (idx < 0 || idx >= jc) { if (w > 0.f) ++badIndex; idx = 0; w = 0.f; }
            if (!(w > 0.f)) w = 0.f;
            idx8[k] = idx;
            w8[k]   = w;
        }
        if (count > hskin::kInfluences) {
            // 무게가 큰 순서로 앞 4칸에 모읍니다(같으면 앞 번호 먼저).
            int order[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
            std::stable_sort(order, order + 8, [&](int a, int b) { return w8[a] > w8[b]; });
            if (w8[order[4]] > 0.f) ++trimmed;
            int ti[4];
            float tw[4];
            for (int k = 0; k < 4; ++k) { ti[k] = idx8[order[k]]; tw[k] = w8[order[k]]; }
            for (int k = 0; k < 4; ++k) { idx8[k] = ti[k]; w8[k] = tw[k]; }
        }
        float sum = 0.f;
        for (int k = 0; k < hskin::kInfluences; ++k) {
            out->jointIndex[i * hskin::kInfluences + k]  = (uint16_t)idx8[k];
            out->jointWeight[i * hskin::kInfluences + k] = w8[k];
            sum += w8[k];
        }
        if (sum > 0.f) for (int k = 0; k < hskin::kInfluences; ++k) out->jointWeight[i * hskin::kInfluences + k] /= sum;
        else ++noWeight;
    }
    if (trimmed) {
        HLOG(LogAsset, Log) << path << ": " << trimmed << " vertex(es) had more than 4 skin influences; kept the largest 4";
    }
    if (badIndex) {
        HLOG(LogAsset, Warning) << path << ": " << badIndex << " skin weight(s) point at a missing joint and were dropped";
    }
    if (noWeight) {
        HLOG(LogAsset, Warning) << path << ": " << noWeight << " vertex(es) have no skin weight and stay at the bind pose";
    }
    if (!hskin::buildOrder(*out)) {
        HLOG(LogAsset, Warning) << path << ": skin " << skinIdx << " joint hierarchy has a cycle; drawn without skinning";
        return nullptr;
    }
    readMorph(model, prim, vertexCount, meshWeights, *out, path);
    out->clips = readClips(model, nodeToJoint, meshNode, (int)out->morphCount, jc, path);
    return out;
}

// ─── 파일 읽기 (2026-10-24) ──────────────────────────────────────────────────
// glTF 는 가상 파일 시스템으로 읽습니다(에디터는 프로젝트 폴더, 게임은 묶음 파일). 본문은 메모리에서 파싱하고, .gltf 가
// 가리키는 외부 파일(.bin · 텍스처)도 이 콜백으로 같은 곳에서 찾습니다. tinygltf 는 기준 폴더와 uri 를 '/' 로 잇습니다.

// 이은 경로의 '.' · '..' 구성요소를 접습니다(DEF-125). 논리 경로는 '.' · '..' 를 받지 않으므로, uri 가 "../textures/a.png"
// 이면 "assets/models/../textures/a.png" 를 "assets/textures/a.png" 로 바꿔야 찾습니다. 맨 앞 위로 올라가는 '..'(프로젝트 ·
// 묶음 밖)는 빈 문자열로 거부합니다. 접은 결과는 논리 경로 규칙(hpak::normalizePath)을 통과해야 합니다. 그래야 tinygltf 가
// 함께 시도하는 "./" + uri 후보가 "./C:/..." 같은 드라이브 문자 uri 를 절대 경로로 바꿔 마운트를 우회해 디스크에서 읽지
// 못합니다(2026-10-31 검토). 그런 파일은 묶음에 들어가지 않아 다른 PC 에서만 빠집니다.
// 가상 경로("/Game/models/a.gltf")면 맨 앞 '/' 와 마운트 이름(Game)을 지킵니다. 예전에는 '/' 를 빈 구성요소로 버려 "Game/models/a.bin"
// 을 찾았고, 씬이 쓰는 정식 경로로 연 .gltf 는 바깥 .bin · 텍스처를 하나도 찾지 못했습니다(DEF-175). 마운트 이름 위로 올라가는
// '..'("/Game/../Config/...")는 다른 마운트로 넘어가므로 거부합니다.
static std::string foldLogicalPath(const std::string& p) {
    std::vector<std::string> parts;
    const bool rooted = !p.empty() && (p[0] == '/' || p[0] == '\\');
    size_t i = 0;
    for (;;) {
        const size_t j = p.find_first_of("/\\", i);
        const std::string part = p.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (part == "..") {
            if (parts.empty() || (rooted && parts.size() == 1)) return {};
            parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        if (j == std::string::npos) break;
        i = j + 1;
    }
    std::string out, norm;
    for (const auto& s : parts) { if (!out.empty()) out += '/'; out += s; }
    if (rooted) out.insert(out.begin(), '/');
    return hpak::normalizePath(out, norm) ? norm : std::string();
}

// 콜백이 아는 것: glTF 파일 자체가 절대 경로로 열렸는지. 그때만(시험의 임시 파일 등) 이은 경로를 운영체제에 그대로 맡깁니다.
// uri 가 절대 경로인지로 정하지 않습니다. 논리 경로의 glTF 가 가리키는 절대 uri 는 위 규칙으로 거부해야 하기 때문입니다.
struct GltfFsContext {
    bool absoluteBase = false;
    std::vector<std::string>* deps = nullptr;   // 읽은 바깥 파일(정식 논리 경로, 핫리로드 D4). null 이면 기록하지 않음
};

static tinygltf::FsCallbacks vfsCallbacks(const GltfFsContext* ctx) {
    tinygltf::FsCallbacks cb;
    cb.FileExists = [](const std::string& p, void*) { return hvfs::exists(p); };
    // tinygltf 는 이 결과로 FileExists · GetFileSizeInBytes · ReadWholeFile 을 모두 부르므로 접는 곳은 여기 하나입니다.
    cb.ExpandFilePath = [](const std::string& p, void* user) {
        const auto* c = static_cast<const GltfFsContext*>(user);
        return (c && c->absoluteBase) ? p : foldLogicalPath(p);
    };
    cb.ReadWholeFile = [](std::vector<unsigned char>* out, std::string* err, const std::string& p, void* user) {
        std::vector<uint8_t> bytes;
        std::string e;
        if (!hvfs::readAll(p, bytes, &e)) { if (err) *err += e; return false; }
        out->assign(bytes.begin(), bytes.end());
        const auto* c = static_cast<const GltfFsContext*>(user);
        if (c && c->deps) {
            std::string canon = hvfs::toCanonical(p);
            if (std::find(c->deps->begin(), c->deps->end(), canon) == c->deps->end()) c->deps->push_back(std::move(canon));
        }
        return true;
    };
    cb.WriteWholeFile = [](std::string* err, const std::string& p, const std::vector<unsigned char>&, void*) {
        if (err) *err += "glTF loader does not write files (" + p + ")";
        return false;
    };
    cb.GetFileSizeInBytes = [](size_t* size, std::string* err, const std::string& p, void*) {
        hvfs::Stat st;
        if (!hvfs::stat(p, st)) { if (err) *err += p + ": not found"; return false; }
        *size = (size_t)st.size;
        return true;
    };
    cb.user_data = const_cast<GltfFsContext*>(ctx);
    return cb;
}

// ─── loadAll ─────────────────────────────────────────────────────────────────

std::vector<MeshSourceData> GltfLoader::loadAll(const std::string& path) {
    return loadAll(path, ImportSettings{}, nullptr);
}

// albedo · emissive 이미지가 선형 값으로 저장된 파일(가져오기 설정 srgb = false, 핫리로드 D8): GPU 형식은 SRGB 그대로 두고 바이트를
// sRGB 로 인코딩해, 샘플 결과가 파일의 선형 값이 되게 합니다. 알파는 그대로입니다.
static void encodeLinearImageToSrgb(TexSourceData& tex) {
    if (!tex.hasData || tex.data.size() != (size_t)tex.w * (size_t)tex.h * 4) return;
    uint8_t lut[256];
    for (int i = 0; i < 256; ++i) lut[i] = toByte(linearToSrgb(i / 255.f));
    for (size_t i = 0; i + 3 < tex.data.size(); i += 4) {
        tex.data[i + 0] = lut[tex.data[i + 0]];
        tex.data[i + 1] = lut[tex.data[i + 1]];
        tex.data[i + 2] = lut[tex.data[i + 2]];
    }
}

std::vector<MeshSourceData> GltfLoader::loadAll(const std::string& path, const ImportSettings& settings,
                                                std::vector<std::string>* dependencies) {
    if (dependencies) dependencies->clear();
    tinygltf::Model    model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    const size_t slash = path.find_last_of("/\\");
    const std::string baseDir = slash == std::string::npos ? std::string() : path.substr(0, slash);
    // 절대 경로 판정은 가상 파일 시스템과 같은 기준(fs::path::is_absolute)으로 합니다.
    GltfFsContext fsCtx;
    fsCtx.deps = dependencies;
    std::filesystem::path basePath;
    fsCtx.absoluteBase = hpak::pathFromUtf8(baseDir, basePath) && basePath.is_absolute();
    if (!loader.SetFsCallbacks(vfsCallbacks(&fsCtx), &err))
        throw std::runtime_error("glTF loader setup failed: " + err);

    std::vector<uint8_t> bytes;
    std::string readErr;
    if (!hvfs::readAll(path, bytes, &readErr)) throw std::runtime_error("glTF load failed: " + readErr);
    if (bytes.size() > (std::numeric_limits<unsigned int>::max)())   // 괄호: windows.h 의 max 매크로 회피
        throw std::runtime_error("glTF file is larger than 4 GiB: " + path);

    bool ok = (path.size() >= 4 && path.substr(path.size() - 4) == ".glb")
        ? loader.LoadBinaryFromMemory(&model, &err, &warn, bytes.data(), (unsigned int)bytes.size(), baseDir)
        : loader.LoadASCIIFromString(&model, &err, &warn, reinterpret_cast<const char*>(bytes.data()),
                                     (unsigned int)bytes.size(), baseDir);

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
    // 가져오기 배율(핫리로드 D8)은 맨 위 변환에 곱해 정점 · 바운드 · LOD · 충돌이 모두 따라가게 합니다. 뼈대 스킨 메시는 노드 변환을
    // 쓰지 않으므로(아래) 배율도 닿지 않습니다(관절 · 클립 이동값까지 바꿔야 해 남음, 아래에서 경고).
    const glm::mat4 rootMat = glm::scale(glm::mat4(1.f), glm::vec3(settings.scale));
    for (int r : roots) flattenNodes(model, r, rootMat, refs);

    // Fallback: if no scene hierarchy, use all meshes with identity
    if (refs.empty()) {
        for (int i = 0; i < (int)model.meshes.size(); ++i)
            refs.push_back({ rootMat, i });
    }

    std::vector<MeshSourceData> result;

    // 스킨이 있으면 관절의 부모 · 조상 행렬을 알아야 합니다(2026-11-07).
    std::vector<glm::mat4> nodeGlobal;
    std::vector<int>       nodeParent;
    if (!model.skins.empty()) nodeGlobals(model, nodeGlobal, nodeParent);

    for (const auto& ref : refs) {
        const int   meshIdx = ref.meshIdx;
        const auto& mesh    = model.meshes[meshIdx];

        for (const auto& prim : mesh.primitives) {
            if (!prim.attributes.count("POSITION")) continue;

            MeshSourceData d;
            d.name = mesh.name.empty()
                ? ("mesh" + std::to_string(meshIdx)) : mesh.name;

            // ── Positions (with baked world transform) ─────────────────────
            auto [posBase, posStride, posCount] =
                accessorInfo(model, prim.attributes.at("POSITION"));
            d.vertices.resize(posCount);

            // 스킨 메시는 정점을 바인드 공간 그대로 둡니다(glTF 규약: 스킨 메시 노드의 트랜스폼은 무시). 스키닝 행렬이
            // 관절 · 조상 트랜스폼을 모두 담습니다. 스킨을 읽지 못하면 보통 메시처럼 노드 트랜스폼을 굽습니다.
            if (ref.skin >= 0)
                d.skin = readSkin(model, ref.skin, prim, posCount, nodeGlobal, nodeParent, ref.node, mesh.weights, path);
            else if (!prim.targets.empty())   // 스킨 없는 모프 메시(애니메이션 A5)
                d.skin = makeMorphSkin(model, prim, posCount, mesh.weights, ref.world, ref.node, path);
            const glm::mat4 worldMat = d.skin ? glm::mat4(1.f) : ref.world;
            // Normals use the inverse-transpose (stay perpendicular under non-uniform scale). Tangents lie in the surface,
            // so they use the plain upper 3x3; a mirroring transform (negative determinant) flips the bitangent sign.
            const glm::mat3 linearMat = glm::mat3(worldMat);
            const glm::mat3 normalMat = glm::transpose(glm::inverse(linearMat));
            const float     mirrorSign = glm::determinant(linearMat) < 0.f ? -1.f : 1.f;

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
                    glm::vec3 wt = glm::normalize(linearMat * lt);
                    d.vertices[i].tangent[0] = wt.x;
                    d.vertices[i].tangent[1] = wt.y;
                    d.vertices[i].tangent[2] = wt.z;
                    d.vertices[i].tangent[3] = readF(base, i, stride, 3) * mirrorSign;
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
                if (!settings.srgb) {
                    encodeLinearImageToSrgb(d.albedo);
                    encodeLinearImageToSrgb(d.emissive);
                }
                d.isBlend    = (mat.alphaMode == "BLEND");
                d.doubleSided = mat.doubleSided;   // glTF 기본값 false. 소비자(그림자 · 컬링)는 아직 없음
                // MASK · alphaCutoff(glTF 기본 0.5, tinygltf 도 같은 기본값). 소비자(컷아웃 그림자 · 프리패스)는 아직 없고,
                // AssetManager 가 MASK 를 쓰는 파일에 경고를 남깁니다. 범위 밖 · NaN 값은 [0, 1] 로 자릅니다.
                d.alphaMask   = (mat.alphaMode == "MASK");
                {
                    const float cut = (float)mat.alphaCutoff;
                    d.alphaCutoff = (cut >= 0.f && cut <= 1.f) ? cut : (cut > 1.f ? 1.f : 0.f);
                }

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
                                        << " emis="        << (d.emissive.hasData ? "Y" : "-") << "]"
                                        << "  doubleSided=" << (mat.doubleSided ? "Y" : "-")
                                        << (d.alphaMask ? "  alphaMode=MASK cutoff=" + std::to_string(d.alphaCutoff)
                                                        : std::string());

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

            // 스킨 메시는 LOD 를 만들지 않습니다(O13).
            // 가져오기 설정 autoLod = false 면 만들지 않습니다(핫리로드 D8).
            if (settings.autoLod && !d.skin && !d.vertices.empty() && d.indices.size() >= hmesh::kLodMinTriangles * 3) {
                glm::vec3 lo( std::numeric_limits<float>::max());
                glm::vec3 hi(-std::numeric_limits<float>::max());
                for (const auto& v : d.vertices) {
                    const glm::vec3 p(v.pos[0], v.pos[1], v.pos[2]);
                    lo = glm::min(lo, p);
                    hi = glm::max(hi, p);
                }
                const float radius = glm::length(hi - (lo + hi) * 0.5f);
                const bool lockBorder = (mesh.primitives.size() > 1);
                const auto t0 = std::chrono::steady_clock::now();
                const auto lods = hmesh::buildLods(d.vertices, d.indices, radius, lockBorder);
                const double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0).count();
                if (!lods.empty()) {
                    d.lods.reserve(lods.size());
                    std::string triList, scrList;
                    for (size_t li = 0; li < lods.size(); ++li) {
                        d.lods.push_back({ lods[li].indices, lods[li].screenSize });
                        if (li > 0) { triList += " / "; scrList += " / "; }
                        triList += std::to_string(lods[li].indices.size() / 3);
                        char sbuf[32];
                        std::snprintf(sbuf, sizeof(sbuf), "%.2f", lods[li].screenSize);
                        scrList += sbuf;
                    }
                    char tbuf[32];
                    std::snprintf(tbuf, sizeof(tbuf), "%.2f", ms);
                    HLOG(LogAsset, Log) << "Auto LOD: " << (d.name.empty() ? path : d.name)
                                        << " " << (d.indices.size() / 3) << " -> " << triList
                                        << " tris, screen size " << scrList << " (" << tbuf << " ms)";
                }
            }

            // 정점 캐시 순서(O3)를 여기서 한 번 정합니다(LOD 를 만든 뒤, 업로드 때 하던 것과 같은 결과). 캐시에 담겨 다음
            // 읽기와 백그라운드 읽기(워커)에서 메인 스레드 비용이 없어집니다(비동기 로딩, 2026-11-20).
            hmesh::optimizeVertexCache(d.indices, (uint32_t)d.vertices.size());
            for (auto& lod : d.lods) hmesh::optimizeVertexCache(lod.indices, (uint32_t)d.vertices.size());
            d.cacheOrdered = true;

            result.push_back(std::move(d));
        }
    }

    if (result.empty())
        throw std::runtime_error("glTF has no valid primitives: " + path);

    size_t totalV = 0, totalT = 0, skinned = 0;
    for (const auto& r : result) { totalV += r.vertices.size(); totalT += r.indices.size() / 3; skinned += r.skin ? 1 : 0; }
    if (settings.scale != 1.f) {
        size_t boned = 0;
        for (const auto& r : result) boned += (r.skin && !r.skin->joints.empty()) ? 1 : 0;
        if (boned > 0) {
            HLOG(LogAsset, Warning) << path << ": import scale " << settings.scale << " is not applied to " << boned
                                    << " skinned mesh(es) (joints and clips keep their size)";
        }
    }
    HLOG(LogAsset, Log) << "Loaded: " << path
                        << "  meshes=" << result.size()
                        << "  verts=" << totalV << "  tris=" << totalT
                        << (skinned ? "  skinned=" + std::to_string(skinned) : std::string());
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
