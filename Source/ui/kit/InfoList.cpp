// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/InfoList.h"

#include "ui/kit/Chip.h"
#include "ui/kit/Pane.h"
#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreLookAndFeel.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

namespace
{
    constexpr int subheaderH = 26, keyW = 84, rowPitch = 22, noteGap = 6;

    int noteHeight (const juce::String& note, int width)
    {
        if (note.isEmpty())
            return 0;

        juce::AttributedString s;
        s.append (note, font (type::caption));
        s.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout layout;
        layout.createLayout (s, (float) juce::jmax (1, width));
        return (int) std::ceil (layout.getHeight()) + noteGap;
    }

    int valueLines (const InfoList::Row& row, int width)
    {
        const auto text = row.value + (row.tail.isNotEmpty() ? " " + row.tail : juce::String());
        return textWidth (font (type::bodySmall), text) > (float) width ? 2 : 1;
    }
}

InfoList::InfoList()
{
    setInterceptsMouseClicks (false, true);
}

void InfoList::setSections (const std::vector<Section>& newSections)
{
    if (newSections == sections)
        return;

    sections = newSections;
    buttons.clear();

    for (int i = 0; i < (int) sections.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (sections[(size_t) i].button));
        setVariant (*b, Variant::small);

        if (sections[(size_t) i].buttonIcon.has_value())
            setButtonIcon (*b, *sections[(size_t) i].buttonIcon, (float) size::infoButtonIcon, false);

        if (sections[(size_t) i].buttonKey.isNotEmpty())
            setButtonKeyCap (*b, sections[(size_t) i].buttonKey);

        b->setWantsKeyboardFocus (false);
        b->onClick = [this, i] { if (onButton != nullptr) onButton (i); };
        addChildComponent (b);
        b->setVisible (sections[(size_t) i].button.isNotEmpty());
    }

    resized();
    repaint();
}

int InfoList::getIdealHeight (int width) const
{
    const auto valueW = width - 2 * size::inspectorPadX - keyW - size::inspectorColGap;
    auto y = 0;

    for (const auto& s : sections)
    {
        y += subheaderH;

        for (const auto& r : s.rows)
            y += rowPitch + (valueLines (r, valueW) - 1) * size::inspectorTextH;

        y += noteHeight (s.note, width - 2 * size::inspectorPadX);

        if (s.button.isNotEmpty())
            y += size::buttonSmallHeight + 8;

        y += 4;
    }

    return y;
}

void InfoList::resized()
{
    const auto valueW = getWidth() - 2 * size::inspectorPadX - keyW - size::inspectorColGap;
    auto y = 0;

    for (size_t i = 0; i < sections.size(); ++i)
    {
        const auto& s = sections[i];
        y += subheaderH;

        for (const auto& r : s.rows)
            y += rowPitch + (valueLines (r, valueW) - 1) * size::inspectorTextH;

        y += noteHeight (s.note, getWidth() - 2 * size::inspectorPadX);

        if (auto* b = buttons[(int) i]; b != nullptr && s.button.isNotEmpty())
        {
            b->setBounds (size::inspectorPadX, y, b->getBestWidthForHeight (size::buttonSmallHeight), size::buttonSmallHeight + size::buttonDrop);
            y += size::buttonSmallHeight + 8;
        }

        y += 4;
    }
}

void InfoList::paint (juce::Graphics& g)
{
    const auto w = getWidth();
    const auto valueX = size::inspectorPadX + keyW + size::inspectorColGap;
    const auto valueW = w - valueX - size::inspectorPadX;
    auto y = 0;

    for (size_t i = 0; i < sections.size(); ++i)
    {
        const auto& s = sections[i];
        paintSubheader (g, { 0, y, w, subheaderH }, paint::caps (type::paneHeader, s.title), s.right, i > 0);
        y += subheaderH;

        for (const auto& r : s.rows)
        {
            const auto lines = valueLines (r, valueW);
            paint::text (g, r.key, { (float) size::inspectorPadX, (float) y + 4.0f, (float) keyW, (float) size::inspectorTextH },
                         type::noteText, colour::textTertiary);

            const auto area = juce::Rectangle<float> ((float) valueX, (float) y + 3.0f, (float) valueW, (float) (lines * size::inspectorTextH));

            if (lines == 1)
            {
                const auto vw = juce::jmin ((float) valueW, textWidth (font (type::bodySmall), r.value) + 1.0f);
                paint::text (g, r.value, area.withWidth (vw), type::bodySmall, r.amber ? colour::faultCardText : colour::text);
                auto rest = area.withTrimmedLeft (vw + 4.0f);

                if (r.tail.isNotEmpty())
                {
                    const auto tw = textWidth (font (type::bodySmall), r.tail) + 1.0f;
                    paint::text (g, r.tail, rest, type::bodySmall, colour::textMuted);
                    rest.removeFromLeft (tw + 4.0f);
                }

                if (r.chip.isNotEmpty())
                    paintChip (g, rest.withWidth (chipWidth (r.chip)).withSizeKeepingCentre (chipWidth (r.chip), 16.0f), r.chip, ChipStyle::mute);
            }
            else
            {
                g.setColour (paint::c (r.amber ? colour::faultCardText : colour::text));
                g.setFont (font (type::bodySmall));
                g.drawFittedText (r.value + (r.tail.isNotEmpty() ? " " + r.tail : juce::String()), area.toNearestInt(),
                                  juce::Justification::topLeft, 2, 1.0f);
            }

            y += rowPitch + (lines - 1) * size::inspectorTextH;
        }

        if (const auto nh = noteHeight (s.note, w - 2 * size::inspectorPadX); nh > 0)
        {
            g.setColour (paint::c (colour::textMuted));
            g.setFont (font (type::caption));
            g.drawFittedText (s.note, { size::inspectorPadX, y, w - 2 * size::inspectorPadX, nh }, juce::Justification::topLeft, 4, 1.0f);
            y += nh;
        }

        if (s.button.isNotEmpty())
            y += size::buttonSmallHeight + 8;

        y += 4;
    }
}

} // namespace encore::kit
