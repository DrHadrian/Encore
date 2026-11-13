// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/BlockPanel.h"

#include "engine/blocks/PluginBlock.h"

#include "engine/blocks/CabIrBlock.h"
#include "engine/blocks/NamAmpBlock.h"
#include "engine/blocks/SplitBlocks.h"
#include "ui/ChainStrip.h"
#include "ui/kit/Chip.h"
#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreLookAndFeel.h"
#include "ui/theme/Paint.h"

namespace encore
{

using namespace theme;

namespace
{
    bool isOnOffParameter (const Parameter& p)
    {
        const auto& r = p.getSpec().range;
        return juce::approximatelyEqual (r.start, 0.0f) && juce::approximatelyEqual (r.end, 1.0f)
            && juce::approximatelyEqual (r.interval, 1.0f);
    }

    bool isAmp (const Block& b)  { return b.getTypeId() == NamAmpBlock::typeId; }
    bool isMerge (const Block& b) { return b.getTypeId() == MergeBlock::typeId; }

    const juce::String& middot()
    {
        static const juce::String text (juce::CharPointer_UTF8 (" \xc2\xb7 "));   // " · "
        return text;
    }

    /** What the panel's box shows: the amp is the amp; every other block
        (the cab too, when it's the one selected) gets the 600 × 300 box
        with the same control strip. */
    kit::BlockInfo panelInfo (const Block& block)
    {
        auto info = ChainStrip::describe (block);

        if (isAmp (block))
        {
            info.kindLabel = "Amp" + middot() + "NAM capture";
        }
        else
        {
            if (block.getTypeId() == CabIrBlock::typeId)
                info.kindLabel = "Cab" + middot() + "IR";
            else if (isMerge (block))
                info.kindLabel = "Merge" + middot() + "paths A + B";
            else if (auto* plugin = dynamic_cast<const PluginBlock*> (&block))
                info.kindLabel = "Plugin" + middot() + (plugin->getPluginRef().format.isNotEmpty() ? plugin->getPluginRef().format
                                                                                                   : juce::String ("VST3"));

            info.shape = kit::BlockInfo::Shape::pedal;   // panel size: a block box with a strip
        }

        return info;
    }
}

//==============================================================================
/** The gear at design size: the placeholder box, and on its control strip
    the knobs, switches, power LED, group labels and status light. The block
    panel scales it to fit. */
class BlockPanel::Gear final : public juce::Component
{
public:
    explicit Gear (BlockPanel& p)
        : owner (p), box (kit::GearView::Size::panel, panelInfo (p.block)),
          plugin (dynamic_cast<const PluginBlock*> (&p.block))
    {
        setInterceptsMouseClicks (false, true);
        strip.setInterceptsMouseClicks (false, true);

        if (plugin != nullptr)
        {
            // The plugin plate: its own box (painted here), the parameters in a recessed panel.
            setSize (size::pluginPlateW, size::pluginPlateH);
            strip.setBounds (size::pluginPlatePadX, size::pluginPlatePanelTop, size::pluginPlateW - 2 * size::pluginPlatePadX,
                             size::pluginPlatePanelH);
            makePluginTexts();
            addChildComponent (card);
        }
        else
        {
            if (isMerge (p.block))
            {
                // PATH A · PATH B: a box of its own size, its label lower.
                setSize (size::mergePanelW, size::mergePanelH);
                box.setLabelBottom (size::mergeLabelBottom);
            }
            else
            {
                setSize (kit::GearView::idealBounds (kit::GearView::Size::panel, box.getBlock().shape).getWidth(),
                         kit::GearView::idealBounds (kit::GearView::Size::panel, box.getBlock().shape).getHeight());
            }

            box.setBounds (getLocalBounds());
            addAndMakeVisible (box);
            strip.setBounds (box.getControlArea());
        }

        addAndMakeVisible (strip);

        power.setOn (owner.block.isEnabled());
        power.onClick = [this]
        {
            owner.block.setEnabled (power.isOn());
            owner.updateHeader();
            box.setOn (power.isOn());

            if (owner.onEdited != nullptr)
                owner.onEdited();
        };
        // On the plate the LED sits top right, outside the panel. The merge
        // has none: it is never off (path B goes off at the split).
        if (plugin != nullptr)
            addAndMakeVisible (power);
        else if (! isMerge (owner.block))
            strip.addAndMakeVisible (power);
    }

    kit::GearPlaceholder& getBox() noexcept { return box; }
    kit::PowerLed& getPower() noexcept { return power; }
    juce::Component& getStrip() noexcept { return strip; }

