// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/RoutingView.h"

#include "engine/dsp/OutputEq.h"
#include "ui/kit/EqCurve.h"
#include "ui/kit/Fader.h"
#include "ui/kit/Group.h"
#include "ui/kit/IconButton.h"
#include "ui/kit/KeyCap.h"
#include "ui/kit/LevelBars.h"
#include "ui/kit/MuteButton.h"
#include "ui/kit/SmallKnob.h"
#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreLookAndFeel.h"
#include "ui/theme/Icons.h"
#include "ui/theme/Paint.h"

namespace encore
{

using namespace theme;

namespace
{
    const juce::String& middot()
    {
        static const juce::String text (juce::CharPointer_UTF8 (" \xc2\xb7 "));
        return text;
    }

    juce::String quoted (const juce::String& s)
    {
        return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x9c")) + s + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x9d"));
    }

    juce::String minus() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")); }

    /** "+1.5 dB", "−2.0 dB", "0.0 dB". */
    juce::String signedDb (float dB)
    {
        const auto r = std::round (dB * 10.0f) / 10.0f;
        const auto text = juce::String (std::abs (r), 1) + " dB";
        return r > 0.0f ? "+" + text : (r < 0.0f ? minus() + text : text);
    }

    juce::String hz (float f)
    {
        return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 0 : 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
    }

    /** "Flat", or what isn't flat: "Low cut 80 Hz · Treble −2.0". */
    juce::String describeEq (const OutputEq& eq)
    {
        if (eq.isFlat())
            return "Flat";

        juce::StringArray parts;

        if (eq.lowCut > OutputEq::minLowCut + 0.5f)    parts.add ("Low cut " + hz (eq.lowCut));
        if (std::abs (eq.bass) >= 0.05f)               parts.add ("Bass " + signedDb (eq.bass).upToFirstOccurrenceOf (" dB", false, false));
        if (std::abs (eq.mid) >= 0.05f)                parts.add ("Mid " + signedDb (eq.mid).upToFirstOccurrenceOf (" dB", false, false));
        if (std::abs (eq.treble) >= 0.05f)             parts.add ("Treble " + signedDb (eq.treble).upToFirstOccurrenceOf (" dB", false, false));
        if (eq.highCut < OutputEq::maxHighCut - 1.0f)  parts.add ("High cut " + hz (eq.highCut));
        return parts.joinIntoString (middot());
    }

    std::vector<float> responseOf (const OutputEq& eq, int points)
    {
        std::vector<float> db ((size_t) points);

        for (int i = 0; i < points; ++i)
            db[(size_t) i] = (float) dsp::outputEq::responseDb (eq, kit::EqCurve::frequencyAt (i, points));

        return db;
    }

    void styleField (juce::TextEditor& e)
    {
        e.setColour (juce::TextEditor::backgroundColourId, paint::c (colour::fieldFill));
        e.setColour (juce::TextEditor::outlineColourId, paint::c (colour::fieldBorder));
        e.setColour (juce::TextEditor::focusedOutlineColourId, paint::c (colour::accent));
        e.setColour (juce::TextEditor::textColourId, paint::c (colour::fieldText));
        e.setColour (juce::TextEditor::highlightColourId, paint::c (colour::textSelection));
        e.setFont (font (type::bodySmall));
        e.setIndents (8, 4);
        e.setJustification (juce::Justification::centredLeft);
        e.setSelectAllWhenFocused (true);
    }

    /** A value in an inset field that can be typed into (double-click). */
    void styleValueLabel (juce::Label& l, juce::Justification j = juce::Justification::centred)
    {
        l.setFont (font (type::bodySmall));
        l.setJustificationType (j);
        l.setColour (juce::Label::textColourId, paint::c (colour::fieldText));
        l.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        l.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l.setColour (juce::TextEditor::backgroundColourId, paint::c (colour::fieldFill));
        l.setColour (juce::TextEditor::textColourId, paint::c (colour::fieldText));
        l.setColour (juce::TextEditor::highlightColourId, paint::c (colour::textSelection));
        l.setEditable (false, true, false);
        l.setBorderSize ({ 0, 4, 0, 4 });
    }

    /** The interface's channel options for an output: stereo pairs, then single
        channels, then every output; enough to show what the routing asks for. */
    std::vector<std::vector<int>> outputChannelOptions (int deviceOutputs, const RoutingOutput& current)
    {
        auto highest = deviceOutputs;

        for (auto ch : current.channels)
            highest = juce::jmax (highest, ch);

        std::vector<std::vector<int>> options;

        for (int ch = 1; ch + 1 <= highest; ch += 2)
            options.push_back ({ ch, ch + 1 });

        for (int ch = 1; ch <= highest; ++ch)
            options.push_back ({ ch });

        if (std::find (options.begin(), options.end(), current.channels) == options.end() && ! current.channels.empty())
            options.push_back (current.channels);

        options.push_back ({});   // every output
        return options;
    }

    juce::String describeOption (const std::vector<int>& channels)
    {
        RoutingOutput o;
        o.channels = channels;
        return describeChannels (o);
    }
}

//==============================================================================
/** Inputs & outputs. */
class RoutingView::IoPage final : public juce::Component
{
public:
    explicit IoPage (RoutingView& o) : owner (o)
    {
        for (auto* g : { &inputsGroup, &interfaceGroup, &namesGroup, &outputsGroup, &detailGroup })
            addAndMakeVisible (g);

        // Heads, bars and texts over the group boxes, under the controls.
        decor.painter = [this] (juce::Graphics& g) { paintDecor (g); };
        decor.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (decor);

        addInputButton.onClick = [this] { addInput ({}); };
        addOutputButton.onClick = [this] { addOutput ({}); };
        setVariant (audioButton, Variant::small);
        audioButton.onClick = [this] { if (owner.onOpenAudioSettings != nullptr) owner.onOpenAudioSettings(); };

        for (auto* b : { &addInputButton, &addOutputButton, &audioButton })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        }

        addInputButton.setTooltip ("A new input on this computer (up to 8)");
        addOutputButton.setTooltip ("A new output on this computer (up to 8)");

        // The selected output's EQ, delay and limiter.
        curve.onClick = [this] { editOutput ([] (RoutingOutput& out) { out.eq = OutputEq(); }); };
        curve.setTitle ("EQ curve");
        curve.setTooltip ("Click to reset the EQ (flat)");
        addAndMakeVisible (curve);

        setUpKnob (lowCut, "Low cut", OutputEq::minLowCut, OutputEq::maxLowCut, false, [] (OutputEq& eq) { return &eq.lowCut; },
                   [] (double v) { return v <= OutputEq::minLowCut + 0.5 ? juce::String ("off") : hz ((float) v); });
        setUpKnob (bass, "Bass", -OutputEq::maxGainDb, OutputEq::maxGainDb, true, [] (OutputEq& eq) { return &eq.bass; },
                   [] (double v) { return signedDb ((float) v); });
        setUpKnob (mid, "Mid", -OutputEq::maxGainDb, OutputEq::maxGainDb, true, [] (OutputEq& eq) { return &eq.mid; },
                   [] (double v) { return signedDb ((float) v); });
        setUpKnob (midFreq, "Mid freq", OutputEq::minMidFreq, OutputEq::maxMidFreq, false, [] (OutputEq& eq) { return &eq.midFreq; },
                   [] (double v) { return hz ((float) v); });
        setUpKnob (treble, "Treble", -OutputEq::maxGainDb, OutputEq::maxGainDb, true, [] (OutputEq& eq) { return &eq.treble; },
                   [] (double v) { return signedDb ((float) v); });
        setUpKnob (highCut, "High cut", OutputEq::minHighCut, OutputEq::maxHighCut, false, [] (OutputEq& eq) { return &eq.highCut; },
                   [] (double v) { return v >= OutputEq::maxHighCut - 1.0 ? juce::String ("off") : hz ((float) v); });

        for (auto* k : { &lowCut, &midFreq, &highCut })
            k->setSkewFactorFromMidPoint (k == &lowCut ? 90.0 : (k == &midFreq ? 1000.0 : 6000.0));

        delayDown.setButtonText (minus());
        delayUp.setButtonText ("+");
        delayDown.setTooltip ("1 ms less (Shift: 0.1 ms)");
        delayUp.setTooltip ("1 ms more (Shift: 0.1 ms)");
        delayDown.onClick = [this] { nudgeDelay (-1.0f); };
        delayUp.onClick = [this] { nudgeDelay (1.0f); };

