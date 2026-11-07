#include "Skin.hpp"
#include "MeshSource.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hskin {

bool buildOrder(SkinSourceData& skin) {
    const int n = (int)skin.joints.size();
    skin.order.clear();
    skin.order.reserve(n);
    // 0 = 아직, 1 = 방문 중, 2 = 끝. 부모를 먼저 넣는 깊이 우선 순회입니다.
    std::vector<uint8_t> state(n, 0);
    for (int start = 0; start < n; ++start) {
        if (state[start] == 2) continue;
        // 뿌리 쪽으로 올라가며 사슬을 모은 뒤 위에서부터 넣습니다(재귀 없이).
        std::vector<int> chain;
        int j = start;
        while (j >= 0 && state[j] == 0) {
            state[j] = 1;
            chain.push_back(j);
            const int p = skin.joints[j].parent;
            if (p < -1 || p >= n) return false;
            j = p;
        }
        if (j >= 0 && state[j] == 1) return false;   // 순환
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            state[*it] = 2;
            skin.order.push_back(*it);
        }
    }
    return true;
}

int findJoint(const SkinSourceData& skin, const std::string& name) {
    for (int i = 0; i < (int)skin.joints.size(); ++i)
        if (skin.joints[i].name == name) return i;
    return -1;
}

static glm::mat4 trs(const glm::vec3& t, const glm::quat& r, const glm::vec3& s) {
    glm::mat4 m = glm::mat4_cast(r);
    m[0] *= s.x; m[1] *= s.y; m[2] *= s.z;
    m[3] = glm::vec4(t, 1.f);
    return m;
}

void computeGlobals(const SkinSourceData& skin, const glm::quat* localRot, std::vector<glm::mat4>& outGlobal) {
    const size_t n = skin.joints.size();
    outGlobal.assign(n, glm::mat4(1.f));
    // order 가 비어 있으면(buildOrder 전) 번호 순서로 계산합니다. 그때는 부모가 앞에 있어야 맞습니다.
    auto eval = [&](int j) {
        const SkinJoint& jt = skin.joints[j];
        const glm::mat4 local = trs(jt.restT, localRot ? localRot[j] : jt.restR, jt.restS);
        outGlobal[j] = (jt.parent >= 0 ? outGlobal[jt.parent] : jt.rootParent) * local;
    };
    if (skin.order.size() == n) for (int j : skin.order) eval(j);
    else                        for (int j = 0; j < (int)n; ++j) eval(j);
}

void computePalette(const SkinSourceData& skin, const std::vector<glm::mat4>& globals, std::vector<glm::mat4>& outPalette) {
    const size_t n = std::min(skin.joints.size(), globals.size());
    outPalette.resize(skin.joints.size(), glm::mat4(1.f));
    for (size_t j = 0; j < n; ++j) outPalette[j] = globals[j] * skin.joints[j].inverseBind;
}

void skinVertices(const MeshSourceVertex* bind, size_t n, const SkinSourceData& skin,
                  const glm::mat4* palette, MeshSourceVertex* out, glm::vec3& bmin, glm::vec3& bmax) {
    bmin = glm::vec3(0.f);
    bmax = glm::vec3(0.f);
    if (!n) return;
    bmin = glm::vec3(INFINITY);
    bmax = glm::vec3(-INFINITY);
    const size_t jointCount = skin.joints.size();
    for (size_t i = 0; i < n; ++i) {
        const MeshSourceVertex& b = bind[i];
        glm::mat4 m(0.f);
        float wsum = 0.f;
        for (int k = 0; k < kInfluences; ++k) {
            const size_t a = i * kInfluences + k;
            const float  w = a < skin.jointWeight.size() ? skin.jointWeight[a] : 0.f;
            if (w <= 0.f) continue;
            const uint16_t ji = skin.jointIndex[a];
            if (ji >= jointCount) continue;
            m += palette[ji] * w;
            wsum += w;
        }
        if (wsum <= 0.f) m = glm::mat4(1.f);   // 무게가 없는 정점은 바인드 자리에 둡니다
        const glm::vec3 p = glm::vec3(m * glm::vec4(b.pos[0], b.pos[1], b.pos[2], 1.f));
        const glm::mat3 r(m);
        glm::vec3 nrm = r * glm::vec3(b.normal[0], b.normal[1], b.normal[2]);
        glm::vec3 tan = r * glm::vec3(b.tangent[0], b.tangent[1], b.tangent[2]);
        const float nl = glm::length(nrm), tl = glm::length(tan);
        nrm = nl > 1e-12f ? nrm / nl : glm::vec3(0.f, 1.f, 0.f);
        tan = tl > 1e-12f ? tan / tl : glm::vec3(1.f, 0.f, 0.f);

        MeshSourceVertex& v = out[i];
        v.pos[0] = p.x; v.pos[1] = p.y; v.pos[2] = p.z;
        v.normal[0] = nrm.x; v.normal[1] = nrm.y; v.normal[2] = nrm.z;
        v.uv[0] = b.uv[0]; v.uv[1] = b.uv[1];
        v.tangent[0] = tan.x; v.tangent[1] = tan.y; v.tangent[2] = tan.z; v.tangent[3] = b.tangent[3];
        bmin = glm::min(bmin, p);
        bmax = glm::max(bmax, p);
    }
}

} // namespace hskin
