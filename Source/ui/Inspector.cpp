// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/Inspector.h"

#include "engine/blocks/PluginBlock.h"
#include "model/MidiMapping.h"
#include "session/PatchBuilder.h"
#include "ui/ChainStrip.h"
#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreLookAndFeel.h"
#include "ui/theme/Paint.h"

namespace encore
{

using namespace theme;

namespace
{
    bool isOnOff (const Parameter& p)
    {
        const auto& r = p.getSpec().range;
        return juce::approximatelyEqual (r.start, 0.0f) && juce::approximatelyEqual (r.end, 1.0f)
            && juce::approximatelyEqual (r.interval, 1.0f);
    }

    /** Values as displays show them: a real minus sign. */
    juce::String display (const juce::String& text)
    {
        return text.startsWithChar ('-') ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + text.substring (1) : text;
    }

    juce::String signedDisplay (const Parameter& p, float v)
    {
        // "+12 dB" for a toe above zero, as the board writes ranges.
        auto text = display (p.toText (v));
        return v > 0.0f && p.getSpec().range.start < 0.0f ? "+" + text : text;
    }

    const juce::String& middot()
    {
        static const juce::String text (juce::CharPointer_UTF8 (" \xc2\xb7 "));
        return text;
    }

    juce::String typeName (const Block& block)
    {
        if (block.getTypeId() == PluginBlock::typeId)
            return "Plugin";

        for (auto& t : getBlockTypes())
            if (t.id == block.getTypeId())
                return t.displayName;

        return block.getName();
    }
}

//==============================================================================
class Inspector::Content final : public juce::Component
{
public:
    explicit Content (Inspector& o) : owner (o)
    {
        setVariant (modelField, Variant::field);
        modelField.setFont (font (type::bodySmall));
        modelField.setMinimumHorizontalScale (1.0f);
        addChildComponent (modelField);

        setVariant (stompBox, Variant::field);
        stompBox.setTitle ("Stomp switch");
        stompBox.addItem ("None", 1);

        for (int n = 1; n <= maxStomps; ++n)
            stompBox.addItem ("Stomp " + juce::String (n), n + 1);

        stompBox.onChange = [this]
        {
            if (updating || owner.blockIndex < 0)
                return;

            owner.session.setBlockStomp (owner.blockIndex, stompBox.getSelectedId() - 1);

            if (owner.onEdited != nullptr)
                owner.onEdited();
        };
        addChildComponent (stompBox);

        // Plugin blocks: its window, and a way back after the crash guard switched it off.
        for (auto* b : { &openWindowButton, &pluginActionButton })
        {
            setVariant (*b, Variant::small);
            b->setWantsKeyboardFocus (false);
            addChildComponent (b);
        }

        openWindowButton.setButtonText ("Open plugin window...");
        openWindowButton.onClick = [this]
        {
            if (owner.onOpenPluginWindow != nullptr)
                owner.onOpenPluginWindow();
        };

        pluginActionButton.onClick = [this]
        {
            // Turning it back on may rebuild the patch (and this inspector): after the click.
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Content> (this), index = owner.blockIndex]
            {
                if (safe != nullptr)
                    safe->owner.session.turnPluginBackOn (index);
            });
        };
    }

    void setBlock (Block* newBlock)
    {
        block = newBlock;
        plugin = dynamic_cast<PluginBlock*> (block);
        switches.clear();
        values.clear();
        params.clear();
        onOff.clear();
        hiddenParams = 0;
        pluginStatus.clear();
        pluginAction.clear();

        if (block != nullptr)
        {
            for (auto& p : block->getParameters())
            {
                if (plugin != nullptr && (int) params.size() >= size::inspectorPluginParams)
                {
                    ++hiddenParams;
                    continue;
                }

                if (isOnOff (*p))
                {
                    onOff.push_back (p.get());
                    auto* b = switches.add (new juce::ToggleButton ("On"));
                    b->setTitle (p->getSpec().name);
                    b->setWantsKeyboardFocus (false);
                    b->setToggleState (p->getValue() >= 0.5f, juce::dontSendNotification);
                    b->onClick = [this, b, param = p.get()]
                    {
                        param->setValue (b->getToggleState() ? 1.0f : 0.0f);

                        if (owner.onEdited != nullptr)
                            owner.onEdited();
                    };
                    addAndMakeVisible (b);
                }
                else
                {
                    auto* param = p.get();
                    params.push_back ({ param, {}, -1, -1.0f });
                    auto* l = values.add (new juce::Label());
                    l->setFont (font (type::body));
                    l->setJustificationType (juce::Justification::centredRight);
                    l->setColour (juce::Label::textColourId, paint::c (colour::textRow));
                    l->setBorderSize ({});

                    // Typed values are in the parameter's units; a plugin's are its own text.
                    if (plugin == nullptr)
                    {
                        l->setEditable (false, true, false);   // double-click to type an exact value
                        l->setTooltip ("Double-click to type a value");
                    }

                    l->setTitle (param->getSpec().name);
                    l->onTextChange = [this, l, param]
                    {
                        const auto typed = l->getText().replace (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")), "-")
                                                       .retainCharacters ("-+.0123456789").getFloatValue();
                        param->setValue (param->getSpec().range.snapToLegalValue (typed));

                        if (owner.onEdited != nullptr)
                            owner.onEdited();

                        refresh (true);
                    };
                    addAndMakeVisible (l);
                }
            }
        }

        modelField.setVisible (block != nullptr && block->getFileSlot() != nullptr);
        stompBox.setVisible (block != nullptr);
        openWindowButton.setVisible (plugin != nullptr);
        refresh (true);
    }

    /** Re-reads everything; repaints and re-lays out only when something changed. */
    void refresh (bool force = false)
    {
        auto relayout = force;
        auto changed = force;

        // Block section.
        auto newModel = juce::String(), newModelTip = juce::String(), newFormat = juce::String();

        if (block != nullptr && block->getFileSlot() != nullptr)
        {
            const auto file = block->getLoadedFile();
            newModel = file == juce::File() ? juce::String ("(none)") : file.getFileNameWithoutExtension();
            newModelTip = file.getFullPathName();
            newFormat = display (block->getLoadedFileInfo());
        }

        if (plugin != nullptr)
        {
            const auto& ref = plugin->getPluginRef();
            newModel = ref.name + (ref.maker.isNotEmpty() ? middot() + ref.maker : juce::String());
            newFormat = plugin->isLoaded() ? display (plugin->getLoadedFileInfo()) : ref.format;

            const auto fault = plugin->getFault();
            const auto status = ! plugin->isLoaded() ? (plugin->getLoadError().isNotEmpty() ? plugin->getLoadError() : juce::String ("Not loaded"))
                              : fault != PluginBlock::Fault::none ? plugin->getFaultText()
                                                                  : juce::String ("Running");
            const auto action = fault == PluginBlock::Fault::crashed ? juce::String ("Load again")
                              : plugin->canTurnBackOn()               ? juce::String ("Turn back on")
                                                                      : juce::String();
            const auto canOpen = plugin->getInstance() != nullptr && plugin->getInstance()->hasEditor();

            if (status != pluginStatus || action != pluginAction || canOpen != openWindowButton.isEnabled())
            {
                pluginStatus = status;
                pluginAction = action;
                pluginProblem = ! plugin->isLoaded() || fault != PluginBlock::Fault::none;
                pluginActionButton.setButtonText (action);
                pluginActionButton.setVisible (action.isNotEmpty());
                openWindowButton.setEnabled (canOpen);
                relayout = true;
            }

            if (newModel != pluginName || newFormat != format)
            {
                pluginName = newModel;
                format = newFormat;
                relayout = true;
            }
        }
        else if (newModel != modelField.getText() || newFormat != format)
        {
            modelField.setText (newModel, juce::dontSendNotification);
            modelField.setTooltip (newModelTip);
            format = newFormat;
            relayout = true;
        }

        if (block != nullptr && owner.blockIndex >= 0)
        {
            const juce::ScopedValueSetter<bool> guard (updating, true);
            const auto stomp = owner.session.getBlockStomp (owner.blockIndex);

            if (stompBox.getSelectedId() != stomp + 1)
                stompBox.setSelectedId (stomp + 1, juce::dontSendNotification);
        }

        for (size_t i = 0; i < onOff.size() && (int) i < switches.size(); ++i)
            if (const auto on = onOff[i]->getValue() >= 0.5f; switches[(int) i]->getToggleState() != on)
                switches[(int) i]->setToggleState (on, juce::dontSendNotification);

        // Parameters: value text, bar, pedal.
        for (size_t i = 0; i < params.size(); ++i)
        {
            auto& p = params[i];
            const auto v = p.parameter->getValue();
            const auto pedal = owner.session.getExpressionPedal (owner.blockIndex, p.parameter->getSpec().id);
            const auto proportion = p.parameter->getSpec().range.convertTo0to1 (juce::jlimit (p.parameter->getSpec().range.start,
                                                                                              p.parameter->getSpec().range.end, v));
            auto* label = values[(int) i];

            // Text only when the value moved (a plugin's text comes from the plugin).
            if (force || ! juce::exactlyEqual (v, p.shownValue))
            {
                p.shownValue = v;

                if (const auto text = display (p.parameter->toText (v)); text != label->getText() && ! label->isBeingEdited())
                    label->setText (text, juce::dontSendNotification);
            }

            if (pedal != p.pedal || std::abs (proportion - p.proportion) > 0.002f)
            {
                p.pedal = pedal;
                p.proportion = proportion;
                p.chip = pedal > 0 ? "EXP " + juce::String (pedal) : juce::String();
                if (i < rowBounds.size())
                    repaint (rowBounds[i]);
            }
        }

        // Assignments of the patch: stomp switches and pedals.
        juce::StringArray newAssignments;

        if (owner.chain != nullptr)
        {
            const auto& blocks = owner.chain->getBlocks();

            for (int b = 0; b < (int) blocks.size(); ++b)
                if (const auto stomp = owner.session.getBlockStomp (b); stomp > 0)
                    newAssignments.add ("Stomp " + juce::String (stomp) + "\t" + blocks[(size_t) b]->getName() + " on/off");

            for (int b = 0; b < (int) blocks.size(); ++b)
                for (auto& p : blocks[(size_t) b]->getParameters())
                    if (const auto pedal = owner.session.getExpressionPedal (b, p->getSpec().id); pedal > 0)
                    {
                        const auto& r = p->getSpec().range;
                        const auto name = b == owner.blockIndex ? p->getSpec().name : blocks[(size_t) b]->getName() + " " + p->getSpec().name;
                        newAssignments.add ("EXP " + juce::String (pedal) + "\t" + name + middot()
                                            + "heel " + signedDisplay (*p, r.start) + ", toe " + signedDisplay (*p, r.end));
                    }
        }

        if (newAssignments != assignments)
        {
            assignments = newAssignments;
            relayout = true;
        }

        if (relayout)
            layOut();
        else if (changed)
            repaint();
    }

    /** Lays out for the current width and sets the height it needs. */
    void layOut()
    {
        const auto w = getWidth();
        auto y = 0;

        // BLOCK
        blockHeader = { 0, y, w, size::paneHeader };
        y += size::paneHeader + size::inspectorPadTop;

        const auto valueX = size::inspectorPadX + size::inspectorKeyW + size::inspectorColGap;
        const auto valueW = juce::jmax (0, w - valueX - size::inspectorPadX);
        const auto row = [&] (int h) { const auto r = juce::Rectangle<int> (valueX, y, valueW, h); y += h + size::inspectorRowGap; return r; };
        keys.clear();

        if (block != nullptr)
        {
            typeText = typeName (*block);
            keys.push_back ({ "Type", row (size::inspectorTextH) });

            if (plugin != nullptr)
            {
                const auto lines = [&] (const juce::String& text) { return textWidth (font (type::bodySmall), text) > (float) valueW ? 2 : 1; };
                keys.push_back ({ "Plugin", row (size::inspectorTextH * lines (pluginName)) });
                keys.push_back ({ "Format", row (size::inspectorTextH * lines (format)) });
                keys.push_back ({ "Status", row (size::inspectorTextH * lines (pluginStatus)) });

                if (pluginActionButton.isVisible())
                    pluginActionButton.setBounds (row (size::buttonSmallHeight).withWidth (pluginActionButton.getBestWidthForHeight (size::buttonSmallHeight)));

                openWindowButton.setBounds (row (size::buttonSmallHeight).withWidth (openWindowButton.getBestWidthForHeight (size::buttonSmallHeight)));
            }
            else if (block->getFileSlot() != nullptr)
            {
                keys.push_back ({ "Model", row (size::fieldHeight) });
                modelField.setBounds (keys.back().second);

                const auto twoLines = textWidth (font (type::bodySmall), format) > (float) valueW;
                keys.push_back ({ "Format", row (twoLines ? size::inspectorTextH * 2 : size::inspectorTextH) });
            }

            keys.push_back ({ "Stomp switch", row (size::fieldHeight) });
            stompBox.setBounds (keys.back().second);

            for (int i = 0; i < switches.size() && i < (int) onOff.size(); ++i)
            {
                keys.push_back ({ onOff[(size_t) i]->getSpec().name, row (size::inspectorTextH + 1) });
                switches[i]->setBounds (keys.back().second);
            }
        }

        y += size::inspectorPadBottom - size::inspectorRowGap;

        // PARAMETERS
        paramsHeader = { 0, y, w, size::paneHeader + 1 };
        y += size::paneHeader + 1 + size::listPad;
        rowBounds.clear();

        for (size_t i = 0; i < params.size(); ++i)
        {
            rowBounds.push_back ({ size::inspectorListPadX, y, w - 2 * size::inspectorListPadX, size::rowHeight });
            const auto r = rowBounds.back().reduced (size::listPad, 0);
            values[(int) i]->setBounds (r.withX (r.getRight() - size::inspectorChipW - size::inspectorRowGap - size::meterValueW)
                                         .withWidth (size::meterValueW));
            y += size::rowHeight;
        }

        if (hiddenParams > 0)
        {
            moreParams = { size::inspectorListPadX + size::listPad, y, w - 2 * (size::inspectorListPadX + size::listPad), size::rowHeight };
            y += size::rowHeight;
        }

        y += size::inspectorPadBottom - size::listPad;

        // ASSIGNMENTS
        assignHeader = { 0, y, w, size::paneHeader + 1 };
        y += size::paneHeader + 1 + size::listPad;
        assignBounds.clear();

        for (auto& a : assignments)
        {
            const auto textW = w - 2 * size::inspectorListPadX - 2 * size::rowPadX - size::assignKeyW - 2 * size::rowGap - size::assignArrowW;
            const auto lines = textWidth (font (type::body), a.fromFirstOccurrenceOf ("\t", false, false)) > (float) textW ? 2 : 1;
            const auto h = lines == 1 ? size::rowHeight : size::rowHeight + size::inspectorTextH;
            assignBounds.push_back ({ size::inspectorListPadX, y, w - 2 * size::inspectorListPadX, h });
            y += h;
        }

        y += size::inspectorPadBottom;
        setSize (w, y);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        kit::paintSubheader (g, blockHeader, "BLOCK", {}, false);   // the first: no line above

        for (auto& [key, r] : keys)
        {
            const auto keyArea = juce::Rectangle<int> (size::inspectorPadX, r.getY(), size::inspectorKeyW, juce::jmin (r.getHeight(), size::fieldHeight));
            paint::text (g, key, keyArea.toFloat(), type::label, colour::textTertiary);

            if (key == "Type")
                paint::text (g, typeText, r.toFloat(), type::bodySmall, colour::text);
            else if (key == "Format" || key == "Plugin" || key == "Status")
            {
                const auto& text = key == "Format" ? format : key == "Plugin" ? pluginName : pluginStatus;
                g.setFont (font (type::bodySmall));
                g.setColour (paint::c (key == "Plugin" ? colour::text
                                       : key == "Status" && pluginProblem ? colour::amber
                                                                          : colour::textSoft));
                g.drawFittedText (text, r, juce::Justification::centredLeft, 2, 1.0f);
            }
        }

        kit::paintSubheader (g, paramsHeader, "PARAMETERS", juce::String (juce::CharPointer_UTF8 ("value \xc2\xb7 pedal")));

        for (size_t i = 0; i < params.size() && i < rowBounds.size(); ++i)
        {
            auto r = rowBounds[i].reduced (size::listPad, 0).toFloat();
            const auto& p = params[i];
            paint::text (g, p.parameter->getSpec().name, r.removeFromLeft ((float) size::inspectorNameW), type::body, colour::textSoft);
            r.removeFromLeft ((float) size::inspectorRowGap);

            const auto chip = r.removeFromRight ((float) size::inspectorChipW);
            r.removeFromRight ((float) size::inspectorRowGap + (float) size::meterValueW + (float) size::inspectorRowGap);

            // The bar: #8E8E95, or blue when a pedal moves it.
            const auto bar = r.withSizeKeepingCentre (r.getWidth(), (float) size::inspectorBarH);
            g.setColour (paint::c (colour::meterTrack));
            g.fillRoundedRectangle (bar, (float) size::inspectorBarH * 0.5f);
            g.setColour (paint::c (p.pedal > 0 ? colour::pedalBar : colour::textTertiary));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, p.proportion)), (float) size::inspectorBarH * 0.5f);

            if (p.chip.isNotEmpty())
            {
                const auto c = chip.withSizeKeepingCentre (chip.getWidth(), (float) size::chipH);
                g.setColour (paint::c (colour::expChipFill));
                g.fillRoundedRectangle (c, (float) size::chipRadius);
                paint::text (g, p.chip, c, type::chip, colour::expChipText, juce::Justification::centred);
            }
        }

        if (hiddenParams > 0)
            paint::text (g, "+ " + juce::String (hiddenParams) + " more in the plugin window", moreParams.toFloat(), type::body, colour::textMuted);

        kit::paintSubheader (g, assignHeader, "ASSIGNMENTS", "right-click a knob or block");

        for (size_t i = 0; i < (size_t) assignments.size() && i < assignBounds.size(); ++i)
        {
            auto r = assignBounds[i].reduced (size::rowPadX, 0).toFloat();
            const auto key = assignments[(int) i].upToFirstOccurrenceOf ("\t", false, false);
            const auto text = assignments[(int) i].fromFirstOccurrenceOf ("\t", false, false);

            if (key.isEmpty())
            {
                paint::text (g, text, r, type::body, colour::textMuted);
                continue;
            }

            paint::text (g, key, r.removeFromLeft ((float) size::assignKeyW), type::body, colour::textSoft);
            r.removeFromLeft ((float) size::rowGap);
            paint::text (g, juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")), r.removeFromLeft ((float) size::assignArrowW),
                         type::body, colour::textDisabled);
            r.removeFromLeft ((float) size::rowGap);
            g.setFont (font (type::body));
            g.setColour (paint::c (colour::textRow));
            g.drawFittedText (text, r.toNearestInt(), juce::Justification::centredLeft, 2, 1.0f);
        }
    }

