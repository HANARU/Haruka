#include "PropertyRowKit.hpp"
#include "AssetPickerModel.hpp"
#include "ContentBrowserPanel.hpp"
#include "../core/HText.hpp"
#include "../math/Color.hpp"
#include <algorithm>

namespace {

constexpr glm::vec4 kRed   = EditorTheme::axisX;
constexpr glm::vec4 kGreen = EditorTheme::axisY;
constexpr glm::vec4 kBlue  = EditorTheme::axisZ;

constexpr float kRowIndent = 12.f;
constexpr float kResetW    = 18.f;
constexpr float kRightPad  = 6.f;
constexpr float kNameFont  = 12.f;

glm::vec4 componentColor(int i) {
    switch (i) {
        case 0:  return kRed;
        case 1:  return kGreen;
        case 2:  return kBlue;
        default: return EditorTheme::textSecondary;
    }
}

} // namespace

ENumberUnit PropertyRowKit::toNumberUnit(PropUnit u) {
    switch (u) {
        case PropUnit::Offset: return ENumberUnit::Offset;
        case PropUnit::Size:   return ENumberUnit::Size;
        case PropUnit::Angle:   return ENumberUnit::Angle;
        case PropUnit::Ratio:   return ENumberUnit::Ratio;
        case PropUnit::Integer: return ENumberUnit::Integer;
        default:                return ENumberUnit::Scalar;
    }
}

glm::vec4 PropertyRowKit::themePropColor(glm::vec4 c) {
    if (c == PropColor::Red)     return EditorTheme::axisX;
    if (c == PropColor::Green)   return EditorTheme::axisY;
    if (c == PropColor::Blue)    return EditorTheme::axisZ;
    if (c == PropColor::Neutral) return EditorTheme::textSecondary;
    return c;
}

void PropertyRowKit::beginRebuild(std::shared_ptr<SVerticalBox> defaultList) {
    defaultList_ = std::move(defaultList);
    rowHits_.clear();
    strShown_ = false;
}

std::shared_ptr<SPropertyRow> PropertyRowKit::addPropRow(const std::string& name, glm::vec4 nameColor,
                                                         std::shared_ptr<HWidget> value,
                                                         std::shared_ptr<RowOps> ops, SVerticalBox* into,
                                                         float extraIndent, const std::string& tooltip) {
    auto text = HNew(STextBlock)->fontSize(s(kNameFont))->color(nameColor)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center);
    std::weak_ptr<STextBlock> weakText = text;
    const EditorUiKit* ui = deps_.ui;
    const float font = s(kNameFont);
    text->text(Attr<std::string>(std::function<std::string()>([weakText, ui, name, font]() -> std::string {
        auto t = weakText.lock();
        const float w = t ? t->cachedGeometry().size.x : 0.f;
        return (ui && w > 1.f) ? ui->ellipsizeText(name, w, font) : name;
    })));
    text->tooltip.bind([weakText, ui, name, font, tooltip]() -> std::string {
        auto t = weakText.lock();
        const float w = t ? t->cachedGeometry().size.x : 0.f;
        if (ui && w > 1.f && EditorUiKit::textWidth(name, font) > w) return name;
        return tooltip;
    });

    std::shared_ptr<HWidget> reset;
    if (ops && ops->reset) {
        auto resetFn = ops->reset;
        auto canFn   = ops->canReset;
        auto b = deps_.ui->resetArrow([canFn] { return canFn && canFn(); }, [this, resetFn] {
            resetFn();
            if (deps_.onChanged) deps_.onChanged();
        });
        b->setTooltip(HTEXT("Editor.Details", "Tip.ResetToDefault", "Reset to Default"));
        reset = b;
    }
    auto row = HNew(SPropertyRow)
        ->columns(deps_.columns)->style(EditorUiKit::propertyRowStyle(s(1.f)))
        ->indent(s(kRowIndent + extraIndent))->resetWidth(s(kResetW))->rightPad(s(kRightPad))->valueVPad(s(1.f))->height(s(EditorTheme::rowH))
        ->name(text)->value(std::move(value))->reset(std::move(reset));
    if (ops) rowHits_.push_back({ row, ops });
    SVerticalBox* dst = into ? into : defaultList_.get();
    if (dst) dst->addSlot(row, SVerticalBox::ESizeRule::Fixed, s(EditorTheme::rowH));
    return row;
}

