// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// EncoreRender: runs a WAV through one block or a whole patch, offline, with
// the same engine code the app plays (native modules, decision 0062). For
// listening, comparing and measuring; also run in CI on a synthetic sweep.

#include <juce_events/juce_events.h>

#include "model/ShowFolder.h"
#include "model/ShowJson.h"
#include "session/OfflineRender.h"
#include "session/PatchBuilder.h"

#include <iostream>

namespace
{
    const char* usage = R"(EncoreRender: a WAV through an Encore block or patch, offline.

  EncoreRender --in IN.wav --out OUT.wav --block TYPE [--preset ID] [--settings S.json] [--set id=value ...] [--ref name=value ...]
               [--sweep id=from:to]
  EncoreRender --in IN.wav --out OUT.wav --show SHOW --patch NAME-OR-NUMBER [--ref blockId.name=value ...]
  EncoreRender --make-sweep OUT.wav [--seconds 5] [--rate 48000]
  EncoreRender --make-plucks OUT.wav [--rate 48000]   (generated guitar-like notes)
  EncoreRender --make-gate-test OUT.wav [--rate 48000] [--noise-db -60]   (notes with gaps of noise)
  EncoreRender --make-gate-show FOLDER.encore [--amp MODEL.nam]        (a show for the gates)
  EncoreRender --make-stereo OUT.wav [--rate 48000]   (stereo plucks: different notes left and right)
  EncoreRender --make-resonance OUT.wav [--rate 48000]   (plucks with a ringing 2.8 kHz resonance)
  EncoreRender --make-eq-show FOLDER.encore [--amp MODEL.nam]          (a show for the Studio EQ's dynamic bands)

  --block TYPE     a block type id (hidden ones too), e.g. engineProbe, eq, gate
  --preset ID      a factory preset of the block (first; settings go on top)
  --settings FILE  JSON: { "parameter id": value }; a choice may be given by name
  --set id=value   one setting (may repeat; after --settings)
  --ref name=value a reference (may repeat): keyBlock, keyInput, linkTo. With --show,
                   blockId.name=value sets it on that block of the patch (e.g.
                   --ref duck.keyBlock=dry); alone, a block has nothing before it,
                   so a keyBlock or linkTo falls back and the report says so
  --noise-db DB    --make-gate-test: the noise floor's peak (default -60 dBFS)
  --sweep id=a:b   moves one parameter linearly from a to b across the render, as an
                   expression pedal would (e.g. --sweep b01Freq=200:8000)
  --show SHOW      a show folder or .encore file; --patch picks a patch (1 = first)
  --buffer N       buffer size in samples (default 64)
  --report FILE    also write the report to FILE
  --list           list the block types and their parameters

Writes a 32-bit float WAV and prints a report: peak, RMS, latency, CPU per block.
)";

    int fail (const juce::String& message)
    {
        std::cerr << message << "\n";
        return 1;
    }

