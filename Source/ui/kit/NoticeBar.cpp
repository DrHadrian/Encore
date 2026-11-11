// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/NoticeBar.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

NoticeBar::NoticeBar (Kind k) : kind (k)
{
    setOpaque (true);
    setMouseCursor (kind == Kind::muted ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void NoticeBar::setKind (Kind k)
{
    if (k != kind)
    {
        kind = k;
        setMouseCursor (kind == Kind::muted ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        update();
        repaint();
    }
}

void NoticeBar::setText (const juce::String& newText, const juce::String& newNote)
{
    if (newText != text || newNote != rightNote)
    {
        text = newText;
        rightNote = newNote;
        update();
        repaint();
    }
}

void NoticeBar::advance()
{
    if (kind == Kind::lost && isShowing())
    {
        spinnerStep = (spinnerStep + 1) % spinnerSteps;
        repaint (0, 0, size::bannerPadX + size::spinner + size::bannerGap, getHeight());
    }
}

void NoticeBar::resized()
{
    update();
}

void NoticeBar::update()
{
    const auto lost = kind == Kind::lost;
    const auto h = (float) getHeight();

    noteWidth = rightNote.isEmpty() ? 0.0f : textWidth (font (lost ? type::lostNote : type::noticeNote), rightNote) + 1.0f;
    fill = juce::ColourGradient::vertical (paint::c (lost ? colour::lostTop : colour::noticeTop), 0.0f,
                                           paint::c (lost ? colour::lostBot : colour::noticeBot), h);

    // A ring with its top quarter missing, made once; paint() turns it.
    spinnerArc.clear();

    if (lost)
    {
        spinnerBox = juce::Rectangle<float> ((float) size::bannerPadX, (h - 1.0f - (float) size::spinner) * 0.5f,
                                             (float) size::spinner, (float) size::spinner);
        const auto radius = (spinnerBox.getWidth() - (float) size::spinnerStroke) * 0.5f;
        const auto quarter = juce::MathConstants<float>::halfPi;
        juce::Path arc;
        arc.addCentredArc (spinnerBox.getCentreX(), spinnerBox.getCentreY(), radius, radius, 0.0f,
                           quarter * 0.5f, quarter * 3.5f, true);
        juce::PathStrokeType ((float) size::spinnerStroke).createStrokedPath (spinnerArc, arc);
    }
}

void NoticeBar::mouseUp (const juce::MouseEvent& e)
{
    if (kind == Kind::muted && onClick != nullptr && getLocalBounds().contains (e.getPosition()))
        onClick();
}

void NoticeBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    if (kind == Kind::muted)
    {
        g.fillAll (paint::c (colour::mutedBanner));
        paint::text (g, text, r, type::mutedBar, colour::textStrong, juce::Justification::centred);
        return;
    }

    const auto lost = kind == Kind::lost;
    const auto edit = kind == Kind::notice;
    g.setGradientFill (fill);
    g.fillRect (r);
    g.setColour (paint::c (colour::paneBorder));
    paint::hline (g, 0.0f, r.getBottom() - 1.0f, r.getWidth());
    r = r.withTrimmedBottom (1.0f);

    // A flex row as on the boards: padding, [badge | spinner], gap, text ... note, padding.
    const auto padX = (float) (edit ? size::noticePadX : size::bannerPadX);
    const auto gap = (float) (edit ? size::noticeGap : size::bannerGap);
    auto row = r.reduced (padX, 0.0f);

    if (edit)
    {
        const auto badge = row.removeFromLeft ((float) size::noticeBadge)
                              .withSizeKeepingCentre ((float) size::noticeBadge, (float) size::noticeBadge);
        g.setColour (paint::c (colour::amberStrong));
        g.fillEllipse (badge);
        paint::text (g, "!", badge, type::badgeMark, colour::amberInk, juce::Justification::centred);
        row.removeFromLeft (gap);
    }
    else if (lost)
    {
        g.setColour (paint::c (colour::danger));
        g.fillPath (spinnerArc, juce::AffineTransform::rotation (juce::MathConstants<float>::twoPi * (float) spinnerStep / (float) spinnerSteps,
                                                                 spinnerBox.getCentreX(), spinnerBox.getCentreY()));
        row.removeFromLeft ((float) size::spinner + gap);
    }

    if (noteWidth > 0.0f)
    {
        paint::text (g, rightNote, row.removeFromRight (juce::jmin (row.getWidth() * 0.5f, noteWidth)),
                     lost ? type::lostNote : type::noticeNote, lost ? colour::lostSubText : colour::noticeSubtle,
                     juce::Justification::centredRight);
        row.removeFromRight (gap);
    }

    paint::text (g, text, row, edit ? type::noticeText : type::bannerText, lost ? colour::lostText : colour::noticeText);
}

} // namespace encore::kit
