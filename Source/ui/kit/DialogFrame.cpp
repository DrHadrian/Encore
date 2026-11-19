// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/DialogFrame.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

//==============================================================================
/** A settings tab: 78 × 54, a 22 px icon over its name; the selected one
    has a lighter fill and white text. */
class DialogFrame::TabButton final : public juce::Button
{
public:
    TabButton (const Tab& t) : juce::Button (t.name), icon (t.icon)
    {
        setTitle (t.name);
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
        setRadioGroupId (1);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto r = getLocalBounds().toFloat();
        const auto on = getToggleState();

        if (on)
        {
            g.setColour (paint::c (colour::tabOn));
            g.fillRoundedRectangle (r, (float) size::tabRadius);
            g.setColour (paint::c (colour::dialogHighlight));
            paint::hline (g, r.getX() + (float) size::tabRadius, r.getY() + 1.0f, r.getWidth() - 2.0f * (float) size::tabRadius);
        }
        else if (highlighted)
        {
            g.setColour (paint::c (colour::iconButtonHover));
            g.fillRoundedRectangle (r, (float) size::tabRadius);
        }

        const auto labelH = type::tabLabel.px * 1.25f;
        const auto blockH = (float) size::tabIcon + (float) size::tabIconGap + labelH;
        const auto top = (r.getHeight() - blockH) * 0.5f;
        const auto ink = on || highlighted ? colour::textStrong : colour::textSecondary;
        drawIcon (g, icon, juce::Rectangle<float> ((float) size::tabIcon, (float) size::tabIcon)
                               .withPosition ((r.getWidth() - (float) size::tabIcon) * 0.5f, top), paint::c (ink));
        paint::text (g, getButtonText(), juce::Rectangle<float> (0.0f, top + (float) (size::tabIcon + size::tabIconGap), r.getWidth(), labelH),
                     type::tabLabel, ink, juce::Justification::centred);
    }

private:
    const Icon icon;
};

//==============================================================================
DialogFrame::DialogFrame (const juce::String& t, const juce::String& closeWhatItDoes)
    : title (t), closeButton (Icon::close, closeWhatItDoes)
{
    setTitle (title);
    setWantsKeyboardFocus (true);
    closeButton.setIconSize ((float) size::dialogCloseIcon);
    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (closeButton);
}

DialogFrame::~DialogFrame() = default;

void DialogFrame::setDialogTitle (const juce::String& t)
{
    if (t != title)
    {
        title = t;
        setTitle (title);
        repaint (0, 0, getWidth(), titleBottom());
    }
}

void DialogFrame::setTabs (const std::vector<Tab>& newTabs)
{
    tabs.clear();

    for (size_t i = 0; i < newTabs.size(); ++i)
    {
        auto* b = tabs.add (new TabButton (newTabs[i]));
        b->setClickingTogglesState (true);
        b->onClick = [this, index = (int) i]
        {
            if (index != selectedTab)
            {
                selectedTab = index;

                if (onTabChange != nullptr)
                    onTabChange (index);
            }
        };
        addAndMakeVisible (b);
    }

    selectedTab = tabs.isEmpty() ? -1 : juce::jlimit (0, tabs.size() - 1, juce::jmax (0, selectedTab));

    if (selectedTab >= 0)
        tabs[selectedTab]->setToggleState (true, juce::dontSendNotification);

    resized();
    repaint();
}

void DialogFrame::setSelectedTab (int index)
{
    if (! juce::isPositiveAndBelow (index, tabs.size()) || index == selectedTab)
        return;

    selectedTab = index;
    tabs[index]->setToggleState (true, juce::dontSendNotification);
}

void DialogFrame::setContent (juce::Component* c)
{
    if (c == content)
        return;

    if (content != nullptr)
        removeChildComponent (content);

    content = c;

    if (content != nullptr)
    {
        addAndMakeVisible (content);
        content->setBounds (getContentBounds());
    }
}

void DialogFrame::setFooterText (const juce::String& text)
{
    if (text != footerText)
    {
        footerText = text;
        repaint (0, footerTop(), getWidth(), getHeight() - footerTop());
    }
}

