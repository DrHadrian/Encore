// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/Fader.h"

#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

namespace faderLaw
{
    namespace
    {
        struct Point { float db, position; };
        constexpr Point points[] = { { 6.0f, 0.0f }, { 0.0f, 0.1f }, { -6.0f, 0.25f }, { -12.0f, 0.4f },
                                     { -24.0f, 0.6f }, { -40.0f, 0.8f }, { -60.0f, 0.95f } };
    }

    float positionFor (float dB) noexcept
    {
        if (dB <= silentDb)
            return 1.0f;

        if (dB >= points[0].db)
            return 0.0f;

        for (size_t i = 1; i < std::size (points); ++i)
        {
            if (dB >= points[i].db)
            {
                const auto& a = points[i - 1];
                const auto& b = points[i];
                return a.position + (b.position - a.position) * (a.db - dB) / (a.db - b.db);
            }
        }

        // Below −60: the last stretch down to −∞ at the end.
        const auto& last = points[std::size (points) - 1];
        return juce::jmin (1.0f, last.position + (1.0f - last.position) * (last.db - dB) / (last.db - silentDb));
    }

    float dbFor (float position) noexcept
    {
        position = juce::jlimit (0.0f, 1.0f, position);

        if (position >= 0.999f)
            return silentDb;

        for (size_t i = 1; i < std::size (points); ++i)
        {
            if (position <= points[i].position)
            {
                const auto& a = points[i - 1];
                const auto& b = points[i];
                return a.db + (b.db - a.db) * (position - a.position) / (b.position - a.position);
            }
        }

        const auto& last = points[std::size (points) - 1];
        return last.db + (silentDb - last.db) * (position - last.position) / (1.0f - last.position);
    }
}

//==============================================================================
Fader::Fader (Orientation o) : orientation (o)
{
    setWantsKeyboardFocus (false);
    setMouseCursor (orientation == Orientation::vertical ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::LeftRightResizeCursor);
}

void Fader::setDb (float dB)
{
    dB = juce::jlimit (faderLaw::silentDb, 6.0f, dB);

    if (! juce::approximatelyEqual (dB, db))
    {
        db = dB;
        repaint();
    }
}

void Fader::setDimmed (bool d)
{
    if (d != dimmed)
    {
        dimmed = d;
        repaint();
    }
}

juce::Rectangle<float> Fader::getTrackBounds() const
{
    const auto r = getLocalBounds().toFloat();
    const auto t = (float) size::faderTrackW;

    if (orientation == Orientation::vertical)
    {
        const auto x = (float) (size::faderScaleW + size::faderTickGap + size::faderTickW + size::faderTickGap + 4);
        const auto top = (float) size::faderThumbH * 0.5f;
        return { x, top, t, (float) size::sendFaderTrackH };
    }

    const auto half = (float) size::faderHThumb * 0.5f;
    return { r.getX(), r.getCentreY() - t * 0.5f, juce::jmax (0.0f, r.getWidth() - half), t };
}

float Fader::positionOf (juce::Point<float> p) const
{
    const auto track = getTrackBounds();

    if (orientation == Orientation::vertical)
        return (p.y - track.getY()) / juce::jmax (1.0f, track.getHeight());

    // Horizontal: left is −∞, so the law runs the other way.
    return 1.0f - (p.x - track.getX()) / juce::jmax (1.0f, track.getWidth());
}

