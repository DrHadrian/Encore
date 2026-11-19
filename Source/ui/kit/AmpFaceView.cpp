// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/AmpFaceView.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreTheme.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

namespace
{
    constexpr float imageScale = 2.0f;   // the bodies are drawn at 2x, like the face
    constexpr int bodyMargin = 6;        // room for the shadow and the glow around a body

    /** A part's body (knob, switch, light) as an image, the part at bodyMargin from its top left. */
    juce::Image renderBody (const AmpPreset& amp, const AmpPart& part, float value, bool lit)
    {
        const auto w = part.width() + 2 * bodyMargin, h = part.height() + 2 * bodyMargin;
        juce::Image image (juce::Image::ARGB, juce::roundToInt ((float) w * imageScale), juce::roundToInt ((float) h * imageScale), true);
        juce::Graphics g (image);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.addTransform (juce::AffineTransform::translation ((float) (bodyMargin - part.x), (float) (bodyMargin - part.y)).scaled (imageScale));
        AmpFace::paintBody (g, amp, part, value, lit);
        return image;
    }

    void drawBody (juce::Graphics& g, const juce::Image& image, const juce::Component& c)
    {
        if (image.isValid())
            g.drawImage (image, c.getLocalBounds().toFloat().expanded ((float) bodyMargin), juce::RectanglePlacement::stretchToFit);
    }

    juce::Rectangle<int> partRect (const AmpPart& p) { return { p.x, p.y, p.width(), p.height() }; }
}

//==============================================================================
/** A driving knob: a rotary slider over 270°, drawn as the part. */
class AmpFaceView::Knob final : public juce::Slider
{
public:
    Knob (const AmpPreset& a, const AmpPart& p) : part (p), amp (a)
    {
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        setMouseClickGrabsKeyboardFocus (false);
        setTitle (p.label.isNotEmpty() ? p.label.replace ("\n", " ") : ampPreset::parameterFor (p.drives));
        setBounds (partRect (p));
        update();
    }

    const AmpPart part;

    void valueChanged() override { update(); }

    /** Redraws the knob's image if its position moved (a value set without a notification). */
    void update()
    {
        const auto proportion = (float) valueToProportionOfLength (getValue());

        if (image.isNull() || std::abs (proportion - shown) > 1.0e-4f)
        {
            shown = proportion;
            image = renderBody (amp, part, proportion, false);
            repaint();
        }
    }

    void paint (juce::Graphics& g) override { drawBody (g, image, *this); }

    bool hitTest (int x, int y) override
    {
        return getLocalBounds().toFloat().getCentre().getDistanceFrom ({ (float) x, (float) y }) <= (float) getWidth() * 0.5f + 2.0f;
    }

private:
    const AmpPreset& amp;
    juce::Image image;
    float shown = -1.0f;
};

/** A driving switch (Level match): up / out while on. */
class AmpFaceView::Switch final : public juce::Button
{
public:
    Switch (const AmpPreset& a, const AmpPart& p) : juce::Button (p.label.replace ("\n", " ")), part (p)
    {
        setClickingTogglesState (true);
        setMouseClickGrabsKeyboardFocus (false);
        setBounds (partRect (p));
        up = renderBody (a, p, 1.0f, false);
        down = renderBody (a, p, 0.0f, false);
    }

    const AmpPart part;

    void paintButton (juce::Graphics& g, bool, bool) override { drawBody (g, getToggleState() ? up : down, *this); }

private:
    juce::Image up, down;
};

/** A light: lit while the block is on (and a click turns it on or off), or always off. */
class AmpFaceView::Light final : public juce::Button
{
public:
    Light (const AmpPreset& a, const AmpPart& p) : juce::Button ("Light"), part (p)
    {
        setMouseClickGrabsKeyboardFocus (false);
        setBounds (partRect (p));
        lit = renderBody (a, p, 0.0f, true);
        unlit = renderBody (a, p, 0.0f, false);

        if (p.lightsWhileOn())
        {
            setTooltip ("Turn the block on or off");
            setTitle ("Power");
        }
        else
        {
            setTooltip ("Always off: it was off when the capture was made");
            setInterceptsMouseClicks (false, false);
        }
    }

    const AmpPart part;
    bool shownLit = true;

    void paintButton (juce::Graphics& g, bool, bool) override { drawBody (g, shownLit ? lit : unlit, *this); }

private:
    juce::Image lit, unlit;
};

/** A Fixed knob or switch: no control, only its tooltip. */
class AmpFaceView::FixedArea final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit FixedArea (const AmpPart& p)
    {
        setBounds (partRect (p));
        setTooltip (p.fixedTooltip());
        setTitle (p.label.replace ("\n", " ") + ": " + p.fixedTooltip());
    }
};

