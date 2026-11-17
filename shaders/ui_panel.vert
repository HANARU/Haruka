#version 450
#extension GL_GOOGLE_include_directive : require
#include "haruka_shared.h"

// 월드 UI 패널(3D UI P1b). 패널은 로컬 XY 평면의 1×1 사각형(가운데 원점, 앞면 +Z)이고 model 이 크기와 자세를
// 줍니다. 정점 버퍼 없이 정점 번호로 두 삼각형을 만듭니다. UV 는 왼쪽 위가 (0,0) 입니다(HSlate 화면 좌표와 같음).
layout(set = 0, binding = 0) uniform FrameBlock { FrameUBO ubo; };
layout(push_constant) uniform PanelPush { mat4 model; int viewIndex; int flags; float brightness; float pad; } pc;   // GpuPanelPush

layout(location = 0) out vec2 fragUV;

const vec2 kCorner[6] = vec2[](vec2(0.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0),
                               vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(1.0, 0.0));

void main() {
    vec2 c = kCorner[gl_VertexIndex];
    fragUV = c;
    vec3 local = vec3(c.x - 0.5, 0.5 - c.y, 0.0);
    gl_Position = ubo.viewProj[pc.viewIndex] * pc.model * vec4(local, 1.0);
}