private:
    struct ParamRow
    {
        Parameter* parameter = nullptr;
        juce::String chip;
        int pedal = -1;
        float proportion = -1.0f;
        float shownValue = -1.0f;
    };

    Inspector& owner;
    Block* block = nullptr;
    PluginBlock* plugin = nullptr;
    juce::TextButton openWindowButton, pluginActionButton;
    juce::String pluginName, pluginStatus, pluginAction;
    bool pluginProblem = false;
    int hiddenParams = 0;
    juce::Rectangle<int> moreParams;
    bool updating = false;

    juce::Label modelField;
    juce::ComboBox stompBox;
    juce::OwnedArray<juce::ToggleButton> switches;
    std::vector<Parameter*> onOff;
    juce::OwnedArray<juce::Label> values;
    std::vector<ParamRow> params;

    juce::String typeText, format;
    juce::StringArray assignments;
    juce::Rectangle<int> blockHeader, paramsHeader, assignHeader;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> keys;
    std::vector<juce::Rectangle<int>> rowBounds, assignBounds;
};

//==============================================================================
Inspector::Inspector (ShowSession& s) : session (s), content (std::make_unique<Content> (*this))
{
    setHeader ("Inspector");
    setTitle ("Inspector");
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (size::scrollbar);
    viewport.setViewedComponent (content.get(), false);
    addAndMakeVisible (viewport);
}