//==============================================================================
AmpFaceView::AmpFaceView (const AmpPreset& preset) : GearView (Size::panel), amp (preset)
{
    setSize (amp::faceW, amp::faceH);
    setOpaque (false);
    face = AmpFace::compose (amp, AmpFace::Layer::staticOnly);
    setTitle ("Amp" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + amp.name);
    auto followingLight = false;

    for (const auto& p : amp.parts)
    {
        if (p.isKnob() && p.drivesParameter())
            addAndMakeVisible (knobs.add (std::make_unique<Knob> (amp, p)));
        else if (p.isToggle() && p.drivesParameter())
            addAndMakeVisible (switches.add (std::make_unique<Switch> (amp, p)));
        else if (p.isFixed())
            addAndMakeVisible (fixedAreas.add (std::make_unique<FixedArea> (p)));
        else if (p.isLight())
        {
            auto* light = lights.add (std::make_unique<Light> (amp, p));
            light->shownLit = p.lightsWhileOn() && on;
            light->onClick = [this] { if (onPowerClicked != nullptr) onPowerClicked(); };
            addAndMakeVisible (light);
            followingLight = followingLight || p.lightsWhileOn();
        }
    }

    if (! followingLight)
    {
        // No light follows the block: the power LED at the top right of the panel.
        const auto& lay = AmpFace::layoutFor (amp.look.body);
        powerLed = std::make_unique<PowerLed>();
        powerLed->setBounds (lay.px + lay.pw - size::ledHalo - 4, lay.py + 4, size::ledHalo, size::ledHalo);
        powerLed->setOn (on);
        powerLed->onClick = [this] { if (onPowerClicked != nullptr) onPowerClicked(); };
        addAndMakeVisible (*powerLed);
    }
}

AmpFaceView::~AmpFaceView() = default;

juce::Rectangle<int> AmpFaceView::getControlArea() const
{
    const auto& lay = AmpFace::layoutFor (amp.look.body);
    return { lay.px, lay.py, lay.pw, lay.panelH };
}

void AmpFaceView::setOn (bool shouldBeOn)
{
    if (shouldBeOn == on)
        return;

    on = shouldBeOn;

    for (auto* l : lights)
    {
        const auto lit = l->part.lightsWhileOn() && on;

        if (l->shownLit != lit)
        {
            l->shownLit = lit;
            l->repaint();
        }
    }

    if (powerLed != nullptr)
        powerLed->setOn (on);
}

juce::Slider* AmpFaceView::findKnob (const juce::String& paramId) const
{
    for (auto* k : knobs)
        if (ampPreset::parameterFor (k->part.drives) == paramId)
            return k;

    return nullptr;
}

juce::Button* AmpFaceView::findSwitch (const juce::String& paramId) const
{
    for (auto* s : switches)
        if (ampPreset::parameterFor (s->part.drives) == paramId)
            return s;

    return nullptr;
}

void AmpFaceView::syncKnobs()
{
    for (auto* k : knobs)
        k->update();
}

void AmpFaceView::setChip (const juce::String& paramId, const juce::String& text)
{
    const auto old = std::find_if (chips.begin(), chips.end(), [&paramId] (const ShownChip& c) { return c.paramId == paramId; });

    if (old != chips.end())
    {
        if (old->text == text)
            return;

        repaint (old->bounds.getSmallestIntegerContainer());
        chips.erase (old);
    }

    const auto* knob = static_cast<Knob*> (findKnob (paramId));

    if (text.isEmpty() || knob == nullptr)
        return;

    // Centred on the knob, on the side away from its label, inside the face.
    const auto k = knob->getBounds().toFloat();
    const auto w = textWidth (font (type::gearChip), text) + (float) size::chipPadX * 2.0f, h = (float) size::chipH;
    const auto gap = (float) size::knobLabelGap;
    const auto y = knob->part.labelPos == "above" ? k.getBottom() + gap : k.getY() - gap - h;
    auto r = juce::Rectangle<float> (w, h).withCentre ({ k.getCentreX(), y + h * 0.5f });
    r = r.constrainedWithin (getLocalBounds().toFloat());
    chips.push_back ({ paramId, text, r });
    repaint (r.getSmallestIntegerContainer());
}

juce::String AmpFaceView::getChip (const juce::String& paramId) const
{
    for (const auto& c : chips)
        if (c.paramId == paramId)
            return c.text;

    return {};
}

juce::StringArray AmpFaceView::getDrivenParameters() const
{
    juce::StringArray ids;

    for (auto* k : knobs)
        ids.addIfNotAlreadyThere (ampPreset::parameterFor (k->part.drives));

    for (auto* s : switches)
        ids.addIfNotAlreadyThere (ampPreset::parameterFor (s->part.drives));

    return ids;
}

juce::String AmpFaceView::getTooltipAt (juce::Point<int> p) const
{
    for (auto* c : getChildren())
    {
        if (! c->isVisible() || ! c->getBounds().contains (p))
            continue;

        if (auto* t = dynamic_cast<juce::TooltipClient*> (c))
            return t->getTooltip();
    }

    return {};
}

void AmpFaceView::clickPower()
{
    for (auto* l : lights)
    {
        if (l->part.lightsWhileOn())
        {
            l->onClick();
            return;
        }
    }

    if (powerLed != nullptr)
    {
        powerLed->setToggleState (! powerLed->getToggleState(), juce::dontSendNotification);
        powerLed->onClick();
    }
}

void AmpFaceView::paint (juce::Graphics& g)
{
    if (face.isValid())
        g.drawImage (face, juce::Rectangle<float> (0.0f, 0.0f, (float) amp::faceW, (float) amp::faceH), juce::RectanglePlacement::stretchToFit);
}

void AmpFaceView::paintOverChildren (juce::Graphics& g)
{
    // The same chip as under a knob in the parameter list (KnobView).
    for (const auto& c : chips)
    {
        g.setColour (paint::c (colour::knobChipFill));
        g.fillRoundedRectangle (c.bounds, (float) size::chipRadius);
        g.setColour (paint::c (colour::expChipText));
        g.setFont (font (type::gearChip));
        g.drawText (c.text, c.bounds, juce::Justification::centred, false);
    }
}

} // namespace encore::kit
