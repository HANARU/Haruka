#pragma once
// 하드튜닝 기준 상수. 값은 2026-10-04 에 사용자와 맞춘 그대로이고, 이름과 근거만 한곳에 모았습니다.
//
//  (a) 태양 3.0(LightComponent 기본값 · LightingProbe.hscene 의 intensity=3) · 하늘 0.30 · 노출 1.0 은 한 세트입니다.
//      따로 움직이면 대비가 다시 무너집니다.
//  (b) 태양 3.0 은 씬 값이라 여기 상수로 두지 않습니다.
//  (c) 블룸 임계는 지금 노출 전 HDR 기준입니다.

namespace lighting_calibration {

// 하늘 복사휘도 배율. 하늘 함수(Renderer.cpp)가 돌려주던 원래 값의 평균은 0.6017 이고, 셰이더가
// 쓰는 단위로 환산한 등가 조도는 pi * 0.6017 = 1.890 이다. 태양이 0.85 였으므로
// 하늘이 2.2 배 밝았고, 그래서 법선이 기울어도 픽셀이 4/255 밖에 움직이지 않았다.
// 0.30 배면 등가 조도가 0.567 이 되어, 태양 3.0 과 함께 5.3 : 1 이 된다.
// 이 배율은 IBL 큐브맵 두 장을 만드는 데에만 쓰이고 뷰포트 배경색은 GeometryPass
// 의 clear value 라서, 조명만 바꾸고 배경은 건드리지 않는다.
inline constexpr float kSkyRadianceScale = 0.30f;

// 노출. 예전 1.0 에서는 태양 0.85 + 하늘 1.0 배 조합에서 흰 표면의 선형값이 0.72 여서 ACES 숄더에
// 얹혀 코드값 221 부근에 몰려 평탄해 보였다. 태양 3.0 / 하늘 0.30 배에 맞춘 값이 0.4 였고, 이때 평면이
// 코드값 169 로 중간톤에 앉았다. 다만 그 0.4 는 감마가 두 번 들어간 화면을 보며 맞춘 값이었다(DEF-96).
// 감마를 바로잡은 뒤 같은 목표(흰 평면이 중간톤)로 돌아가는 값이 약 1.0 이라 1.0 으로 둔다
// (2026-10-04 사용자 결정).
inline constexpr float kBaseExposure      = 1.0f;
inline constexpr float kBaseAoStrength    = 0.8f;
inline constexpr float kBaseBloomStrength = 0.04f;
inline constexpr float kBloomThreshold    = 1.0f;

} // namespace lighting_calibration
