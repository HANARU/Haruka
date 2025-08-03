#pragma once
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ── Transform ─────────────────────────────────────────────────────────────────
// 로컬 트랜스폼(위치/회전/크기)입니다. **필드는 private 이고 접근자·세터로만 만집니다.**
//
// 이유: 로컬을 바꾸면 그로부터 유도된 값(월드 행렬 등)이 무효화되어야 한다는 불변식을,
// 규약이 아니라 타입이 강제하기 위해서입니다. 지금은 Actor 가 월드를 온디맨드로
// 유도하므로(캐시 없음) 무효화할 것이 없지만, 나중에 월드 바운드 캐시 같은 파생값을
// 되살릴 때 이 캡슐화가 있어야 우회로 인한 stale 이 재발하지 않습니다.
// (Transform_Design.md DEF-49, Spatial_Representation_Design.md 5절)
//
// 값 타입이라 복사·대입이 자유롭습니다(undo 스냅샷, 직렬화 왕복에 그대로 씁니다).
class Transform {
public:
    const glm::vec3& position() const { return position_; }
    const glm::quat& rotation() const { return rotation_; }
    const glm::vec3& scale()    const { return scale_;    }

    void setPosition(const glm::vec3& p) { position_ = p; }
    void setRotation(const glm::quat& r) { rotation_ = r; }
    void setScale   (const glm::vec3& s) { scale_    = s; }

    // 편의 변이. 누적 이동과 로컬 축 기준 회전.
    void translate(const glm::vec3& d)   { position_ += d; }
    void rotate   (const glm::quat& dq)  { rotation_  = dq * rotation_; }

    glm::mat4 matrix() const {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), position_);
        glm::mat4 r = glm::mat4_cast(rotation_);
        glm::mat4 s = glm::scale(glm::mat4(1.0f), scale_);
        return t * r * s;
    }

private:
    glm::vec3 position_ {0.0f, 0.0f, 0.0f};
    glm::quat rotation_ {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale_    {1.0f, 1.0f, 1.0f};
};
