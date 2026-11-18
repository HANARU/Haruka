#pragma once

// ── 회전의 글자 표현 (DEF-108) ────────────────────────────────────────────────────
// 씬 · 프리팹 파일은 회전을 `ROT:` 오일러 각(도)으로 써 왔습니다. 사람이 읽고 고치기 쉽지만, 엔진이 들고 있는
// 값은 쿼터니언이라 쿼터니언 → 오일러 → 쿼터니언을 거칠 때마다 마지막 자리가 흔들렸습니다. Play → Stop 은 씬을
// 글로 써 두었다가 다시 읽어 복원하므로, 아무것도 바꾸지 않아도 Play 할 때마다 회전이 조금씩 바뀌었습니다
// (예: -10° 가 -9.999999 → -9.999998).
//
// 지금은 `ROT:` 다음 줄에 쿼터니언을 정확한 값으로 함께 씁니다(`QUAT:x y z w`). Unity 가 쿼터니언을 저장하고
// 오일러 각은 편집 화면용으로 따로 두는 것과 같은 방식입니다. 읽을 때는:
//   · QUAT 에서 다시 구한 오일러 각이 ROT 와 비트까지 같으면 QUAT 를 씁니다(엔진이 쓴 그대로라는 뜻).
//   · 다르면 사람이 ROT 를 손으로 고친 것이므로 ROT 를 씁니다. QUAT 만 남아 고친 값이 무시되는 일이 없습니다.
//   · QUAT 가 없으면(v3 이하 · 손으로 쓴 파일) 예전처럼 ROT 를 씁니다.
// QUAT 줄을 모르는 옛 엔진은 모르는 키로 건너뜁니다.
//
// 렌더러 · 에셋에 의존하지 않아 HarukaSceneTest 가 화면 없이 검산합니다.

#include "../core/TextFloat.hpp"

#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace transform_text {

inline glm::vec3 quatToEulerDeg(const glm::quat& q) {
    return glm::degrees(glm::vec3(glm::pitch(q), glm::yaw(q), glm::roll(q)));
}

// 쿼터니언 성분은 -0 도 그대로 씁니다. floatToText 는 -0 을 0 으로 쓰는데, 성분의 부호가 바뀌면 다시 구한
// 오일러 각이 ±180° 로 갈려 위의 비교가 어긋날 수 있습니다.
inline std::string exactFloat(float v) {
    if (v == 0.f) return std::signbit(v) ? "-0" : "0";
    return floatToText(v);
}

// "x y z w". 줄 머리(QUAT:)는 호출자가 씁니다.
inline std::string quatText(const glm::quat& q) {
    return exactFloat(q.x) + " " + exactFloat(q.y) + " " + exactFloat(q.z) + " " + exactFloat(q.w);
}

// 네 값을 모두 읽었을 때만 true. 실패하면 out 은 그대로입니다.
inline bool parseQuat(const std::string& s, glm::quat& out) {
    std::istringstream ss(s);
    float x = 0.f, y = 0.f, z = 0.f, w = 1.f;
    if (!(ss >> x >> y >> z >> w)) return false;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(w)) return false;
    out = glm::quat(w, x, y, z);
    return true;
}

// 파일에서 읽은 ROT(euler, 도)와 QUAT 로 회전을 정합니다. 규칙은 머리 주석을 보십시오.
inline glm::quat resolve(const glm::vec3& eulerDeg, bool hasQuat, const glm::quat& quat) {
    if (hasQuat && quatToEulerDeg(quat) == eulerDeg) return quat;
    return glm::quat(glm::radians(eulerDeg));
}

} // namespace transform_text