juce::TextButton& DialogFrame::addFooterButton (const juce::String& text, Variant variant)
{
    auto* b = footerButtons.add (new juce::TextButton (text));
    setVariant (*b, variant);
    b->setWantsKeyboardFocus (false);
    addAndMakeVisible (b);
    resized();
    return *b;
}

void DialogFrame::showFooter (bool shouldShow)
{
    if (shouldShow != hasFooter)
    {
        hasFooter = shouldShow;

        for (auto* b : footerButtons)
            b->setVisible (hasFooter);

        resized();
        repaint();
    }
}

int DialogFrame::titleBottom() const noexcept   { return 1 + size::dialogTitleBar; }
int DialogFrame::tabsBottom() const noexcept    { return titleBottom() + (tabs.isEmpty() ? 0 : size::tabStrip + 1); }
int DialogFrame::footerTop() const noexcept     { return hasFooter ? getHeight() - 1 - size::dialogFooter - 1 : getHeight() - 1; }

juce::Rectangle<int> DialogFrame::getContentBounds() const
{
    return juce::Rectangle<int>::leftTopRightBottom (1, tabsBottom(), getWidth() - 1, juce::jmax (tabsBottom(), footerTop()));
}

void DialogFrame::resized()
{
    const auto b = getLocalBounds().toFloat();
    const auto radius = (float) size::dialogRadius;

    bodyShape.clear();
    bodyShape.addRoundedRectangle (b, radius);
    titleShape.clear();
    titleShape.addRoundedRectangle (1.0f, 1.0f, b.getWidth() - 2.0f, (float) size::dialogTitleBar, radius - 1.0f, radius - 1.0f,
                                    true, true, false, false);
    titleFill = juce::ColourGradient::vertical (paint::c (colour::dialogTitleTop), 1.0f,
                                                paint::c (colour::dialogTitleBot), (float) titleBottom());
    tabFill = juce::ColourGradient::vertical (paint::c (colour::tabStripTop), (float) titleBottom(),
                                              paint::c (colour::tabStripBot), (float) (titleBottom() + size::tabStrip));

    closeButton.setBounds (getWidth() - 1 - size::dialogCloseInset - size::dialogClose,
                           1 + (size::dialogTitleBar - size::dialogClose) / 2, size::dialogClose, size::dialogClose);

    if (! tabs.isEmpty())
    {
        const auto total = tabs.size() * size::tabW + (tabs.size() - 1) * size::tabGap;
        auto x = (getWidth() - total) / 2;
        const auto y = titleBottom() + (size::tabStrip - size::tabH) / 2;

        for (auto* t : tabs)
        {
            t->setBounds (x, y, size::tabW, size::tabH);
            x += size::tabW + size::tabGap;
        }
    }

    if (hasFooter)
    {
        const auto footer = juce::Rectangle<int> (1, footerTop() + 1, getWidth() - 2, size::dialogFooter);
        auto right = footer.getRight() - size::dialogPadX;

        for (int i = footerButtons.size(); --i >= 0;)
        {
            auto* button = footerButtons[i];
            const auto w = juce::jmax (size::dialogButtonW, button->getBestWidthForHeight (size::buttonHeight));
            button->setBounds (right - w, footer.getCentreY() - size::buttonHeight / 2, w, size::buttonHeight);
            right -= w + size::dialogButtonGap;
        }
    }

    if (content != nullptr)
        content->setBounds (getContentBounds());
}

