#pragma once
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class HWidget;

enum class EDockZone { None, Center, Left, Right, Top, Bottom };

// ── DockNode ──────────────────────────────────────────────────────────────────
// 에디터 도크 트리의 한 노드입니다. children 이 비면 **리프(탭 스택)**, 아니면 **분할 노드**.
// 분할 비율은 children 과 1:1 인 ratios(합 ≈ 1)로 두고, 실제 배치는 SHorizontalBox/
// SVerticalBox 의 Fill 가중치로 매핑됩니다(가중치 = 비율). 트리 자체는 위젯과 독립이라
// 위젯을 리빌드해도(스플리터 드래그·재도킹) 노드 포인터는 안정적입니다.
struct DockNode {
    enum class Split { Horizontal, Vertical };   // H = 자식 좌우, V = 자식 상하

    Split                                  split = Split::Horizontal;
    std::vector<std::unique_ptr<DockNode>> children;   // 분할 노드일 때
    std::vector<float>                     ratios;      // children 과 1:1, 합 ≈ 1

    std::vector<std::string>               panelIds;    // 리프의 탭 목록
    int                                    activeTab = 0;

    bool isLeaf() const { return children.empty(); }

    static std::unique_ptr<DockNode> makeLeaf(std::vector<std::string> ids, int active = 0) {
        auto n = std::make_unique<DockNode>();
        n->panelIds  = std::move(ids);
        n->activeTab = active;
        return n;
    }
    static std::unique_ptr<DockNode> makeSplit(Split s,
            std::vector<std::unique_ptr<DockNode>> kids, std::vector<float> r) {
        auto n = std::make_unique<DockNode>();
        n->split    = s;
        n->children = std::move(kids);
        n->ratios   = std::move(r);
        return n;
    }
};

// ── PanelRegistry ─────────────────────────────────────────────────────────────
// id → {제목, 위젯, closable}. 패널 위젯은 EditorApp 이 1회 생성해 보관합니다(패널은
// 싱글턴이라 재도킹 시 같은 인스턴스가 이동). **신규 위젯도 add() 한 줄이면 도킹 대상**이
// 됩니다 — 사용자가 말한 "위에 위젯을 디자인"하는 확장성 seam.
struct PanelDef {
    std::string              title;
    std::shared_ptr<HWidget> widget;
    bool                     closable = true;
    bool                     floatable = true;   // false 면 떠 있는 창으로 뗄 수 없음(결정 D8: Viewport 만)
};

class PanelRegistry {
public:
    void add(const std::string& id, const std::string& title,
             std::shared_ptr<HWidget> widget, bool closable = true, bool floatable = true) {
        if (!defs_.count(id)) order_.push_back(id);
        defs_[id] = { std::move(title), std::move(widget), closable, floatable };
    }
    const PanelDef* find(const std::string& id) const {
        auto it = defs_.find(id);
        return it == defs_.end() ? nullptr : &it->second;
    }
    const std::vector<std::string>& order() const { return order_; }

private:
    std::unordered_map<std::string, PanelDef> defs_;
    std::vector<std::string>                  order_;
};

// ── 순수 도크 트리 연산 (위젯·화면 크기 비의존, S6f) ────────────────────────────
using DockPanelHome = std::unordered_map<std::string, std::vector<std::string>>;

DockNode* dockFindParent(DockNode* cur, DockNode* target, int& outIdx);
void      dockNormalize(DockNode* n);
void      dockSerialize(const DockNode* n, std::ostringstream& o);
std::unique_ptr<DockNode> dockParse(std::istringstream& in, int depth, bool& ok);
std::unique_ptr<DockNode> dockPrune(std::unique_ptr<DockNode> n,
                                    const std::function<bool(const std::string&)>& valid,
                                    std::unordered_set<std::string>& seen);

bool      dockContains(const DockNode* n, const std::string& id);
DockNode* dockFindLeaf(DockNode* n, const std::string& id);
void      dockRemoveTab(DockNode* leaf, int idx);
bool      dockPanelIsActive(const DockNode* n, const std::string& id);

