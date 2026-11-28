// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "session/OfflineRender.h"

#include <span>

#include "engine/blocks/DelayBlock.h"
#include "engine/blocks/GateBlock.h"
#include "engine/blocks/NamAmpBlock.h"
#include "engine/blocks/PedalEqBlock.h"
#include "engine/blocks/StudioEqBlock.h"
#include "engine/blocks/StudioGateBlock.h"
#include "engine/core/Chain.h"
#include "engine/core/FactoryPresets.h"
#include "model/ShowFolder.h"
#include "session/PatchBuilder.h"

namespace encore::render
{

namespace
{
    /** Collects one block's (or the chain's) buffer times. */
    struct Timer
    {
        juce::String name;
        double total = 0.0, worst = 0.0;
        int count = 0;

        void add (double seconds)
        {
            total += seconds;
            worst = juce::jmax (worst, seconds);
            ++count;
        }

        CpuStats stats (double bufferSeconds) const
        {
            CpuStats s;
            s.name = name;
            s.averageMicros = count > 0 ? total / count * 1.0e6 : 0.0;
            s.worstMicros = worst * 1.0e6;
            s.averagePercent = count > 0 ? total / count / bufferSeconds * 100.0 : 0.0;
            s.worstPercent = worst / bufferSeconds * 100.0;
            return s;
        }
    };

    double now() { return juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks()); }

    /** B2: what a gate did over a render, for the report's notes. */
    struct GateWatch
    {
        const GateBlock* gate = nullptr;
        juce::String name;
        bool wasOpen = true;
        int opens = 0;

        void update() noexcept
        {
            const auto open = gate->isOpen();
            opens += open && ! wasOpen ? 1 : 0;
            wasOpen = open;
        }

        juce::String note() const
        {
            juce::String s;
            s << name << ": opened " << opens << " time" << (opens == 1 ? "" : "s") << " (read per buffer), "
              << "learned floor " << juce::String (gate->getLearnedFloorDb(), 1) << " dBFS"
              << (gate->isLearningFrozen() ? " (frozen)" : "");

            if (const auto key = gate->getKeyInfo(); key.isNotEmpty())
                s << "; " << key;

            if (const auto link = gate->getLinkInfo(); link.isNotEmpty())
                s << "; " << link;
            else if (gate->getLinkState() == GateBlock::LinkState::following)
                s << "; linked (following)";

            return s;
        }
    };

    void measureLevels (Result& r, const juce::AudioBuffer<float>& input)
    {
        const auto levels = [] (const juce::AudioBuffer<float>& b, float& peakOut, float& rmsOut)
        {
            peakOut = 0.0f;
            double sum = 0.0;

            for (int ch = 0; ch < b.getNumChannels(); ++ch)
            {
                peakOut = juce::jmax (peakOut, b.getMagnitude (ch, 0, b.getNumSamples()));

                for (int i = 0; i < b.getNumSamples(); ++i)
                    sum += (double) b.getSample (ch, i) * b.getSample (ch, i);
            }

            const auto n = juce::jmax (1, b.getNumChannels() * b.getNumSamples());
            rmsOut = (float) std::sqrt (sum / n);
        };

        levels (input, r.peakIn, r.rmsIn);
        levels (r.output, r.peakOut, r.rmsOut);
    }

    juce::String dbfs (float gain)
    {
        return gain > 0.0f ? juce::String (juce::Decibels::gainToDecibels (gain), 1) + " dBFS" : juce::String ("silence");
    }
}

