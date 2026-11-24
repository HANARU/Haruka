#pragma once
#include "EditorUiKit.hpp"
#include "EditorMenuModel.hpp"
#include "TextInputController.hpp"
#include "PropertyTextEdit.hpp"
#include "ColorPickerPopup.hpp"
#include "../scene/Reflect.hpp"
#include "../ui/slate/HSlate.hpp"
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ── 속성 행 도구 (PS1a, 2026-11-24, Docs/Project_Settings_Design.md PS-D4) ────────────────────────────────────
// DetailsPanel 과 Project Settings(PS1b) 가 함께 쓰는 범용 속성 행 위젯 빌더입니다.
// Actor · Component · World · HCommandStack 에 의존하지 않고, PropDesc 와 콜백만으로 동작합니다.
class PropertyRowKit {
public:
    struct RowOps {
        std::string                              label;      // 행 이름(덤프 · 시험이 찾는 이름)
        PropDesc::Kind                           kind = PropDesc::Float;
        std::function<bool()>                    canReset;   // 기본값과 다름(화살표 보임 · 메뉴 켜짐)
        std::function<void()>                    reset;      // 기본값으로
        std::function<std::string()>             copyText;   // 값을 글로
        std::function<bool(const std::string&)> paste;      // 글을 값으로, 모양이 틀리면 거짓
        std::function<void()>                    openCombo;  // 열거 행: 콤보 열기(덤프)
        std::function<void()>                    beginEdit;  // 글 행: 편집 시작(덤프)
        std::string                              english;    // 영어 원문 이름(속성 검색)
        std::function<bool(const std::string&)> acceptsAsset;   // 에셋 행: 그 경로의 종류를 받는가(끌어 놓기)
        std::function<void(const std::string&)> setAsset;       // 에셋 행: 값 넣기
        std::function<void()>                    openPicker;     // 에셋 행: 고르기 팝업 열기(덤프)
        std::function<void(bool)>                openColor;      // 색 행: 색 선택기 열기(덤프)
    };

    struct Deps {
        const EditorUiKit*                ui = nullptr;
        std::shared_ptr<HPropertyColumns> columns;
        std::function<void(const std::shared_ptr<HWidget>&, std::vector<EditorMenuEntry>, int)> openCombo;
        std::function<bool(const HWidget*)> isComboOpen;
        std::function<void()>               closeMenu;
        std::function<void(const std::shared_ptr<HWidget>&)> focusText;
        std::function<void(const std::shared_ptr<HWidget>&, const std::string&, const std::string&,
                           std::function<void(const std::string&)>)> openAssetPicker;
        std::function<void(const std::string&)> browseToAsset;
        std::function<void(const std::shared_ptr<HWidget>&, ColorPickerPopup::Target)> openColorPicker;
        std::function<void(const std::string&, uint64_t, std::function<void(uint64_t)>)> pickActor;
        std::function<std::string(uint64_t)> actorLabel;
        std::function<void()>                onChanged;
    };

    // buildRow 에 넘기는 행별 선택적 확장 콜백(비우면 PropDesc 게터·세터에 직접 씁니다).
    struct RowParams {
        std::string rowKey;
        std::string englishName;
        glm::vec4   nameColor = EditorUiKit::kTextBody;
        float       extraIndent = 0.f;
        bool        readOnly    = false;
        bool        dropHot     = false;
        bool        dropOk      = false;
        std::string tooltip;
        std::shared_ptr<HWidget> trailing;
        std::function<void()> onDragBegin;
        std::function<void()> onDragEnd;
        std::function<void(const std::function<void()>&)> commit;
        std::function<void()>                             onReset;
        std::function<bool(const std::string&)>           onPaste;
        std::function<void(const std::shared_ptr<HWidget>&)> onOpenColor;
        // 다중 선택 등 외부 동기화가 필요한 경우의 값 변경 훅
        std::function<void(float)>               onSetFloat;
        std::function<void(bool)>                onSetBool;
        std::function<void(int)>                 onSetEnum;
        std::function<void(uint64_t)>            onSetActor;
        std::function<void(const std::string&)>  onSetString;
        std::function<void(const std::string&)>  onSetAsset;
        std::function<void(int, float)>          onSetVecComp;
    };