std::shared_ptr<HWidget> PropertyRowKit::numberField(std::function<float()> getter, std::function<void(float)> setter, float step,
                                                     ENumberUnit unit, glm::vec4 barColor, std::function<void()> bFn,
                                                     std::function<void()> eFn) {
    auto f = HNew(SNumberField)
        ->style(EditorUiKit::numberFieldStyle(s(1.f)))->fontSize(s(12.f))
        ->get(std::move(getter))->set(std::move(setter))->step(step)->unit(unit)
        ->onDragBegin(std::move(bFn))->onDragEnd(std::move(eFn))->clipText(true);
    if (barColor.a > 0.f) f->axisBar(barColor, s(3.f));
    return f;
}

std::shared_ptr<SVectorField> PropertyRowKit::vectorField(const PropDesc& pd, std::function<float(int)> get,
                                                          std::function<void(int, float)> set, std::function<void()> bFn,
                                                          std::function<void()> eFn) {
    const int n = std::clamp(pd.components, 1, 4);
    std::vector<std::string> labels;
    std::vector<glm::vec4> colors;
    for (int i = 0; i < n; ++i) { labels.push_back(""); colors.push_back(componentColor(i)); }
    auto v = HNew(SVectorField)
        ->components(n)->style(EditorUiKit::numberFieldStyle(s(1.f)))->fontSize(s(12.f))
        ->gap(s(EditorTheme::space2))->barWidth(s(3.f))->step(pd.step)->unit(toNumberUnit(pd.unit))
        ->labels(std::move(labels))->colors(std::move(colors))
        ->get(std::move(get))->set(std::move(set))
        ->onDragBegin([bFn](int) { if (bFn) bFn(); })
        ->onDragEnd([eFn](int) { if (eFn) eFn(); });
    for (int i = 0; i < n; ++i)
        if (auto f = v->field(i)) f->tooltip.set(hprop::componentName(pd, i));
    return v;
}

std::shared_ptr<HWidget> PropertyRowKit::colorField(const PropDesc& pd, std::function<float(int)> get,
                                                    std::function<void(int, float)> set, std::function<void()> bFn,
                                                    std::function<void()> eFn,
                                                    std::function<void(const std::shared_ptr<HWidget>&)> openPicker,
                                                    const std::shared_ptr<RowOps>& ops) {
    const bool srgb = pd.srgb;
    const int n = std::clamp(pd.components, 3, 4);
    auto swatch = HNew(SColorSwatch)
        ->color([get, srgb, n]() -> glm::vec4 {
            glm::vec4 c{ get(0), get(1), get(2), n > 3 ? get(3) : 1.f };
            if (srgb) { c.r = hcolor::srgbToLinear(c.r); c.g = hcolor::srgbToLinear(c.g); c.b = hcolor::srgbToLinear(c.b); }
            return c;
        })
        ->border(EditorTheme::borderPopup, EditorTheme::borderThin)->backdrop(EditorTheme::bgInput)
        ->cornerRadius(s(EditorTheme::radiusInput))->size({ s(28.f), s(18.f) });
    swatch->setTooltip(HTEXT("Editor.Details", "Tip.ColorPicker", "Open the color picker"));
    std::weak_ptr<SColorSwatch> weak = swatch;
    if (openPicker) {
        swatch->onClick([weak, openPicker] { if (auto w = weak.lock()) openPicker(w); });
        if (ops) ops->openColor = [weak, openPicker](bool) { if (auto w = weak.lock()) openPicker(w); };
    }
    using HR = SHorizontalBox;
    return HNew(SHorizontalBox)
        ->addSlot(swatch, HR::ESizeRule::Fixed, s(28.f))
        ->addSlot(deps_.ui->spacer(), HR::ESizeRule::Fixed, s(EditorTheme::space4))
        ->addSlot(vectorField(pd, std::move(get), std::move(set), std::move(bFn), std::move(eFn)), HR::ESizeRule::Fill);
}

