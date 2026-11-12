// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// The reference boards' data (docs/design/reference/target): an audio device
// like the boards', and shows with their running orders, patches and notes.

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <cmath>
#include <vector>

#include "model/Routing.h"
#include "model/ShowFolder.h"
#include "model/ShowModel.h"
#include "model/StageLayout.h"

namespace encore::test
{

/** What a board's audio interface looks like. */
struct BoardDeviceSpec
{
    juce::String name = "Headset (2- EarPods)", type = "Windows Audio";
    juce::StringArray inputs { "Input channel 1", "Input channel 2" };
    juce::StringArray outputs { "Output 1", "Output 2" };
    int bufferSize = 480, latency = 480, xruns = 1;
    float toneLevel = 0.0f;   // a 220 Hz sine on every input (0 = silence), so meters move
};

/** The M11 boards' interface: Focusrite USB ASIO, 4 in and 4 out, 64
    samples, 5.3 ms round trip, a guitar-like tone on the inputs. */
inline BoardDeviceSpec focusriteSpec()
{
    BoardDeviceSpec spec;
    spec.name = "Focusrite USB ASIO";
    spec.type = "ASIO";
    spec.inputs = { "Input 1 (Mic/Inst)", "Input 2 (Inst)", "Input 3", "Input 4" };
    spec.outputs = { "Output 1", "Output 2", "Output 3", "Output 4" };
    spec.bufferSize = 64;
    spec.latency = 128;
    spec.xruns = 0;
    spec.toneLevel = 0.34f;   // about -9.4 dBFS
    return spec;
}

/** An audio interface that plays from its own thread: by default like the
    reference board's "Headset (2- EarPods)" on Windows Audio, 48 kHz,
    480 samples, 10 ms in and out (20.0 ms round trip), silent. */
class BoardDevice final : public juce::AudioIODevice,
                          private juce::Thread
{
public:
    explicit BoardDevice (const BoardDeviceSpec& s = {})
        : juce::AudioIODevice (s.name, s.type), juce::Thread ("Board device"), spec (s) {}
    ~BoardDevice() override { close(); }

    juce::StringArray getOutputChannelNames() override       { return spec.outputs; }
    juce::StringArray getInputChannelNames() override        { return spec.inputs; }
    juce::Array<double> getAvailableSampleRates() override   { return { 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override      { return { spec.bufferSize }; }
    int getDefaultBufferSize() override                      { return spec.bufferSize; }

    juce::String open (const juce::BigInteger& ins, const juce::BigInteger& outs, double, int) override
    {
        activeIns = ins;
        activeOuts = outs;
        opened = true;
        return {};
    }

    void close() override
    {
        stop();
        opened = false;
    }

    bool isOpen() override                                   { return opened; }

    void start (juce::AudioIODeviceCallback* cb) override
    {
        if (cb == nullptr || callback != nullptr)
            return;

        cb->audioDeviceAboutToStart (this);
        callback = cb;
        startThread();
    }

    void stop() override
    {
        stopThread (2000);

        if (auto* cb = std::exchange (callback, nullptr))
            cb->audioDeviceStopped();
    }

    bool isPlaying() override                                { return callback != nullptr; }
    juce::String getLastError() override                     { return {}; }
    int getCurrentBufferSizeSamples() override               { return spec.bufferSize; }
    double getCurrentSampleRate() override                   { return 48000.0; }
    int getCurrentBitDepth() override                        { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return activeOuts; }
    juce::BigInteger getActiveInputChannels() const override  { return activeIns; }
    int getOutputLatencyInSamples() override                 { return spec.latency; }
    int getInputLatencyInSamples() override                  { return spec.latency; }
    int getXRunCount() const noexcept override               { return spec.xruns; }

private:
    void run() override
    {
        const auto n = spec.bufferSize;
        const auto numIns = juce::jmax (1, spec.inputs.size()), numOuts = juce::jmax (1, spec.outputs.size());
        juce::AudioBuffer<float> in (numIns, n), out (numOuts, n);
        in.clear();
        double phase = 0.0;
        const auto step = juce::MathConstants<double>::twoPi * 220.0 / 48000.0;
        const auto period = juce::jmax (1, juce::roundToInt (1000.0 * n / 48000.0));

        while (! threadShouldExit())
        {
            if (spec.toneLevel > 0.0f)
            {
                for (int i = 0; i < n; ++i, phase += step)
                    for (int ch = 0; ch < numIns; ++ch)
                        in.setSample (ch, i, spec.toneLevel * (float) std::sin (phase));
            }

            // Only the active channels, packed, as a real device hands them over.
            std::vector<const float*> ins;
            std::vector<float*> outs;

            for (int ch = 0; ch < numIns; ++ch)
                if (activeIns[ch])
                    ins.push_back (in.getReadPointer (ch));

            for (int ch = 0; ch < numOuts; ++ch)
                if (activeOuts[ch])
                    outs.push_back (out.getWritePointer (ch));

            out.clear();
            callback->audioDeviceIOCallbackWithContext (ins.data(), (int) ins.size(), outs.data(), (int) outs.size(), n, {});
            wait (period);
        }
    }

    BoardDeviceSpec spec;
    juce::BigInteger activeIns, activeOuts;
    juce::AudioIODeviceCallback* volatile callback = nullptr;
    bool opened = false;
};

class BoardDeviceType final : public juce::AudioIODeviceType
{
public:
    explicit BoardDeviceType (const BoardDeviceSpec& s = {}) : juce::AudioIODeviceType (s.type), spec (s) {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool) const override  { return { spec.name }; }
    int getDefaultDeviceIndex (bool) const override         { return 0; }
    int getIndexOfDevice (juce::AudioIODevice* d, bool) const override { return d != nullptr ? 0 : -1; }
    bool hasSeparateInputsAndOutputs() const override       { return false; }

    juce::AudioIODevice* createDevice (const juce::String&, const juce::String&) override
    {
        return new BoardDevice (spec);
    }

private:
    BoardDeviceSpec spec;
};

/** The M11 boards' computer (REFERENCES.md §4): inputs Guitar (1) and
    Acoustic (2); outputs Front of house (1/2), In-ear (3/4, -6 dB, its EQ
    with a low cut at 80 Hz) and Amp DI (5, muted: the interface has 4). */
inline RoutingConfig makeBoardRouting()
{
    RoutingConfig r;
    r.inputs = { { "Guitar", 1 }, { "Acoustic", 2 } };

    RoutingOutput front;
    front.name = "Front of house";
    front.channels = { 1, 2 };

    RoutingOutput inEar;
    inEar.name = "In-ear";
    inEar.channels = { 3, 4 };
    inEar.levelDb = -6.0f;
    inEar.eq.lowCut = 80.0f;
    inEar.eq.mid = 1.5f;
    inEar.eq.treble = -2.0f;

    RoutingOutput amp;
    amp.name = "Amp DI";
    amp.channels = { 5 };
    amp.mute = true;

    r.outputs = { front, inEar, amp };
    return r;
}

/** The M11 boards' show: Dual amp listens to "Mic" (not on the computer) and
    splits into two amps (path B 3 dB down, EXP 1 on Level B, stomp 2 path B
    on/off); every patch sends to the outputs as on M11RoutingSends,
    including "Monitor wedge", which isn't on the computer. */
inline juce::File makeRoutingShow (const juce::File& parent)
{
    auto show = makeDefaultShow ("My Show");
    show.showTemplate = ShowTemplate::band;

    auto base = show.patches[0];

    for (auto& b : base.mainLane().blocks)
        if (b.type == "gate" || b.type == "eq")
            b.enabled = false;

    auto block = [] (const char* type, bool enabled = true)
    {
        BlockModel b;
        b.id = juce::Uuid().toDashedString();
        b.type = type;
        b.enabled = enabled;
        return b;
    };

    struct Send { const char* output; float db; bool on; };

    auto makePatch = [&] (const char* name, juce::uint32 colour, const char* input, std::vector<Send> sends)
    {
        auto p = duplicatePatch (base, name);
        p.colour = colour;
        p.mainLane().input = input;

        for (auto& snd : sends)
            p.setSend (snd.output, snd.db, snd.on);

        return p;
    };

    const auto off = routing::silentDb;
    std::vector<PatchModel> patches;
    patches.push_back (makePatch ("Clean", 0xff4C8DF6, "Guitar",
                                  { { "Front of house", 0, true }, { "In-ear", 0, true }, { "Amp DI", off, false }, { "Monitor wedge", off, false } }));

    // Dual amp: gate, split, amp + cab over amp + cab, merge, delay.
    auto dual = makePatch ("Dual amp", 0xffFF6B4A, "Mic",
                           { { "Front of house", 0, true }, { "In-ear", -6, true }, { "Amp DI", 0, true }, { "Monitor wedge", -3, true } });
    {
        auto& lanes = dual.lanes;
        lanes.clear();
        lanes.resize (2);
        lanes[0].input = "Mic";
        auto split = block (splitBlockType);
        split.stomp = 2;
        auto merge = block (mergeBlockType);
        merge.params[mergeParams::panA] = -100.0f;
        merge.params[mergeParams::levelB] = -3.0f;
        merge.params[mergeParams::panB] = 100.0f;
        merge.expression[mergeParams::levelB] = { 1, routing::silentDb, 0.0f };
        auto delay = block ("delay");
        lanes[0].blocks = { block ("gate"), split, block ("nam"), block ("cab"), merge, delay };
        lanes[1].blocks = { block ("nam"), block ("cab") };
        normaliseSplit (dual);
    }
    patches.push_back (dual);

    patches.push_back (makePatch ("Crunch", 0xffFF9F0A, "Guitar",
                                  { { "Front of house", 0, true }, { "In-ear", -3, true }, { "Amp DI", 0, true }, { "Monitor wedge", off, false } }));
    patches.push_back (makePatch ("Acoustic", 0xff30C25A, "Acoustic",
                                  { { "Front of house", -3, true }, { "In-ear", 0, true }, { "Amp DI", off, false }, { "Monitor wedge", off, false } }));
    patches.push_back (makePatch ("Edge", 0xff30C25A, "Guitar",
                                  { { "Front of house", 0, true }, { "In-ear", 0, true }, { "Amp DI", 0, true }, { "Monitor wedge", off, false } }));
    patches.push_back (makePatch ("Lead", 0xffFF6B4A, "Guitar",
                                  { { "Front of house", 0, true }, { "In-ear", -6, true }, { "Amp DI", 0, true }, { "Monitor wedge", 0, true } }));
    show.patches = patches;

    std::map<juce::String, juce::String> ids;

    for (auto& p : show.patches)
        ids[p.name] = p.id;

    auto cue = [&] (const char* number, const char* name, const char* patch, int pc = -1, bool out = false)
    {
        auto c = RunItem::makeCue (number, name, ids[patch]);
        c.programChange = pc;

        if (out)
            c.midiOut.push_back ({ MidiOutMessage::Kind::programChange, 1, 7, 0 });

        return c;
    };

    auto set1 = RunItem::makeSection ("Set 1", "set");
    set1.children = { cue ("1", "Clean", "Clean"), cue ("2", "Dual amp intro", "Dual amp", 2), cue ("3", "Opener", "Crunch"),
                      cue ("4", "Ballad in D", "Acoustic") };
    auto set2 = RunItem::makeSection ("Set 2", "set");
    set2.children = { cue ("5", "Shuffle", "Edge"), cue ("6", "Rock Medley", "Lead", 7, true), cue ("7", "Closer", "Dual amp") };
    auto encoreSet = RunItem::makeSection ("Encore", "set");
    encoreSet.children = { cue ("8", "Last Call", "Lead") };
    show.runningOrder.children = { set1, set2, encoreSet };

    const auto folder = parent.getChildFile ("My Show");
    ShowFolder::create (folder, show);
    return folder;
}

/** The reference board's show: three sets, eight songs, five patches. */
inline juce::File makeBoardShow (const juce::File& parent)
{
    auto show = makeDefaultShow ("My Show");
    show.showTemplate = ShowTemplate::band;

    const std::pair<const char*, juce::uint32> patches[] = {
        { "Clean", 0xff4C8DF6 }, { "Clean 2", 0xff4C8DF6 }, { "Crunch", 0xffFF9F0A }, { "Edge", 0xff30C25A }, { "Lead", 0xffFF6B4A },
    };

    // As on the board: stomp 1 turns the amp on and off, EXP 1 moves its
    // output; gate and EQ bypassed, a bypassed delay at the end.
    for (auto& b : show.patches[0].mainLane().blocks)
    {
        if (b.type == "nam")
        {
            b.stomp = 1;
            b.expression["output"] = { 1, -40.0f, 12.0f };
        }
        else if (b.type == "gate" || b.type == "eq")
        {
            b.enabled = false;
        }
    }

    BlockModel delay;
    delay.id = juce::Uuid().toDashedString();
    delay.type = "delay";
    delay.enabled = false;
    show.patches[0].mainLane().blocks.push_back (delay);

    std::map<juce::String, juce::String> ids;
    show.patches[0].name = patches[0].first;
    show.patches[0].colour = patches[0].second;
    ids["Clean"] = show.patches[0].id;

    for (size_t i = 1; i < std::size (patches); ++i)
    {
        auto p = duplicatePatch (show.patches[0], patches[i].first);
        p.colour = patches[i].second;
        ids[p.name] = p.id;
        show.patches.push_back (std::move (p));
    }

    auto cue = [&] (const char* number, const char* name, const char* patch, int pc = -1, bool out = false)
    {
        auto c = RunItem::makeCue (number, name, ids[patch]);
        c.programChange = pc;

        if (out)
            c.midiOut.push_back ({ MidiOutMessage::Kind::programChange, 1, 7, 0 });

        return c;
    };

    auto set1 = RunItem::makeSection ("Set 1", "set");
    set1.children = { cue ("1", "Clean", "Clean"), cue ("2", "Clean 2", "Clean 2"), cue ("3", "Opener", "Crunch", 3),
                      cue ("4", "Ballad in D", "Clean 2") };
    auto set2 = RunItem::makeSection ("Set 2", "set");
    set2.children = { cue ("5", "Shuffle", "Edge"), cue ("6", "Rock Medley", "Lead", 7, true), cue ("7", "Closer", "Crunch") };
    auto encoreSet = RunItem::makeSection ("Encore", "set");
    encoreSet.children = { cue ("8", "Last Call", "Lead") };
    show.runningOrder.children = { set1, set2, encoreSet };

    const auto folder = parent.getChildFile ("My Show");
    ShowFolder::create (folder, show);
    return folder;
}

/** The Stage board's pedals: a delay on stomp 1 (on), the EQ on stomp 2
    (off), the gate on stomp 3 (on); expression pedal 1 moves the amp's output. */
inline void addStageStomps (PatchModel& patch)
{
    auto& blocks = patch.mainLane().blocks;

    for (auto& b : blocks)
    {
        if (b.type == "nam")
            b.expression["output"] = { 1, -40.0f, 12.0f };
        else if (b.type == "eq")
        {
            b.stomp = 2;
            b.enabled = false;
        }
        else if (b.type == "gate")
            b.stomp = 3;
    }

    BlockModel delay;
    delay.id = juce::Uuid().toDashedString();
    delay.type = "delay";
    delay.stomp = 1;
    blocks.push_back (delay);
}

/** The Stage board's layout (V3Stage.dc.html: its 12 x 8 grid, doubled). */
inline std::vector<StageWidget> boardStageWidgets()
{
    using T = StageWidgetType;
    return {
        { T::currentCue,      0,  0, 16,  6, 1.2f, {} },
        { T::clock,          16,  0,  4,  2, 1.0f, {} },
        { T::health,         20,  0,  4,  2, 1.0f, {} },
        { T::lockButton,     16,  2,  4,  4, 1.0f, {} },
        { T::panicButton,    20,  2,  4,  4, 1.0f, {} },
        { T::patchName,       0,  6,  8,  2, 1.0f, {} },
        { T::cueNote,         8,  6,  8,  2, 1.0f, {} },
        { T::nextCue,         0,  8, 16,  2, 1.0f, {} },
        { T::previousButton,  0, 10,  6,  2, 1.0f, {} },
        { T::nextButton,      6, 10, 10,  2, 1.0f, {} },
        { T::stomps,          0, 12, 12,  4, 1.0f, {} },
        { T::pedals,         12, 12,  2,  4, 1.0f, {} },
        { T::meters,         14, 12,  2,  4, 1.0f, {} },
        { T::setList,        16,  6,  8, 10, 1.0f, {} },
    };
}

/** The Stage board's show: three sets, eight songs, "Ballad in D" with a
    capo note; every patch has the board's three stomps. */
inline juce::File makeStageShow (const juce::File& parent)
{
    auto show = makeDefaultShow ("My Show");
    show.showTemplate = ShowTemplate::band;
    addStageStomps (show.patches[0]);
    std::map<juce::String, juce::String> ids;
    show.patches[0].name = "Clean";
    ids["Clean"] = show.patches[0].id;

    for (auto name : { "Crunch", "Clean 2", "Edge of breakup", "Lead", "Acoustic sim" })
    {
        auto p = duplicatePatch (show.patches[0], name);
        ids[name] = p.id;
        show.patches.push_back (std::move (p));
    }

    auto cue = [&] (const char* number, const char* name, const char* patch)
    {
        return RunItem::makeCue (number, name, ids[patch]);
    };

    auto ballad = cue ("3", "Ballad in D", "Clean 2");
    ballad.note = juce::String (juce::CharPointer_UTF8 ("Capo 2 \xc2\xb7 start on clean arpeggio"));
    ballad.barMark = "bar 1 / A";

    auto set1 = RunItem::makeSection ("Set 1", "set");
    set1.children = { cue ("1", "Soundcheck", "Clean"), cue ("2", "Opener", "Crunch"), ballad, cue ("4", "Shuffle", "Edge of breakup") };
    auto set2 = RunItem::makeSection ("Set 2", "set");
    set2.children = { cue ("5", "Rock Medley", "Lead"), cue ("6", "Acoustic Set", "Acoustic sim"), cue ("7", "Closer", "Crunch") };
    auto encoreSet = RunItem::makeSection ("Encore", "set");
    encoreSet.children = { cue ("8", "Last Call", "Lead") };
    show.runningOrder.children = { set1, set2, encoreSet };

    const auto folder = parent.getChildFile ("Stage Show");
    ShowFolder::create (folder, show);
    return folder;
}

/** The layout editor board's layout: the Stage board's with a wider stomp
    board and level meters. */
inline std::vector<StageWidget> layoutEditorWidgets()
{
    auto widgets = boardStageWidgets();

    for (auto& w : widgets)
    {
        if (w.type == StageWidgetType::stomps)  w.width = 10;
        if (w.type == StageWidgetType::pedals)  w.x = 10;
        if (w.type == StageWidgetType::meters)  { w.x = 12; w.width = 4; }
    }

    return widgets;
}

/** The layout editor board's show: a new show's two cues, "Clean" and
    "Clean 2", the first with a capo note. */
inline juce::File makeLayoutShow (const juce::File& parent)
{
    auto show = makeDefaultShow ("My Show");
    show.showTemplate = ShowTemplate::band;
    addStageStomps (show.patches[0]);
    show.patches[0].name = "Clean";
    auto clean2 = duplicatePatch (show.patches[0], "Clean 2");

    auto first = RunItem::makeCue ("1", "Clean", show.patches[0].id);
    first.note = juce::String (juce::CharPointer_UTF8 ("Capo 2 \xc2\xb7 start on clean arpeggio"));
    first.barMark = "bar 1 / A";
    show.runningOrder.children = { first, RunItem::makeCue ("2", "Clean 2", clean2.id) };
    show.patches.push_back (std::move (clean2));

    const auto folder = parent.getChildFile ("Layout Show");
    ShowFolder::create (folder, show);
    return folder;
}

/** The full-list board's show (a musical): two acts, scenes, bar marks,
    notes, a cut cue, a cue with its own patch, program changes and MIDI out. */
inline juce::File makeMusicalShow (const juce::File& parent)
{
    auto show = makeDefaultShow (juce::String (juce::CharPointer_UTF8 ("Phantom \xe2\x80\x94 tour 2027")));
    show.showTemplate = ShowTemplate::musical;
    std::map<juce::String, juce::String> ids;
    show.patches[0].name = "Clean";
    ids["Clean"] = show.patches[0].id;
    addStageStomps (show.patches[0]);

    for (auto name : { "Clean 2", "Edge of breakup", "Lead", "Crunch" })
    {
        auto p = duplicatePatch (show.patches[0], name);
        ids[name] = p.id;
        show.patches.push_back (std::move (p));
    }

    auto cue = [&] (const char* name, const char* bar, const char* note, const char* patch, int pc = -1, bool out = false)
    {
        auto c = RunItem::makeCue ({}, name, ids[patch]);
        c.barMark = bar;
        c.note = note;
        c.programChange = pc;

        if (out)
            c.midiOut.push_back ({ MidiOutMessage::Kind::programChange, 1, 7, 0 });

        return c;
    };

    auto prologue = RunItem::makeSection ("Prologue", "scene");
    prologue.children = { cue ("Overture", "bar 1", "Wait for the MD", "Clean"),
                          cue ("Auction", "bar 34 / B", "Swell on the chandelier", "Clean 2", 2) };
    auto rehearsal = RunItem::makeSection ("Rehearsal", "scene");
    auto reprise = cue ("Reprise", "bar 64", "Cut in rehearsal 3", "Edge of breakup");
    reprise.cut = true;
    rehearsal.children = { cue ("Think of Me", "bar 1", "", "Clean"), cue ("Angel of Music", "bar 17 / C", "Capo 2", "Edge of breakup"),
                           reprise, cue ("Title Song", "bar 1 / A", "Big entrance, fade in", "Lead", 5, true),
                           cue ("Title Song coda", "", "", "Lead") };
    auto rooftop = RunItem::makeSection ("Rooftop", "scene");
    auto finale = cue ("Act 1 finale", "bar 88 / F", "Hold for the blackout", "Crunch", -1, true);
    finale.uniquePatch = duplicatePatch (show.patches.back(), "Crunch");
    finale.patchId.clear();
    rooftop.children = { cue ("All I Ask", "bar 1", "Soft, neck pickup", "Clean 2"), finale };
    auto act1 = RunItem::makeSection ("Act 1", "act");
    act1.children = { prologue, rehearsal, rooftop };

    auto masquerade = RunItem::makeSection ("Masquerade", "scene");
    masquerade.children = { cue ("Entr'acte", "", "", "Crunch"), cue ("Masquerade", "bar 1", "", "Lead", 9) };
    auto act2 = RunItem::makeSection ("Act 2", "act");
    act2.children = { masquerade };
    show.runningOrder.children = { act1, act2 };

    const auto folder = parent.getChildFile ("Phantom");
    ShowFolder::create (folder, show);
    return folder;
}

inline juce::XmlElement boardDeviceSetup (const BoardDeviceSpec& spec = {})
{
    juce::XmlElement setup ("DEVICESETUP");
    setup.setAttribute ("deviceType", spec.type);
    setup.setAttribute ("audioOutputDeviceName", spec.name);
    setup.setAttribute ("audioInputDeviceName", spec.name);
    setup.setAttribute ("audioDeviceRate", 48000);
    setup.setAttribute ("audioDeviceBufferSize", spec.bufferSize);
    return setup;
}

} // namespace encore::test
