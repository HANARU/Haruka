# Haruka Engine — 작업 내역 전체 기록

작성일: 2026-05-14  
대상 세션: 에디터 기능 강화 (Component System UI + 5개 에디터 기능)

---

## 1. 작업 개요

Vulkan 1.4 기반 Haruka 엔진의 에디터 기능을 강화했습니다.  
크게 두 단계로 나뉩니다:

- **Phase A**: Component System UI (컴포넌트 목록 표시 / 프로퍼티 편집 / 추가·제거)  
- **Phase B**: 에디터 추가 5기능 (삭제, 빈 Actor 추가, 하이라이트 박스, Transform Gizmo, F2 이름 편집)  
- **BugFix**: `prevLDown` 항상 false 버그 수정

---

## 2. 파일별 변경 내역

### 2-1. `src/core/Component.hpp`
- `virtual const char* typeName() const { return "Component"; }` 추가  
  → 런타임 컴포넌트 타입명 반환 (RTTI 없이 UI 표시용)

---

### 2-2. `src/core/Actor.hpp`
- `#include <algorithm>` 추가
- `getComponents()` 추가:
  ```cpp
  const std::vector<std::unique_ptr<Component>>& getComponents() const { return components; }
  ```
- `removeComponent<T>()` 추가:
  ```cpp
  template<typename T>
  bool removeComponent() {
      auto it = std::find_if(components.begin(), components.end(),
          [](const auto& c){ return dynamic_cast<T*>(c.get()) != nullptr; });
      if (it == components.end()) return false;
      (*it)->onDestroy();
      components.erase(it);
      return true;
  }
  ```

---

### 2-3. `src/components/StaticMeshComponent.hpp`
- `const char* typeName() const override { return "StaticMeshComponent"; }` 추가

### 2-4. `src/components/LightComponent.hpp`
- `const char* typeName() const override { return "LightComponent"; }` 추가

### 2-5. `src/components/CameraComponent.hpp`
- `const char* typeName() const override { return "CameraComponent"; }` 추가

---

### 2-6. `src/platform/IWindow.hpp`
- `HInput` 네임스페이스에 키 상수 추가:
  ```cpp
  constexpr int KEY_DELETE    = 261;   // GLFW_KEY_DELETE
  constexpr int KEY_F2        = 291;   // GLFW_KEY_F2
  constexpr int KEY_BACKSPACE = 259;   // GLFW_KEY_BACKSPACE
  constexpr int KEY_ENTER     = 257;   // GLFW_KEY_ENTER
  ```
- 순수 가상 함수 추가:
  ```cpp
  virtual void drainCharQueue(std::vector<unsigned int>& out) = 0;
  ```

---

### 2-7. `src/platform/glfw/GlfwWindow.hpp`
- `static void charCallback(GLFWwindow* w, unsigned int codepoint);` 추가
- `void drainCharQueue(std::vector<unsigned int>& out) override;` 추가
- `std::vector<unsigned int> charQueue_;` 멤버 추가

### 2-8. `src/platform/glfw/GlfwWindow.cpp`
- `init()`에 `glfwSetCharCallback(window, charCallback);` 등록
- `charCallback()` 구현:
  ```cpp
  void GlfwWindow::charCallback(GLFWwindow* w, unsigned int codepoint) {
      auto self = reinterpret_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
      self->charQueue_.push_back(codepoint);
  }
  ```
- `drainCharQueue()` 구현:
  ```cpp
  void GlfwWindow::drainCharQueue(std::vector<unsigned int>& out) {
      out = std::move(charQueue_);
      charQueue_.clear();
  }
  ```

---

### 2-9. `src/renderer/Renderer.hpp`

**추가된 public 멤버:**
```cpp
struct LineVertex {
    glm::vec3 pos;
    glm::vec4 color;
};

void setEditorLines(std::vector<LineVertex> lines) {
    pendingLines_ = std::move(lines);
}
```

**추가된 private 멤버:**
```cpp
VkPipeline            linePipeline_       = VK_NULL_HANDLE;
VkPipelineLayout      linePipelineLayout_ = VK_NULL_HANDLE;
VkBuffer              lineVertBuf_        = VK_NULL_HANDLE;
VkDeviceMemory        lineVertMem_        = VK_NULL_HANDLE;
void*                 lineVertPtr_        = nullptr;
uint32_t              lineVertCount_      = 0;
std::vector<LineVertex> pendingLines_;
static constexpr uint32_t kMaxLineVerts = 2048;
```

**추가된 private 메서드:**
```cpp
void createLinePipeline();
void createLineBuffer();
uint32_t findMemType(uint32_t filter, VkMemoryPropertyFlags flags);
```

---