void dockCollapseEmptyLeaf(std::unique_ptr<DockNode>& root, DockNode* leaf);
void dockAttachSplit(std::unique_ptr<DockNode>& root, DockNode* target,
                     const std::string& panelId, EDockZone zone, float share);
bool dockRedock(std::unique_ptr<DockNode>& root, DockNode* src, int srcIdx,
                const std::string& panelId, DockNode* target, EDockZone zone);

bool dockClosePanel(std::unique_ptr<DockNode>& root, DockPanelHome& home,
                    const std::string& id, bool closable);
bool dockShowPanel(std::unique_ptr<DockNode>& root, DockPanelHome& home,
                   const std::string& id);

std::string               dockSerializeLayout(const DockNode* root);
std::unique_ptr<DockNode> dockParseLayout(const std::string& text,
                                          const std::function<bool(const std::string&)>& valid);

// ── 떠 있는 창 (다중 OS 창 M1) ────────────────────────────────────────────────
// 떠 있는 창 하나 = {도크 트리, 화면 위치 · 크기}. 이 파일은 데이터 · 순수 연산 · 직렬화만
// 맡고, OS 창은 에디터(EditorFloatWindows, M3a)가 만듭니다. id 는 배치 파일의 `F <번호>` 로, 창이 살아 있는 동안 바뀌지
// 않습니다(창별 덤프 · 복원이 이 번호로 창을 가리키기 위함).
struct DockFloat {
    int                       id = 0;
    std::unique_ptr<DockNode> root;
    // 화면 좌표(픽셀). 내용 영역(제목 줄 · 테두리 제외) 기준입니다(M3a: glfwGetWindowPos · glfwGetWindowSize 와 같음).
    int                       x = 0, y = 0, w = 0, h = 0;
    bool                      maximized = false;
};

// 배치 파일에서 고쳐 넣는 떠 있는 창의 최소 · 최대 크기입니다.
inline constexpr int kDockFloatMinW = 160;
inline constexpr int kDockFloatMinH = 120;
inline constexpr int kDockFloatMaxDim = 16384;

// 패널이 어느 트리에 있는지. floatIndex 가 -1 이면 메인 트리입니다.
struct DockLocation {
    DockNode* leaf       = nullptr;
    int       floatIndex = -1;
    explicit operator bool() const { return leaf != nullptr; }
};

using DockPanelPredicate = std::function<bool(const std::string&)>;

// 트리가 비었는지(루트 없음, 또는 탭 없는 리프 하나). 마지막 탭이 빠진 떠 있는 창을 알아보는 데 씁니다.
bool dockTreeEmpty(const DockNode* root);
// node 가 root 트리에 속하는지.
bool dockOwns(const DockNode* root, const DockNode* node);
// 새 떠 있는 창에 줄 번호(지금 쓰는 번호 중 가장 큰 값 + 1, 최소 1).
int  dockNextFloatId(const std::vector<DockFloat>& floats);

bool         dockContainsAll(const DockNode* mainRoot, const std::vector<DockFloat>& floats,
                             const std::string& id);
DockLocation dockFindLeafAll(DockNode* mainRoot, std::vector<DockFloat>& floats,
                             const std::string& id);

// 탭 하나를 떼어 새 떠 있는 창(out)으로 만듭니다. out.root 만 채우고 번호 · 위치 · 크기는 호출한
// 쪽(M4: 커서 자리)이 정합니다. floatable=false 거나 메인 트리에서 Viewport 가 빠지게 되면 거부합니다.
// 메인에서 뗄 때는 home[panelId] 에 원래 리프의 동료 탭을 남겨, 떠 있는 창을 닫은 뒤 Window 메뉴로
// 다시 열면 메인의 원래 자리 근처로 오게 합니다. srcEmptied 는 원래 트리가 비었는지입니다(떠 있는
// 창에서 뗀 경우만 참이 될 수 있고, 그 창을 지우는 일은 호출한 쪽이 합니다).
bool dockMoveToFloat(std::unique_ptr<DockNode>& srcRoot, bool srcIsMain, DockNode* src, int srcIdx,
                     const std::string& panelId, bool floatable, DockPanelHome& home,
                     DockFloat& out, bool& srcEmptied);