    /** Columns: knobs (74), then on the right the switches, and the LED.
        The amp groups them: PREAMP (input) · EQ (bass, mid, treble) ·
        OUTPUT (level match, output, LED). */
    void layOut()
    {
        if (plugin != nullptr)
        {
            layOutPlugin();
            return;
        }

        if (isMerge (owner.block))
        {
            layOutMerge();
            return;
        }

        auto row = strip.getLocalBounds().withTrimmedTop (size::stripPadTop).withTrimmedBottom (size::stripPadBottom)
                       .reduced (size::stripPadX, 0);
        labelsRow = row.removeFromTop (size::groupLabelH);
        row.removeFromTop (size::groupLabelGap);

        std::vector<Control*> knobs, toggles;
        Control* output = nullptr;
        const auto amp = isAmp (owner.block);

        for (auto& c : owner.controls)
        {
            if (c->isSwitch)
                toggles.push_back (c.get());
            else if (amp && c->parameter.getSpec().id == "output")
                output = c.get();
            else
                knobs.push_back (c.get());
        }

        const auto knobH = kit::KnobView::idealBounds().getHeight();
        const auto toggleH = kit::GearToggle::idealBounds().getHeight();
        auto right = row;

        // Right to left: LED, the output knob (amp), the switches.
        // The LED's halo (54) is wider than its column (40): centred on it, top of the row.
        const auto ledColumn = right.removeFromRight (size::powerColumn);
        power.setBounds (juce::Rectangle<int> (size::ledHalo, size::ledHalo).withCentre ({ ledColumn.getCentreX(), ledColumn.getY() + size::ledHalo / 2 }));

        if (output != nullptr)
            output->knob->setBounds (right.removeFromRight (size::knobColumn).withHeight (knobH));

        for (auto it = toggles.rbegin(); it != toggles.rend(); ++it)
            (*it)->toggle->setBounds (right.removeFromRight (size::toggleColumn).withHeight (toggleH));

        // Knobs from the left; narrower columns when many (the EQ has seven).
        const auto columnW = knobs.empty() ? size::knobColumn
                                           : juce::jmin (size::knobColumn, right.getWidth() / (int) knobs.size());
        auto left = row.withRight (right.getRight());

        for (auto* c : knobs)
            c->knob->setBounds (left.removeFromLeft (columnW).withHeight (knobH));

        // Group labels (amp): PREAMP over the first column, OUTPUT over the
        // last 170 px, EQ between them (10 px in from both).
        groups.clear();

        if (amp && ! knobs.empty())
        {
            const auto first = knobs.front()->knob->getBounds();
            groups.push_back ({ labelsRow.withLeft (first.getX()).withWidth (first.getWidth()).toFloat(), "PREAMP", false });
            const auto outputArea = labelsRow.withLeft (labelsRow.getRight() - size::outputGroupW).toFloat();
            groups.push_back ({ labelsRow.toFloat().withLeft ((float) first.getRight()).withRight (outputArea.getX())
                                    .reduced ((float) size::groupLabelPadX, 0.0f), "EQ", true });
            groups.push_back ({ outputArea, "OUTPUT", true });

            for (auto& gr : groups)
                gr.width = kit::groupLabelWidth (gr.text);
        }

        repaint();
    }

    /** The merge: PATH A (level, pan) and PATH B, two knob columns each,
        centred on the strip; under each group its summary line. */
    void layOutMerge()
    {
        auto row = strip.getLocalBounds().withTrimmedTop (size::stripPadTop).withTrimmedBottom (size::stripPadBottom);
        labelsRow = row.removeFromTop (size::groupLabelH);
        row.removeFromTop (size::groupLabelGap);

        const auto groupW = 2 * size::knobColumn;
        auto x = strip.getWidth() / 2 - (2 * groupW + size::mergeGroupGap) / 2;
        const auto knobH = kit::KnobView::idealBounds().getHeight();
        groups.clear();
        mergeGroups.clear();

        for (const auto* path : { "A", "B" })
        {
            const auto level = juce::String (path) == "A" ? mergeParams::levelA : mergeParams::levelB;
            const auto pan = juce::String (path) == "A" ? mergeParams::panA : mergeParams::panB;
            auto column = x;

            for (const auto* id : { level, pan })
                for (auto& c : owner.controls)
                    if (! c->isSwitch && c->parameter.getSpec().id == id)
                    {
                        c->knob->setBounds (column, row.getY(), size::knobColumn, knobH);
                        column += size::knobColumn;
                    }

            Group gr { labelsRow.withX (x).withWidth (groupW).toFloat(), juce::String ("PATH ") + path, true };
            gr.width = kit::groupLabelWidth (gr.text);
            groups.push_back (gr);
            mergeGroups.push_back (juce::Rectangle<int> (x, 0, groupW, 0));
            x += groupW + size::mergeGroupGap;
        }

        repaint();
    }