//==============================================================================
bool applySettings (Block& block, const juce::var& settings, juce::String& error)
{
    if (settings.isVoid())
        return true;

    auto* object = settings.getDynamicObject();

    if (object == nullptr)
    {
        error = "Settings must be a JSON object: { \"parameter id\": value }";
        return false;
    }

    for (const auto& property : object->getProperties())
    {
        const auto id = property.name.toString();
        auto* parameter = block.findParameter (id);

        if (parameter == nullptr)
        {
            juce::StringArray known;

            for (const auto& p : block.getParameters())
                known.add (p->getSpec().id);

            error = "\"" + block.getTypeId() + "\" has no parameter \"" + id + "\" (it has: " + known.joinIntoString (", ") + ")";
            return false;
        }

        const auto& value = property.value;

        if (value.isString() && parameter->isChoice())
        {
            const auto index = parameter->getSpec().choices.indexOf (value.toString(), true);

            if (index < 0)
            {
                error = "\"" + id + "\" has no choice \"" + value.toString() + "\" (it has: "
                        + parameter->getSpec().choices.joinIntoString (", ") + ")";
                return false;
            }

            parameter->setValue ((float) index);
        }
        else if (value.isDouble() || value.isInt() || value.isInt64() || value.isBool())
        {
            parameter->setValue ((float) (double) value);
        }
        else if (value.isString() && value.toString().containsOnly ("-+.0123456789eE"))
        {
            parameter->setValue (value.toString().getFloatValue());
        }
        else
        {
            error = "\"" + id + "\" needs a number" + juce::String (parameter->isChoice() ? " or a choice name" : "");
            return false;
        }
    }

    return true;
}

//==============================================================================
Result renderBlock (const juce::AudioBuffer<float>& input, double sampleRate, const juce::String& typeId,
                    const juce::var& settings, int blockSize, const juce::String& presetId,
                    const std::map<juce::String, juce::String>& refs, const std::optional<Sweep>& sweep)
{
    Result r;
    r.sampleRate = sampleRate;
    r.blockSize = blockSize = juce::jlimit (1, 8192, blockSize);

    auto block = createBlock (typeId);

    if (block == nullptr)
    {
        juce::StringArray known;

        for (const auto& t : getBlockTypes())
            known.add (t.id + (t.hidden ? " (hidden)" : ""));

        r.error = "Unknown block type \"" + typeId + "\". Types: " + known.joinIntoString (", ");
        return r;
    }

    if (presetId.isNotEmpty())
    {
        const auto* preset = presets::find (*block, presetId);

        if (preset == nullptr)
        {
            r.error = "\"" + typeId + "\" has no factory preset \"" + presetId + "\" (it has: " + presets::listIds (*block) + ")";
            return r;
        }

        presets::apply (*block, *preset);
    }

    if (! applySettings (*block, settings, r.error))
        return r;

    block->setRefs (refs);

    Parameter* swept = nullptr;

    if (sweep.has_value())
    {
        swept = block->findParameter (sweep->parameterId);

        if (swept == nullptr)
        {
            r.error = "\"" + typeId + "\" has no parameter \"" + sweep->parameterId + "\" to sweep";
            return r;
        }

        swept->setValue (sweep->from);
    }

    // Alone, a block has no earlier block to key from or link to (B2): those
    // refs fall back, and the notes say why.
    if (! refs.empty())
    {
        if (refs.count ("keyBlock") > 0)
            block->setKeyTap (nullptr, 0, "Key: a block rendered alone has no block before it, so it listens to its own input");

        if (refs.count ("keyInput") > 0)
            block->setKeyInputSlot (-1);   // no interface offline

        if (refs.count ("linkTo") > 0)
            block->setLinkLeader (nullptr, "Link: a gate rendered alone has no gate before it to follow");
    }

    if (input.getNumChannels() < 1 || sampleRate <= 0.0)
    {
        r.error = "The input has no audio";
        return r;
    }

    const auto inChannels = juce::jlimit (1, Block::maxChannels, input.getNumChannels());
    const auto outChannels = juce::jlimit (1, Block::maxChannels, block->getOutputChannels (inChannels));

    block->prepare ({ sampleRate, blockSize });
    block->reset();

    // As in the audio callback: denormals flushed (they'd make some buffers
    // many times slower than they are when playing).
    const juce::ScopedNoDenormals noDenormals;

    // Work buffer with room for the block's output channels.
    juce::AudioBuffer<float> work (Block::maxChannels, blockSize);
    r.output.setSize (outChannels, input.getNumSamples());

    Timer timer { typeId, 0.0, 0.0, 0 };
    std::optional<GateWatch> watch;

    if (auto* gate = dynamic_cast<const GateBlock*> (block.get()))
        watch = GateWatch { gate, typeId };

    for (int start = 0; start < input.getNumSamples(); start += blockSize)
    {
        const auto length = juce::jmin (blockSize, input.getNumSamples() - start);
        work.clear();

        for (int ch = 0; ch < inChannels; ++ch)
            work.copyFrom (ch, 0, input, ch, start, length);

        if (swept != nullptr)
            swept->setValue (sweep->from + (sweep->to - sweep->from) * (float) start / (float) juce::jmax (1, input.getNumSamples()));

        // The guitar, as the chain would give it: the input's first channel.
        ProcessContext context;
        context.chainInput = input.getReadPointer (0, start);

        const auto t0 = now();
        block->process (work.getArrayOfWritePointers(), inChannels, length, context);
        timer.add (now() - t0);

        if (watch)
            watch->update();

        for (int ch = 0; ch < outChannels; ++ch)
            r.output.copyFrom (ch, start, work, ch, 0, length);
    }

    r.latencySamples = block->getLatencySamples();
    r.cpu.push_back (timer.stats (blockSize / sampleRate));

    if (watch)
        r.notes.add (watch->note());
    measureLevels (r, input);
    r.ok = true;
    return r;
}

