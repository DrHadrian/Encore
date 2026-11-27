// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).
//
// The rest of v3.2 (docs/design/reference/v32-README.md, A2 and A3): each state
// rendered as its target picture is named, for tools/ui-compare.py.

#include "ui/BottomRow.h"

#include "UiSnapshot.h"

namespace encore::test
{

class V32TargetTests final : public juce::UnitTest
{
public:
    V32TargetTests() : juce::UnitTest ("v3.2 targets (A2, A3)", "Encore") {}

    void runTest() override
    {
        beginTest ("System health window (v32-health, v32-health-healthy): 640 x 492, summary, To check, Audio, Device, Computer");
        {
            ScopedEncoreLook look;

            // The board's values (BHealth).
            HealthReport report;
            report.toCheck = { juce::String::fromUTF8 ("Output \xe2\x80\x9c" "Amp DI\xe2\x80\x9d is muted") };
            report.summary = juce::String::fromUTF8 ("CPU 9% \xc2\xb7 5.3 ms round trip");
            report.audio = { { "CPU", "9%" }, { "Overruns", "0" }, { "Driver dropouts", "0" }, { "Bad audio caught", "0" },
                             { "Limiter", "idle" }, { "Clips", "0" }, { "Memory", "101 MB of 15 GB" } };
            report.device = { { "Interface", juce::String::fromUTF8 ("Focusrite USB ASIO \xc2\xb7 ASIO") }, { "Rate", "48.0 kHz" },
                              { "Buffer", "64 samples (1.3 ms)" }, { "Latency", "in 2.6 + out 2.7 = 5.3 ms" } };
            report.computer = { { "Mains power", true }, { "High priority", true }, { "Power throttling off", true },
                                { "Pro Audio audio thread", true } };

            bool closed = false, reset = false;
            HealthDetailsDialog dialog ([&] { reset = true; }, [&] { closed = true; });
            auto& frame = dialog.getFrame();
            frame.setBounds (0, 0, theme::size::healthDialogW + 2, theme::size::healthDialogH + 2);   // as the overlay places it
            dialog.setReport (report);
            expect (dialog.getReport() == report);

            expectEquals (frame.getDialogTitle(), juce::String ("System health"));
            juce::TextButton* resetButton = nullptr;
            juce::TextButton* doneButton = nullptr;

            for (auto* child : frame.getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*> (child))
                {
                    if (b->getButtonText() == "Reset counters") resetButton = b;
                    if (b->getButtonText() == "Done")           doneButton = b;
                }

            expect (resetButton != nullptr && doneButton != nullptr, "Reset counters and Done in the footer");

            if (resetButton != nullptr && doneButton != nullptr)
            {
                expectEquals (resetButton->getTooltip(), juce::String ("Start the overrun, dropout and clip counts again from zero"));
                resetButton->onClick();
                doneButton->onClick();
                expect (reset && closed);
            }

            for (const auto scale : { 1.0f, 2.0f })
                writeSnapshot (frame.createComponentSnapshot (frame.getLocalBounds(), false, scale),
                               juce::String ("v32-health-window") + (scale > 1.0f ? "@2x" : ""));

            // Healthy: no "To check" group; the columns move up.
            auto healthy = report;
            healthy.toCheck.clear();
            dialog.setReport (healthy);

            for (const auto scale : { 1.0f, 2.0f })
                writeSnapshot (frame.createComponentSnapshot (frame.getLocalBounds(), false, scale),
                               juce::String ("v32-health-healthy-window") + (scale > 1.0f ? "@2x" : ""));

            // Many things to check: the body scrolls, nothing is cut off the window.
            auto many = report;

            for (int i = 0; i < 8; ++i)
                many.toCheck.add ("Something else to check " + juce::String (i + 1)
                                  + ", with a long description that doesn't fit on one line of the window at all");

            dialog.setReport (many);
            expect (frame.createComponentSnapshot (frame.getLocalBounds()).isValid());

            // No device, a lost device, not on Windows: the words the README gives.
            auto none = report;
            none.device = { { "Interface", "No audio device open" }, { "Rate", "-" }, { "Buffer", "-" }, { "Latency", "-" } };
            none.computer.clear();
            dialog.setReport (none);
            expect (dialog.getReport().computer.empty());
        }
    }
};

static V32TargetTests v32TargetTests;

} // namespace encore::test