    /** "A: 0.0 dB · L 100" under PATH A, the same for B (from refresh). */
    void setMergeLines (const juce::String& a, const juce::String& b)
    {
        if (a == mergeLines[0] && b == mergeLines[1])
            return;

        mergeLines[0] = a;
        mergeLines[1] = b;
        repaint();
    }

    /** The plate's parameters: four to a row, knob (or switch) centred in
        its column; the power LED top right. */
    void layOutPlugin()
    {
        const auto inner = strip.getLocalBounds().reduced (size::pluginPlatePanelPadX, 0).withTrimmedTop (size::pluginPlatePanelPadTop);
        const auto columnW = (float) inner.getWidth() / (float) size::pluginPlateColumns;
        const auto knob = kit::KnobView::idealBounds (true);
        const auto toggle = kit::GearToggle::idealBounds();

        for (size_t i = 0; i < owner.controls.size(); ++i)
        {
            auto& c = *owner.controls[i];
            const auto column = (int) i % size::pluginPlateColumns, row = (int) i / size::pluginPlateColumns;
            const auto x = inner.getX() + juce::roundToInt ((float) column * columnW);
            const auto cell = juce::Rectangle<int> (x, inner.getY() + row * size::pluginPlateRowPitch,
                                                    juce::roundToInt ((float) (column + 1) * columnW) - juce::roundToInt ((float) column * columnW),
                                                    size::pluginPlateRowPitch);

            if (c.isSwitch)
                c.toggle->setBounds (toggle.withCentre ({ cell.getCentreX(), cell.getY() + toggle.getHeight() / 2 }));
            else
                c.knob->setBounds (cell.withHeight (knob.getHeight()));
        }

        power.setBounds (juce::Rectangle<int> (size::ledHalo, size::ledHalo)
                             .withCentre ({ getWidth() - size::pluginPlatePadX - size::powerColumn / 2,
                                            size::pluginPlateNameTop + size::pluginPlateNameH / 2 }));
        layOutCard();
        repaint();
    }

    void makePluginTexts()
    {
        const auto& ref = plugin->getPluginRef();
        formatText = ref.format.isNotEmpty() ? ref.format : juce::String ("VST3");
        formatCaps = paint::caps (type::tagChip, formatText);
        nameText = ref.name.isNotEmpty() ? ref.name : owner.block.getName();

        juce::StringArray maker;

        if (ref.maker.isNotEmpty())
            maker.add (ref.maker);

        if (ref.version.isNotEmpty())
            maker.add ("v" + ref.version);

        makerText = maker.joinIntoString (middot());
        kindCaps = paint::caps (type::gearPanelKind, "Plugin" + middot() + formatText);

        const auto hidden = (int) owner.block.getParameters().size() - size::pluginPanelKnobs;
        moreText = hidden > 0 ? "+ " + juce::String (hidden) + " more in the plugin window" : juce::String();
        nameWidth = textWidth (font (type::plateName), nameText);
    }

