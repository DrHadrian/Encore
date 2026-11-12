// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Plugins as on the milestone 10 boards (docs/design/reference/REFERENCES.md
// §3): a catalog of twelve plugins, three blocked files, two folders, and a
// stand-in "ValhallaRoom" whose parameters read as on the boards. For UI
// snapshots: nothing here is loaded from disk.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cmath>
#include <thread>

#include "session/PluginCatalog.h"

namespace encore::test
{

/** A plugin made in the test: the board's name, maker, version and
    parameters (their text as on the board at the board's value). */
class FixturePlugin final : public juce::AudioPluginInstance
{
public:
    struct Param { const char* name; float value; const char* text; };

    FixturePlugin (juce::PluginDescription d, const std::vector<Param>& params)
        : juce::AudioPluginInstance (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo())
                                                      .withOutput ("Output", juce::AudioChannelSet::stereo())),
          description (std::move (d))
    {
        int index = 0;

        for (const auto& p : params)
            addHostedParameter (std::make_unique<Parameter> ("p" + juce::String (index++), p));
    }

    /** A parameter reading as on the board: its text at the board's value,
        elsewhere a percentage. */
    class Parameter final : public juce::AudioPluginInstance::HostedParameter
    {
    public:
        Parameter (juce::String parameterId, const Param& p)
            : id (std::move (parameterId)), name (p.name), text (juce::CharPointer_UTF8 (p.text)), board (p.value), value (p.value) {}

        juce::String getParameterID() const override          { return id; }
        float getValue() const override                       { return value; }
        void setValue (float v) override                      { value = v; }
        float getDefaultValue() const override                { return board; }
        juce::String getName (int maximumLength) const override { return name.substring (0, maximumLength); }
        juce::String getLabel() const override                { return {}; }
        float getValueForText (const juce::String&) const override { return board; }

        juce::String getText (float v, int) const override
        {
            return std::abs (v - board) < 0.001f ? text : juce::String (juce::roundToInt (v * 100.0f)) + " %";
        }

    private:
        juce::String id, name, text;
        float board, value;
    };

    /** Takes far longer than a buffer (the crash guard switches it off). */
    static inline std::atomic<bool> slow { false };

