// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/JackStage.h"

#include "ui/kit/Chip.h"
#include "ui/kit/Fader.h"
#include "ui/kit/LevelBars.h"
#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreLookAndFeel.h"
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

    juce::String channelsLine (const RoutingOutput& o)
    {
        return describeShortChannels (o) + middot() + (o.isAllOutputs() ? "all" : (o.isStereo() ? "stereo" : "mono"));
    }
}

//==============================================================================
/** A send: the output's name, its channels (or MUTED / NOT HERE), On, the
    fader beside the output's meters, the level. */
class JackStage::SendStrip final : public juce::Component
{
public:
    SendStrip (JackStage& o, const juce::String& n) : owner (o), name (n)
    {
        setTitle ("Send to " + name);

        on.setClickingTogglesState (false);
        on.setTooltip ("Send to " + name + ": on or off");
        on.setWantsKeyboardFocus (false);
        on.onClick = [this] { setSend (fader.getDb(), ! sendOn); };
        addAndMakeVisible (on);

        fader.setTitle ("Send level to " + name);
        fader.onChange = [this] (float dB) { setSend (dB, sendOn || dB > routing::silentDb); };
        addAndMakeVisible (fader);

        meter.setBarThickness (size::levelBarH);
        meter.setGap (size::levelBarGap);
        meter.setGreenShare (0.7f);
        addAndMakeVisible (meter);

        value.setFont (font (type::bodySmall));
        value.setJustificationType (juce::Justification::centred);
        value.setColour (juce::Label::textColourId, paint::c (colour::fieldText));
        value.setColour (juce::TextEditor::backgroundColourId, paint::c (colour::fieldFill));
        value.setColour (juce::TextEditor::textColourId, paint::c (colour::fieldText));
        value.setEditable (false, true, false);
        value.setBorderSize ({});
        value.setTooltip ("Double-click to type the send level (dB, or off)");
        value.onTextChange = [this]
        {
            const auto text = value.getText().trim().replace (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")), "-");

            if (text.startsWithIgnoreCase ("off") || text.containsIgnoreCase ("inf"))
                setSend (fader.getDb(), false);
            else if (text.containsAnyOf ("0123456789"))
                setSend (routing::clampLevel (text.getFloatValue()), true);
        };
        addAndMakeVisible (value);
    }

    /** From the patch and the routing. */
    void show (const SendModel& send, const RoutingOutput* output, bool muted)
    {
        here = output != nullptr;
        sendOn = send.on && send.levelDb > routing::silentDb;
        fader.setDb (send.levelDb);
        fader.setDimmed (! sendOn);
        value.setText (describeLevel (send.levelDb, sendOn), juce::dontSendNotification);
        on.setButtonText ({});   // the dot and the word are painted over it
        on.setTitle (sendOn ? "On" : "Off");
        on.setToggleState (sendOn, juce::dontSendNotification);
        sub = output != nullptr ? channelsLine (*output) : juce::String();
        chip = ! here ? paint::caps (type::tagChip, "Not here") : (muted ? paint::caps (type::tagChip, "Output muted") : juce::String());
        meter.setNumChannels (output != nullptr && ! output->isStereo() ? 1 : 2);
        meter.setMuted (muted || ! here);
        setAlpha (here ? 1.0f : 0.55f);
        repaint();
    }

    void resized() override
    {
        on.setBounds (size::sendOnX, size::sendOnTop, size::sendOnW, size::sendOnH);
        const auto faderW = size::sendMeterX - 2;
        fader.setBounds (0, size::sendFaderTop, faderW, size::sendFaderTrackH + size::faderThumbH);
        const auto track = fader.getTrackBounds().toNearestInt() + fader.getPosition();
        meter.setBounds (size::sendMeterX - 1, track.getY(), 2 * size::levelBarH + size::levelBarGap + 2, track.getHeight());
        value.setBounds (size::sendValueX, size::sendValueTop, size::sendValueW, size::sendValueH);
    }

    void paint (juce::Graphics& g) override
    {
        const auto w = (float) getWidth();
        paint::text (g, name, { 0.0f, (float) size::sendNameTop, w, 15.0f }, type::sendName, here ? colour::text : colour::faultCardText,
                     juce::Justification::centred);

        if (chip.isNotEmpty())
        {
            const auto cw = kit::chipWidth (chip);
            kit::paintChip (g, juce::Rectangle<float> (cw, 16.0f).withCentre ({ w * 0.5f, (float) size::sendSubTop + 8.0f }), chip,
                            here ? kit::ChipStyle::mute : kit::ChipStyle::amber);
        }
        else
        {
            paint::text (g, sub, { 0.0f, (float) size::sendSubTop, w, 12.0f }, type::sendSub, colour::textMuted, juce::Justification::centred);
        }

        paint::inset (g, value.getBounds().toFloat(), colour::fieldFill, colour::fieldBorder, (float) size::fieldRadius, colour::fieldShadow);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        // On: a green dot before the word (grey when off).
        const auto b = on.getBounds().toFloat();
        g.setColour (paint::c (sendOn ? colour::healthy : colour::dotOff));
        const auto dot = (float) size::sendOnDot;
        g.fillEllipse (b.getX() + (float) size::sendOnDotX, b.getCentreY() - dot * 0.5f, dot, dot);
        const auto x = b.getX() + (float) (size::sendOnDotX + size::sendOnDot + size::sendOnTextGap);
        paint::text (g, on.getTitle(), b.withLeft (x).withTrimmedBottom (1.0f), type::buttonSmall, colour::text, juce::Justification::centredLeft);
    }

    const juce::String name;
    kit::LevelBars meter { kit::LevelBars::Orientation::vertical, 2 };

private:
    void setSend (float dB, bool newOn)
    {
        owner.session.setPatchSend (owner.session.getActivePatchId(), name, dB, newOn && dB > routing::silentDb);
        owner.refresh();
    }

    JackStage& owner;
    juce::TextButton on;
    kit::Fader fader { kit::Fader::Orientation::vertical };
    juce::Label value;
    juce::String sub, chip;
    bool here = true, sendOn = true;
};

//==============================================================================
/** An input as a card: name, channel, meter; a click picks it for the patch. */
class JackStage::InputCard final : public juce::Component
{
public:
    enum class Mark { none, playing, playingInstead, savedNotHere };