    void paint (juce::Graphics& g) override
    {
        if (plugin == nullptr)
            return;

        // The plate: gear box gradient, 1 px border, inner top highlight; the recessed panel.
        if (dimmed)
            g.beginTransparencyLayer (size::pluginPlateDimmed);

        const auto r = getLocalBounds().toFloat();
        const auto radius = (float) size::gearRadius;
        g.setGradientFill (juce::ColourGradient::vertical (paint::c (colour::gearTop), 0.0f, paint::c (colour::gearBot), r.getBottom()));
        g.fillRoundedRectangle (r, radius);
        g.setColour (paint::c (colour::gearHighlight));
        paint::hline (g, radius, 1.0f, r.getWidth() - radius * 2.0f);
        g.setColour (paint::c (colour::gearBorder));
        g.drawRoundedRectangle (r.reduced (0.5f), radius - 0.5f, 1.0f);

        const auto panel = strip.getBounds().toFloat();
        g.setColour (paint::c (colour::gearStrip));
        g.fillRoundedRectangle (panel, (float) size::stripRadius);
        g.setColour (paint::c (colour::gearStripBorder));
        g.drawRoundedRectangle (panel.reduced (0.5f), (float) size::stripRadius - 0.5f, 1.0f);

        // VST3 · name · maker and version, on one line above the panel.
        auto x = (float) size::pluginPlatePadX;
        const auto chipW = kit::chipWidth (formatCaps);
        kit::paintChip (g, { x, (float) size::pluginPlateChipTop, chipW, (float) size::pluginPlateChipH }, formatCaps, kit::ChipStyle::plain);
        x += chipW + (float) size::pluginPlateTextGap;

        const auto right = (float) (getWidth() - size::pluginPlatePadX - size::powerColumn);
        const auto nameW = juce::jmin (nameWidth, right - x);
        paint::text (g, nameText, { x, (float) size::pluginPlateNameTop, nameW, (float) size::pluginPlateNameH }, type::plateName, colour::text);
        x += nameW + (float) size::pluginPlateTextGap;
        paint::text (g, makerText, { x, (float) size::pluginPlateNameTop, juce::jmax (0.0f, right - x), (float) size::pluginPlateNameH },
                     type::plateMaker, colour::textMuted);

        // Under the panel: what isn't here, and what it is.
        const auto foot = juce::Rectangle<float> ((float) size::pluginPlatePadX, (float) size::pluginPlateFootTop,
                                                  r.getWidth() - 2.0f * (float) size::pluginPlatePadX, (float) size::pluginPlateFootH);
        paint::text (g, kindCaps, foot, type::gearPanelKind, colour::gearLabel, juce::Justification::centredRight);
        paint::text (g, moreText, foot, type::plateMore, colour::textMuted);

        if (dimmed)
            g.endTransparencyLayer();
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        const auto origin = strip.getPosition().toFloat();

        for (auto& gr : groups)
            kit::paintGroupLabel (g, gr.area + origin, gr.text, gr.width, gr.rules);

        // The merge: each group's summary under the strip.
        for (size_t i = 0; i < mergeGroups.size() && i < 2; ++i)
        {
            const auto r = juce::Rectangle<float> (origin.x + (float) mergeGroups[i].getX(), origin.y + (float) strip.getHeight() + (float) size::mergeLineGap,
                                                   (float) mergeGroups[i].getWidth(), (float) size::mergeLineH);
            paint::text (g, mergeLines[i], r, type::caption, colour::textTertiary, juce::Justification::centred);
        }

        // Status light (gate open): a small LED and its label, top left of the strip.
        if (owner.statusLit != nullptr)
        {
            auto r = (labelsRow.toFloat() + origin);
            const auto led = r.removeFromLeft ((float) size::statusLed).withSizeKeepingCentre ((float) size::statusLed, (float) size::statusLed);
            kit::paintLed (g, led, owner.statusWasLit, (float) size::statusLed / (float) size::pedalLed);
            r.removeFromLeft ((float) size::rowGap);
            paint::text (g, owner.statusLabelCaps, r, type::gearGroup, colour::gearGroupLabel);
        }
    }

private:
    struct Group
    {
        juce::Rectangle<float> area;
        juce::String text;
        bool rules = false;
        float width = 0.0f;
    };

    BlockPanel& owner;
    kit::GearPlaceholder box;
    const PluginBlock* plugin;
    juce::Component strip;
    kit::PowerLed power;
    juce::String formatText, formatCaps, nameText, makerText, kindCaps, moreText;
    float nameWidth = 0.0f;
    bool dimmed = false;

public:
    kit::FaultCard card;

    /** A plugin with a problem: the plate at 45 % under the card. */
    void setDimmed (bool shouldDim)
    {
        if (shouldDim == dimmed)
            return;

        dimmed = shouldDim;
        const auto alpha = dimmed ? size::pluginPlateDimmed : 1.0f;
        strip.setAlpha (alpha);
        power.setAlpha (alpha);
        card.setVisible (dimmed);
        card.toFront (false);   // over the knobs
        repaint();
    }