Result renderPatch (const juce::AudioBuffer<float>& input, double sampleRate, const PatchModel& patch,
                    const ShowFolder& folder, int blockSize)
{
    Result r;
    r.sampleRate = sampleRate;
    r.blockSize = blockSize = juce::jlimit (1, 8192, blockSize);

    if (input.getNumChannels() < 1 || sampleRate <= 0.0)
    {
        r.error = "The input has no audio";
        return r;
    }

    const ProcessSpec spec { sampleRate, blockSize };
    auto chain = buildChain (patch, folder, spec, r.warnings);

    if (chain == nullptr)
    {
        r.error = "The patch could not be built";
        return r;
    }

    chain->prepare (spec);
    chain->reset();

    const juce::ScopedNoDenormals noDenormals;   // as in the audio callback

    juce::AudioBuffer<float> work (Block::maxChannels, blockSize);
    r.output.setSize (juce::jlimit (1, Block::maxChannels, chain->getOutputChannels()), input.getNumSamples());

    // The chain as a whole is timed; each block's share comes from its own
    // load meter (the one the block header shows), read after every buffer.
    Timer total { "patch", 0.0, 0.0, 0 };
    std::vector<Timer> perBlock;

    std::vector<GateWatch> gates;

    for (const auto& b : chain->getBlocks())
    {
        perBlock.push_back ({ b->getTypeId() + " (" + b->getName() + ")", 0.0, 0.0, 0 });

        if (auto* gate = dynamic_cast<const GateBlock*> (b.get()))
            gates.push_back ({ gate, b->getTypeId() + " " + b->getModelId() });
    }

    const auto bufferSeconds = blockSize / sampleRate;

    for (int start = 0; start < input.getNumSamples(); start += blockSize)
    {
        const auto length = juce::jmin (blockSize, input.getNumSamples() - start);
        work.clear();
        work.copyFrom (0, 0, input, 0, start, length);   // the guitar: mono

        const auto t0 = now();
        const auto channels = chain->process (work.getArrayOfWritePointers(), length);
        total.add (now() - t0);

        for (size_t i = 0; i < perBlock.size(); ++i)
            perBlock[i].add (chain->getBlocks()[i]->getCpuLoad() * bufferSeconds);

        for (auto& g : gates)
            g.update();

        for (int ch = 0; ch < r.output.getNumChannels(); ++ch)
            r.output.copyFrom (ch, start, work, juce::jmin (ch, channels - 1), 0, length);
    }

    r.latencySamples = chain->getLatencySamples();
    r.cpu.push_back (total.stats (bufferSeconds));

    for (const auto& t : perBlock)
        r.cpu.push_back (t.stats (bufferSeconds));

    for (const auto& g : gates)
        r.notes.add (g.note());

    measureLevels (r, input);
    r.ok = true;
    return r;
}