    InputCard (JackStage& o, const juce::String& n, const juce::String& c, Mark m) : owner (o), name (n), caption (c), mark (m)
    {
        setTitle ("Input " + name);
        setMouseCursor (mark == Mark::savedNotHere ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
        meter.setBarThickness (size::inputCardMeterH);
        addAndMakeVisible (meter);   // (a name that isn't here: an empty track)

        chip = mark == Mark::savedNotHere   ? paint::caps (type::tagChip, juce::String ("Saved") + middot() + "Not here")
             : mark == Mark::playingInstead ? paint::caps (type::tagChip, "Playing instead")
             : mark == Mark::playing        ? paint::caps (type::tagChip, "Playing")
                                            : juce::String();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (mark == Mark::savedNotHere || ! getLocalBounds().contains (e.getPosition()))
            return;

        // The patch listens to this one from now on (saved with the show).
        owner.session.setPatchInput (owner.session.getActivePatchId(), name);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<JackStage> (&owner)]
        {
            if (safe != nullptr)
                safe->refresh();
        });
    }

    void resized() override
    {
        meter.setBounds (size::inputCardPad, size::inputCardMeterTop, getWidth() - 2 * size::inputCardPad, size::inputCardMeterH);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const auto radius = (float) size::inputCardRadius;
        g.setGradientFill (juce::ColourGradient::vertical (paint::c (colour::inputCardTop), r.getY(), paint::c (colour::inputCardBottom), r.getBottom()));
        g.fillRoundedRectangle (r, radius);

        const auto border = mark == Mark::savedNotHere ? colour::amber : (mark == Mark::playing ? colour::accent : colour::inputCardBorder);
        const auto thick = mark == Mark::savedNotHere || mark == Mark::playing ? 2.0f : 1.0f;
        g.setColour (paint::c (border));
        g.drawRoundedRectangle (r.reduced (thick * 0.5f), radius - thick * 0.5f, thick);

        drawIcon (g, Icon::jack, { (float) size::inputCardIconX, (float) size::inputCardIconTop, (float) size::inputCardIcon, (float) size::inputCardIcon },
                  paint::c (colour::textSoft));

        if (chip.isNotEmpty())
        {
            const auto cw = kit::chipWidth (chip);
            kit::paintChip (g, { r.getRight() - (float) size::inputCardPad + 2.0f - cw, (float) size::inputCardChipTop, cw, (float) size::tagChipH }, chip,
                            mark == Mark::savedNotHere ? kit::ChipStyle::amber : kit::ChipStyle::green);
        }

        const auto x = (float) size::inputCardPad - 1.0f;
        const auto w = r.getWidth() - 2.0f * x;
        paint::text (g, name, { x, (float) size::inputCardNameTop, w, 22.0f }, type::inputCardName,
                     mark == Mark::savedNotHere ? colour::faultCardText : colour::text);
        paint::text (g, caption, { x, (float) size::inputCardCaptionTop, w, 14.0f }, type::caption, colour::textMuted);
    }