    void layOutCard()
    {
        const auto w = size::faultCardW;
        const auto body = juce::Rectangle<int> ((getWidth() - w) / 2, size::faultCardTop, w, card.getBodyHeight (w));
        card.setBounds (kit::FaultCard::boundsFor (body));
    }
    juce::Rectangle<int> labelsRow;
    std::vector<Group> groups;
    std::vector<juce::Rectangle<int>> mergeGroups;   // x and width of PATH A, PATH B
    juce::String mergeLines[2];
};

//==============================================================================
/** The cab beside the amp: its box, "Cab · <IR> (IR)" and what its knobs
    are; a click selects the cab block. */
class BlockPanel::CabColumn final : public juce::Component,
                                    public juce::SettableTooltipClient
{
public:
    CabColumn (BlockPanel& p, const Block& c)
        : owner (p), cab (c), box (kit::GearView::Size::panel, kit::BlockInfo { kit::BlockInfo::Shape::cab, "Cab" + middot() + "IR", c.getName(), {}, c.isEnabled() })
    {
        box.setBounds ((size::cabColumnW - size::cabPanelW) / 2, 0, size::cabPanelW, size::cabPanelH);
        addAndMakeVisible (box);
        setSize (size::cabColumnW, size::cabPanelH + size::cabCaptionGap + size::cabCaptionH + size::cabCaptionGap + size::cabHelpH);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Click to edit the cab");

        juce::StringArray names;

        for (auto& param : cab.getParameters())
            names.add (param->getSpec().name);

        help = names.joinIntoString (middot()) + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x94 ")) + "click the Cab block to edit";
        refresh();
    }

    void refresh()
    {
        const auto file = cab.getLoadedFile();
        const auto text = "Cab" + middot() + (file == juce::File() ? juce::String ("(none)") : file.getFileNameWithoutExtension() + " (IR)");

        if (text != caption)
        {
            caption = text;
            repaint();
        }

        box.setOn (cab.isEnabled());
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().withTrimmedTop ((float) (size::cabPanelH + size::cabCaptionGap));
        paint::text (g, caption, r.removeFromTop ((float) size::cabCaptionH), type::cabCaption, colour::cabCaption, juce::Justification::centred);
        r.removeFromTop ((float) size::cabCaptionGap);
        g.setFont (font (type::bodySmall));
        g.setColour (paint::c (colour::plusTile));
        g.drawFittedText (help, r.toNearestInt(), juce::Justification::centredTop, 2, 1.0f);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() && getLocalBounds().contains (e.getPosition()) && owner.onCabClicked != nullptr)
            owner.onCabClicked();
    }

private:
    BlockPanel& owner;
    const Block& cab;
    kit::GearPlaceholder box;
    juce::String caption, help;
};

//==============================================================================
BlockPanel::BlockPanel (Block& blockToEdit) : block (blockToEdit)
{
    setHeader (headerLabel());
    setHeaderColours (colour::stageHeaderTop, colour::stageHeaderBot);
    setTitle (block.getName());

    // A plugin can have hundreds of parameters: its first few are knobs
    // here, the rest are in its own window.
    const auto* plugin = dynamic_cast<const PluginBlock*> (&block);
    auto shown = 0;

    for (auto& p : block.getParameters())
    {
        if (plugin != nullptr && shown++ >= size::pluginPanelKnobs)
            break;

        auto control = std::make_unique<Control> (*p);
        const auto& spec = p->getSpec();
        auto* param = p.get();

        if (isOnOffParameter (*p))
        {
            // On/off parameters (level match, detect guitar) are switches, not knobs.
            control->isSwitch = true;
            control->toggle = std::make_unique<kit::GearToggle> (spec.name);
            auto& b = *control->toggle;
            b.setToggleState (p->getValue() >= 0.5f, juce::dontSendNotification);
            b.onClick = [this, param, &b]
            {
                param->setValue (b.getToggleState() ? 1.0f : 0.0f);

                if (onEdited != nullptr)
                    onEdited();
            };
        }
        else
        {
            const auto& r = spec.range;
            control->knob = std::make_unique<kit::KnobView>();
            auto& s = *control->knob;
            // The merge's knobs sit under PATH A / PATH B: "Level", "Pan".
            s.setLabel (isMerge (block) ? spec.name.upToLastOccurrenceOf (" ", false, false) : spec.name);
            s.setNormalisableRange ({ (double) r.start, (double) r.end, (double) r.interval, (double) r.skew, r.symmetricSkew });
            s.setValue (p->getValue(), juce::dontSendNotification);
            s.setDoubleClickReturnValue (true, spec.defaultValue);
            s.textFromValueFunction = [param] (double v) { return param->toText ((float) v); };
            s.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue(); };
            s.setPopupDisplayEnabled (true, false, nullptr);   // the value while dragging
            s.onValueChange = [this, param, &s]
            {
                param->setValue ((float) s.getValue());

                if (onEdited != nullptr)
                    onEdited();
            };
            s.setTooltip ("Right-click to assign an expression pedal");
            s.setShowsValue (plugin != nullptr);   // the plugin's own text under its name

            auto& menu = knobMenus.emplace_back (std::make_unique<KnobMenu> (*this, *control));
            s.addMouseListener (menu.get(), false);
        }

        controls.push_back (std::move (control));
    }

    gear = std::make_unique<Gear> (*this);

    for (auto& c : controls)
        gear->getStrip().addAndMakeVisible (c->isSwitch ? static_cast<juce::Component*> (c->toggle.get()) : c->knob.get());

    addAndMakeVisible (*gear);

    if (auto* slot = block.getFileSlot())
    {
        setVariant (loadButton, Variant::small);
        loadButton.setButtonText (slot->buttonText);
        loadButton.onClick = [this] { chooseFile(); };
        addAndMakeVisible (loadButton);
    }
    else if (plugin != nullptr)
    {
        // The header button opens the plugin's own window instead.
        setVariant (loadButton, Variant::small);
        loadButton.setButtonText ("Open plugin window...");
        setButtonIcon (loadButton, Icon::window, (float) size::smallButtonIcon, false);
        loadButton.setTooltip ("The plugin's own controls, in a window of their own");
        loadButton.onClick = [this]
        {
            if (onOpenPluginWindow != nullptr)
                onOpenPluginWindow();
        };
        addAndMakeVisible (loadButton);

        gear->card.getSecondaryButton().onClick = loadButton.onClick;
        gear->card.getPrimaryButton().onClick = [this]
        {
            // Turning it back on may rebuild the patch (and this panel): after the click.
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<BlockPanel> (this)]
            {
                if (safe != nullptr && safe->onTurnPluginBackOn != nullptr)
                    safe->onTurnPluginBackOn();
            });
        };
    }

    refresh();
}