    const juce::String getName() const override              { return description.name; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override
    {
        if (slow.load())
            std::this_thread::sleep_for (std::chrono::milliseconds (40));
    }

    double getTailLengthSeconds() const override             { return 0.0; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    juce::AudioProcessorEditor* createEditor() override      { return nullptr; }
    bool hasEditor() const override                          { return false; }
    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& block) override { block.setSize (0); }
    void setStateInformation (const void*, int) override {}
    void fillInPluginDescription (juce::PluginDescription& d) const override { d = description; }

private:
    juce::PluginDescription description;
};

/** ValhallaRoom's parameters as on the boards (V3PluginWindow, V3EditPlugin). */
inline std::vector<FixturePlugin::Param> valhallaRoomParams (int extra = 0)
{
    std::vector<FixturePlugin::Param> params {
        { "Mix", 0.35f, "35 %" }, { "Pre-delay", 0.2f, "20 ms" }, { "Decay", 0.45f, "2.8 s" }, { "Size", 0.6f, "60 %" },
        { "Low cut", 0.15f, "120 Hz" }, { "High cut", 0.7f, "8.2 kHz" }, { "Early/Late", 0.5f, "50 %" },
        { "Mod depth", 0.3f, "30 %" }, { "Diffusion", 0.8f, "80 %" }, { "Mod rate", 0.25f, "0.5 Hz" },
        { "Bass mult", 0.4f, "0.8 \xc3\x97" }, { "Bass xover", 0.3f, "400 Hz" }, { "Mode", 0.0f, "Large Room" },
    };

    static const char* const names[] = { "Width", "Shape", "Spin", "Wander", "Treble mult", "Treble xover", "Gate",
                                         "Duck", "Freeze", "Size mod", "Lo shelf", "Hi shelf", "Swap", "Phase",
                                         "Damping", "Spread", "Density", "Output", "Input", "Spare A", "Spare B",
                                         "Spare C", "Spare D", "Spare E" };

    for (int i = 0; i < extra && i < (int) std::size (names); ++i)
        params.push_back ({ names[i], 0.5f, "50 %" });

    return params;
}

struct FixtureCatalogEntry { const char* name; const char* maker; const char* version; };

/** The twelve plugins of V3Plugins (makers as on the board, or none). */
inline std::vector<FixtureCatalogEntry> boardPlugins (bool everyMakerKnown = true)
{
    return { { "FabFilter Pro-Q 3", "FabFilter", "3.24" }, { "FabFilter Pro-R 2", "FabFilter", "2.10" },
             { "FabFilter Saturn 2", "FabFilter", "2.05" }, { "Blackhole", "Eventide", "3.7.2" },
             { "H9 Shimmer", "Eventide", "3.7.2" }, { "MicroPitch", "Eventide", "3.7.2" },
             { "ValhallaDelay", "Valhalla DSP", "3.0.1" }, { "ValhallaRoom", "Valhalla DSP", "2.0.0" },
             { "ValhallaShimmer", "Valhalla DSP", "1.4.0" }, { "ValhallaSupermassive", "Valhalla DSP", "3.0.0" },
             { "TAL-Chorus-LX", everyMakerKnown ? "TAL Software" : "", "1.8.4" },
             { "OTT", everyMakerKnown ? "Xfer Records" : "", "1.37" } };
}

inline juce::PluginDescription describe (const FixtureCatalogEntry& e)
{
    juce::PluginDescription d;
    d.name = d.descriptiveName = e.name;
    d.manufacturerName = e.maker;
    d.version = e.version;
    d.pluginFormatName = "VST3";
    d.category = "Fx";
    d.fileOrIdentifier = "C:\\Program Files\\Common Files\\VST3\\" + juce::String (e.name) + ".vst3";
    d.uniqueId = d.deprecatedUid = juce::String (e.name).hashCode();
    d.numInputChannels = d.numOutputChannels = 2;
    return d;
}

/** Writes plugins.xml as a scan would have left it: the board's plugins
    (but those `notInstalled`), its two folders and its three blocked files. */
inline void writeBoardCatalog (const juce::File& listFile, bool everyMakerKnown = true, const juce::StringArray& notInstalled = {})
{
    juce::XmlElement root ("ENCORE_PLUGINS");
    root.createNewChildElement ("FOLDERS")->setAttribute ("paths", "C:\\Program Files\\Common Files\\VST3;D:\\Plugins\\VST3");

    auto* blocked = root.createNewChildElement ("BLOCKED");
    const std::pair<const char*, const char*> files[] = {
        { "C:\\Program Files\\Common Files\\VST3\\CrashyVerb.vst3", "Crashed while being scanned" },
        { "C:\\Program Files\\Common Files\\VST3\\OldSynth.vst3", "Is an instrument (no audio input)" },
        { "D:\\Plugins\\VST3\\BigLoader.vst3", "Didn't finish loading within 60 s while being scanned" },
    };

    for (auto& [path, reason] : files)
    {
        auto* f = blocked->createNewChildElement ("FILE");
        f->setAttribute ("path", path);
        f->setAttribute ("reason", reason);
    }

    juce::KnownPluginList known;

    for (const auto& e : boardPlugins (everyMakerKnown))
        if (! notInstalled.contains (e.name))
            known.addType (describe (e));

    if (auto xml = known.createXml())
        root.addChildElement (xml.release());

    listFile.getParentDirectory().createDirectory();
    root.writeTo (listFile);
}

/** While alive, the catalog makes the board's plugins instead of loading
    them: ValhallaRoom with its parameters (plus `extra`), the rest bare. */
struct ScopedBoardPlugins
{
    explicit ScopedBoardPlugins (int extraParams = 0)
    {
        PluginCatalog::instanceMakerForTesting() = [extraParams] (const juce::PluginDescription& d) -> std::unique_ptr<juce::AudioPluginInstance>
        {
            if (d.name == "ValhallaRoom")
                return std::make_unique<FixturePlugin> (d, valhallaRoomParams (extraParams));

            return std::make_unique<FixturePlugin> (d, std::vector<FixturePlugin::Param> {});
        };
    }

    ~ScopedBoardPlugins()
    {
        PluginCatalog::instanceMakerForTesting() = nullptr;
        FixturePlugin::slow = false;
    }
};

} // namespace encore::test