    const juce::String name;
    kit::LevelBars meter { kit::LevelBars::Orientation::horizontal, 1 };

private:
    JackStage& owner;
    const juce::String caption;
    const Mark mark;
    juce::String chip;
};

//==============================================================================
JackStage::JackStage (Kind k, AudioEngine& e, ShowSession& s, juce::AudioDeviceManager& dm)
    : kind (k), engine (e), session (s), deviceManager (dm)
{
    setHeaderColours (colour::stageHeaderTop, colour::stageHeaderBot);
    setTitle (kind == Kind::input ? "Input" : "Outputs");

    setVariant (routingButton, Variant::small);
    setButtonKeyCap (routingButton, "F7");
    routingButton.setTooltip ("Routing: this computer's inputs and outputs (F7)");
    routingButton.setWantsKeyboardFocus (false);
    routingButton.onClick = [this] { if (onOpenRouting != nullptr) onOpenRouting(); };
    addChildComponent (routingButton);
    routingButton.setVisible (kind == Kind::output);

    refresh();
}

JackStage::~JackStage() = default;

juce::String JackStage::patchName() const
{
    const auto* patch = session.getPatch (session.getActivePatchId());
    return patch != nullptr ? patch->name : juce::String();
}

bool JackStage::refresh()
{
    // Rebuilt when what it shows changed (cheap to compare).
    juce::String state;
    state << session.getActivePatchId() << juce::JSON::toString (routingToVar (engine.getRouting()), true);

    if (const auto* patch = session.getPatch (session.getActivePatchId()))
    {
        state << patch->inputName() << session.getPatchOutputChannels (session.getActivePatchId());

        for (const auto& send : patch->sends)
            state << send.output << send.levelDb << (int) send.on;
    }

    for (const auto& o : engine.getRouting().outputs)
        if (const auto slot = engine.getOutputSlot (o.name); slot >= 0)
            state << (int) engine.isOutputMuted (slot);

    if (auto* device = deviceManager.getCurrentAudioDevice())
        state << device->getInputChannelNames().joinIntoString ("|");

    const auto hash = state.hashCode64();

    if (hash == lastState)
        return false;

    lastState = hash;
    rebuild();
    return true;
}