BlockPanel::~BlockPanel()
{
    for (auto& menu : knobMenus)
        if (menu->control.knob != nullptr)
            menu->control.knob->removeMouseListener (menu.get());
}

void BlockPanel::KnobMenu::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() || panel.onExpressionChosen == nullptr)
        return;

    const auto id = control.parameter.getSpec().id;
    const auto current = panel.getExpressionPedal != nullptr ? panel.getExpressionPedal (id) : 0;
    juce::Component::SafePointer<BlockPanel> safe (&panel);

    juce::PopupMenu pedals;

    for (int n = 0; n <= maxExpressionPedals; ++n)
        pedals.addItem (n == 0 ? juce::String ("None") : "Expression " + juce::String (n), true, n == current,
                        [safe, id, n]
        {
            // Rebuilds the patch (and this panel): call after the menu is gone.
            if (safe != nullptr && safe->onExpressionChosen != nullptr)
                safe->onExpressionChosen (id, n);
        });

    juce::PopupMenu menu;
    menu.addSectionHeader (control.parameter.getSpec().name);
    menu.addSubMenu ("Expression pedal (heel = min, toe = max)", pedals);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (control.knob.get()));
}

void BlockPanel::setStatusLight (juce::String text, std::function<bool()> isLit)
{
    statusLabel = std::move (text);
    statusLabelCaps = statusLabel.toUpperCase();
    statusLit = std::move (isLit);
    gear->repaint();
}

void BlockPanel::setCabBeside (const Block* cab)
{
    cabColumn = nullptr;

    if (cab != nullptr)
    {
        cabColumn = std::make_unique<CabColumn> (*this, *cab);
        addAndMakeVisible (*cabColumn);
    }

    resized();
}

//==============================================================================
void BlockPanel::chooseFile()
{
    auto* slot = block.getFileSlot();

    if (slot == nullptr || loading)
        return;

    const auto start = getBrowseFolder != nullptr ? getBrowseFolder() : juce::File();
    chooser = std::make_unique<juce::FileChooser> (slot->buttonText.trimCharactersAtEnd ("."), start, slot->wildcard);

    juce::WeakReference<BlockPanel> weakThis (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [weakThis] (const juce::FileChooser& fc)
                          {
                              if (auto* self = weakThis.get())
                                  if (fc.getResult().existsAsFile())
                                      self->loadFile (fc.getResult());
                          });
}

void BlockPanel::loadFile (const juce::File& file)
{
    setLoading (true);

    juce::WeakReference<BlockPanel> weakThis (this);
    block.loadFileAsync (file, [weakThis, file] (juce::String error)
    {
        auto* self = weakThis.get();

        if (self == nullptr)
            return;

        self->setLoading (false);

        if (error.isNotEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Couldn't load " + file.getFileName(), error);
            return;
        }

        if (self->onFileLoaded != nullptr)
            self->onFileLoaded (file);

        if (self->onEdited != nullptr)
            self->onEdited();
    });
}

void BlockPanel::setLoading (bool isLoading)
{
    loading = isLoading;
    loadButton.setEnabled (! isLoading);
    shownFile.clear();   // force the header to update
    refresh();
}

//==============================================================================
juce::String BlockPanel::headerLabel() const
{
    // "AMP · <model>", "CAB · <IR>", "GATE · Noise gate", "EQ".
    // A plugin: "PLUGIN · VST3 · <name>".
    // The merge: "MERGE · paths A + B".
    const auto kind = dynamic_cast<const PluginBlock*> (&block) != nullptr || isMerge (block) ? panelInfo (block).kindLabel
                                                                                               : ChainStrip::describe (block).kindLabel;

    if (isMerge (block))
        return kind;


    if (block.getFileSlot() != nullptr)
        return kind + middot() + shownFile;

    return block.getName() != kind ? kind + middot() + block.getName() : kind;
}