        for (auto* b : { &delayDown, &delayUp })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        }

        styleValueLabel (delayField);
        delayField.setFont (font (type::routingDelay));
        delayField.setTooltip ("Double-click to type the delay (0 to 100 ms)");
        delayField.onTextChange = [this]
        {
            const auto ms = delayField.getText().retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();
            editOutput ([ms] (RoutingOutput& out) { out.delayMs = juce::jlimit (0.0f, routing::maxDelayMs, ms); });
        };
        addAndMakeVisible (delayField);
    }

    //==========================================================================
    /** Everything from the routing, the device and the show. */
    void rebuild()
    {
        const auto& routing = owner.engine.getRouting();
        auto* device = owner.deviceManager.getCurrentAudioDevice();
        inputChannelNames = device != nullptr ? device->getInputChannelNames() : juce::StringArray();
        deviceOutputs = device != nullptr ? device->getOutputChannelNames().size() : 0;

        // Inputs.
        inputRows.clear();

        for (size_t i = 0; i < routing.inputs.size(); ++i)
        {
            inputRows.push_back (std::make_unique<InputRow> (*this, (int) i, routing.inputs[i]));
            addAndMakeVisible (*inputRows.back());
        }

        addInputButton.setEnabled ((int) routing.inputs.size() < routing::maxInputs);
        inputNote = juce::String ((int) routing.inputs.size()) + " of " + juce::String (routing::maxInputs) + middot() + "each patch picks one";

        // Outputs.
        outputRows.clear();

        for (size_t i = 0; i < routing.outputs.size(); ++i)
        {
            outputRows.push_back (std::make_unique<OutputRow> (*this, (int) i, routing.outputs[i]));
            addAndMakeVisible (*outputRows.back());
        }

        addOutputButton.setEnabled ((int) routing.outputs.size() < routing::maxOutputs);
        outputNote = juce::String ((int) routing.outputs.size()) + " of " + juce::String (routing::maxOutputs) + middot()
                   + "mono into a stereo output plays on both sides; stereo into a mono output is summed";

        // The interface.
        if (device != nullptr)
        {
            deviceName = device->getName();
            deviceLine = juce::String (device->getInputChannelNames().size()) + " inputs" + middot()
                       + juce::String (device->getOutputChannelNames().size()) + " outputs" + middot()
                       + juce::String (device->getCurrentSampleRate() / 1000.0, 0) + " kHz" + middot()
                       + juce::String (device->getCurrentBufferSizeSamples()) + " samples";
        }
        else
        {
            deviceName = "No audio interface";
            deviceLine = "Choose one in Audio settings.";
        }

        rebuildNames();
        showSelected();
        resized();
        repaint();
    }

    /** The names the show uses, here or not, with the patches that use them. */
    void rebuildNames()
    {
        const auto& routing = owner.engine.getRouting();
        const auto patches = owner.session.getAllPatches();
        nameRows.clear();

        auto usedBy = [&] (const std::function<bool (const PatchModel&)>& uses)
        {
            juce::StringArray names;

            for (const auto& p : patches)
                if (p.patch != nullptr && uses (*p.patch))
                    names.add (p.label);

            if (! names.isEmpty() && names.size() == (int) patches.size() && patches.size() > 1)
                return juce::String ("every patch");

            if (names.size() > 3)
                return names[0] + ", " + names[1] + ", " + names[2] + " +" + juce::String (names.size() - 3);

            return names.joinIntoString (", ");
        };

        const auto& show = owner.session.getShow();

        for (const auto& name : show.inputNamesUsed())
        {
            const auto here = routing.findInput (name) != nullptr;
            nameRows.push_back (std::make_unique<NameRow> (*this, name, true, here,
                                                           usedBy ([&] (const PatchModel& p) { return routing::sameName (p.inputName(), name); })));
        }

        for (const auto& name : show.outputNamesUsed())
        {
            const auto here = routing.findOutput (name) != nullptr;
            nameRows.push_back (std::make_unique<NameRow> (*this, name, false, here,
                                                           usedBy ([&] (const PatchModel& p)
                                                           {
                                                               const auto s = p.sendFor (name);
                                                               return s.on && s.levelDb > routing::silentDb;
                                                           })));
        }

        for (auto& row : nameRows)
            addAndMakeVisible (*row);
    }

    /** The selected output's detail: title, knobs, delay. */
    void showSelected()
    {
        const auto& outputs = owner.engine.getRouting().outputs;

        if (! juce::isPositiveAndBelow (owner.selectedOutput, (int) outputs.size()))
            owner.selectedOutput = outputs.empty() ? -1 : 0;

        const auto has = owner.selectedOutput >= 0;
        detailGroup.setVisible (has);

        for (auto* c : std::initializer_list<juce::Component*> { &curve, &lowCut, &bass, &mid, &midFreq, &treble, &highCut,
                                                                 &delayDown, &delayUp, &delayField })
            c->setVisible (has);

        if (! has)
            return;

        const auto& out = outputs[(size_t) owner.selectedOutput];
        detailGroup.setGroupTitle (out.name + middot() + "EQ, delay and safety");
        curve.setResponse (responseOf (out.eq, 96));

        const juce::ScopedValueSetter<bool> quiet (updating, true);
        lowCut.setValue (out.eq.lowCut, juce::dontSendNotification);
        bass.setValue (out.eq.bass, juce::dontSendNotification);
        mid.setValue (out.eq.mid, juce::dontSendNotification);
        midFreq.setValue (out.eq.midFreq, juce::dontSendNotification);
        treble.setValue (out.eq.treble, juce::dontSendNotification);
        highCut.setValue (out.eq.highCut, juce::dontSendNotification);

        for (auto* k : { &lowCut, &bass, &mid, &midFreq, &treble, &highCut })
            k->valueChanged();   // the value text

        delayField.setText (describeDelay (out.delayMs), juce::dontSendNotification);
        distance = describeDistance (out.delayMs);

        for (auto& row : outputRows)
            row->setSelected (row->index == owner.selectedOutput);
    }

    /** Meters, mutes and the limiter line (UI timer). */
    void update (const StageSnapshot& s)
    {
        for (auto& row : inputRows)
        {
            const auto slot = owner.engine.getInputSlot (row->name);
            row->meter.setPeaks (juce::isPositiveAndBelow (slot, routing::maxInputs) ? s.inputSlotPeaks[(size_t) slot] : 0.0f, 0.0f);
        }

        for (auto& row : outputRows)
        {
            const auto slot = owner.engine.getOutputSlot (row->name);

            if (slot < 0)
                continue;

            row->meter.setPeaks (s.outputSlotPeaks[(size_t) slot][0], s.outputSlotPeaks[(size_t) slot][1]);
            const auto muted = owner.engine.isOutputMuted (slot);
            row->meter.setMuted (muted);

            if (row->mute.getToggleState() != muted)
                row->mute.setToggleState (muted, juce::dontSendNotification);
        }

        // The selected output's limiter: idle, or how deep it pulls (held ~1 s).
        if (juce::isPositiveAndBelow (owner.selectedOutput, (int) owner.engine.getRouting().outputs.size()))
        {
            const auto slot = owner.engine.getOutputSlot (owner.engine.getRouting().outputs[(size_t) owner.selectedOutput].name);
            const auto reduction = slot >= 0 ? owner.engine.takeOutputLimiterReductionDb (slot) : 0.0f;
            const auto now = juce::Time::getMillisecondCounter();

            if (reduction > 0.05f)
            {
                heldReduction = juce::jmax (reduction, now < holdUntil ? heldReduction : 0.0f);
                holdUntil = now + 1000;
            }
            else if (now >= holdUntil)
            {
                heldReduction = 0.0f;
            }

            const auto clips = slot >= 0 ? owner.engine.getOutputClipCount (slot) : 0;
            const auto text = "Limiter at " + minus() + "1 dBFS" + middot()
                            + (heldReduction > 0.05f ? "limiting " + minus() + juce::String (heldReduction, 1) + " dB" : juce::String ("idle"))
                            + middot() + juce::String (clips) + (clips == 1 ? " clip" : " clips");

            if (text != limiterLine)
            {
                limiterLine = text;
                decor.repaint (limiterArea);
            }
        }
    }

    //==========================================================================
    void resized() override
    {
        // The group titles start 56 below the view's top: under its 1 + 41 px header.
        const auto leftW = size::routingLeftW;
        const auto top = size::routingContentTop - 1 - size::routingHeaderH;
        const auto left = juce::Rectangle<int> (size::routingPadX, top, leftW, getHeight() - top);
        const auto right = juce::Rectangle<int> (left.getRight() + size::routingColumnGap, top,
                                                 getWidth() - left.getRight() - size::routingColumnGap - size::routingPadX, getHeight() - top);

        // Inputs: head, a row each, the bar.
        const auto inputsBox = size::routingHeadH + (int) inputRows.size() * size::routingInputRowH + size::routingBarH + 2;
        inputsGroup.setBounds (left.getX(), top, leftW, kit::Group::heightFor (inputsBox));
        auto box = inputsGroup.getBoxBounds().reduced (1) + inputsGroup.getPosition();
        inputsHead = box.removeFromTop (size::routingHeadH);

        for (auto& row : inputRows)
            row->setBounds (box.removeFromTop (size::routingInputRowH));

        inputsBar = box.removeFromTop (size::routingBarH);
        addInputButton.setBounds (inputsBar.getX() + size::routingCellPadX, inputsBar.getY() + 7,
                                  addInputButton.getBestWidthForHeight (size::buttonHeight), size::buttonHeight);

        // Interface.
        interfaceGroup.setBounds (left.getX(), inputsGroup.getBottom() + size::routingGroupGap, leftW, kit::Group::heightFor (size::routingInterfaceH));
        interfaceBox = interfaceGroup.getBoxBounds() + interfaceGroup.getPosition();
        audioButton.setBounds (interfaceBox.getX() + 15, interfaceBox.getY() + 56,
                               audioButton.getBestWidthForHeight (size::buttonSmallHeight), size::buttonSmallHeight + size::buttonDrop);

        // Names this show uses.
        const auto namesBox = 8 + (int) nameRows.size() * size::routingNameRowH + size::routingNamesHelpH;
        namesGroup.setBounds (left.getX(), interfaceGroup.getBottom() + size::routingGroupGap, leftW, kit::Group::heightFor (namesBox));
        box = namesGroup.getBoxBounds().reduced (1) + namesGroup.getPosition();
        box.removeFromTop (3);

        for (auto& row : nameRows)
            row->setBounds (box.removeFromTop (size::routingNameRowH));

        namesHelp = box.withTrimmedTop (2);

        // Outputs.
        const auto outputsBox = size::routingHeadH + (int) outputRows.size() * size::routingOutputRowH + size::routingBarH + 2;
        outputsGroup.setBounds (right.getX(), top, right.getWidth(), kit::Group::heightFor (outputsBox));
        box = outputsGroup.getBoxBounds().reduced (1) + outputsGroup.getPosition();
        outputsHead = box.removeFromTop (size::routingHeadH);

        for (auto& row : outputRows)
            row->setBounds (box.removeFromTop (size::routingOutputRowH));

        outputsBar = box.removeFromTop (size::routingBarH);
        addOutputButton.setBounds (outputsBar.getX() + size::routingCellPadX, outputsBar.getY() + 7,
                                   addOutputButton.getBestWidthForHeight (size::buttonHeight), size::buttonHeight);

        // The selected output's detail.
        detailGroup.setBounds (right.getX(), outputsGroup.getBottom() + size::routingGroupGap, right.getWidth(),
                               kit::Group::heightFor (size::routingDetailH));
        detailBox = detailGroup.getBoxBounds() + detailGroup.getPosition();
        const auto d = detailBox.getTopLeft();
        curve.setBounds (d.x + size::routingDetailPad, d.y + 39, size::routingCurveW, size::routingCurveH);

        const auto knob = kit::SmallKnob::idealBounds();
        auto x = d.x + 14;

        for (auto* k : { &lowCut, &bass, &mid, &midFreq, &treble, &highCut })
        {
            k->setBounds (x, d.y + size::routingKnobsTop, knob.getWidth(), knob.getHeight());
            x += size::smallKnobColumn;
        }

        const auto rx = d.x + size::routingRightX;
        delayDown.setBounds (rx, d.y + 40, size::routingStepW, size::buttonHeight);
        delayField.setBounds (rx + size::routingStepW + 8, d.y + 37, size::routingDelayFieldW, size::routingDelayFieldH);
        delayUp.setBounds (delayField.getRight() + 8, d.y + 40, size::routingStepW, size::buttonHeight);
        limiterArea = { rx, d.y + 167, size::routingRightW, 15 };
        decor.setBounds (getLocalBounds());
    }

    void paintDecor (juce::Graphics& g)
    {
        // Table heads and bars.
        paintHead (g, inputsHead, { { "Name", size::routingCellPadX }, { "Channel", size::routingCellPadX + size::routingInNameW + size::routingColGap },
                                    { "Level", size::routingCellPadX + size::routingInNameW + size::routingInChannelW + 2 * size::routingColGap } });
        paintBar (g, inputsBar, addInputButton, inputNote);

        const auto ox = size::routingCellPadX;
        const auto col = [] (std::initializer_list<int> widths)
        {
            auto x = size::routingCellPadX;

            for (auto w : widths)
                x += w + size::routingColGap;

            return x;
        };
        paintHead (g, outputsHead, { { "Name", ox },
                                     { "Channels", col ({ size::routingOutNameW }) },
                                     { "Level", col ({ size::routingOutNameW, size::routingOutChannelsW }) },
                                     { "M", col ({ size::routingOutNameW, size::routingOutChannelsW, size::routingFaderW + 8 + size::routingLevelFieldW }) },
                                     { "Meter", col ({ size::routingOutNameW, size::routingOutChannelsW, size::routingFaderW + 8 + size::routingLevelFieldW, size::outputMuteW }) },
                                     { "EQ", col ({ size::routingOutNameW, size::routingOutChannelsW, size::routingFaderW + 8 + size::routingLevelFieldW, size::outputMuteW, size::routingOutMeterW }) },
                                     { "Delay", col ({ size::routingOutNameW, size::routingOutChannelsW, size::routingFaderW + 8 + size::routingLevelFieldW, size::outputMuteW, size::routingOutMeterW, size::routingEqThumbW + 6 + size::routingEqSummaryW }) } });
        paintBar (g, outputsBar, addOutputButton, outputNote);

        // Interface: a dot (green with a device), its name, what it has.
        {
            const auto b = interfaceBox.toFloat();
            const auto dot = juce::Rectangle<float> (b.getX() + 15.0f, b.getY() + 17.0f, 8.0f, 8.0f);
            g.setColour (paint::c (owner.deviceManager.getCurrentAudioDevice() != nullptr ? colour::healthy : colour::dotOff));
            g.fillEllipse (dot);
            paint::text (g, deviceName, { b.getX() + 31.0f, b.getY() + 12.0f, b.getWidth() - 46.0f, 17.0f }, type::routingDevice, colour::text);
            paint::text (g, deviceLine, { b.getX() + 15.0f, b.getY() + 34.0f, b.getWidth() - 30.0f, 16.0f }, type::bodySmall, colour::textSecondary);
        }

        // Names: the help under the rows.
        g.setColour (paint::c (colour::textMuted));
        g.setFont (font (type::caption));
        g.drawFittedText ("A name that isn't here plays input 1 (inputs) or stays silent (outputs). Add it, or pick another name in the patch.",
                          namesHelp.reduced (size::routingCellPadX, 0).withTrimmedTop (4), juce::Justification::topLeft, 3, 1.0f);

        if (! detailGroup.isVisible())
            return;

        // Detail: EQ label, the divider, Delay and Safety.
        const auto d = detailBox.toFloat();
        const auto eqTitle = juce::String ("EQ");
        paint::text (g, eqTitle, { d.getX() + 17.0f, d.getY() + 15.0f, 30.0f, 13.0f }, type::paneHeader, colour::textMuted);
        paint::text (g, "same ranges as the EQ block" + middot() + "click the curve to reset",
                     { d.getX() + 40.0f, d.getY() + 15.0f, 380.0f, 14.0f }, type::caption, colour::textMuted);

        g.setColour (paint::c (colour::routingDivider));
        g.fillRect (d.getX() + (float) size::routingDividerX, d.getY() + 15.0f, 1.0f, 227.0f);

        const auto rx = d.getX() + (float) size::routingRightX;
        const auto rw = (float) size::routingRightW;
        paint::inset (g, delayField.getBounds().toFloat(), colour::fieldFill, colour::fieldBorder, (float) size::fieldRadius, colour::fieldShadow);
        paint::text (g, "DELAY", { rx, d.getY() + 15.0f, rw, 13.0f }, type::paneHeader, colour::textMuted);
        paint::text (g, distance, { (float) delayUp.getRight() + 8.0f, d.getY() + 44.0f, 120.0f, 16.0f }, type::bodySmall, colour::textSecondary);

        g.setColour (paint::c (colour::textTertiary));
        g.setFont (font (type::noteText));
        g.drawFittedText ("Holds this output back to line it up with speakers further away: 1 ms " + juce::String (juce::CharPointer_UTF8 ("\xe2\x89\x88"))
                              + " 34 cm. Up to 100 ms.",
                          juce::Rectangle<int> ((int) rx, (int) d.getY() + 77, (int) rw, 50), juce::Justification::topLeft, 3, 1.0f);

        paint::text (g, "SAFETY", { rx, d.getY() + 140.0f, rw, 13.0f }, type::paneHeader, colour::textMuted);
        paint::text (g, limiterLine, limiterArea.toFloat(), type::bodySmall, colour::textSecondary);
        g.setColour (paint::c (colour::textTertiary));
        g.setFont (font (type::noteText));
        g.drawFittedText ("Each output has its own limiter, so a hot in-ear never pulls the PA down.",
                          juce::Rectangle<int> ((int) rx, (int) d.getY() + 192, (int) rw, 36), juce::Justification::topLeft, 2, 1.0f);
    }

