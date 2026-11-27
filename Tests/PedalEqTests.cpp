// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// The Pedal EQ "G7" (FX Unit, stage B1, decision 0063): its parameters and
// its sound, measured against its maths.

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/PedalEqBlock.h"
#include "engine/core/Realtime.h"

namespace encore::test
{

class PedalEqTests final : public juce::UnitTest
{
public:
    PedalEqTests() : juce::UnitTest ("Pedal EQ", "Encore") {}

    void runTest() override
    {
        beginTest ("Parameters: ids, order, ranges, defaults and choices");
        {
            PedalEqBlock eq;
            juce::StringArray ids;

            for (const auto& p : eq.getParameters())
                ids.add (p->getSpec().id);

            expectEquals (ids.joinIntoString (","),
                          juce::String ("g100,g200,g400,g800,g1k6,g3k2,g6k4,level,qMode,input,quality,captureLevel,realClip"));

            for (const auto* id : { "g100", "g200", "g400", "g800", "g1k6", "g3k2", "g6k4", "level" })
            {
                const auto& parameterSpec = eq.findParameter (id)->getSpec();
                expectEquals (parameterSpec.range.start, -15.0f, id);
                expectEquals (parameterSpec.range.end, 15.0f, id);
                expectEquals (parameterSpec.defaultValue, 0.0f, id);
                expectEquals (parameterSpec.unit, juce::String ("dB"), id);
                expect (! eq.findParameter (id)->isChoice() && parameterSpec.rampSeconds > 0.0, juce::String (id) + " ramps");
            }

            expectEquals (eq.findParameter ("g1k6")->getSpec().name, juce::String ("1.6 kHz"));
            expectEquals (eq.findParameter ("g100")->getSpec().name, juce::String ("100 Hz"));

            const auto choices = [&eq] (const char* id) { return eq.findParameter (id)->getSpec().choices.joinIntoString (" | "); };
            const auto choiceDefault = [&eq] (const char* id) { return eq.findParameter (id)->getChoiceIndex(); };
            expectEquals (choices ("qMode"), juce::String ("Classic | Wide"));
            expectEquals (choices ("input"), juce::String ("Pad -6 dB | Normal | Boost +6 dB"));
            expectEquals (choices ("quality"), juce::String ("Live | High | Max"));
            expectEquals (choices ("captureLevel"), juce::String ("As captured | Level-matched"));
            expectEquals (choices ("realClip"), juce::String ("Off | On"));
            expectEquals (choiceDefault ("qMode"), 0);
            expectEquals (choiceDefault ("input"), 1);
            expectEquals (choiceDefault ("quality"), 0);
            expectEquals (choiceDefault ("captureLevel"), 0);
            expectEquals (choiceDefault ("realClip"), 0);
        }

        beginTest ("Keeps the channels it gets; no tail");
        {
            PedalEqBlock eq;
            expectEquals (eq.getOutputChannels (1), 1);
            expectEquals (eq.getOutputChannels (2), 2);
            expect (! eq.hasTail());
        }
    }
};

static PedalEqTests pedalEqTests;

} // namespace encore::test