### 2-10. `src/renderer/Renderer.cpp`
- `init()` 끝에 두 호출 추가:
  ```cpp
  createLinePipeline();
  createLineBuffer();
  ```
- `findMemType()` 구현 추가 (HOST_VISIBLE|HOST_COHERENT 타입 탐색)
- `createLineBuffer()` 구현 추가:
  - `kMaxLineVerts * sizeof(LineVertex)` 크기의 HOST_VISIBLE|HOST_COHERENT 버퍼 생성
  - `vkMapMemory`로 영구 매핑 → `lineVertPtr_`에 저장

---

### 2-11. `src/renderer/Renderer_swap.cpp`
- `createLinePipeline()` 구현 추가:
  - `shaders/line.vert.spv` / `line.frag.spv` 로드
  - `VK_PRIMITIVE_TOPOLOGY_LINE_LIST`
  - 별도 `linePipelineLayout_`: set 0 (`frameSetLayout`) 만 바인딩, push constant 없음
  - `depthTestEnable = VK_FALSE` (항상 최상위에 그림)
  - `lineWidth = 1.5f`
  - Vertex 입력: binding 0, stride = `sizeof(LineVertex)`, location 0 = `vec3 pos`, location 1 = `vec4 color`
  - Dynamic Rendering: `colorAttachmentFormat = scFormat`, `depthAttachmentFormat = depthFormat`

---

### 2-12. `src/renderer/Renderer_render.cpp`

**`drawFrame()` 내 라인 오버레이 드로우 추가** (메시 드로우 이후, `vkCmdEndRendering` 직전):
```cpp
if (lineVertPtr_ && !pendingLines_.empty()) {
    uint32_t count = std::min((uint32_t)pendingLines_.size(), kMaxLineVerts);
    memcpy(lineVertPtr_, pendingLines_.data(), count * sizeof(LineVertex));
    lineVertCount_ = count;
    vkCmdBindPipeline(cmdBuffers[frameIdx], VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline_);
    vkCmdBindDescriptorSets(cmdBuffers[frameIdx], VK_PIPELINE_BIND_POINT_GRAPHICS,
        linePipelineLayout_, 0, 1, &frameDescSets[frameIdx], 0, nullptr);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmdBuffers[frameIdx], 0, 1, &lineVertBuf_, &offset);
    vkCmdDraw(cmdBuffers[frameIdx], lineVertCount_, 1, 0, 0);
}
```

**`cleanup()` 내 라인 리소스 해제 추가:**
```cpp
if (lineVertPtr_) { vkUnmapMemory(device, lineVertMem_); lineVertPtr_ = nullptr; }
if (lineVertBuf_) { vkDestroyBuffer(device, lineVertBuf_, nullptr); lineVertBuf_ = VK_NULL_HANDLE; }
if (lineVertMem_) { vkFreeMemory(device, lineVertMem_, nullptr);    lineVertMem_ = VK_NULL_HANDLE; }
if (linePipeline_)       { vkDestroyPipeline(device, linePipeline_, nullptr);             linePipeline_ = VK_NULL_HANDLE; }
if (linePipelineLayout_) { vkDestroyPipelineLayout(device, linePipelineLayout_, nullptr); linePipelineLayout_ = VK_NULL_HANDLE; }
```

---

### 2-13. `shaders/line.vert` (신규)
```glsl
#version 450
layout(set=0,binding=0) uniform FrameUBO {
    mat4 viewProj[2]; vec4 lightDir; vec4 lightColor;
} ubo;
layout(location=0) in vec3 inPos;
layout(location=1) in vec4 inColor;
layout(location=0) out vec4 fragColor;
void main() {
    gl_Position = ubo.viewProj[0] * vec4(inPos, 1.0);
    fragColor = inColor;
}
```

### 2-14. `shaders/line.frag` (신규)
```glsl
#version 450
layout(location=0) in vec4 fragColor;
layout(location=0) out vec4 outColor;
void main() { outColor = fragColor; }
```

---

### 2-15. `CMakeLists.txt`
- 셰이더 컴파일 루프에 `line.vert`, `line.frag` 추가:
  ```cmake
  foreach(SHADER triangle.vert triangle.frag ui.vert ui.frag line.vert line.frag)
  ```

---

### 2-16. `src/editor/EditorApp.hpp`

**추가된 private 멤버:**
```cpp
// Gizmo state
int       gizmoHovered_        = -1;   // -1=none, 0=X, 1=Y, 2=Z
bool      gizmoDragging_       = false;
int       gizmoAxis_           = -1;
glm::vec2 gizmoPrevMouse_      {};
Transform gizmoUndoSnap_       {};

// Rename state
bool        renaming_          = false;
bool        prevRenaming_      = false;
std::string renameBuffer_;

// Key edge tracking
bool prevDeleteDown_    = false;
bool prevF2Down_        = false;
bool prevBackspaceDown_ = false;
bool prevEnterDown_     = false;

// Actor counter (Add Empty)
int actorCounter_ = 0;
```