private:
    //==========================================================================
        //==========================================================================
    /** An input: its name (typed), its channel (popup), its meter, remove. */
    class InputRow final : public juce::Component
    {
    public:
        InputRow (IoPage& p, int i, const RoutingInput& input) : page (p), index (i), name (input.name)
        {
            styleField (nameField);
            nameField.setText (input.name, false);
            nameField.setTitle ("Input name");
            nameField.setTooltip ("This input's name: patches pick inputs by name");
            nameField.onReturnKey = [this] { commitName(); };
            nameField.onFocusLost = [this] { commitName(); };
            nameField.onEscapeKey = [this] { nameField.setText (name, false); };
            addAndMakeVisible (nameField);

            for (int ch = 0; ch < page.inputChannelNames.size(); ++ch)
                channel.addItem (juce::String (ch + 1) + middot() + page.inputChannelNames[ch], ch + 1);

            if (input.channel > page.inputChannelNames.size())
                channel.addItem (juce::String (input.channel) + middot() + "not on this interface", input.channel);

            channel.setSelectedId (input.channel, juce::dontSendNotification);
            setAmber (channel, input.channel > page.inputChannelNames.size());
            channel.setTitle ("Channel of " + input.name);
            channel.onChange = [this]
            {
                const auto ch = channel.getSelectedId();
                auto routing = page.owner.engine.getRouting();

                if (ch > 0 && juce::isPositiveAndBelow (index, (int) routing.inputs.size()))
                {
                    routing.inputs[(size_t) index].channel = ch;
                    page.owner.applyRouting (routing);
                }
            };
            channel.setWantsKeyboardFocus (false);
            addAndMakeVisible (channel);

            meter.setBarThickness (size::levelBarH);
            addAndMakeVisible (meter);

            remove.onClick = [this]
            {
                auto routing = page.owner.engine.getRouting();

                if (routing.inputs.size() > 1 && juce::isPositiveAndBelow (index, (int) routing.inputs.size()))
                {
                    routing.inputs.erase (routing.inputs.begin() + index);
                    page.owner.applyRouting (routing);
                }
            };
            remove.setEnabled (page.owner.engine.getRouting().inputs.size() > 1);
            addAndMakeVisible (remove);
        }

        void paint (juce::Graphics& g) override
        {
            if (index % 2 == 1)
                g.fillAll (paint::c (colour::mappingRowAlt));
        }

        void resized() override
        {
            auto x = size::routingCellPadX;
            nameField.setBounds (x, (getHeight() - 24) / 2, size::routingInNameW, 24);
            x += size::routingInNameW + size::routingColGap;
            channel.setBounds (x, (getHeight() - size::routingComboH) / 2, size::routingInChannelW, size::routingComboH);
            x += size::routingInChannelW + size::routingColGap;
            meter.setBounds (x, 0, size::routingInLevelW, getHeight());
            remove.setBounds (getWidth() - size::routingCellPadX - size::routingRemoveW, (getHeight() - size::routingRemoveW) / 2,
                              size::routingRemoveW, size::routingRemoveW);
        }

        IoPage& page;
        const int index;
        const juce::String name;
        juce::TextEditor nameField;
        juce::ComboBox channel;
        kit::LevelBars meter { kit::LevelBars::Orientation::horizontal, 1 };
        kit::IconButton remove { Icon::close, "Remove this input" };

    private:
        void commitName()
        {
            const auto typed = routing::cleanName (nameField.getText());
            auto routing = page.owner.engine.getRouting();

            if (typed.isEmpty() || typed == name || ! juce::isPositiveAndBelow (index, (int) routing.inputs.size()))
            {
                nameField.setText (name, false);
                return;
            }

            routing.inputs[(size_t) index].name = routing.findInput (typed) != nullptr ? routing.uniqueInputName (typed) : typed;

            // After this callback returns: applying rebuilds the rows (this one too).
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RoutingView> (&page.owner), routing]
            {
                if (safe != nullptr)
                    safe->applyRouting (routing);
            });
        }
    };

        //==========================================================================
    /** An output: name (double-click to rename), channels, level, M, meters,
        EQ thumbnail, delay, remove. A click selects it (its detail below). */
    class OutputRow final : public juce::Component
    {
    public:
        OutputRow (IoPage& p, int i, const RoutingOutput& output) : page (p), index (i), name (output.name)
        {
            setTitle ("Output " + output.name);

            nameLabel.setText (output.name, juce::dontSendNotification);
            nameLabel.setFont (font (type::routingRowName));
            nameLabel.setColour (juce::Label::textColourId, paint::c (colour::text));
            nameLabel.setColour (juce::TextEditor::backgroundColourId, paint::c (colour::fieldFill));
            nameLabel.setColour (juce::TextEditor::textColourId, paint::c (colour::fieldText));
            nameLabel.setBorderSize ({});
            nameLabel.setEditable (false, true, true);
            nameLabel.setTooltip ("Double-click to rename: patches send to outputs by name");
            nameLabel.onTextChange = [this] { commitName(); };
            addAndMakeVisible (nameLabel);

            // Channels: pairs, singles, every output; amber when the interface hasn't it.
            options = outputChannelOptions (page.deviceOutputs, output);

            for (size_t o = 0; o < options.size(); ++o)
                channels.addItem (describeOption (options[o]), (int) o + 1);

            for (size_t o = 0; o < options.size(); ++o)
                if (options[o] == output.channels)
                    channels.setSelectedId ((int) o + 1, juce::dontSendNotification);

            for (auto ch : output.channels)
                missingChannel = ch > page.deviceOutputs ? juce::jmax (missingChannel, ch) : missingChannel;

            setAmber (channels, missingChannel > 0);
            channels.setTitle ("Channels of " + output.name);
            channels.setWantsKeyboardFocus (false);
            channels.onChange = [this]
            {
                const auto o = channels.getSelectedId() - 1;

                if (juce::isPositiveAndBelow (o, (int) options.size()))
                    page.owner.selectOutput (index), page.editOutput ([chosen = options[(size_t) o]] (RoutingOutput& out) { out.channels = chosen; });
            };
            addAndMakeVisible (channels);

            fader.setDb (output.levelDb);
            fader.setTitle ("Level of " + output.name);
            fader.onChange = [this] (float dB)
            {
                // Live while it moves; saved when it lets go.
                if (const auto slot = page.owner.engine.getOutputSlot (name); slot >= 0)
                    page.owner.engine.setOutputLevel (slot, dB);

                levelField.setText (describeLevel (dB), juce::dontSendNotification);
            };
            fader.onGestureEnd = [this] { setLevel (fader.getDb()); };
            addAndMakeVisible (fader);

            styleValueLabel (levelField);
            levelField.setText (describeLevel (output.levelDb), juce::dontSendNotification);
            levelField.setTooltip ("Double-click to type a level (dB)");
            levelField.onTextChange = [this]
            {
                const auto text = levelField.getText().trim().replace (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")), "-");
                setLevel (text.startsWithIgnoreCase ("off") || text.containsIgnoreCase ("inf") ? routing::silentDb
                                                                                              : routing::clampLevel (text.getFloatValue()));
            };
            addAndMakeVisible (levelField);

            mute.setToggleState (output.mute, juce::dontSendNotification);
            mute.setTitle ("Mute " + output.name);
            mute.onClick = [this]
            {
                if (page.owner.onSetOutputMuted != nullptr)
                    page.owner.onSetOutputMuted (name, mute.getToggleState());
            };
            addAndMakeVisible (mute);

            meter.setNumChannels (output.isStereo() ? 2 : 1);
            meter.setBarThickness (size::levelBarH);
            meter.setGap (size::levelBarGap);
            meter.setMuted (output.mute);
            addAndMakeVisible (meter);

            thumb.setResponse (responseOf (output.eq, 24));
            thumb.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (thumb);
            eqText = describeEq (output.eq);
            delayText = describeDelay (output.delayMs);
            distanceText = describeDistance (output.delayMs);

            remove.onClick = [this]
            {
                auto routing = page.owner.engine.getRouting();

                if (juce::isPositiveAndBelow (index, (int) routing.outputs.size()))
                {
                    routing.outputs.erase (routing.outputs.begin() + index);
                    page.owner.applyRouting (routing);
                }
            };
            remove.setTooltip ("Remove this output from this computer");
            addAndMakeVisible (remove);
        }

        void setSelected (bool s)
        {
            if (s != selected)
            {
                selected = s;
                repaint();
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (getLocalBounds().contains (e.getPosition()))
                page.owner.selectOutput (index);
        }

        void paint (juce::Graphics& g) override
        {
            if (selected)
                g.fillAll (paint::c (colour::routingSelectedRow));
            else if (index % 2 == 1)
                g.fillAll (paint::c (colour::mappingRowAlt));

            // The level field's well (the label paints only its text).
            paint::inset (g, levelField.getBounds().toFloat(), colour::fieldFill, colour::fieldBorder, (float) size::fieldRadius, colour::fieldShadow);

            if (missingChannel > 0)
                paint::text (g, juce::String (juce::CharPointer_UTF8 ("\xe2\x9a\xa0 ")) + "needs output " + juce::String (missingChannel),
                             { (float) size::routingCellPadX, (float) getHeight() / 2.0f + 1.0f, (float) size::routingOutNameW, 13.0f },
                             type::outputValue, colour::amber);

            const auto eqX = (float) thumb.getRight() + 6.0f;
            paint::text (g, eqText, { eqX, 0.0f, (float) size::routingEqSummaryW, (float) getHeight() }, type::caption, colour::textSecondary);

            const auto delayX = eqX + (float) (size::routingEqSummaryW + size::routingColGap);
            const auto delayW = textWidth (font (type::bodySmall), delayText) + 4.0f;
            paint::text (g, delayText, { delayX, 0.0f, delayW, (float) getHeight() }, type::bodySmall, colour::text);
            paint::text (g, distanceText, { delayX + delayW, 0.0f, (float) size::routingDelayW - delayW, (float) getHeight() }, type::bodySmall, colour::textMuted);
        }

        void resized() override
        {
            const auto h = getHeight();
            auto x = size::routingCellPadX;
            nameLabel.setBounds (x, missingChannel > 0 ? h / 2 - 16 : (h - 16) / 2, size::routingOutNameW, 16);
            x += size::routingOutNameW + size::routingColGap;
            channels.setBounds (x, (h - size::routingComboH) / 2, size::routingOutChannelsW, size::routingComboH);
            x += size::routingOutChannelsW + size::routingColGap;
            fader.setBounds (x, (h - 16) / 2, size::routingFaderW + size::faderHThumb / 2, 16);
            x += size::routingFaderW + 8;
            levelField.setBounds (x, (h - size::routingComboH) / 2, size::routingLevelFieldW, size::routingComboH);
            x += size::routingLevelFieldW + size::routingColGap;
            mute.setBounds (x, (h - size::outputMuteH) / 2, size::outputMuteW, size::outputMuteH);
            x += size::outputMuteW + size::routingColGap + 2;
            meter.setBounds (x, (h - 10) / 2, size::routingOutMeterW, 10);
            x += size::routingOutMeterW + size::routingColGap;
            thumb.setBounds (x, (h - size::routingComboH) / 2, size::routingEqThumbW, size::routingComboH);
            remove.setBounds (getWidth() - 32 - size::routingRemoveW, (h - size::routingRemoveW) / 2, size::routingRemoveW, size::routingRemoveW);
        }

            IoPage& page;
        const int index;
        const juce::String name;
        kit::LevelBars meter { kit::LevelBars::Orientation::horizontal, 2 };
        kit::MuteButton mute;

    private:
        void setLevel (float dB)
        {
            page.owner.selectOutput (index);
            page.editOutput ([dB] (RoutingOutput& out) { out.levelDb = dB; });
        }

        void commitName()
        {
            const auto typed = routing::cleanName (nameLabel.getText());
            auto routing = page.owner.engine.getRouting();

            if (typed.isEmpty() || typed == name || ! juce::isPositiveAndBelow (index, (int) routing.outputs.size()))
            {
                nameLabel.setText (name, juce::dontSendNotification);
                return;
            }

            routing.outputs[(size_t) index].name = routing.findOutput (typed) != nullptr ? routing.uniqueOutputName (typed) : typed;

            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RoutingView> (&page.owner), routing]
            {
                if (safe != nullptr)
                    safe->applyRouting (routing);
            });
        }

        juce::Label nameLabel;
        juce::ComboBox channels;
        std::vector<std::vector<int>> options;
        kit::Fader fader { kit::Fader::Orientation::horizontal };
        juce::Label levelField;
        kit::EqCurve thumb { false };
        kit::IconButton remove { Icon::close, "Remove this output" };
        juce::String eqText, delayText, distanceText;
        int missingChannel = 0;
        bool selected = false;
    };

        //==========================================================================
    /** A name the show uses: ✓ here or ! not here, input / output, the patches
        that use it; + Add for one that isn't here. */
    class NameRow final : public juce::Component
    {
    public:
        NameRow (IoPage& p, const juce::String& n, bool isInput, bool isHere, const juce::String& patches)
            : page (p), name (n), input (isInput), here (isHere), usedBy (patches)
        {
            setTitle ((input ? "Input " : "Output ") + name + (here ? juce::String() : juce::String (", not here")));

            if (! here)
            {
                setVariant (add, Variant::small);
                add.setButtonText (input ? "+ Add input" : "+ Add output");
                add.setTooltip ("Add " + quoted (name) + " to this computer");
                add.onClick = [this]
                {
                    if (input)
                        page.addInput (name);
                    else
                        page.addOutput (name);
                };
                add.setWantsKeyboardFocus (false);
                addAndMakeVisible (add);
            }
        }

        void resized() override
        {
            const auto w = add.getBestWidthForHeight (size::buttonSmallHeight);
            add.setBounds (getWidth() - size::routingCellPadX - w, (getHeight() - size::buttonSmallHeight) / 2 - 1, w, size::buttonSmallHeight + size::buttonDrop);
        }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            const auto glyph = juce::Rectangle<float> ((float) size::routingCellPadX, 0.0f, 14.0f, r.getHeight());

            if (here)
                paint::tick (g, glyph.withSizeKeepingCentre (12.0f, 12.0f), paint::c (colour::tickGreen), 2.0f);
            else
                paint::text (g, "!", glyph, type::routingGlyph, colour::amber, juce::Justification::centred);

            paint::text (g, name, { 35.0f, 0.0f, 108.0f, r.getHeight() }, type::routingRowName, here ? colour::text : colour::faultCardText);
            paint::text (g, input ? "input" : "output", { 151.0f, 0.0f, 46.0f, r.getHeight() }, type::caption, colour::textMuted);
            const auto right = here ? r.getWidth() - (float) size::routingCellPadX : (float) add.getX() - 8.0f;
            paint::text (g, usedBy, { 205.0f, 0.0f, right - 205.0f, r.getHeight() }, type::caption, colour::textMuted);
        }

    private:
        IoPage& page;
        const juce::String name;
        const bool input, here;
        const juce::String usedBy;
        juce::TextButton add;
    };


    struct Head { const char* text; int x; };

    void paintHead (juce::Graphics& g, juce::Rectangle<int> head, std::initializer_list<Head> heads)
    {
        if (head.isEmpty())
            return;

        g.setColour (paint::c (colour::routingHead));
        g.fillRect (head);

        for (const auto& h : heads)
            paint::text (g, paint::caps (type::columnHead, h.text), head.toFloat().withTrimmedLeft ((float) h.x), type::columnHead, colour::textDisabled);
    }

    void paintBar (juce::Graphics& g, juce::Rectangle<int> bar, juce::Button& button, const juce::String& note)
    {
        if (bar.isEmpty())
            return;

        g.setColour (paint::c (colour::routingHead));
        g.fillRect (bar);
        g.setColour (paint::c (colour::paneHeaderLine));
        g.fillRect (bar.withHeight (1));
        paint::text (g, note, bar.toFloat().withTrimmedLeft ((float) (button.getRight() - bar.getX() + 8)).withTrimmedRight (12.0f),
                     type::noteText, colour::textTertiary);
    }

    void setUpKnob (kit::SmallKnob& k, const juce::String& label, float min, float max, bool bipolar,
                    std::function<float* (OutputEq&)> field, std::function<juce::String (double)> text)
    {
        k.setLabel (label);
        k.setBipolar (bipolar);
        k.setRange (min, max, 0.0);
        k.textFromValueFunction = std::move (text);
        k.setDoubleClickReturnValue (true, bipolar ? 0.0 : (k.getRange().getStart() == OutputEq::minHighCut ? max : (label == "Mid freq" ? 800.0 : min)));
        k.onValueChange = [this, &k, field]
        {
            if (updating)
                return;

            const auto v = (float) k.getValue();
            editOutput ([field, v] (RoutingOutput& out) { *field (out.eq) = v; });
        };
        addAndMakeVisible (k);
    }

    /** Changes the selected output and applies the routing. */
    void editOutput (const std::function<void (RoutingOutput&)>& change)
    {
        auto routing = owner.engine.getRouting();

        if (! juce::isPositiveAndBelow (owner.selectedOutput, (int) routing.outputs.size()))
            return;

        change (routing.outputs[(size_t) owner.selectedOutput]);
        owner.applyRouting (routing);
    }

    void nudgeDelay (float ms)
    {
        const auto step = juce::ModifierKeys::currentModifiers.isShiftDown() ? routing::delayStepMs * (ms > 0 ? 1.0f : -1.0f) : ms;
        editOutput ([step] (RoutingOutput& out) { out.delayMs = juce::jlimit (0.0f, routing::maxDelayMs, std::round ((out.delayMs + step) * 10.0f) / 10.0f); });
    }

    void addInput (const juce::String& name)
    {
        auto routing = owner.engine.getRouting();

        if ((int) routing.inputs.size() >= routing::maxInputs)
            return;

        // The next channel no input uses yet.
        auto channel = 1;

        while (std::any_of (routing.inputs.begin(), routing.inputs.end(), [channel] (const RoutingInput& in) { return in.channel == channel; }))
            ++channel;

        routing.inputs.push_back ({ name.isNotEmpty() ? name : routing.uniqueInputName ("Input"), channel });
        owner.applyRouting (routing);
    }

    void addOutput (const juce::String& name)
    {
        auto routing = owner.engine.getRouting();

        if ((int) routing.outputs.size() >= routing::maxOutputs)
            return;

        // The next channel past the ones in use (mono; pick a pair in its popup).
        auto channel = 1;

        for (const auto& o : routing.outputs)
            for (auto ch : o.channels)
                channel = juce::jmax (channel, ch + 1);

        RoutingOutput out;
        out.name = name.isNotEmpty() ? name : routing.uniqueOutputName ("Output");
        out.channels = { channel };
        routing.outputs.push_back (out);
        owner.selectedOutput = (int) routing.outputs.size() - 1;
        owner.applyRouting (routing);
    }

    /** Paints what the owner says (over the groups, under the controls). */
    struct Decor final : public juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter != nullptr) painter (g); }
    };

    RoutingView& owner;
    Decor decor;
    kit::Group inputsGroup { "Inputs" }, interfaceGroup { "Interface" }, namesGroup { "Names this show uses" },
               outputsGroup { "Outputs" }, detailGroup { "EQ, delay and safety" };
    juce::TextButton addInputButton { "+ Add input" }, addOutputButton { "+ Add output" }, audioButton { "Audio settings..." };
    std::vector<std::unique_ptr<InputRow>> inputRows;
    std::vector<std::unique_ptr<OutputRow>> outputRows;
    std::vector<std::unique_ptr<NameRow>> nameRows;

    kit::EqCurve curve { true };
    kit::SmallKnob lowCut, bass, mid, midFreq, treble, highCut;
    juce::TextButton delayDown, delayUp;
    juce::Label delayField;

    juce::StringArray inputChannelNames;
    int deviceOutputs = 0;
    juce::String inputNote, outputNote, deviceName, deviceLine, distance, limiterLine;
    juce::Rectangle<int> inputsHead, inputsBar, outputsHead, outputsBar, interfaceBox, namesHelp, detailBox, limiterArea;
    float heldReduction = 0.0f;
    juce::uint32 holdUntil = 0;
    bool updating = false;
};