//==============================================================================
juce::String Result::report() const
{
    juce::String s;

    if (! ok)
        return "Render failed: " + error + "\n";

    s << "Rendered " << output.getNumSamples() << " samples, " << output.getNumChannels() << " channel(s), at "
      << juce::String (sampleRate, 0) << " Hz in buffers of " << blockSize << "\n";
    s << "Peak   in " << dbfs (peakIn) << ", out " << dbfs (peakOut) << "\n";
    s << "RMS    in " << dbfs (rmsIn) << ", out " << dbfs (rmsOut) << "\n";
    s << "Latency " << latencySamples << " samples (" << juce::String (latencySamples * 1000.0 / juce::jmax (1.0, sampleRate), 2) << " ms)\n";
    s << "CPU per buffer (this computer, offline; average / worst):\n";

    for (const auto& c : cpu)
        s << "  " << c.name << ": " << juce::String (c.averageMicros, 1) << " / " << juce::String (c.worstMicros, 1) << " us, "
          << juce::String (c.averagePercent, 2) << " % / " << juce::String (c.worstPercent, 2) << " % of the buffer\n";

    for (const auto& n : notes)
        s << "Note: " << n << "\n";

    for (const auto& w : warnings)
        s << "Warning: " << w << "\n";

    return s;
}

//==============================================================================
std::optional<Audio> readAudioFile (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "No such file: " + file.getFullPathName();
        return std::nullopt;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        error = "Not a WAV or AIFF file Encore can read: " + file.getFullPathName();
        return std::nullopt;
    }

    if (reader->lengthInSamples > (juce::int64) std::numeric_limits<int>::max() || reader->numChannels < 1)
    {
        error = "The file is empty or too long";
        return std::nullopt;
    }

    Audio audio;
    audio.sampleRate = reader->sampleRate;
    audio.buffer.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&audio.buffer, 0, (int) reader->lengthInSamples, 0, true, true);
    return audio;
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate, juce::String& error)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);

    if (! static_cast<juce::FileOutputStream&> (*stream).openedOk())
    {
        error = "Can't write " + file.getFullPathName();
        return false;
    }

    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions {}
                             .withSampleRate (sampleRate)
                             .withNumChannels (buffer.getNumChannels())
                             .withBitsPerSample (32)
                             .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = wav.createWriterFor (stream, options);

    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
    {
        error = "Can't write a WAV to " + file.getFullPathName();
        return false;
    }

    return true;
}

juce::AudioBuffer<float> makeSweep (double sampleRate, double seconds)
{
    const auto n = juce::jmax (1, (int) (sampleRate * seconds));
    juce::AudioBuffer<float> b (1, n);
    const auto f0 = 20.0, f1 = 20000.0, k = std::log (f1 / f0);

    for (int i = 0; i < n; ++i)
    {
        const auto t = i / sampleRate;
        const auto phase = juce::MathConstants<double>::twoPi * f0 * seconds / k * (std::exp (t / seconds * k) - 1.0);
        b.setSample (0, i, 0.5f * (float) std::sin (phase));
    }

    return b;
}

namespace
{
    struct Note { double start, fundamental; float peak; double decayMs; int harmonics; };