void JackStage::rebuild()
{
    const auto& routing = engine.getRouting();
    const auto* patch = session.getPatch (session.getActivePatchId());

    if (kind == Kind::output)
    {
        // This computer's outputs, then the patch's names that aren't here.
        juce::StringArray names;

        for (const auto& o : routing.outputs)
            names.add (o.name);

        if (patch != nullptr)
            for (const auto& s : patch->sends)
                if (routing.findOutput (s.output) == nullptr)
                    names.addIfNotAlreadyThere (s.output);

        const auto sameNames = (int) strips.size() == names.size()
                            && std::equal (strips.begin(), strips.end(), names.begin(), [] (const auto& strip, const juce::String& n) { return strip->name == n; });

        if (! sameNames)
        {
            strips.clear();

            for (const auto& n : names)
            {
                strips.push_back (std::make_unique<SendStrip> (*this, n));
                addAndMakeVisible (*strips.back());
            }
        }

        for (auto& strip : strips)
        {
            const auto* output = routing.findOutput (strip->name);
            const auto slot = output != nullptr ? engine.getOutputSlot (strip->name) : -1;
            strip->show (patch != nullptr ? patch->sendFor (strip->name) : SendModel { strip->name },
                         output, slot >= 0 && engine.isOutputMuted (slot));
        }

        setHeader ("Out" + middot() + "sends of " + quoted (patchName()));
        setHeaderDot (colour::healthy);
        rightNote = juce::String ((int) names.size()) + " outputs" + middot();
        const auto channels = session.getPatchOutputChannels (session.getActivePatchId());
        const auto chain = engine.getChain();
        footer = { { "Patch ends ", channels == 1 ? "mono" : "stereo", chain != nullptr && chain->hasSplit() ? " (merge)" : "" },
                   { "Outputs not listed get this patch at 0 dB", {}, {} },
                   { "Tails keep their own sends", {}, {} } };
    }
    else
    {
        // The patch's saved name first when this computer hasn't it.
        cards.clear();
        const auto wanted = patch != nullptr ? patch->inputName() : juce::String();
        const auto missing = wanted.isNotEmpty() && routing.findInput (wanted) == nullptr;
        const auto& playing = routing.inputs.empty() ? RoutingInput() : routing.inputFor (wanted);
        auto* device = deviceManager.getCurrentAudioDevice();
        const auto channelNames = device != nullptr ? device->getInputChannelNames() : juce::StringArray();

        if (missing)
            cards.push_back (std::make_unique<InputCard> (*this, wanted, "not on this computer", InputCard::Mark::savedNotHere));

        for (const auto& in : routing.inputs)
        {
            auto caption = "input " + juce::String (in.channel);

            if (juce::isPositiveAndBelow (in.channel - 1, channelNames.size()))
                caption << middot() << channelNames[in.channel - 1];

            const auto isPlaying = routing::sameName (in.name, playing.name);
            cards.push_back (std::make_unique<InputCard> (*this, in.name, caption,
                                                          ! isPlaying ? InputCard::Mark::none
                                                          : missing   ? InputCard::Mark::playingInstead : InputCard::Mark::playing));
        }

        for (auto& card : cards)
            addAndMakeVisible (*card);

        setHeader ("In" + middot() + "input of " + quoted (patchName()));
        setHeaderDot (missing ? colour::amber : colour::healthy);
        rightNote = "input " + juce::String (playing.channel) + " playing";
        intro = "This patch listens to one input. Pick it here; inputs are named in Routing (this computer).";
        warningLead = missing ? quoted (wanted) + " isn't an input on this computer" : juce::String();
        warningRest = missing ? ", so " + quoted (patchName()) + " plays " + playing.name + " (input " + juce::String (playing.channel)
                                    + ") instead. Pick an input here to change the patch, or add an input called " + wanted + " in Routing."
                              : juce::String();
    }

    setHeaderRightText (kind == Kind::input ? rightNote : juce::String());
    resized();
    repaint();
}

void JackStage::update (const StageSnapshot& s)
{
    for (auto& strip : strips)
    {
        const auto slot = engine.getOutputSlot (strip->name);

        if (slot >= 0 && engine.getRouting().findOutput (strip->name) != nullptr)
            strip->meter.setPeaks (s.outputSlotPeaks[(size_t) slot][0], s.outputSlotPeaks[(size_t) slot][1]);
    }

    for (auto& card : cards)
    {
        if (engine.getRouting().findInput (card->name) == nullptr)
            continue;

        const auto slot = engine.getInputSlot (card->name);
        card->meter.setPeaks (juce::isPositiveAndBelow (slot, routing::maxInputs) ? s.inputSlotPeaks[(size_t) slot] : 0.0f, 0.0f);
    }
}