juce::uint32 BlockPanel::headerDot() const
{
    return shownProblem ? colour::amber : (block.isEnabled() ? colour::healthy : colour::dotOff);
}

void BlockPanel::updateHeader()
{
    setHeader (headerLabel(), shownCpu);
    setHeaderDot (headerDot());
}

void BlockPanel::refresh()
{
    for (auto& c : controls)
    {
        // "EXP 1" under a knob a pedal moves.
        if (! c->isSwitch && getExpressionPedal != nullptr)
        {
            const auto pedal = getExpressionPedal (c->parameter.getSpec().id);

            if (pedal != c->shownPedal)
            {
                c->shownPedal = pedal;
                c->knob->setChip (pedal > 0 ? "EXP " + juce::String (pedal) : juce::String());
            }
        }

        const auto value = c->parameter.getValue();

        if (c->isSwitch)
        {
            const auto on = value >= 0.5f;

            if (c->toggle->getToggleState() != on)
                c->toggle->setToggleState (on, juce::dontSendNotification);
        }
        else if (! c->knob->isMouseButtonDown() && ! juce::approximatelyEqual ((float) c->knob->getValue(), value))
        {
            c->knob->setValue (value, juce::dontSendNotification);
        }
    }

    if (gear->getPower().isOn() != block.isEnabled())
    {
        gear->getPower().setOn (block.isEnabled());
        gear->getBox().setOn (block.isEnabled());
    }

    auto cpuText = block.isEnabled() ? juce::String (block.getCpuLoad() * 100.0, 1) + "% CPU" : juce::String();

    if (block.getFileSlot() != nullptr)
    {
        const auto file = block.getLoadedFile();
        const auto name = loading ? juce::String ("Loading...")
                                  : (file == juce::File() ? juce::String ("(none)") : file.getFileNameWithoutExtension());
        const auto info = loading ? juce::String() : block.getLoadedFileInfo();

        if (name != shownFile || info != shownInfo)
        {
            shownFile = name;
            shownInfo = info;

            // Until the inspector shows them: the file and its info on the button.
            loadButton.setTooltip (file == juce::File() ? info : file.getFullPathName() + (info.isNotEmpty() ? "\n" + info : juce::String()));
            shownCpu = cpuText;
            updateHeader();
        }
    }

    if (auto* plugin = dynamic_cast<PluginBlock*> (&block))
    {
        // Not loaded, or switched off by the crash guard: an amber dot, and
        // the window can't be opened.
        const auto problem = ! plugin->isLoaded() || plugin->getFault() != PluginBlock::Fault::none;
        loadButton.setEnabled (plugin->getInstance() != nullptr);   // never hasEditor() (see Inspector)

        if (problem)
            cpuText.clear();

        updateFaultCard();

        if (problem != shownProblem)
        {
            shownProblem = problem;
            shownCpu = cpuText;
            updateHeader();
        }
    }

    if (cpuText != shownCpu)
    {
        shownCpu = cpuText;
        updateHeader();
    }
    else
    {
        setHeaderDot (headerDot());
    }

    if (cabColumn != nullptr)
        cabColumn->refresh();

    // The merge's summary lines, when a value moved.
    if (isMerge (block))
    {
        const auto* la = block.findParameter (mergeParams::levelA);
        const auto* pa = block.findParameter (mergeParams::panA);
        const auto* lb = block.findParameter (mergeParams::levelB);
        const auto* pb = block.findParameter (mergeParams::panB);

        if (la != nullptr && pa != nullptr && lb != nullptr && pb != nullptr)
        {
            const std::array<float, 4> values { la->getValue(), pa->getValue(), lb->getValue(), pb->getValue() };

            if (values != shownMerge)
            {
                shownMerge = values;
                gear->setMergeLines ("A: " + la->toText (values[0]) + middot() + pa->toText (values[1]),
                                     "B: " + lb->toText (values[2]) + middot() + pb->toText (values[3]));
            }
        }
    }

    if (statusLit != nullptr)
    {
        const auto lit = block.isEnabled() && statusLit();

        if (lit != statusWasLit)
        {
            statusWasLit = lit;
            gear->repaint();
        }
    }
}

