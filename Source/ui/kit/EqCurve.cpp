// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/EqCurve.h"

#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

namespace
{
    constexpr double lowHz = 20.0, highHz = 20000.0;
    constexpr float rangeDb = 12.0f;

    float xFor (double hz, float width) noexcept
    {
        return (float) (std::log (hz / lowHz) / std::log (highHz / lowHz)) * width;
    }
}

EqCurve::EqCurve (bool isFull) : full (isFull)
{
    setOpaque (false);
}

double EqCurve::frequencyAt (int index, int numPoints) noexcept
{
    const auto t = numPoints > 1 ? (double) index / (double) (numPoints - 1) : 0.0;
    return lowHz * std::pow (highHz / lowHz, t);
}

void EqCurve::setResponse (std::vector<float> decibels)
{
    if (decibels == response)
        return;

    response = std::move (decibels);
    rebuild();
    repaint();
}

void EqCurve::resized()
{
    rebuild();
}

void EqCurve::rebuild()
{
    curve.clear();
    fill.clear();
    grid.clear();
    zero.clear();

    const auto area = getLocalBounds().toFloat().reduced (1.0f);

    if (area.isEmpty())
        return;

    const auto yFor = [&] (float db) { return area.getCentreY() - juce::jlimit (-rangeDb, rangeDb, db) / rangeDb * area.getHeight() * 0.5f; };

    if (full)
    {
        // Lines at 50, 100, 200 Hz ... 10 kHz; and at ±6 / ±12 dB.
        for (const auto hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const auto x = area.getX() + xFor (hz, area.getWidth());
            grid.addRectangle (x, area.getY(), 1.0f, area.getHeight());
        }

        for (const auto db : { -9.0f, -4.5f, 4.5f, 9.0f })
            grid.addRectangle (area.getX(), yFor (db), area.getWidth(), 1.0f);

        zero.addRectangle (area.getX(), yFor (0.0f), area.getWidth(), 1.0f);
    }

    const auto n = (int) response.size();

    if (n < 2)
        return;

    for (int i = 0; i < n; ++i)
    {
        const auto x = area.getX() + area.getWidth() * (float) i / (float) (n - 1);
        const auto y = yFor (response[(size_t) i]);

        if (i == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }

    fill = curve;
    fill.lineTo (area.getRight(), area.getBottom());
    fill.lineTo (area.getX(), area.getBottom());
    fill.closeSubPath();
}

void EqCurve::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const auto radius = full ? 5.0f : 3.0f;
    paint::inset (g, r, colour::eqWell, colour::fieldBorder, radius);

    g.setColour (paint::c (colour::eqGrid));
    g.fillPath (grid);
    g.setColour (paint::c (colour::eqZero));
    g.fillPath (zero);

    g.setColour (paint::c (colour::eqCurveFill));
    g.fillPath (fill);
    g.setColour (paint::c (colour::eqCurve));
    g.strokePath (curve, juce::PathStrokeType (full ? 2.0f : 1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (full)
    {
        const auto pad = (float) size::eqCurveLabelPad;
        const auto label = r.reduced (pad + 6.0f, pad);
        paint::text (g, "20 Hz", label.withTop (label.getBottom() - 11.0f), type::eqScale, colour::meterScale);
        paint::text (g, "20 kHz", label.withTop (label.getBottom() - 11.0f), type::eqScale, colour::meterScale, juce::Justification::centredRight);
        paint::text (g, "+12 dB", label.withHeight (11.0f), type::eqScale, colour::meterScale, juce::Justification::centredRight);
    }
}

void EqCurve::mouseUp (const juce::MouseEvent& e)
{
    if (onClick != nullptr && getLocalBounds().contains (e.getPosition()))
        onClick();
}

} // namespace encore::kit