std::shared_ptr<HWidget> PropertyRowKit::comboField(const PropDesc& pd, std::function<void(int)> choose,
                                                    const std::shared_ptr<RowOps>& ops) {
    const int idx = pd.getI ? pd.getI() : -1;
    const std::vector<const char*> opts = pd.options;
    const char* cur = (idx >= 0 && idx < (int)opts.size() && opts[(size_t)idx]) ? opts[(size_t)idx] : "?";
    auto btn = deps_.ui->comboButton(cur, 12.f, {});
    std::weak_ptr<SButton> weak = btn;
    auto getI = pd.getI;
    auto open = [this, weak, opts, getI, choose] {
        auto b = weak.lock();
        if (!b || !deps_.openCombo) return;
        if (deps_.isComboOpen && deps_.isComboOpen(b.get())) { if (deps_.closeMenu) deps_.closeMenu(); return; }
        const int now = getI ? getI() : -1;
        std::vector<EditorMenuEntry> entries;
        for (int i = 0; i < (int)opts.size(); ++i) {
            EditorMenuEntry e = EditorMenuEntry::act(opts[(size_t)i] ? opts[(size_t)i] : "", [choose, i] { choose(i); });
            e.checked = [getI, i] { return getI && getI() == i; };
            entries.push_back(std::move(e));
        }
        deps_.openCombo(b, std::move(entries), now);
    };
    btn->onClick(open);
    if (ops) ops->openCombo = open;
    return btn;
}

std::shared_ptr<HWidget> PropertyRowKit::actorRefField(const PropDesc& pd, std::function<void(uint64_t)> choose) {
    const std::string none    = HTEXT("Editor.Details", "ActorRef.None", "None").toString();
    const std::string missing = HTEXT("Editor.Details", "ActorRef.Missing", "(missing)").toString();
    auto getG = pd.getG;
    const std::string cur   = (deps_.actorLabel && getG) ? deps_.actorLabel(getG()) : none;
    const std::string title = pd.label ? pd.label : "";
    auto b = deps_.ui->textButton(cur, 12.f, EditorUiKit::kTextBody, { 6.f, 0.f }, EditorTheme::ctlNormal, EditorTheme::ctlHover,
                                  [this, title, getG, choose] {
                                      if (!deps_.pickActor || !getG) return;
                                      deps_.pickActor(title, getG(), [this, choose](uint64_t g) {
                                          choose(g);
                                          if (deps_.onChanged) deps_.onChanged();
                                      });
                                  });
    b->hAlign(EHAlign::Left);
    return b;
}

std::shared_ptr<HWidget> PropertyRowKit::checkField(std::function<bool()> getB, std::function<void(bool)> setB) {
    auto box = HNew(SCheckBox)
        ->style(EditorUiKit::checkBoxStyle(s(1.f)))
        ->checked(getB)
        ->onClick([this, getB, setB] {
            setB(!getB());
            if (deps_.onChanged) deps_.onChanged();
        });
    return HNew(SBox)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)->content(box);
}

std::shared_ptr<HWidget> PropertyRowKit::stringValue(const std::string& key, std::function<std::string()> get,
                                                     std::function<void(const std::string&)> commit,
                                                     const std::shared_ptr<RowOps>& ops) {
    auto begin = [this, key, get, commit] {
        str_.prepare(key, get, commit);
        if (deps_.focusText) deps_.focusText(strText_);
    };
    if (ops) ops->beginEdit = begin;
    if (str_.editing(key)) {
        strShown_ = true;
        SEditableText::Style es;
        es.text        = EditorUiKit::kTextBody;
        es.composition = EditorUiKit::kTextBody;
        es.selection   = EditorTheme::accent;
        es.caret       = EditorUiKit::kTextBody;
        es.caretWidth  = std::max(1.f, std::floor(s(1.f)));
        auto edit = HReuse(strText_)->focusable(true)->state(&str_.edit())->focused([this] { return str_.focused(); })
                        ->fontSize(s(12.f))->textHeight(s(16.f))->style(es);
        str_.rebind(get, commit);
        return HNew(SBorder)->background(EditorTheme::bgInput)->borderColor(EditorTheme::accent)
            ->borderThickness(EditorTheme::borderThin)->cornerRadius(s(EditorTheme::radiusInput))
            ->padding(HPadding{ s(5.f), 0.f })->vAlign(EVAlign::Center)->content(edit);
    }
    const std::string now = get ? get() : std::string();
    auto b = HNew(SButton)
        ->style(EditorUiKit::buttonStyle(s(1.f)))->cornerRadius(s(EditorTheme::radiusInput))
        ->normalColor(EditorTheme::bgInput)->hoveredColor(EditorTheme::ctlNormal)->pressedColor(EditorTheme::bgInput)
        ->padding(HPadding{ s(5.f), 0.f })->hAlign(EHAlign::Left)->vAlign(EVAlign::Center)
        ->content(HNew(STextBlock)->text(now)->fontSize(s(12.f))->color(EditorUiKit::kTextBody))
        ->onClick(begin);
    return b;
}