    /** Notes over a noise floor (its peak in dBFS), mono, deterministic: each a
        fundamental and falling harmonics with a 2 ms rise and an exponential
        decay (decayMs to fall 60 dB). */
    juce::AudioBuffer<float> synthesise (std::span<const Note> notes, double seconds, double sampleRate, float noiseFloorDb)
    {
        const auto length = (int) (seconds * sampleRate);
        juce::AudioBuffer<float> b (1, length);
        juce::Random random (42);
        const auto noisePeak = juce::Decibels::decibelsToGain (noiseFloorDb, -200.0f);

        for (int i = 0; i < length; ++i)
            b.setSample (0, i, noisePeak * (random.nextFloat() * 2.0f - 1.0f));

        for (const auto& n : notes)
        {
            const auto onset = (int) (n.start * sampleRate);
            const auto rise = juce::jmax (1, (int) (0.002 * sampleRate));
            const auto decay = std::pow (10.0, -60.0 / 20.0 / (n.decayMs * 0.001 * sampleRate));
            auto norm = 0.0;

            for (int h = 1; h <= n.harmonics; ++h)
                norm += 1.0 / h;

            auto env = 0.0;

            for (int i = onset; i < length; ++i)
            {
                const auto k = i - onset;
                env = k < rise ? (double) (k + 1) / rise : env * decay;

                if (env < 1.0e-5 && k > rise)
                    break;

                auto tone = 0.0;

                for (int h = 1; h <= n.harmonics; ++h)
                    tone += std::sin (juce::MathConstants<double>::twoPi * n.fundamental * h * k / sampleRate) / h;

                b.addSample (0, i, (float) (n.peak * env * tone / norm));
            }
        }

        return b;
    }
}

juce::AudioBuffer<float> makePlucks (double sampleRate)
{
    const Note notes[] = {
        { 0.10, 82.41, 0.50f, 2500.0, 8 },   // low E, open
        { 1.30, 110.0, 0.45f, 2000.0, 8 },   // A
        { 2.40, 82.41, 0.40f, 120.0, 3 },    // palm-muted chugs
        { 2.65, 82.41, 0.40f, 120.0, 3 },
        { 2.90, 82.41, 0.40f, 120.0, 3 },
        { 3.30, 293.7, 0.35f, 1500.0, 6 },   // high D
        { 4.40, 98.0,  0.89f, 1800.0, 10 },  // a hard accent (G), near full scale
    };

    auto b = synthesise (notes, 6.5, sampleRate, -70.0f);   // the noise floor, -70 dBFS peak

    // The harmonics never peak together: scale so the accent reaches -1 dBFS.
    if (const auto top = b.getMagnitude (0, 0, b.getNumSamples()); top > 0.0f)
        b.applyGain (juce::Decibels::decibelsToGain (-1.0f) / top);

    return b;
}

juce::AudioBuffer<float> makeStereoPlucks (double sampleRate)
{
    const Note right[] = {
        { 0.60, 123.5, 0.45f, 2000.0, 8 },   // B, after the left's low E
        { 1.80, 164.8, 0.40f, 1500.0, 6 },   // E an octave up
        { 3.00, 146.8, 0.40f, 900.0, 6 },    // D
        { 3.30, 440.0, 0.30f, 1500.0, 6 },   // high A with the left's high D
        { 4.40, 146.8, 0.80f, 1800.0, 10 },  // an accent with the left's
    };

    const auto left = makePlucks (sampleRate);
    auto r = synthesise (right, 6.5, sampleRate, -70.0f);

    if (const auto top = r.getMagnitude (0, 0, r.getNumSamples()); top > 0.0f)
        r.applyGain (juce::Decibels::decibelsToGain (-2.0f) / top);

    juce::AudioBuffer<float> b (2, left.getNumSamples());
    b.copyFrom (0, 0, left, 0, 0, left.getNumSamples());
    b.copyFrom (1, 0, r, 0, 0, juce::jmin (r.getNumSamples(), left.getNumSamples()));
    return b;
}

juce::AudioBuffer<float> makeGateTest (double sampleRate, float noiseFloorDb)
{
    const Note notes[] = {
        { 1.50, 110.0, 0.50f, 1200.0, 6 },   // a note, ringing out into the noise
        { 3.50, 82.41, 0.45f, 120.0, 3 },    // palm-muted chugs
        { 3.75, 82.41, 0.45f, 120.0, 3 },
        { 4.00, 82.41, 0.45f, 120.0, 3 },
        { 4.25, 82.41, 0.45f, 120.0, 3 },
        { 5.50, 98.0,  0.30f, 2500.0, 8 },   // a held chord, slowly decaying
        { 5.50, 123.5, 0.25f, 2500.0, 6 },
        { 5.50, 146.8, 0.25f, 2500.0, 6 },
    };

    // Not normalised: the noise floor stays where it was asked for.
    return synthesise (notes, 10.0, sampleRate, noiseFloorDb);
}