**추가된 private 메서드:**
```cpp
void deleteSelectedActor();
void tickGizmoAndHighlight(float winW, float winH);
void tickRenameInput();
```

---

### 2-17. `src/editor/EditorApp.cpp`

#### (A) LightSnapshot / SetLightCmd 구조체 추가
```cpp
struct LightSnapshot { glm::vec3 color; float intensity, ambient; };

struct SetLightCmd : ICommand {
    LightComponent* light;
    LightSnapshot   before, after;
    void undo() override { light->color=before.color; light->intensity=before.intensity; light->ambient=before.ambient; }
    void redo() override { light->color=after.color;  light->intensity=after.intensity;  light->ambient=after.ambient;  }
};
```

#### (B) `tick()` 수정
- `tickRenameInput()` 호출 추가
- `ctx_.window->getFramebufferSize()` 후 `tickGizmoAndHighlight(winW, winH)` 호출 추가
- `renamChanged = (renaming_ != prevRenaming_)` 조건 추가 → Outliner 리빌드 트리거
- 선택 변경 시 `renaming_ = false` 강제 초기화

#### (C) `rebuildOutliner()` 전면 수정
- **"+ Add Empty" 버튼** Outliner 최상단 추가:
  - 클릭 시 `"Actor_N"` 이름으로 빈 Actor 스폰, 즉시 선택
- **각 Actor 행**: 단일 버튼 → `SHorizontalBox` ([이름 버튼 | Fill] + ["x" 버튼 | 20px])
  - 이름 버튼 onClick: `renaming_ = false` 후 선택 토글
  - "[x]" 버튼 onClick: `editorCamActor_` 보호, 선택 클리어, `cmdStack_.clear()`, `destroyActor()`
- **Rename 표시**: 선택 Actor이면서 `renaming_` 활성 시 `renameBuffer_ + "|"` 표시

#### (D) `rebuildDetails()` 전면 수정
- `addNumRow` 시그니처 변경: `step`, `bFn`(onDragBegin), `eFn`(onDragEnd) 명시적 매개변수화
- **Transform 섹션**: `txBegin`/`txEnd` 람다로 `SetTransformCmd` 언두
- **컴포넌트 루프** (`sel->getComponents()` 순회):
  - 컴포넌트 헤더 행: `[typeName | Fill] [On/Off | 32px] [x | 20px]`
  - On/Off: `comp->setEnabled(!comp->isEnabled())`
  - [x]: 타입 별 `removeComponent<T>()` 람다
  - `StaticMeshComponent`: Visible 토글 + modelPath 표시
  - `LightComponent`: Intensity, Ambient, R, G, B — `SetLightCmd` 언두 (shared_ptr\<LightSnapshot\> 패턴)
  - `CameraComponent`: FovY, zNear, zFar, Primary 토글
- **ADD COMPONENT 섹션**: 없는 타입만 `+Light`, `+Camera`, `+Mesh` 버튼 표시

#### (E) `handleInput()` 수정 — Gizmo 드래그 추가
```
gizmoConsumed 플래그 도입:
  - LMB 다운 + gizmoHovered_ >= 0 → gizmoDragging_ 시작, undo 스냅샷
  - gizmoDragging_ + LMB 다운 → 스크린-스페이스 투영 드래그 적용
  - gizmoDragging_ + LMB 업   → SetTransformCmd 푸시
gizmoConsumed == true이면 pick/UI 이벤트 차단
```

**드래그 수식:**
```
sa = projectScreen(pos + axis) - projectScreen(pos)
worldDelta = dot(mouseDelta, sa) / dot(sa, sa)
transform.position += axis * worldDelta
```

#### (F) `deleteSelectedActor()` 추가
```cpp
void EditorApp::deleteSelectedActor() {
    if (!ctx_.selected || ctx_.selected == editorCamActor_) return;
    cmdStack_.clear();             // dangling pointer 방지
    ctx_.world->destroyActor(ctx_.selected);
    ctx_.selected  = nullptr;
    renaming_      = false;
    gizmoDragging_ = false;
    gizmoHovered_  = -1;
    gizmoAxis_     = -1;
}
```

#### (G) `tickRenameInput()` 추가
| 입력 | 처리 |
|------|------|
| F2 (엣지) | 미선택 → 무시 / 선택 + 비렌네임 → renaming_ 시작, renameBuffer_ 초기화 / 렌네임 중 → 취소 |
| 문자 (drainCharQueue) | ASCII 32~126만 renameBuffer_ 에 추가 |
| Backspace (엣지) | renameBuffer_ 마지막 문자 삭제 |
| Enter (엣지) | actor.name = renameBuffer_, renaming_ 종료 |
| Delete (엣지, 비렌네임) | deleteSelectedActor() 호출 |
| 변경 있을 때 | rebuildOutliner() 호출 |