std::vector<kit::InfoList::Section> JackStage::getInspectorSections() const
{
    using Section = kit::InfoList::Section;
    const auto& routing = engine.getRouting();
    const auto* patch = session.getPatch (session.getActivePatchId());
    const auto wanted = patch != nullptr ? patch->inputName() : juce::String();
    const auto missing = wanted.isNotEmpty() && routing.findInput (wanted) == nullptr;
    const auto& playing = routing.inputs.empty() ? RoutingInput() : routing.inputFor (wanted);
    std::vector<Section> sections;

    if (kind == Kind::output)
    {
        Section sends { "Sends", "this patch", {}, {}, {}, {} };

        for (const auto& strip : strips)
        {
            const auto send = patch != nullptr ? patch->sendFor (strip->name) : SendModel { strip->name };
            const auto on = send.on && send.levelDb > routing::silentDb;
            const auto* output = routing.findOutput (strip->name);
            const auto slot = output != nullptr ? engine.getOutputSlot (strip->name) : -1;

            if (output == nullptr)
                sends.rows.push_back ({ strip->name, describeLevel (send.levelDb, on) + middot() + "not on this computer", {}, true, {} });
            else
                sends.rows.push_back ({ strip->name, describeLevel (send.levelDb, on), {}, false,
                                        slot >= 0 && engine.isOutputMuted (slot) ? paint::caps (type::tagChip, "Muted") : juce::String() });
        }

        sections.push_back (sends);

        Section signal { "Signal", {}, {}, {}, {}, {} };
        signal.rows.push_back ({ "Input", playing.name, juce::String (juce::CharPointer_UTF8 ("\xc2\xb7 ")) + "input " + juce::String (playing.channel), missing, {} });
        const auto channels = session.getPatchOutputChannels (session.getActivePatchId());
        const auto chain = engine.getChain();
        signal.rows.push_back ({ "At the end", channels == 1 ? "Mono" : "Stereo", chain != nullptr && chain->hasSplit() ? "(merge)" : juce::String(), false, {} });

        if (chain != nullptr && chain->hasSplit())
        {
            const auto diff = chain->getPathLatencyDifference();
            const auto ms = std::abs (diff) * 1000.0 / juce::jmax (1.0, engine.getCurrentSpec().sampleRate);
            signal.rows.push_back ({ "Paths", diff == 0 ? juce::String ("Lined up")
                                                        : juce::String (diff > 0 ? "A" : "B") + " delayed " + juce::String (ms, 1) + " ms",
                                     diff == 0 ? juce::String() : juce::String ("to line up"), false, {} });
        }

        sections.push_back (signal);
        sections.push_back ({ "Outputs", "this computer", {},
                              "Outputs, their channels, level, EQ and delay belong to this computer. Shows use the names.", "Open routing", "F7", Icon::route });
        return sections;
    }

    Section input { "Input", {}, {}, {}, {}, {} };
    input.rows.push_back ({ "Type", "Input", {}, false, {} });

    if (missing)
        input.rows.push_back ({ "Saved", wanted + middot() + "not on this computer", {}, true, {} });

    input.rows.push_back ({ "Playing", playing.name, juce::String (juce::CharPointer_UTF8 ("\xc2\xb7 ")) + "input " + juce::String (playing.channel), false, {} });
    input.rows.push_back ({ "Tuner", "listens to this input", {}, false, {} });
    sections.push_back (input);

    juce::StringArray list;

    for (const auto& in : routing.inputs)
        list.add (in.name + " (" + juce::String (in.channel) + ")");

    sections.push_back ({ "Inputs", "this computer", {},
                          juce::String ((int) routing.inputs.size()) + ((int) routing.inputs.size() == 1 ? " input: " : " inputs: ")
                              + list.joinIntoString (", ") + ". Add, rename or change channels in Routing.",
                          "Open routing", "F7", Icon::route });
    return sections;
}

void JackStage::resized()
{
    Pane::resized();
    const auto content = getContentBounds();

    if (kind == Kind::output)
    {
        // The header's right side: "4 outputs ·" then Routing... F7.
        auto header = getHeaderBounds().withTrimmedRight (size::paneHeaderPadR);
        const auto w = routingButton.getBestWidthForHeight (size::buttonSmallHeight);
        routingButton.setBounds (header.removeFromRight (w).withTrimmedTop ((header.getHeight() - size::buttonSmallHeight) / 2)
                                                           .withHeight (size::buttonSmallHeight + size::buttonDrop));
        setHeaderRightInset (w + size::outGap);

        const auto n = juce::jmax (1, (int) strips.size());
        const auto boxW = 2 * size::sendBoxPad + n * size::sendStripW + (n - 1) * size::sendStripGap;
        box = juce::Rectangle<int> (boxW, size::sendBoxH).withCentre (content.getCentre()).withY (content.getY() + size::sendBoxTop);
        auto x = box.getX() + size::sendBoxPad;

        for (auto& strip : strips)
        {
            strip->setBounds (x, box.getY(), size::sendStripW, box.getHeight());   // its tokens are box-relative
            x += size::sendStripW + size::sendStripGap;
        }

        return;
    }

    const auto n = juce::jmax (1, (int) cards.size());
    const auto rowW = n * size::inputCardW + (n - 1) * size::inputCardGap;
    auto x = content.getCentreX() - rowW / 2;

    for (auto& card : cards)
    {
        card->setBounds (x, content.getY() + size::inputCardsTop, size::inputCardW, size::inputCardH);
        x += size::inputCardW + size::inputCardGap;
    }

    const auto left = content.getCentreX() - rowW / 2;
    warning = { left, content.getY() + size::inputWarningTop, rowW, size::inputWarningH };
}

