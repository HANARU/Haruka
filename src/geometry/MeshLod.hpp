#pragma once
#include "MeshSource.hpp"
#include <cstdint>
#include <vector>

namespace hmesh {

// O13: glTF 가져오기 시 자동 생성한 메시 LOD 단계 하나.
struct LodLevel {
    std::vector<uint32_t> indices;
    float error      = 0.f;   // 로컬 길이 단위 오차 (result_error * simplifyScale)
    float screenSize = 0.f;   // 이 단계로 넘어가는 화면 크기 문턱(ScreenSize.hpp 규약)
};

inline constexpr uint32_t kLodMinTriangles    = 512;     // 이 미만이면 LOD 생성 안 함
inline constexpr uint32_t kLodMaxLevels       = 3;       // 최대 3단계
inline constexpr float    kLodTargetError     = 0.05f;   // 메시 크기 대비 오차 상한
inline constexpr float    kLodStopRatio       = 0.80f;   // 앞 단계의 80% 초과면 멈춤
inline constexpr float    kLodPixelTolerance  = 1.0f;    // T: 허용 투영 오차(화소)
inline constexpr float    kLodRefScreenHeight = 1800.f;  // H: 기준 화면 높이(화소, Quest Pro 눈 높이)

// 법선 · UV 속성을 보존하며 최대 3단계의 LOD 인덱스와 전환 화면 크기를 만듭니다.
// lockBorder 는 같은 glTF mesh 에 프리미티브가 둘 이상일 때만 켜 재질 경계 틈을 막습니다.
std::vector<LodLevel> buildLods(const std::vector<MeshSourceVertex>& vertices,
                                const std::vector<uint32_t>& indices,
                                float boundRadius,
                                bool lockBorder = false);

} // namespace hmesh