//==============================================================================
/** Sends: every patch against every output name (§B.2). Rows are a ListBox
    (a show can have many patches); each cell shows a small bar on the send
    law and the level, or "off". Click selects a cell, drag sideways sets
    it, double-click (or Return) types a level ("off" switches it off). */
class RoutingView::SendsPage final : public juce::Component,
                                     private juce::ListBoxModel
{
public:
    explicit SendsPage (RoutingView& o) : owner (o)
    {
        list.setModel (this);
        list.setRowHeight (size::sendsRowH);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        list.setWantsKeyboardFocus (false);
        list.setTitle ("Sends");
        list.addMouseListener (this, true);   // drags on a cell
        addAndMakeVisible (list);

        styleField (editor);
        editor.onReturnKey = [this] { commitEdit(); };
        editor.onEscapeKey = [this] { editor.setVisible (false); };
        editor.onFocusLost = [this] { commitEdit(); };
        addChildComponent (editor);
    }

    ~SendsPage() override
    {
        list.removeMouseListener (this);
        list.setModel (nullptr);
    }

    struct Column
    {
        juce::String name, sub;
        bool here = true, muted = false;
    };

    /** The patches and the output names: this computer's, then the show's
        that aren't here. */
    void rebuild()
    {
        patches = owner.session.getAllPatches();
        const auto& routing = owner.engine.getRouting();
        columns.clear();

        for (const auto& o : routing.outputs)
        {
            const auto slot = owner.engine.getOutputSlot (o.name);
            columns.push_back ({ o.name, describeShortChannels (o) + middot() + (o.isAllOutputs() ? "all" : (o.isStereo() ? "stereo" : "mono")),
                                 true, slot >= 0 ? owner.engine.isOutputMuted (slot) : o.mute });
        }

        for (const auto& name : owner.session.getShow().outputNamesUsed())
            if (routing.findOutput (name) == nullptr)
                columns.push_back ({ name, "not on this computer", false, false });

        list.updateContent();
        list.repaint();
        resized();
        repaint();
    }

    void select (int row, int column)
    {
        selectedRow = row;
        selectedColumn = column;
        list.repaint();
    }

    void update (const StageSnapshot&)
    {
        // Mutes can change from Stage or a footswitch: the MUTED chips follow.
        auto changed = false;

        for (auto& c : columns)
        {
            if (! c.here)
                continue;

            const auto slot = owner.engine.getOutputSlot (c.name);
            const auto muted = slot >= 0 && owner.engine.isOutputMuted (slot);
            changed = changed || muted != c.muted;
            c.muted = muted;
        }

        if (changed)
            repaint (headArea());
    }

    int columnX (int c) const { return size::routingCellPadX + size::sendsPatchW + size::sendsInputW + size::sendsEndW + c * columnW(); }
    int columnW() const
    {
        const auto room = tableBox().getWidth() - (size::routingCellPadX + size::sendsPatchW + size::sendsInputW + size::sendsEndW);
        return columns.empty() ? size::sendsColumnW : juce::jlimit (120, size::sendsColumnW, room / (int) columns.size());
    }

    juce::Rectangle<int> tableBox() const
    {
        const auto top = size::routingContentTop - 1 - size::routingHeaderH;
        const auto rows = juce::jmax (1, (int) patches.size());
        const auto maxH = getHeight() - top - size::sendsHelpTop - 60;
        const auto h = juce::jmin (maxH, size::sendsHeadH + rows * size::sendsRowH + 2);
        return { size::routingPadX, top, getWidth() - 2 * size::routingPadX, h };
    }

    juce::Rectangle<int> headArea() const { return tableBox().reduced (1).removeFromTop (size::sendsHeadH); }

    void resized() override
    {
        const auto box = tableBox().reduced (1);
        list.setBounds (box.withTrimmedTop (size::sendsHeadH));
    }

    void paint (juce::Graphics& g) override
    {
        const auto box = tableBox().toFloat();
        g.setColour (paint::c (colour::groupBox));
        g.fillRoundedRectangle (box, (float) size::groupRadius);
        g.setColour (paint::c (colour::paneBorder));
        g.drawRoundedRectangle (box.reduced (0.5f), (float) size::groupRadius, 1.0f);

        // The head: Patch, Input, End, then a name and its channels per output.
        const auto head = headArea();
        g.setColour (paint::c (colour::routingHead));
        g.fillRect (head);
        const auto h = head.toFloat();
        const auto textY = h.getY() + 14.0f;
        const auto hx = h.getX() - 1.0f;
        paint::text (g, paint::caps (type::columnHead, "Patch"), { hx + (float) size::routingCellPadX, h.getY(), 300.0f, h.getHeight() }, type::columnHead, colour::textDisabled);
        paint::text (g, paint::caps (type::columnHead, "Input"), { hx + (float) (size::routingCellPadX + size::sendsPatchW), h.getY(), 120.0f, h.getHeight() }, type::columnHead, colour::textDisabled);
        paint::text (g, paint::caps (type::columnHead, "End"), { hx + (float) (size::routingCellPadX + size::sendsPatchW + size::sendsInputW), h.getY(), 70.0f, h.getHeight() }, type::columnHead, colour::textDisabled);

        for (size_t c = 0; c < columns.size(); ++c)
        {
            const auto& col = columns[c];
            const auto x = hx + (float) columnX ((int) c);
            const auto w = (float) columnW() - 8.0f;
            const auto nameW = juce::jmin (w, textWidth (font (type::sendsHead), col.name) + 1.0f);
            paint::text (g, col.name, { x, h.getY() + 8.0f, nameW, 15.0f }, type::sendsHead, col.here ? colour::text : colour::faultCardText);

            const auto chip = ! col.here ? paint::caps (type::tagChip, "Not here") : (col.muted ? paint::caps (type::tagChip, "Muted") : juce::String());

            if (chip.isNotEmpty())
                kit::paintChip (g, { x + nameW + 6.0f, h.getY() + 9.0f, kit::chipWidth (chip), 14.0f }, chip,
                                col.here ? kit::ChipStyle::mute : kit::ChipStyle::amber);

            paint::text (g, col.sub, { x, textY + 10.0f, w, 12.0f }, type::sendsHeadSub, colour::textMuted);
        }

        // Under the table: how sends work, and what a missing send means.
        const auto helpTop = box.getBottom() + (float) size::sendsHelpTop;
        g.setColour (paint::c (colour::textTertiary));
        g.setFont (font (type::noteText));
        g.drawFittedText ("Each patch sends to every output at its own level, like a mixer. Click a cell and type a level, or drag; Off = not sent. "
                          "The same faders are on the patch's OUT tile in Edit.",
                          juce::Rectangle<float> (box.getX(), helpTop, (float) size::sendsHelpW, 50.0f).toNearestInt(), juce::Justification::topLeft, 3, 1.0f);
        g.drawFittedText ("An output a patch doesn't mention gets it at 0 dB, so a new output plays everything until you turn it down. "
                          "Switching patches crossfades the sends.",
                          juce::Rectangle<float> (box.getX() + (float) (size::sendsHelpW + size::sendsHelpGap), helpTop, (float) size::sendsHelpW, 50.0f).toNearestInt(),
                          juce::Justification::topLeft, 3, 1.0f);
    }

    //==========================================================================
    int getNumRows() override { return (int) patches.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
    {
        if (! juce::isPositiveAndBelow (row, (int) patches.size()) || patches[(size_t) row].patch == nullptr)
            return;

        const auto& entry = patches[(size_t) row];
        const auto& patch = *entry.patch;
        const auto selected = row == selectedRow;

        if (selected)
            g.fillAll (paint::c (colour::sendsSelectedRow));
        else if (row % 2 == 1)
            g.fillAll (paint::c (colour::mappingRowAlt));

        const auto h = (float) height;
        const auto px = (float) size::routingCellPadX - 1.0f;
        const auto own = entry.key != patch.id;

        g.setColour (own ? paint::c (colour::textDisabled) : juce::Colour (patch.colour));
        g.fillRoundedRectangle ({ px, (h - 9.0f) * 0.5f, 9.0f, 9.0f }, 2.0f);

        paint::text (g, entry.label, { px + 17.0f, 0.0f, (float) size::sendsPatchW - 20.0f, h }, selected ? type::routingRowName : type::body, colour::text);

        // Input: amber with ⚠ when this computer hasn't the name.
        const auto& routing = owner.engine.getRouting();
        const auto input = patch.inputName().isNotEmpty() ? patch.inputName() : routing.inputFor ({}).name;
        const auto missing = patch.inputName().isNotEmpty() && routing.findInput (patch.inputName()) == nullptr;
        paint::text (g, missing ? input + juce::String (juce::CharPointer_UTF8 (" \xe2\x9a\xa0")) : input,
                     { px + (float) size::sendsPatchW, 0.0f, (float) size::sendsInputW - 8.0f, h }, type::body, missing ? colour::faultCardText : colour::textSoft);

        const auto channels = owner.session.getPatchOutputChannels (entry.key);
        paint::text (g, channels == 2 ? "stereo" : (channels == 1 ? "mono" : juce::String()),
                     { px + (float) (size::sendsPatchW + size::sendsInputW), 0.0f, (float) size::sendsEndW - 8.0f, h }, type::caption, colour::textMuted);

        for (size_t c = 0; c < columns.size(); ++c)
        {
            const auto x = (float) columnX ((int) c) - 1.0f;
            const auto send = patch.sendFor (columns[c].name);
            const auto on = send.on && send.levelDb > routing::silentDb;

            if (selected && (int) c == selectedColumn)
            {
                const auto cell = juce::Rectangle<float> (x, (h - (float) size::sendsCellH) * 0.5f, (float) columnW() - 8.0f, (float) size::sendsCellH);
                g.setColour (paint::c (colour::sendsCellWell));
                g.fillRoundedRectangle (cell, 4.0f);
                g.setColour (paint::c (colour::accent));
                g.drawRoundedRectangle (cell.reduced (0.5f), 4.0f, 1.0f);
            }

            // An output that isn't here: its sends dimmed (they stay silent here).
            const auto here = columns[c].here;

            if (! here)
                g.beginTransparencyLayer (state::disabledAlpha);

            if (! on)
            {
                paint::text (g, "off", { x + 8.0f, 0.0f, 40.0f, h }, type::body, colour::textDisabled);

                if (! here)
                    g.endTransparencyLayer();

                continue;
            }

            const auto bar = juce::Rectangle<float> (x + 8.0f, h * 0.5f - 2.0f, (float) size::sendsBarW + 6.0f, 4.0f);
            g.setColour (paint::c (colour::meterTrack));
            g.fillRoundedRectangle (bar, 2.0f);
            g.setColour (paint::c (colour::smallKnobArc));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (1.0f - kit::faderLaw::positionFor (send.levelDb))), 2.0f);
            paint::text (g, describeLevel (send.levelDb), { x + 86.0f, 0.0f, 70.0f, h }, type::body, colour::text);

            if (! here)
                g.endTransparencyLayer();
        }

        juce::ignoreUnused (width);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        const auto column = columnAt (e.x);
        select (row, column);
        dragRow = row;
        dragColumn = column;
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent& e) override
    {
        const auto column = columnAt (e.x);

        if (column < 0 || ! juce::isPositiveAndBelow (row, (int) patches.size()))
            return;

        select (row, column);
        const auto send = patches[(size_t) row].patch->sendFor (columns[(size_t) column].name);
        const auto rowArea = list.getRowPosition (row, true).translated (list.getX(), list.getY());
        editor.setBounds (columnX (column) + list.getX() + 2, rowArea.getY() + 4, 90, rowArea.getHeight() - 8);
        editor.setText (send.on && send.levelDb > routing::silentDb ? juce::String (send.levelDb, 1) : juce::String ("off"), false);
        editor.setVisible (true);
        editor.grabKeyboardFocus();
    }

    juce::String getNameForRow (int row) override
    {
        return juce::isPositiveAndBelow (row, (int) patches.size()) ? patches[(size_t) row].label : juce::String();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // A drag that started on a cell: sideways sets its level (on the send law).
        if (dragColumn < 0 || ! juce::isPositiveAndBelow (dragRow, (int) patches.size()))
            return;

        const auto local = e.getEventRelativeTo (this);
        const auto x0 = (float) (columnX (dragColumn) + 7 + size::routingPadX);
        const auto p = juce::jlimit (0.0f, 1.0f, ((float) local.x - x0) / (float) (size::sendsBarW + 6));
        setSend (dragRow, dragColumn, kit::faderLaw::dbFor (1.0f - p), true);
    }