void Fader::paint (juce::Graphics& g)
{
    const auto alpha = dimmed ? state::disabledAlpha : (isEnabled() ? 1.0f : state::disabledAlpha);
    const auto track = getTrackBounds();
    const auto p = faderLaw::positionFor (db);

    g.setColour (paint::c (colour::meterTrack).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (track, 2.0f);

    if (orientation == Orientation::vertical)
    {
        // The scale: labels right-aligned before a 6 px tick.
        static const char* const labels[] = { "+6", "0", "-6", "-12", "-24", "-40", "-inf" };
        static const float marks[] = { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -40.0f, faderLaw::silentDb };

        for (size_t i = 0; i < std::size (marks); ++i)
        {
            const auto y = track.getY() + track.getHeight() * faderLaw::positionFor (marks[i]);
            juce::String text (labels[i]);

            if (text.startsWithChar ('-'))
                text = juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + (text == "-inf" ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")) : text.substring (1));

            paint::text (g, text, { 0.0f, y - 6.0f, (float) size::faderScaleW, 11.0f }, type::faderScale,
                         colour::meterScale, juce::Justification::centredRight);
            g.setColour (paint::c (colour::faderTick).withMultipliedAlpha (alpha));
            g.fillRect ((float) (size::faderScaleW + size::faderTickGap), y, (float) size::faderTickW, 1.0f);
        }

        const auto thumbY = track.getY() + track.getHeight() * p;
        g.setColour (paint::c (colour::sliderFill).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (track.withTop (thumbY), 2.0f);

        const auto thumb = juce::Rectangle<float> ((float) size::faderThumbW, (float) size::faderThumbH).withCentre ({ track.getCentreX(), thumbY });
        g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
        g.fillRoundedRectangle (thumb.translated (0.0f, 2.0f), (float) size::faderThumbRadius);
        g.setGradientFill (juce::ColourGradient::vertical (paint::c (colour::thumbTop).withMultipliedAlpha (alpha), thumb.getY(),
                                                           paint::c (colour::thumbBot).withMultipliedAlpha (alpha), thumb.getBottom()));
        g.fillRoundedRectangle (thumb, (float) size::faderThumbRadius);
        g.setColour (paint::c (colour::knobBorder).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (thumb.reduced (0.5f), (float) size::faderThumbRadius - 0.5f, 1.0f);
        g.setColour (paint::c (colour::buttonTop).withMultipliedAlpha (alpha));
        g.fillRect (juce::Rectangle<float> (18.0f, 1.0f).withCentre (thumb.getCentre()));
        return;
    }

    const auto thumbX = track.getX() + track.getWidth() * (1.0f - p);
    g.setColour (paint::c (colour::sliderFill).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (track.withRight (thumbX), 2.0f);

    const auto thumb = juce::Rectangle<float> ((float) size::faderHThumb, (float) size::faderHThumb).withCentre ({ thumbX, track.getCentreY() });
    g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
    g.fillEllipse (thumb.translated (0.0f, 1.0f));
    g.setGradientFill (juce::ColourGradient::vertical (paint::c (colour::thumbTop).withMultipliedAlpha (alpha), thumb.getY(),
                                                       paint::c (colour::thumbBot).withMultipliedAlpha (alpha), thumb.getBottom()));
    g.fillEllipse (thumb);
}

void Fader::setFromUser (float newDb)
{
    newDb = juce::jlimit (faderLaw::silentDb, 6.0f, newDb);

    if (juce::approximatelyEqual (newDb, db))
        return;

    db = newDb;
    repaint();

    if (onChange != nullptr)
        onChange (db);
}

void Fader::mouseDown (const juce::MouseEvent& e)
{
    // A click on the track jumps there; a drag then moves from the thumb.
    const auto track = getTrackBounds();
    const auto thumbPos = faderLaw::positionFor (db);
    const auto clicked = juce::jlimit (0.0f, 1.0f, positionOf (e.position));
    const auto thumbCentre = orientation == Orientation::vertical ? track.getY() + track.getHeight() * thumbPos
                                                                  : track.getX() + track.getWidth() * (1.0f - thumbPos);
    const auto onThumb = std::abs ((orientation == Orientation::vertical ? e.position.y : e.position.x) - thumbCentre) <= 8.0f;

    if (! onThumb)
        setFromUser (faderLaw::dbFor (clicked));

    dragStartPosition = faderLaw::positionFor (db);
}

void Fader::mouseDrag (const juce::MouseEvent& e)
{
    const auto track = getTrackBounds();
    const auto delta = orientation == Orientation::vertical ? (float) e.getDistanceFromDragStartY() / juce::jmax (1.0f, track.getHeight())
                                                            : -(float) e.getDistanceFromDragStartX() / juce::jmax (1.0f, track.getWidth());
    const auto fine = e.mods.isShiftDown() ? 0.25f : 1.0f;
    setFromUser (faderLaw::dbFor (juce::jlimit (0.0f, 1.0f, dragStartPosition + delta * fine)));
}

void Fader::mouseUp (const juce::MouseEvent&)
{
    if (onGestureEnd != nullptr)
        onGestureEnd();
}

void Fader::mouseDoubleClick (const juce::MouseEvent&)
{
    setFromUser (0.0f);

    if (onGestureEnd != nullptr)
        onGestureEnd();
}

void Fader::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const auto step = (w.deltaY != 0.0f ? w.deltaY : -w.deltaX) > 0.0f ? -0.01f : 0.01f;
    setFromUser (faderLaw::dbFor (juce::jlimit (0.0f, 1.0f, faderLaw::positionFor (db) + step)));

    if (onGestureEnd != nullptr)
        onGestureEnd();
}

} // namespace encore::kit