    juce::String listTypes()
    {
        juce::String s;

        for (const auto& type : encore::getBlockTypes())
        {
            s << type.id << (type.hidden ? " (hidden)" : "") << ": " << type.displayName << "\n";

            if (auto block = encore::createBlock (type.id))
            {
                for (const auto& p : block->getParameters())
                {
                    const auto& spec = p->getSpec();
                    s << "    " << spec.id << "  ";

                    if (p->isChoice())
                        s << spec.choices.joinIntoString (" | ") << "  (default " << spec.choices[juce::roundToInt (spec.defaultValue)] << ")";
                    else
                        s << spec.range.start << " .. " << spec.range.end << " " << spec.unit << "  (default " << spec.defaultValue << ")";

                    s << "\n";
                }

                if (! block->getFactoryPresets().empty())
                    s << "    presets: " << encore::presets::listIds (*block) << "\n";
            }
        }

        return s;
    }
}

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI juce;   // message manager for show loading; no window opens

    juce::StringArray args;

    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    const auto value = [&args] (const juce::String& option) -> juce::String
    {
        const auto i = args.indexOf (option);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : juce::String();
    };

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto file = [&cwd] (const juce::String& path) { return cwd.getChildFile (path); };

    if (args.isEmpty() || args.contains ("--help") || args.contains ("-h"))
    {
        std::cout << usage;
        return args.isEmpty() ? 1 : 0;
    }

    if (args.contains ("--list"))
    {
        std::cout << listTypes();
        return 0;
    }

    juce::String error;

    if (const auto sweepOut = value ("--make-sweep"); sweepOut.isNotEmpty())
    {
        const auto seconds = value ("--seconds").isNotEmpty() ? value ("--seconds").getDoubleValue() : 5.0;
        const auto rate = value ("--rate").isNotEmpty() ? value ("--rate").getDoubleValue() : 48000.0;

        if (seconds <= 0.0 || rate < 8000.0)
            return fail ("--seconds must be > 0 and --rate at least 8000");

        if (! encore::render::writeWav (file (sweepOut), encore::render::makeSweep (rate, seconds), rate, error))
            return fail (error);

        std::cout << "Wrote a " << seconds << " s log sweep (20 Hz - 20 kHz, -6 dBFS) at " << rate << " Hz to " << sweepOut << "\n";
        return 0;
    }

    if (const auto plucksOut = value ("--make-plucks"); plucksOut.isNotEmpty())
    {
        const auto rate = value ("--rate").isNotEmpty() ? value ("--rate").getDoubleValue() : 48000.0;

        if (rate < 8000.0)
            return fail ("--rate must be at least 8000");

        if (! encore::render::writeWav (file (plucksOut), encore::render::makePlucks (rate), rate, error))
            return fail (error);

        std::cout << "Wrote generated plucks (6.5 s) at " << rate << " Hz to " << plucksOut << "\n";
        return 0;
    }

    if (const auto testOut = value ("--make-gate-test"); testOut.isNotEmpty())
    {
        const auto rate = value ("--rate").isNotEmpty() ? value ("--rate").getDoubleValue() : 48000.0;
        const auto noiseDb = value ("--noise-db").isNotEmpty() ? (float) value ("--noise-db").getDoubleValue() : -60.0f;

        if (rate < 8000.0)
            return fail ("--rate must be at least 8000");

        if (! encore::render::writeWav (file (testOut), encore::render::makeGateTest (rate, noiseDb), rate, error))
            return fail (error);

        std::cout << "Wrote the gate test (10 s, noise floor " << noiseDb << " dBFS) at " << rate << " Hz to " << testOut << "\n";
        return 0;
    }

    if (const auto showOut = value ("--make-gate-show"); showOut.isNotEmpty())
    {
        const auto folder = file (showOut);
        folder.createDirectory();
        const auto amp = value ("--amp").isNotEmpty() ? file (value ("--amp")) : juce::File();

        if (! folder.getChildFile ("show.json").replaceWithText (encore::json::toJson (encore::render::makeGateShow (amp))))
            return fail ("Couldn't write " + folder.getChildFile ("show.json").getFullPathName());

        std::cout << "Wrote the gate show (patches: Gate pair, Ducking delay, Smart X4) to " << showOut << "\n";
        return 0;
    }

    if (const auto stereoOut = value ("--make-stereo"); stereoOut.isNotEmpty())
    {
        const auto rate = value ("--rate").isNotEmpty() ? value ("--rate").getDoubleValue() : 48000.0;

        if (rate < 8000.0)
            return fail ("--rate must be at least 8000");

        if (! encore::render::writeWav (file (stereoOut), encore::render::makeStereoPlucks (rate), rate, error))
            return fail (error);

        std::cout << "Wrote stereo plucks (6.5 s) at " << rate << " Hz to " << stereoOut << "\n";
        return 0;
    }

    if (const auto resonanceOut = value ("--make-resonance"); resonanceOut.isNotEmpty())
    {
        const auto rate = value ("--rate").isNotEmpty() ? value ("--rate").getDoubleValue() : 48000.0;

        if (rate < 8000.0)
            return fail ("--rate must be at least 8000");

        if (! encore::render::writeWav (file (resonanceOut), encore::render::makeResonance (rate), rate, error))
            return fail (error);

        std::cout << "Wrote plucks with a 2.8 kHz resonance (6.5 s) at " << rate << " Hz to " << resonanceOut << "\n";
        return 0;
    }

    if (const auto showOut = value ("--make-eq-show"); showOut.isNotEmpty())
    {
        const auto folder = file (showOut);
        folder.createDirectory();
        const auto amp = value ("--amp").isNotEmpty() ? file (value ("--amp")) : juce::File();

        if (! folder.getChildFile ("show.json").replaceWithText (encore::json::toJson (encore::render::makeEqShow (amp))))
            return fail ("Couldn't write " + folder.getChildFile ("show.json").getFullPathName());

        std::cout << "Wrote the Studio EQ show (patches: Dynamic EQ, dry guitar; Dynamic EQ, this band) to " << showOut << "\n";
        return 0;
    }

    const auto inPath = value ("--in"), outPath = value ("--out");

    if (inPath.isEmpty() || outPath.isEmpty())
        return fail (juce::String ("--in and --out are needed.\n\n") + usage);

    const auto input = encore::render::readAudioFile (file (inPath), error);

    if (! input)
        return fail (error);

    const auto bufferSize = value ("--buffer").isNotEmpty() ? value ("--buffer").getIntValue() : 64;
    encore::render::Result result;

    if (const auto type = value ("--block"); type.isNotEmpty())
    {
        juce::var settings (new juce::DynamicObject());

        if (const auto settingsPath = value ("--settings"); settingsPath.isNotEmpty())
        {
            if (! file (settingsPath).existsAsFile())
                return fail ("No such settings file: " + settingsPath);

            const auto parsed = juce::JSON::parse (file (settingsPath));

            if (parsed.getDynamicObject() == nullptr)
                return fail ("The settings file isn't a JSON object: " + settingsPath);

            settings = parsed;
        }

        for (int i = 0; i + 1 < args.size(); ++i)
        {
            if (args[i] != "--set")
                continue;

            const auto assignment = args[i + 1];

            if (! assignment.contains ("="))
                return fail ("--set needs id=value, got \"" + assignment + "\"");

            const auto v = assignment.fromFirstOccurrenceOf ("=", false, false).trim();
            juce::var parsedValue = v.containsOnly ("-+.0123456789eE") && v.isNotEmpty() ? juce::var (v.getDoubleValue()) : juce::var (v);
            settings.getDynamicObject()->setProperty (assignment.upToFirstOccurrenceOf ("=", false, false).trim(), parsedValue);
        }

        std::map<juce::String, juce::String> refs;

        for (int i = 0; i + 1 < args.size(); ++i)
        {
            if (args[i] != "--ref")
                continue;

            if (! args[i + 1].contains ("="))
                return fail ("--ref needs name=value, got \"" + args[i + 1] + "\"");

            refs[args[i + 1].upToFirstOccurrenceOf ("=", false, false).trim()] = args[i + 1].fromFirstOccurrenceOf ("=", false, false).trim();
        }

        std::optional<encore::render::Sweep> sweep;

        if (const auto text = value ("--sweep"); text.isNotEmpty())
        {
            const auto range = text.fromFirstOccurrenceOf ("=", false, false);

            if (! text.contains ("=") || ! range.contains (":"))
                return fail ("--sweep needs id=from:to, got \"" + text + "\"");

            sweep = encore::render::Sweep { text.upToFirstOccurrenceOf ("=", false, false).trim(),
                                            range.upToFirstOccurrenceOf (":", false, false).getFloatValue(),
                                            range.fromFirstOccurrenceOf (":", false, false).getFloatValue() };
        }

        result = encore::render::renderBlock (input->buffer, input->sampleRate, type, settings, bufferSize, value ("--preset"), refs, sweep);
    }
    else if (const auto showPath = value ("--show"); showPath.isNotEmpty())
    {
        encore::ShowFolder folder;
        const auto show = encore::ShowFolder::open (file (showPath), folder, error);

        if (! show)
            return fail (error);

        const auto which = value ("--patch");
        const encore::PatchModel* patch = nullptr;

        for (const auto& p : show->patches)
            if (p.name == which)
                patch = &p;

        if (patch == nullptr && which.containsOnly ("0123456789") && which.isNotEmpty())
        {
            const auto index = which.getIntValue() - 1;

            if (index >= 0 && index < (int) show->patches.size())
                patch = &show->patches[(size_t) index];
        }

        if (patch == nullptr && which.isEmpty() && ! show->patches.empty())
            patch = &show->patches.front();

        if (patch == nullptr)
            return fail ("No patch \"" + which + "\" in the show");

        auto edited = *patch;

        for (int i = 0; i + 1 < args.size(); ++i)
        {
            if (args[i] != "--ref")
                continue;

            const auto assignment = args[i + 1];
            const auto target = assignment.upToFirstOccurrenceOf (".", false, false).trim();
            const auto name = assignment.fromFirstOccurrenceOf (".", false, false).upToFirstOccurrenceOf ("=", false, false).trim();

            if (! assignment.contains (".") || ! assignment.contains ("=") || name.isEmpty())
                return fail ("With --show, --ref needs blockId.name=value, got \"" + assignment + "\"");

            auto found = false;

            for (int b = 0; b < edited.numBlocks(); ++b)
                if (auto* block = edited.blockAt (b); block != nullptr && block->id == target)
                {
                    block->refs[name] = assignment.fromFirstOccurrenceOf ("=", false, false).trim();
                    found = true;
                }

            if (! found)
                return fail ("No block \"" + target + "\" in the patch");
        }

        result = encore::render::renderPatch (input->buffer, input->sampleRate, edited, folder, bufferSize);
    }
    else
    {
        return fail (juce::String ("Give --block TYPE or --show SHOW.\n\n") + usage);
    }

    const auto report = result.report();
    std::cout << report;

    if (! result.ok)
        return 1;

    if (! encore::render::writeWav (file (outPath), result.output, result.sampleRate, error))
        return fail (error);

    if (const auto reportPath = value ("--report"); reportPath.isNotEmpty())
        file (reportPath).replaceWithText (report);

    return 0;
}