TextInputField PropertyRowKit::stringField() {
    return str_.field([this] { if (str_.focused() != strShown_ || !str_.focused()) { if (deps_.onChanged) deps_.onChanged(); } },
                      [this](glm::vec2 p) { return strShown_ && strText_ && strText_->cachedGeometry().contains(p); });
}

std::shared_ptr<HWidget> PropertyRowKit::assetPathValue(const PropDesc& pd, std::function<void(const std::string&)> setPath,
                                                        const std::shared_ptr<RowOps>& ops, bool dropHot, bool dropOk) {
    const EditorUiKit& u = *deps_.ui;
    const std::string path = pd.getS ? pd.getS() : std::string();
    const std::string none = HTEXT("Editor.Details", "ActorRef.None", "None").toString();
    const hcb::AssetKind kind = path.empty() ? hcb::AssetKind::Other : hcb::kindOfExt(hcb::extOf(hcb::nameOf(path)));
    const std::string assetType = pd.assetType ? pd.assetType : "";
    const std::vector<hcb::AssetKind> kinds = hasset::kindsOf(assetType);
    using HR = SHorizontalBox;

    auto row = HNew(SHorizontalBox);
    if (!path.empty())
        row->addSlot(u.icon(hcbui::kindIcon(kind), 13.f, hcbui::kindColor(kind), hcbui::kindFallback(kind), 10.f),
                     HR::ESizeRule::Fixed, s(17.f), {}, EVAlign::Center);
    row->addSlot(HNew(STextBlock)->text(path.empty() ? none : hasset::displayName(path))->fontSize(s(12.f))
                     ->color(path.empty() ? EditorUiKit::kTextDim : EditorUiKit::kTextBody)->hAlign(EHAlign::Left)->vAlign(EVAlign::Center),
                 HR::ESizeRule::Fill);
    row->addSlot(u.icon("chevron-down", 12.f, EditorUiKit::kTextDim, "v", 11.f), HR::ESizeRule::Auto, 0.f, {}, EVAlign::Center);
    auto btn = HNew(SButton)
        ->style(EditorUiKit::buttonStyle(s(1.f)))->content(row)
        ->padding(HPadding{ s(5.f), 0.f, s(4.f), 0.f })->hAlign(EHAlign::Fill)->vAlign(EVAlign::Fill)
        ->normalColor(EditorTheme::bgInput)->hoveredColor(EditorTheme::ctlNormal)->cornerRadius(s(EditorTheme::radiusInput));
    btn->tooltip.set(path.empty() ? HTEXT("Editor.Details", "Tip.AssetPick", "Choose an asset").toString() : path);
    std::weak_ptr<SButton> weak = btn;
    auto getS = pd.getS;
    auto open = [this, weak, assetType, getS, setPath] {
        auto b = weak.lock();
        if (!b || !deps_.openAssetPicker) return;
        deps_.openAssetPicker(b, assetType, getS ? getS() : std::string(), setPath);
    };
    btn->onClick(open);

    auto browse = u.iconButton("search", 12.f, EditorUiKit::kTextBody, "", 12.f, EditorUiKit::kTextBody, { 3.f, 0.f },
                               EditorTheme::ctlNormal, EditorTheme::ctlHover, [this, getS] {
                                   const std::string p = getS ? getS() : std::string();
                                   if (!p.empty() && deps_.browseToAsset) deps_.browseToAsset(p);
                               }, "o");
    browse->setTooltip(HTEXT("Editor.Details", "Tip.AssetBrowse", "Browse to Asset in Content Browser"));
    browse->enabled.set(!path.empty());
    auto clear = u.iconButton("x", 12.f, EditorUiKit::kTextBody, "", 12.f, EditorUiKit::kTextBody, { 3.f, 0.f },
                              EditorTheme::ctlNormal, EditorTheme::ctlHover, [setPath] { if (setPath) setPath(""); }, "x");
    clear->setTooltip(HTEXT("Editor.Details", "Tip.AssetClear", "Clear"));
    clear->enabled.set(!path.empty());

    if (ops) {
        ops->acceptsAsset = [kinds](const std::string& p) { return hasset::acceptsPath(kinds, p); };
        ops->setAsset     = setPath;
        ops->openPicker   = open;
    }
    auto frame = HNew(SBorder)->background(EditorTheme::transparent)
        ->borderColor(dropHot ? (dropOk ? EditorTheme::accent : EditorTheme::error) : EditorTheme::transparent)
        ->borderThickness(dropHot ? std::max(1.f, s(1.f)) : 0.f)->cornerRadius(s(EditorTheme::radiusInput))
        ->padding(dropHot ? HPadding{ std::max(1.f, s(1.f)), std::max(1.f, s(1.f)) } : HPadding{ 0.f, 0.f })
        ->content(HNew(SHorizontalBox)
            ->addSlot(btn, HR::ESizeRule::Fill)
            ->addSlot(u.spacer(), HR::ESizeRule::Fixed, s(2.f))
            ->addSlot(browse, HR::ESizeRule::Fixed, s(22.f))
            ->addSlot(u.spacer(), HR::ESizeRule::Fixed, s(2.f))
            ->addSlot(clear, HR::ESizeRule::Fixed, s(22.f)));
    return frame;
}