//==============================================================================
ShowModel makeGateShow (const juce::File& ampModel)
{
    auto show = makeDefaultShow ("Gates");
    show.patches.clear();

    const auto fromBlock = [] (Block& block, const juce::String& id, const juce::String& presetId = {})
    {
        if (presetId.isNotEmpty())
            if (const auto* preset = presets::find (block, presetId))
                presets::apply (block, *preset);

        BlockModel m;
        m.id = id;
        m.type = block.getTypeId();

        for (const auto& p : block.getParameters())
            m.params[p->getSpec().id] = p->getValue();

        return m;
    };

    // The Gate pair around an amp.
    auto pair = makeDefaultPatch ("Gate pair");
    pair.mainLane().blocks.clear();
    BlockModel amp;
    amp.id = "amp";
    amp.type = NamAmpBlock::typeId;
    amp.file = ampModel.getFullPathName();
    pair.mainLane().blocks.push_back (amp);
    auto n = 0;
    addGatePair (pair, [&n] { return juce::String (++n == 1 ? "gateIn" : "gateOut"); });
    show.patches.push_back (pair);

    // A ducking delay: the X4 after the delay, keyed from the dry signal before it.
    auto duck = makeDefaultPatch ("Ducking delay");
    auto& blocks = duck.mainLane().blocks;
    blocks.clear();
    PedalEqBlock dry;
    blocks.push_back (fromBlock (dry, "dry", "flat"));
    DelayBlock delay;
    blocks.push_back (fromBlock (delay, "delay"));
    blocks.back().params["time"] = 350.0f;
    blocks.back().params["feedback"] = 45.0f;
    blocks.back().params["mix"] = 50.0f;
    StudioGateBlock ducker;
    blocks.push_back (fromBlock (ducker, "duck", "duckDelay"));
    blocks.back().refs["keyBlock"] = "dry";
    show.patches.push_back (duck);

    // A Studio Gate in Smart mode, learning from -70.
    auto smart = makeDefaultPatch ("Smart X4");
    smart.mainLane().blocks.clear();
    StudioGateBlock x4;
    smart.mainLane().blocks.push_back (fromBlock (x4, "x4", "highGain"));
    show.patches.push_back (smart);

    return show;
}

ShowModel makeEqShow (const juce::File& ampModel)
{
    auto show = makeDefaultShow ("Studio EQ dynamics");
    show.patches.clear();

    for (const auto side : { StudioEqBlock::Side::dryGuitar, StudioEqBlock::Side::thisBand })
    {
        auto patch = makeDefaultPatch (side == StudioEqBlock::Side::dryGuitar ? "Dynamic EQ, dry guitar" : "Dynamic EQ, this band");
        patch.mainLane().blocks.clear();

        BlockModel amp;
        amp.id = "amp";
        amp.type = NamAmpBlock::typeId;
        amp.file = ampModel.getFullPathName();
        patch.mainLane().blocks.push_back (amp);

        StudioEqBlock eq;
        BlockModel m;
        m.id = "q24";
        m.type = StudioEqBlock::typeId;

        for (const auto& p : eq.getParameters())
            m.params[p->getSpec().id] = p->getValue();

        m.params["b01Used"] = 1.0f;
        m.params["b01Freq"] = 2500.0f;
        m.params["b01Q"] = 1.5f;
        m.params["b01Mode"] = (float) StudioEqBlock::Mode::dynamic;
        m.params["b01Range"] = -4.0f;
        m.params["b01Thresh"] = -61.0f;
        m.params["b01Attack"] = 2.0f;
        m.params["b01Release"] = 80.0f;
        m.params["b01Side"] = (float) side;
        patch.mainLane().blocks.push_back (m);
        show.patches.push_back (patch);
    }

    return show;
}

} // namespace encore::render