private:
    int columnAt (int x) const
    {
        for (size_t c = 0; c < columns.size(); ++c)
            if (x >= columnX ((int) c) && x < columnX ((int) c) + columnW())
                return (int) c;

        return -1;
    }

    void setSend (int row, int column, float dB, bool on)
    {
        if (! juce::isPositiveAndBelow (row, (int) patches.size()) || ! juce::isPositiveAndBelow (column, (int) columns.size()))
            return;

        owner.session.setPatchSend (patches[(size_t) row].key, columns[(size_t) column].name, dB, on && dB > routing::silentDb);
        patches = owner.session.getAllPatches();   // the models moved
        list.repaintRow (row);
    }

    void commitEdit()
    {
        if (! editor.isVisible())
            return;

        editor.setVisible (false);
        const auto text = editor.getText().trim().replace (minus(), "-");

        if (text.startsWithIgnoreCase ("off") || text.containsIgnoreCase ("inf"))
            setSend (selectedRow, selectedColumn, routing::silentDb, false);
        else if (text.containsAnyOf ("0123456789"))
            setSend (selectedRow, selectedColumn, routing::clampLevel (text.getFloatValue()), true);
    }

    RoutingView& owner;
    juce::ListBox list;
    juce::TextEditor editor;
    std::vector<ShowSession::PatchEntry> patches;
    std::vector<Column> columns;
    int selectedRow = -1, selectedColumn = -1, dragRow = -1, dragColumn = -1;
};