#### (H) `tickGizmoAndHighlight(float winW, float winH)` 추가
```
선택 없음 → setEditorLines({}) 클리어

선택 있음:
  1. 와이어프레임 박스 생성 (12엣지 × 2정점, 노란색 {1, 0.9, 0.2, 1})
     - 8 코너: pos ± 0.55f on each axis
     - 12 엣지: 하단 4 + 상단 4 + 측면 4
  
  2. 카메라까지 거리 기반 scale = max(0.01f, dist * 0.25f)
  
  3. 축별 히트 테스트 (gizmoDragging_ 아닐 때):
     - s0 = projectScreen(pos), s1 = projectScreen(pos + axis * scale)
     - segDist(mouse, s0, s1) < 12px → gizmoHovered_ = i
  
  4. 축 라인 + 팁 크로스 생성 (3개 축, RGB 색상):
     - 라인: pos → tip
     - 팁 크로스: tip ± (next_axis * scale * 0.08)
     - 호버/드래그 중인 축은 노란색 {1, 1, 0.2, 1}
  
  5. setEditorLines(lines) 호출
```

---

### 2-18. `src/main.cpp`

**`prevLDown` 버그 수정:**

이전 (버그):
```cpp
bool lmb = win.isMouseButtonDown(HInput::MOUSE_BUTTON_LEFT);
prevLDown = lmb;   // lmbJustUp 계산 전에 갱신 → 항상 false
...
editor.handleInput(mx, my, lmb, (!lmb && prevLDown), ...);
```

수정 후:
```cpp
bool lmb       = win.isMouseButtonDown(HInput::MOUSE_BUTTON_LEFT);
bool lmbJustUp = !lmb && prevLDown;   // prevLDown 갱신 전에 먼저 계산
prevLDown      = lmb;
...
editor.handleInput(mx, my, lmb, lmbJustUp, ...);
```

---

## 3. 아키텍처 결정 사항

| 항목 | 결정 | 이유 |
|------|------|------|
| 라인 파이프라인 레이아웃 | 별도 `linePipelineLayout_` (set 0만) | 라인은 material/push constant 불필요 |
| 라인 버퍼 메모리 | `vkAllocateMemory` 직접 (커스텀 allocator 외부) | 라인 지오메트리는 소량, 단순성 우선 |
| 깊이 테스트 | `depthTestEnable = VK_FALSE` | 기즈모가 항상 메시 위에 표시되어야 함 |
| 라인 드로우 위치 | 메시 드로우 이후, `vkCmdEndRendering` 직전 | 동일 렌더링 스코프 내에서 처리 |
| 기즈모 드래그 수식 | 스크린-스페이스 투영 기반 | 카메라 각도 무관하게 직관적인 드래그 감 |
| Rename 저장 | `renameBuffer_` 별도 문자열 | 원본 name은 Enter 확정 전까지 유지 |
| Delete Actor 시 cmdStack_ 클리어 | 즉시 클리어 | 언두 커맨드가 삭제된 Actor 포인터를 참조하는 댕글링 방지 |
| SetLightCmd undo | `shared_ptr<LightSnapshot>` | 람다 캡처 시 LightSnapshot 복사본 보장 |

---

## 4. 빌드 결과

```
shaders: line.vert.spv, line.frag.spv 컴파일 성공
HarukaEngine.exe 링크 성공 (경고 없음)
```

---

## 5. 기능별 UX 요약

| 기능 | 트리거 | 동작 |
|------|--------|------|
| Actor 선택 하이라이트 | 선택 시 자동 | 노란 와이어프레임 박스 표시 |
| Transform Gizmo | 선택 Actor 위에 마우스 | RGB 축 라인 표시, 12px 이내 호버 시 노란색 |
| Gizmo 드래그 | 호버 상태에서 LMB 드래그 | 해당 축 방향 이동, 릴리즈 시 언두 기록 |
| Actor 삭제 | Delete 키 또는 Outliner [x] 버튼 | 선택 Actor 삭제 (editorCam 보호) |
| Add Empty Actor | Outliner "+ Add Empty" 버튼 | Actor_N 이름으로 빈 Actor 생성 |
| F2 이름 편집 | F2 키 | Outliner에 `이름|` 커서 표시, 타이핑으로 편집 |
| 이름 확정 | Enter | actor.name 갱신 |
| 이름 취소 | F2 재입력 또는 다른 Actor 클릭 | 원본 이름 유지 |