void BlockPanel::updateFaultCard()
{
    auto* plugin = dynamic_cast<PluginBlock*> (&block);

    if (plugin == nullptr)
        return;

    // What the card says, by state; -1 = no problem. The words are the code's facts:
    // the guard's count, and a switched-off plugin passes the dry signal.
    const auto fault = ! plugin->isLoaded() ? 100 : (int) plugin->getFault();
    const auto canOpen = plugin->getInstance() != nullptr;
    const auto key = fault * 2 + (canOpen ? 1 : 0);

    if (key == shownFault)
        return;

    shownFault = key;

    if (fault == (int) PluginBlock::Fault::none)
    {
        gear->setDimmed (false);
        return;
    }

    static const juce::String dry = " The signal passes through it dry, so the show goes on.";
    juce::String lead = "Switched off:", rest, action;

    switch ((PluginBlock::Fault) fault)
    {
        case PluginBlock::Fault::crashed:
            rest = "it crashed.";
            action = "Load again";
            break;
        case PluginBlock::Fault::badOutput:
            rest = "it sent broken audio.";
            break;
        case PluginBlock::Fault::tooSlow:
            rest = "it took longer than the audio buffer " + juce::String (PluginBlock::overrunsBeforeSwitchingOff) + " times in a row.";
            break;
        case PluginBlock::Fault::crashedLastTime:
            rest = "it crashed Encore last time.";
            break;
        case PluginBlock::Fault::none:
            break;
    }

    if (fault == 100)
    {
        lead = "Not loaded:";
        rest = plugin->getLoadError().isNotEmpty() ? plugin->getLoadError() : juce::String ("it couldn't be loaded");

        if (! rest.endsWithChar ('.'))
            rest << ".";
    }
    else if (action.isEmpty() && plugin->canTurnBackOn())
    {
        action = "Turn back on";
    }

    gear->card.setMessage (lead, rest + dry);
    gear->card.setActions (action, canOpen ? juce::String ("Open plugin window...") : juce::String());
    gear->layOutCard();
    gear->setDimmed (true);
}

//==============================================================================
void BlockPanel::paint (juce::Graphics& g)
{
    Pane::paint (g);

    // The lit stage (kit::StageBackdrop).
    backdrop.paint (g, getContentBounds());
}

void BlockPanel::resized()
{
    Pane::resized();


    // Header: "6.9% CPU", then the file button (a plugin: its window) on the right.
    auto header = getHeaderBounds().withTrimmedRight (size::paneHeaderPadR);

    if (loadButton.isVisible())
    {
        const auto w = loadButton.getBestWidthForHeight (size::buttonSmallHeight);
        loadButton.setBounds (header.removeFromRight (w).withTrimmedTop ((header.getHeight() - size::buttonSmallHeight) / 2)
                                                        .withHeight (size::buttonSmallHeight + size::buttonDrop));
        setHeaderRightInset (w + size::outGap);
    }

    layOutGear();
}

void BlockPanel::layOutGear()
{
    gear->layOut();

    // Centred, standing on the bottom (padding 6 12 14), the cab 22 px to
    // the right; scaled down (never up) to fit.
    const auto area = getContentBounds().withTrimmedTop (size::stagePadTop).withTrimmedBottom (size::stagePadBottom)
                                        .reduced (size::stagePadX, 0);
    const auto groupW = gear->getWidth() + (cabColumn != nullptr ? size::cabGap + cabColumn->getWidth() : 0);
    const auto groupH = juce::jmax (gear->getHeight(), cabColumn != nullptr ? cabColumn->getHeight() : 0);

    gearScale = area.isEmpty() ? 0.0f : juce::jmin (1.0f, (float) area.getWidth() / (float) groupW, (float) area.getHeight() / (float) groupH);
    gearScale = juce::jmax (0.01f, gearScale);

    const auto left = (float) area.getX() + ((float) area.getWidth() - (float) groupW * gearScale) * 0.5f;
    auto bottom = (float) area.getBottom();

    // The plugin plate hangs 22 px under the stage header instead (as on the board).
    if (dynamic_cast<const PluginBlock*> (&block) != nullptr)
        bottom = juce::jmin (bottom, (float) (getContentBounds().getY() + size::pluginPlateTop) + (float) gear->getHeight() * gearScale);

    // So does the merge's box, 40 px under it.
    if (isMerge (block))
        bottom = juce::jmin (bottom, (float) (getContentBounds().getY() + size::mergePanelTop) + (float) gear->getHeight() * gearScale);

    gear->setTransform (juce::AffineTransform::scale (gearScale)
                            .translated (left, bottom - (float) gear->getHeight() * gearScale));

    if (cabColumn != nullptr)
        cabColumn->setTransform (juce::AffineTransform::scale (gearScale)
                                     .translated (left + (float) (gear->getWidth() + size::cabGap) * gearScale,
                                                  bottom - (float) cabColumn->getHeight() * gearScale));
}

} // namespace encore