//==============================================================================
RoutingView::RoutingView (AudioEngine& e, ShowSession& s, juce::AudioDeviceManager& dm)
    : engine (e), session (s), deviceManager (dm)
{
    setTitle ("Routing");

    tabs.setSegments ({ "Inputs & outputs", "Sends" });
    tabs.setTooltips ({ "This computer's inputs and outputs", "Every patch's sends (the show)" });
    tabs.onClick = [this] (int index) { setTab (index == 0 ? Tab::inputsOutputs : Tab::sends); };
    addAndMakeVisible (tabs);
    addAndMakeVisible (scopeChip);

    setButtonKeyCap (backButton, "F7");
    setButtonIcon (backButton, Icon::route, 0.0f, false);
    backButton.getProperties().remove ("encoreIcon");   // the key cap only
    backButton.setTooltip ("Back to the patches (F7)");
    backButton.setWantsKeyboardFocus (false);
    backButton.onClick = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (backButton);

    ioPage = std::make_unique<IoPage> (*this);
    sendsPage = std::make_unique<SendsPage> (*this);
    addAndMakeVisible (*ioPage);
    addChildComponent (*sendsPage);

    setTab (Tab::inputsOutputs);
    ioPage->rebuild();
}

RoutingView::~RoutingView() = default;

void RoutingView::setTab (Tab t)
{
    tab = t;
    const auto io = tab == Tab::inputsOutputs;
    tabs.setSelected (io ? 0 : 1);
    ioPage->setVisible (io);
    sendsPage->setVisible (! io);
    scopeChip.setText (paint::caps (type::tagChip, io ? "This computer" : "This show"));
    scopeChip.setStyle (io ? kit::ChipStyle::plain : kit::ChipStyle::exp);
    footer = io ? "Inputs and outputs belong to this computer (%APPDATA%\\Encore). Shows use the names: a show sending to "
                    + quoted ("In-ear") + " plays right on any rig that has an output called In-ear."
                : "Sends belong to the patches, so they travel with the show. Outputs shown in amber aren't on this computer: those sends stay silent here.";
    summary = summaryText();
    resized();
    repaint();
}