std::shared_ptr<SPropertyRow> PropertyRowKit::buildRow(const PropDesc& pd, const RowParams& params, SVerticalBox* into) {
    const std::string lbl = pd.label ? pd.label : "";
    const PropDesc::Kind kind = pd.kind;
    auto commitFn = params.commit ? params.commit : [](const std::function<void()>& fn) { fn(); };

    auto ops = std::make_shared<RowOps>();
    ops->label   = lbl;
    ops->english = params.englishName.empty() ? lbl : params.englishName;
    ops->kind    = kind;
    if (pd.hasDefault && !params.readOnly) {
        ops->canReset = [pd] { return hprop::canReset(pd); };
        ops->reset    = params.onReset ? params.onReset : [this, pd, onEnd = params.onDragEnd] {
            hprop::resetToDefault(pd);
            if (onEnd) onEnd();
            if (deps_.onChanged) deps_.onChanged();
        };
    }
    ops->copyText = [pd] { return hprop::valueText(pd); };
    if (hprop::writable(pd) && !params.readOnly) {
        ops->paste = params.onPaste ? params.onPaste : [this, pd, commitFn, onEnd = params.onDragEnd](const std::string& t) {
            bool ok = false;
            commitFn([&] { ok = hprop::setFromText(pd, t); });
            if (ok) {
                if (onEnd) onEnd();
                if (deps_.onChanged) deps_.onChanged();
            }
            return ok;
        };
    }

    std::shared_ptr<HWidget> value;
    if (params.readOnly) {
        value = HNew(SBorder)->background(EditorTheme::bgInput)->cornerRadius(s(EditorTheme::radiusInput))
            ->padding(HPadding{ s(5.f), 0.f })->vAlign(EVAlign::Center)
            ->content(HNew(STextBlock)->text(hprop::valueText(pd))->fontSize(s(12.f))->color(EditorUiKit::kTextDim));
    } else switch (kind) {
        case PropDesc::Float: {
            const glm::vec4 bar = pd.color == PropColor::Neutral ? glm::vec4(0.f) : themePropColor(pd.color);
            value = numberField([pd] { return pd.getF ? pd.getF() : 0.f; },
                                [pd, onSet = params.onSetFloat](float v) {
                                    if (onSet) onSet(v);
                                    else if (pd.setF) pd.setF(v);
                                },
                                pd.step, toNumberUnit(pd.unit), bar, params.onDragBegin, params.onDragEnd);
            break;
        }
        case PropDesc::Toggle:
            value = checkField(pd.getB, [pd, commitFn, onSet = params.onSetBool](bool v) {
                commitFn([&] {
                    if (onSet) onSet(v);
                    else if (pd.setB) pd.setB(v);
                });
            });
            break;
        case PropDesc::Enum:
            value = comboField(pd, [this, pd, commitFn, onSet = params.onSetEnum](int v) {
                commitFn([&] {
                    if (onSet) onSet(v);
                    else if (pd.setI) pd.setI(v);
                });
                if (deps_.onChanged) deps_.onChanged();
            }, ops);
            break;
        case PropDesc::ActorRef:
            value = actorRefField(pd, [pd, commitFn, onSet = params.onSetActor](uint64_t v) {
                commitFn([&] {
                    if (onSet) onSet(v);
                    else if (pd.setG) pd.setG(v);
                });
            });
            break;
        case PropDesc::String:
            value = stringValue(params.rowKey.empty() ? lbl : params.rowKey, pd.getS,
                                [pd, commitFn, onSet = params.onSetString](const std::string& v) {
                                    commitFn([&] {
                                        if (onSet) onSet(v);
                                        else if (pd.setS) pd.setS(v);
                                    });
                                }, ops);
            break;
        case PropDesc::AssetPath:
            value = assetPathValue(pd, params.onSetAsset ? params.onSetAsset : [this, pd, commitFn](const std::string& v) {
                commitFn([&] {
                    if (!pd.getS || pd.getS() != v) {
                        if (pd.setS) pd.setS(v);
                    }
                });
                if (deps_.onChanged) deps_.onChanged();
            }, ops, params.dropHot, params.dropOk);
            break;
        case PropDesc::Vec3:
        case PropDesc::Color: {
            auto get = [pd](int i) { return pd.getV ? pd.getV()[std::clamp(i, 0, 3)] : 0.f; };
            auto set = params.onSetVecComp ? params.onSetVecComp : [pd](int i, float v) {
                if (!pd.getV || !pd.setV) return;
                glm::vec4 cur = pd.getV();
                cur[std::clamp(i, 0, 3)] = v;
                pd.setV(cur);
            };
            if (kind == PropDesc::Color) {
                auto openPicker = params.onOpenColor ? params.onOpenColor : [this, pd, set, lbl](const std::shared_ptr<HWidget>& anchor) {
                    if (!deps_.openColorPicker) return;
                    const int n = std::clamp(pd.components, 3, 4);
                    const glm::vec4 before = pd.getV ? pd.getV() : glm::vec4(0.f);
                    ColorPickerPopup::Target t;
                    t.title      = lbl;
                    t.components = n;
                    t.srgb       = pd.srgb;
                    t.get        = [pd] { return pd.getV ? pd.getV() : glm::vec4(0.f); };
                    t.set        = [set, n](const glm::vec4& v) { for (int i = 0; i < n; ++i) set(i, v[i]); };
                    t.alive      = [] { return true; };
                    t.commit     = [this] { if (deps_.onChanged) deps_.onChanged(); };
                    t.cancel     = [this, set, n, before] {
                        for (int i = 0; i < n; ++i) set(i, before[i]);
                        if (deps_.onChanged) deps_.onChanged();
                    };
                    deps_.openColorPicker(anchor, std::move(t));
                };
                value = colorField(pd, get, set, params.onDragBegin, params.onDragEnd, openPicker, ops);
            } else {
                value = vectorField(pd, get, set, params.onDragBegin, params.onDragEnd);
            }
            break;
        }
    }
    if (!value) return nullptr;
    if (params.trailing) {
        using HR = SHorizontalBox;
        value = HNew(HR)
            ->addSlot(std::move(value), HR::ESizeRule::Fill)
            ->addSlot(deps_.ui->spacer(), HR::ESizeRule::Fixed, s(EditorTheme::space4))
            ->addSlot(params.trailing, HR::ESizeRule::Auto);
    }
    return addPropRow(lbl, params.nameColor, std::move(value), std::move(ops), into, params.extraIndent, params.tooltip);
}

const PropertyRowKit::RowOps* PropertyRowKit::findRow(const std::string& label) const {
    for (const auto& [w, ops] : rowHits_)
        if (ops && ops->label == label) return ops.get();
    return nullptr;
}

std::shared_ptr<PropertyRowKit::RowOps> PropertyRowKit::hitTestRow(glm::vec2 p) const {
    for (const auto& [w, ops] : rowHits_) {
        auto row = w.lock();
        if (row && row->cachedGeometry().contains(p)) return ops;
    }
    return nullptr;
}