Inspector::~Inspector() = default;

void Inspector::setBlock (Chain* newChain, int index)
{
    chain = newChain;
    blockIndex = chain != nullptr && juce::isPositiveAndBelow (index, (int) chain->getBlocks().size()) ? index : -1;
    auto* block = blockIndex >= 0 ? chain->getBlocks()[(size_t) blockIndex].get() : nullptr;

    setHeader (block != nullptr ? "Inspector" + middot() + ChainStrip::describe (*block).kindLabel : juce::String ("Inspector"));
    shownCpu.clear();
    content->setBlock (block);
    resized();
}

void Inspector::refresh()
{
    if (chain != nullptr && blockIndex >= 0)
    {
        const auto& block = *chain->getBlocks()[(size_t) blockIndex];
        const auto cpu = block.isEnabled() ? juce::String (block.getCpuLoad() * 100.0, 1) + "% CPU" : juce::String();

        if (cpu != shownCpu)
        {
            shownCpu = cpu;
            setHeaderRightText (cpu);
        }
    }

    content->refresh();
}

void Inspector::resized()
{
    Pane::resized();
    viewport.setBounds (getContentBounds());
    content->setSize (viewport.getMaximumVisibleWidth(), content->getHeight());
    content->layOut();
    content->setSize (viewport.getWidth() - (content->getHeight() > viewport.getHeight() ? viewport.getScrollBarThickness() : 0),
                      content->getHeight());
    content->layOut();
}

} // namespace encore