void RoutingView::selectOutput (int index)
{
    if (index != selectedOutput)
    {
        selectedOutput = index;
        ioPage->showSelected();
        ioPage->repaint();
    }
}

void RoutingView::selectSend (int row, int column)
{
    sendsPage->select (row, column);
}

void RoutingView::applyRouting (const RoutingConfig& routing)
{
    if (onSetRouting != nullptr)
        onSetRouting (routing);

    // Rebuilt after the click or edit that got here returns: it deletes the
    // rows, maybe the very control whose callback this is.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RoutingView> (this)]
    {
        if (safe != nullptr)
            safe->refresh();
    });
}

void RoutingView::refresh()
{
    // Rebuilt when what it shows changed (cheap to compare).
    juce::String state;
    const auto& routing = engine.getRouting();
    state << juce::JSON::toString (routingToVar (routing), true);

    if (auto* device = deviceManager.getCurrentAudioDevice())
        state << device->getName() << device->getInputChannelNames().size() << device->getOutputChannelNames().size()
              << device->getCurrentSampleRate() << device->getCurrentBufferSizeSamples();

    for (const auto& p : session.getAllPatches())
        if (p.patch != nullptr)
        {
            state << p.key << p.label << p.patch->inputName() << (int) p.patch->colour;

            for (const auto& send : p.patch->sends)
                state << send.output << send.levelDb << (int) send.on;
        }

    const auto hash = state.hashCode64();

    if (hash == lastState)
        return;

    lastState = hash;
    ioPage->rebuild();
    sendsPage->rebuild();
    summary = summaryText();
    repaint();
}

