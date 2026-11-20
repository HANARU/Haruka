#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_multiview : require   // gl_ViewIndex(HARUKA_EYE_INDEX, 멀티뷰 메인 패스)
#include "haruka_shared.h"

// 빌보드 파티클(2026-11-20, Docs/Particles_And_Decals.md D7 · D8). 정점 버퍼 없이 정점 번호 6개로 사각형을 만들고, 파티클 하나는
// 인스턴스 정점 입력 하나(vfx::ParticleInstance, 48 B)입니다. 사각형은 두 눈의 가운데 위치를 바라봅니다(두 눈이 같은 사각형을 보고,
// 고개를 기울여도 돌지 않음). 위쪽은 월드 +Y 를 시선에 수직으로 맞춘 것이고, 바로 위 · 아래를 볼 때는 월드 +Z 입니다.
layout(set = 0, binding = 0) uniform FrameBlock { FrameUBO ubo; };
layout(push_constant) uniform ParticlePush { int viewIndex; int pad0; int pad1; int pad2; } pc;   // GpuParticlePush

layout(location = 0) in vec4 inPosSize;   // xyz = 월드 위치, w = 한 변(m)
layout(location = 1) in vec4 inColor;     // 선형 색 · 알파
layout(location = 2) in vec4 inParams;    // x = 회전(라디안), y = 아틀라스 칸

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;

const vec2 kCorner[6] = vec2[](vec2(0.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0),
                               vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(1.0, 0.0));

void main() {
    vec2 c = kCorner[gl_VertexIndex];
    vec3 head = 0.5 * (ubo.eyePos[0].xyz + ubo.eyePos[1].xyz);
    vec3 P = inPosSize.xyz;
    vec3 f = head - P;
    float fl = length(f);
    f = fl > 1e-6 ? f / fl : vec3(0.0, 0.0, 1.0);
    vec3 up0   = abs(f.y) > 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up0, f));
    vec3 up    = cross(f, right);

    float s = sin(inParams.x), co = cos(inParams.x);
    vec2  q = c - 0.5;
    vec2  r = vec2(q.x * co - q.y * s, q.x * s + q.y * co);
    vec3  world = P + (right * r.x + up * r.y) * inPosSize.w;

    int cell = int(inParams.y + 0.5);
    fragUV    = (vec2(float(cell & 1), float(cell >> 1)) + vec2(c.x, 1.0 - c.y)) * 0.5;
    fragColor = inColor;
    gl_Position = ubo.viewProj[pc.viewIndex + int(gl_ViewIndex)] * vec4(world, 1.0);
}