void DialogFrame::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto radius = (float) size::dialogRadius;

    g.setColour (paint::c (colour::dialog));
    g.fillPath (bodyShape);

    g.setGradientFill (titleFill);
    g.fillPath (titleShape);
    g.setColour (paint::c (colour::dialogHighlight));
    paint::hline (g, radius, 1.0f, b.getWidth() - radius * 2.0f);

    const auto titleArea = juce::Rectangle<float> (1.0f, 1.0f, b.getWidth() - 2.0f, (float) size::dialogTitleBar)
                               .reduced ((float) (size::dialogCloseInset + size::dialogClose + size::dialogCloseInset), 0.0f);
    paint::text (g, title, titleArea, type::dialogTitle, colour::textSoft, juce::Justification::centred);

    if (! tabs.isEmpty())
    {
        g.setGradientFill (tabFill);
        g.fillRect (juce::Rectangle<float> (1.0f, (float) titleBottom(), juce::jmax (0.0f, b.getWidth() - 2.0f), (float) size::tabStrip));
        g.setColour (paint::c (colour::paneBorder));
        paint::hline (g, 1.0f, (float) (titleBottom() + size::tabStrip), b.getWidth() - 2.0f);
    }

    if (hasFooter)
    {
        const auto top = (float) footerTop();
        const juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (bodyShape);
        g.setColour (paint::c (colour::paneBorder));
        paint::hline (g, 1.0f, top, b.getWidth() - 2.0f);
        g.setColour (paint::c (colour::dialogFooter));
        g.fillRect (juce::Rectangle<float> (1.0f, top + 1.0f, juce::jmax (0.0f, b.getWidth() - 2.0f), (float) size::dialogFooter));

        if (footerText.isNotEmpty())
        {
            auto textRight = b.getWidth() - (float) size::dialogPadX;

            for (auto* button : footerButtons)
                textRight = juce::jmin (textRight, (float) button->getX() - (float) size::dialogButtonGap);

            paint::text (g, footerText, juce::Rectangle<float>::leftTopRightBottom ((float) size::dialogPadX, top + 1.0f, textRight,
                                                                                     top + 1.0f + (float) size::dialogFooter),
                         type::label, colour::textMuted);
        }
    }

    g.setColour (paint::c (colour::paneBorder));
    g.drawRoundedRectangle (b.reduced (0.5f), radius - 0.5f, 1.0f);
}

bool DialogFrame::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && onClose != nullptr)
    {
        onClose();
        return true;
    }

    return false;
}

//==============================================================================
DialogOverlay::DialogOverlay (DialogFrame& f, juce::Rectangle<int> frameSize) : frame (f), wantedSize (frameSize)
{
    setInterceptsMouseClicks (true, true);   // nothing behind the dialog takes clicks
    addAndMakeVisible (frame);
}

juce::Rectangle<int> DialogOverlay::getFrameBounds() const
{
    const auto area = getLocalBounds().reduced (size::outerPadding);
    return juce::Rectangle<int> (juce::jmin (wantedSize.getWidth(), area.getWidth()), juce::jmin (wantedSize.getHeight(), area.getHeight()))
        .withCentre (getLocalBounds().getCentre());
}

bool DialogOverlay::keyPressed (const juce::KeyPress& key)
{
    frame.keyPressed (key);
    return true;
}

void DialogOverlay::resized()
{
    frame.setBounds (getFrameBounds());
}

void DialogOverlay::paint (juce::Graphics& g)
{
    g.fillAll (paint::c (colour::dimmer));

    // The shadow (0, 30, 80 px, black 70 %), made once per frame size and screen scale.
    const auto bounds = getFrameBounds();
    const auto scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto margin = size::dialogShadowBlur + size::dialogShadowY;

    if (shadow.isNull() || shadowFor != bounds.withZeroOrigin() || ! juce::approximatelyEqual (scale, shadowScale))
    {
        shadowFor = bounds.withZeroOrigin();
        shadowScale = scale;
        const auto area = shadowFor.expanded (margin);
        shadow = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) area.getWidth() * scale)),
                              juce::jmax (1, juce::roundToInt ((float) area.getHeight() * scale)), true);
        juce::Graphics ig (shadow);
        ig.addTransform (juce::AffineTransform::scale (scale));
        juce::Path body;
        body.addRoundedRectangle (shadowFor.translated (margin, margin).toFloat(), (float) size::dialogRadius);
        juce::DropShadow (paint::c (colour::dialogShadow), size::dialogShadowBlur, { 0, size::dialogShadowY }).drawForPath (ig, body);
    }

    g.drawImage (shadow, bounds.expanded (margin).toFloat());
}

} // namespace encore::kit