void RoutingView::update (const StageSnapshot& s)
{
    if (! isVisible())
        return;

    if (tab == Tab::inputsOutputs)
        ioPage->update (s);
    else
        sendsPage->update (s);
}

juce::String RoutingView::summaryText() const
{
    const auto& routing = engine.getRouting();

    if (tab == Tab::inputsOutputs)
    {
        auto text = juce::String ((int) routing.inputs.size()) + ((int) routing.inputs.size() == 1 ? " input" : " inputs") + middot()
                  + juce::String ((int) routing.outputs.size()) + ((int) routing.outputs.size() == 1 ? " output" : " outputs");

        if (auto* device = deviceManager.getCurrentAudioDevice())
            text << middot() << device->getName() << " (" << device->getInputChannelNames().size() << " in, "
                 << device->getOutputChannelNames().size() << " out)";

        return text;
    }

    const auto patches = session.getAllPatches();
    int own = 0;

    for (const auto& p : patches)
        own += p.key != (p.patch != nullptr ? p.patch->id : juce::String()) ? 1 : 0;

    auto names = session.getShow().outputNamesUsed();

    for (const auto& o : routing.outputs)
        names.addIfNotAlreadyThere (o.name);

    return juce::String ((int) patches.size() - own) + " patches" + middot() + juce::String (own) + " own patches" + middot()
         + juce::String (names.size()) + " output names";
}

void RoutingView::resized()
{
    const auto header = getLocalBounds().reduced (1).removeFromTop (size::routingHeaderH);
    tabs.setBounds (size::routingTabsX, header.getY() + (header.getHeight() - size::routingTabsH) / 2, size::routingTabsW, size::routingTabsH);
    const auto chipW = scopeChip.getIdealWidth();
    scopeChip.setBounds (tabs.getRight() + size::routingChipGap, header.getCentreY() - size::pluginsBlockedRowH / 5,
                         chipW, size::paramListChipH);
    backButton.setBounds (header.getRight() - 8 - size::routingBackW, header.getY() + (header.getHeight() - size::buttonHeight) / 2,
                          size::routingBackW, size::buttonHeight);

    const auto content = getLocalBounds().withTrimmedTop (1 + size::routingHeaderH).withTrimmedBottom (1 + size::routingFooterH);
    ioPage->setBounds (content);
    sendsPage->setBounds (content);
}

void RoutingView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    paint::pane (g, r);

    // Sub-toolbar: icon, "Routing", (tabs, chip), the summary right of them.
    const auto header = r.reduced (1.0f).removeFromTop ((float) size::routingHeaderH);
    paint::verticalGradient (g, header, colour::subToolbarTop, colour::subToolbarBot, 0.0f);
    g.setColour (paint::c (colour::paneHeaderLine));
    g.fillRect (header.withTop (header.getBottom() - 1.0f));

    drawIcon (g, Icon::route, { (float) size::routingIconX, header.getCentreY() - (float) size::routingIcon * 0.5f,
                                (float) size::routingIcon, (float) size::routingIcon }, paint::c (colour::textSoft));
    paint::text (g, "Routing", header.withX ((float) size::routingTitleX).withWidth (90.0f), type::routingTitle, colour::text);

    const auto summaryArea = header.withLeft ((float) scopeChip.getRight() + 10.0f).withRight ((float) backButton.getX() - 22.0f);
    paint::text (g, summary, summaryArea, type::bodySmall, colour::textTertiary, juce::Justification::centredRight);

    // Footer: a line above, the note.
    const auto foot = r.reduced (1.0f).removeFromBottom ((float) size::routingFooterH);
    g.setColour (paint::c (colour::paneHeaderLine));
    g.fillRect (foot.withHeight (1.0f));
    paint::text (g, footer, foot.withTrimmedLeft ((float) size::routingPadX - 1.0f).withTrimmedRight ((float) size::routingPadX),
                 type::noteText, colour::textTertiary);
}

} // namespace encore