// 한 트리의 탭을 다른 트리의 리프로 옮깁니다(가운데 = 탭, 가장자리 = 분할). 두 트리가 같으면
// dockRedock 과 같습니다. 떠 있는 창으로 옮기는데 floatable=false 면, 또는 메인에서 Viewport 가
// 빠지게 되면 거부합니다. 원래 트리가 비면 srcEmptied 가 참이고, 그 창은 호출한 쪽이 지웁니다.
bool dockRedockAcross(std::unique_ptr<DockNode>& srcRoot, bool srcIsMain, DockNode* src, int srcIdx,
                      const std::string& panelId,
                      std::unique_ptr<DockNode>& dstRoot, bool dstIsMain, DockNode* target,
                      EDockZone zone, bool floatable, bool& srcEmptied);

// 떠 있는 창 하나를 통째로 다른 트리의 리프에 합칩니다. 가운데는 모든 탭을 target 의 탭으로
// (트리 순서, 활성 탭은 창의 첫 리프의 활성 탭), 가장자리는 창의 트리를 그대로 분할로 붙입니다.
// 성공하면 fl.root 가 비고, 그 창을 목록에서 지우는 일은 호출한 쪽이 합니다. 떠 있는 창으로
// 합칠 때 floatable 이 거짓인 패널이 있으면 거부합니다. target 이 fl 자신의 트리면 거부합니다.
bool dockMergeFloatInto(DockFloat& fl, std::unique_ptr<DockNode>& dstRoot, bool dstIsMain,
                        DockNode* target, EDockZone zone, const DockPanelPredicate& floatable);

// 떠 있는 창을 닫습니다(결정 D4: 안의 탭을 모두 닫음). closable 이 거짓인 패널이 있으면 아무것도
// 하지 않고 거부합니다. 닫은 패널마다 home 에 [예전 기록(메인에서 뗄 때 남긴 동료) → 같은 창의
// 다른 패널] 순으로 남겨, dockShowPanel 이 메인의 원래 자리 근처에 다시 열고 같은 창에 있던
// 패널끼리는 다시 모이게 합니다. 닫은 패널 id 를 트리 순서로 closed 에 넣고, fl.root 를 비웁니다.
bool dockCloseFloat(DockFloat& fl, DockPanelHome& home, const DockPanelPredicate& closable,
                    std::vector<std::string>* closed = nullptr);

// 떠 있는 창 안의 탭 하나를 닫습니다(탭의 x). 마지막 탭이면 창 트리가 비고 emptied 가 참입니다.
// home 에는 [예전 기록 → 같은 리프의 다른 탭] 순으로 남깁니다(dockClosePanel 과 같되 예전 기록이 먼저).
bool dockClosePanelInFloat(DockFloat& fl, DockPanelHome& home, const std::string& id,
                           bool closable, bool& emptied);

// 배치 파일 전체(메인 + 떠 있는 창). main 이 null 이면 거부된 파일입니다.
struct DockLayoutData {
    std::unique_ptr<DockNode> main;
    std::vector<DockFloat>    floats;
};

// 결정 D9: 머리말 · 메인 트리는 v1 그대로, 떠 있는 창이 있을 때만 그 뒤에 `# floating windows`
// 주석 줄과 창마다 `F <번호> <x> <y> <w> <h> <maximized>` 줄 + 그 창의 트리를 적습니다.
// 떠 있는 창이 없으면 dockSerializeLayout(root) 와 같은 글입니다.
std::string    dockSerializeLayout(const DockNode* mainRoot, const std::vector<DockFloat>& floats);
// 새 파서. 메인 트리는 dockParseLayout 과 같은 규칙(메인에 Viewport 가 없으면 전체 거부)이고,
// 모든 트리가 dockPrune 의 seen 을 함께 써 중복 패널을 거릅니다(메인이 먼저). 떠 있는 창 안의
// floatable=false 패널은 걸러 내고, 빈 창은 버리며, 크기는 최소 · 최대 크기로 고칩니다.
// 형식이 깨진 F 블록은 그 블록만 버립니다.
DockLayoutData dockParseLayoutAll(const std::string& text, const DockPanelPredicate& valid,
                                  const DockPanelPredicate& floatable);