    void init(Deps deps) { deps_ = std::move(deps); }
    const Deps& deps() const { return deps_; }
    float s(float x) const { return deps_.ui ? deps_.ui->s(x) : x; }

    // 프레임 다시 만들기 시작 시 호출합니다(행 히트 목록 초기화 · 글 칸 표시 플래그 초기화).
    void beginRebuild(std::shared_ptr<SVerticalBox> defaultList);

    // 범용 PropDesc 하나로 행 전체(이름 · 값 위젯 · 되돌리기 화살표 · RowOps)를 만들어 넣습니다.
    std::shared_ptr<SPropertyRow> buildRow(const PropDesc& pd, const RowParams& params = {}, SVerticalBox* into = nullptr);

    // 기존 행 틀 + 종류별 값 위젯 빌더(Transform 절 등 특수 행도 직접 쓸 수 있음).
    std::shared_ptr<SPropertyRow> addPropRow(const std::string& name, glm::vec4 nameColor, std::shared_ptr<HWidget> value,
                                             std::shared_ptr<RowOps> ops, SVerticalBox* into = nullptr,
                                             float extraIndent = 0.f, const std::string& tooltip = {});
    std::shared_ptr<HWidget> numberField(std::function<float()> getter, std::function<void(float)> setter, float step,
                                         ENumberUnit unit, glm::vec4 barColor, std::function<void()> bFn,
                                         std::function<void()> eFn);
    std::shared_ptr<SVectorField> vectorField(const PropDesc& pd, std::function<float(int)> get,
                                              std::function<void(int, float)> set, std::function<void()> bFn,
                                              std::function<void()> eFn);
    std::shared_ptr<HWidget> colorField(const PropDesc& pd, std::function<float(int)> get, std::function<void(int, float)> set,
                                        std::function<void()> bFn, std::function<void()> eFn,
                                        std::function<void(const std::shared_ptr<HWidget>&)> openPicker,
                                        const std::shared_ptr<RowOps>& ops);
    std::shared_ptr<HWidget> comboField(const PropDesc& pd, std::function<void(int)> choose, const std::shared_ptr<RowOps>& ops);
    std::shared_ptr<HWidget> actorRefField(const PropDesc& pd, std::function<void(uint64_t)> choose);
    std::shared_ptr<HWidget> checkField(std::function<bool()> getB, std::function<void(bool)> setB);
    std::shared_ptr<HWidget> stringValue(const std::string& key, std::function<std::string()> get,
                                         std::function<void(const std::string&)> commit, const std::shared_ptr<RowOps>& ops);
    std::shared_ptr<HWidget> assetPathValue(const PropDesc& pd, std::function<void(const std::string&)> setPath,
                                            const std::shared_ptr<RowOps>& ops, bool dropHot = false, bool dropOk = false);

    // 글 칸 포커스 관리
    std::shared_ptr<HWidget> stringWidget() const { return strText_; }
    TextInputField stringField();
    bool isStringFocused() const { return str_.focused(); }
    bool isStringShown()   const { return strShown_; }
    void forgetString()    { str_.forget(); }

    // 행 조회(오른쪽 메뉴 · 드롭 · 덤프)
    const std::vector<std::pair<std::weak_ptr<HWidget>, std::shared_ptr<RowOps>>>& rowHits() const { return rowHits_; }
    const RowOps* findRow(const std::string& label) const;
    std::shared_ptr<RowOps> hitTestRow(glm::vec2 p) const;

    static ENumberUnit toNumberUnit(PropUnit u);
    static glm::vec4   themePropColor(glm::vec4 c);

private:
    Deps deps_;
    std::shared_ptr<SVerticalBox> defaultList_;
    std::vector<std::pair<std::weak_ptr<HWidget>, std::shared_ptr<RowOps>>> rowHits_;
    std::shared_ptr<SEditableText> strText_ = HNew(SEditableText)->focusable(true);
    PropertyTextEdit               str_;
    bool                           strShown_ = false;
};