void JackStage::paint (juce::Graphics& g)
{
    Pane::paint (g);
    backdrop.paint (g, getContentBounds());
    const auto content = getContentBounds().toFloat();

    if (kind == Kind::output)
    {
        // The header's note before the button.
        const auto noteW = textWidth (font (type::paneHeaderNote), rightNote) + 4.0f;
        paint::text (g, rightNote, juce::Rectangle<float> ((float) routingButton.getX() - noteW - 4.0f, (float) getHeaderBounds().getY(), noteW,
                                                           (float) getHeaderBounds().getHeight()),
                     type::paneHeaderNote, colour::textMuted, juce::Justification::centredRight);

        // The box, its highlight and shadow.
        const auto b = box.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (b.translated (0.0f, 10.0f).expanded (2.0f), (float) size::sendBoxRadius + 2.0f);
        g.setGradientFill (juce::ColourGradient::vertical (paint::c (colour::sendBoxTop), b.getY(), paint::c (colour::sendBoxBottom), b.getBottom()));
        g.fillRoundedRectangle (b, (float) size::sendBoxRadius);
        g.setColour (paint::c (colour::gearHighlight));
        paint::hline (g, b.getX() + (float) size::sendBoxRadius, b.getY() + 1.0f, b.getWidth() - 2.0f * (float) size::sendBoxRadius);
        g.setColour (paint::c (colour::paneBorder));
        g.drawRoundedRectangle (b.reduced (0.5f), (float) size::sendBoxRadius - 0.5f, 1.0f);

        // Under the box: the items, centred, a dot between them.
        const auto regular = font (type::noteText), strong = font (type::noteTextStrong);
        const auto dotW = textWidth (regular, middot().trim());
        auto total = 0.0f;

        for (const auto& item : footer)
            total += textWidth (regular, item.lead) + textWidth (strong, item.strong) + textWidth (regular, item.tail);

        total += (float) juce::jmax (0, (int) footer.size() - 1) * (dotW + 2.0f * (float) size::jackFooterSeparator);
        auto x = b.getCentreX() - total / 2.0f;
        const auto y = b.getBottom() + (float) size::jackFooterGap;

        const auto run = [&] (const juce::String& text, const juce::Font& f)
        {
            if (text.isEmpty())
                return;

            const auto w = textWidth (f, text);
            g.setFont (f);
            g.drawText (text, juce::Rectangle<float> (x, y, w + 1.0f, (float) size::jackFooterH), juce::Justification::centredLeft, false);
            x += w;
        };

        for (size_t i = 0; i < footer.size(); ++i)
        {
            if (i > 0)
            {
                x += (float) size::jackFooterSeparator;
                g.setColour (paint::c (colour::textTertiary));
                run (middot().trim(), regular);
                x += (float) size::jackFooterSeparator;
            }

            g.setColour (paint::c (colour::textTertiary));
            run (footer[i].lead, regular);
            g.setColour (paint::c (colour::text));
            run (footer[i].strong, strong);
            g.setColour (paint::c (colour::textTertiary));
            run (footer[i].tail, regular);
        }

        return;
    }

    paint::text (g, intro, content.withTop (content.getY() + (float) size::inputIntroTop).withHeight (15.0f)
                               .withTrimmedLeft ((float) (warning.getX() - getContentBounds().getX())),
                 type::bodySmall, colour::textSecondary);

    if (warningLead.isEmpty())
        return;

    // The warning under the cards: amber, its lead in bold.
    const auto w = warning.toFloat();
    g.setColour (paint::c (colour::faultCardBg));
    g.fillRoundedRectangle (w, 6.0f);
    g.setColour (paint::c (colour::chipAmberBg));
    g.drawRoundedRectangle (w.reduced (0.5f), 6.0f, 1.0f);
    drawIcon (g, Icon::warn, { w.getX() + 12.0f, w.getY() + 12.0f, 13.0f, 13.0f }, paint::c (colour::amber));

    juce::AttributedString text;
    text.append (warningLead, font (type::faultStrong).withHeight (12.0f), paint::c (colour::faultCardText));
    text.append (warningRest, font (type::bodySmall), paint::c (colour::faultCardText));
    text.setWordWrap (juce::AttributedString::byWord);
    text.setLineSpacing (3.0f);
    text.draw (g, w.withTrimmedLeft ((float) size::inputWarningPadX).withTrimmedRight (12.0f).withTrimmedTop (11.0f));
}

} // namespace encore
