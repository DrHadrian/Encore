// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/AmpBuilderView.h"

#include "session/AmpLibrary.h"
#include "session/ShowSession.h"
#include "ui/kit/AmpArt.h"
#include "ui/kit/AmpFace.h"
#include "ui/kit/AmpTemplates.h"
#include "ui/kit/SegmentedControl.h"
#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreLookAndFeel.h"
#include "ui/theme/Icons.h"
#include "ui/theme/Paint.h"

#include <cmath>

namespace encore
{

using namespace theme;

namespace
{
    juce::String middot()  { return juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")); }
    juce::String ellipsis() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")); }

    //==========================================================================
    // The parts list (board: lib), the names and the new parts' sizes (board: TYPE, SIZE).
    struct LibItem { const char* type; const char* name; const char* style; const char* chip; };
    struct LibGroup { const char* name; std::vector<LibItem> items; };

    const std::vector<LibGroup>& partsLibrary()
    {
        static const std::vector<LibGroup> groups {
            { "Controls", { { "knob", "Chickenhead knob", "chicken", "" }, { "knob", "Black knurled knob", "black", "" },
                            { "knob", "Cream dome knob", "cream", "" }, { "knob", "Silver knob", "silver", "" },
                            { "knob", "Black knob, white line", "plain", "" }, { "toggle", "Bat toggle", "bat", "TG" },
                            { "toggle", "Push button", "push", "PB" } } },
            { "Lights", { { "jewel", "Pilot light", "jewel", "PL" }, { "jewel", "LED", "led", "LD" } } },
            { "Panel", { { "section", "Panel section", "", "PS" }, { "outline", "Frame line", "", "FL" }, { "title", "Section title", "", "ST" },
                         { "text", "Label text", "", "TX" }, { "symbol", "Symbol", "", "SY" }, { "plate", "Name plate", "", "NP" },
                         { "badge", "Badge", "", "BD" }, { "handle", "Panel handle", "", "HD" }, { "jack", "Input jack", "", "IN" },
                         { "screw", "Screw", "", "SC" } } },
            { "Front", { { "logo", "Encore logo", "", "EN" }, { "trim", "Trim strip", "", "TR" } } },
        };
        return groups;
    }

    juce::String tipFor (const juce::String& type)
    {
        if (type == "knob")   return juce::String (juce::CharPointer_UTF8 ("A knob: drives Input, Bass, Mid, Treble or Output, or is fixed at the capture\xe2\x80\x99s setting"));
        if (type == "toggle") return "Drives Level match, or is fixed as it was in the capture";
        if (type == "jewel")  return "Lights while the amp block is on, or is always off";
        if (type == "logo")   return "The Encore logo, in a colour and size you pick";
        return "Print or decor: drives nothing";
    }

    juce::String typeName (const AmpPart& p)
    {
        if (p.isToggle()) return p.switchStyle == "push" ? "Push button" : "Bat toggle";
        if (p.isLight())  return p.style == "led" ? "LED" : "Pilot light";

        static const std::map<juce::String, juce::String> names {
            { "knob", "Knob" }, { "symbol", "Symbol" }, { "badge", "Badge" }, { "handle", "Panel handle" }, { "outline", "Frame line" },
            { "logo", "Encore logo" }, { "text", "Label text" }, { "plate", "Name plate" }, { "jack", "Input jack" }, { "screw", "Screw" },
            { "section", "Panel section" }, { "title", "Section title" }, { "trim", "Trim strip" },
        };
        const auto found = names.find (p.type);
        return found != names.end() ? found->second : p.type;
    }

    juce::Point<int> newSize (const juce::String& type)
    {
        static const std::map<juce::String, juce::Point<int>> sizes {
            { "symbol", { 14, 14 } }, { "badge", { 36, 42 } }, { "handle", { 12, 200 } }, { "outline", { 200, 100 } }, { "toggle", { 24, 42 } },
            { "jewel", { 28, 28 } }, { "logo", { 150, 26 } }, { "text", { 90, 14 } }, { "plate", { 70, 20 } }, { "jack", { 18, 18 } },
            { "screw", { 10, 10 } }, { "section", { 120, 56 } }, { "title", { 140, 12 } }, { "trim", { 556, 5 } },
        };
        const auto found = sizes.find (type);
        return found != sizes.end() ? found->second : juce::Point<int> (40, 40);
    }

    /** "→ Treble" / "FIXED 6" / "NOT SET", and its colours (board: bind). */
    struct Bind { juce::String text; juce::uint32 fill, ink; };

    Bind bindFor (const AmpPart& p)
    {
        static const std::map<juce::String, juce::String> drives {
            { "input", "Input" }, { "bass", "Bass" }, { "mid", "Mid" }, { "treble", "Treble" }, { "output", "Output" }, { "levelMatch", "Level match" },
        };
        const auto arrow = juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92 "));

        if (p.isNotSet())
            return { "NOT SET", amp::bindNotSetFill, amp::bindNotSetText };

        if (p.isFixed())
        {
            if (p.isKnob())
                return { "FIXED " + ampPreset::tenth (p.value), amp::bindFixedFill, amp::bindFixedText };

            const auto push = p.switchStyle == "push";
            return { juce::String ("FIXED ") + (p.value >= 0.5f ? (push ? "OUT" : "UP") : (push ? "IN" : "DOWN")), amp::bindFixedFill, amp::bindFixedText };
        }

        const auto found = drives.find (p.drives);
        return { arrow + (found != drives.end() ? found->second : p.drives), amp::bindDrivesFill, amp::bindDrivesText };
    }

    juce::Rectangle<int> rectOf (const AmpPart& p) { return { p.x, p.y, p.width(), p.height() }; }

    int snapTo (int v) { return (int) std::round ((float) v / (float) amp::snap) * amp::snap; }
}

//==============================================================================
/** Parts: every part in four groups; drag one onto the face, or click it
    (Enter with the keyboard) to add it at the face's centre. */
class AmpBuilderView::PartsList final : public juce::Component, public juce::TooltipClient
{
public:
    explicit PartsList (AmpBuilderView& o) : owner (o)
    {
        setWantsKeyboardFocus (true);
        setTitle ("Parts");

        for (const auto& g : partsLibrary())
        {
            rows.push_back ({ true, g.name, {} });

            for (const auto& it : g.items)
                rows.push_back ({ false, g.name, it });
        }

        // The knob icons: each knob style drawn small, once.
        for (const auto* style : { "chicken", "black", "cream", "silver", "plain" })
        {
            AmpPart k;
            k.type = "knob";
            k.style = style;
            k.size = "S";
            AmpPreset none;
            juce::Image icon (juce::Image::ARGB, 40, 40, true);
            juce::Graphics g (icon);
            g.addTransform (juce::AffineTransform::scale (40.0f / 26.0f));
            kit::AmpFace::paintBody (g, none, k, 0.5f, false);
            knobIcons[style] = icon;
        }
    }

    juce::String getTooltip() override
    {
        return juce::isPositiveAndBelow (hovered, (int) rows.size()) && ! rows[(size_t) hovered].group ? tipFor (rows[(size_t) hovered].item.type) : juce::String();
    }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < (int) rows.size(); ++i)
        {
            const auto& row = rows[(size_t) i];
            const auto r = rowBounds (i).toFloat();

            if (row.group)
            {
                paint::text (g, paint::caps (type::widgetGroup, row.groupName), r.withTrimmedLeft (4.0f).withTrimmedTop (6.0f), type::widgetGroup, colour::textDim);
                continue;
            }

            if (i == hovered || i == focused)
            {
                g.setColour (paint::c (colour::rowHover));
                g.fillRoundedRectangle (r, (float) size::rowRadius);
            }

            const auto chip = juce::Rectangle<float> (r.getX() + 8.0f, r.getCentreY() - 9.0f, 22.0f, 18.0f);

            if (juce::String (row.item.type) == "knob")
            {
                const auto& icon = knobIcons[row.item.style];
                g.drawImage (icon, chip.withSizeKeepingCentre (20.0f, 20.0f), juce::RectanglePlacement::centred);
            }
            else
            {
                g.setColour (paint::c (colour::iconChipFill));
                g.fillRoundedRectangle (chip, (float) size::widgetChipRadius);
                g.setColour (paint::c (colour::iconChipBorder));
                g.drawRoundedRectangle (chip.reduced (0.5f), (float) size::widgetChipRadius, 1.0f);
                paint::text (g, row.item.chip, chip, type::widgetChip, colour::iconChipText, juce::Justification::centred);
            }

            paint::text (g, row.item.name, r.withTrimmedLeft (39.0f).withTrimmedRight (24.0f), type::body, colour::textRow);

            const auto grip = juce::Rectangle<float> ((float) size::gripW, (float) size::gripH)
                                  .withPosition (r.getRight() - (float) (size::gripPadRight + size::gripW), r.getCentreY() - (float) size::gripH * 0.5f);
            g.setColour (paint::c (colour::gripDots));

            for (int dot = 0; dot < 6; ++dot)
                g.fillEllipse (grip.getX() + 1.0f + (float) (dot % 2) * 4.0f, grip.getY() + 1.0f + (float) (dot / 2) * 4.0f, 2.0f, 2.0f);
        }

        if (focused >= 0 && hasKeyboardFocus (false))
            paint::focusRing (g, rowBounds (focused).toFloat(), (float) size::rowRadius);
    }

    void mouseMove (const juce::MouseEvent& e) override { setHovered (rowAt (e.y)); }
    void mouseExit (const juce::MouseEvent&) override   { setHovered (-1); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressed = rowAt (e.y);
        dragging = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (pressed < 0 || ! e.mouseWasDraggedSinceMouseDown())
            return;

        dragging = true;
        const auto& item = rows[(size_t) pressed].item;
        owner.dragNewPart (item.type, item.style, owner.getFace()->getLocalPoint (nullptr, e.getScreenPosition()), false);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto row = pressed;
        pressed = -1;

        if (row < 0)
            return;

        const auto& item = rows[(size_t) row].item;

        if (dragging)
        {
            dragging = false;
            owner.dragNewPart (item.type, item.style, owner.getFace()->getLocalPoint (nullptr, e.getScreenPosition()), true);
            return;
        }

        if (rowAt (e.y) == row)
            owner.addPart (item.type, item.style, { amp::faceW / 2, amp::faceH / 2 });   // a click: at the face's centre
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
        {
            const auto step = key == juce::KeyPress::upKey ? -1 : 1;
            auto i = focused;

            do { i += step; } while (juce::isPositiveAndBelow (i, (int) rows.size()) && rows[(size_t) i].group);

            if (juce::isPositiveAndBelow (i, (int) rows.size()))
            {
                focused = i;
                repaint();
            }

            return true;
        }

        if (key == juce::KeyPress::returnKey && juce::isPositiveAndBelow (focused, (int) rows.size()) && ! rows[(size_t) focused].group)
        {
            const auto& item = rows[(size_t) focused].item;
            owner.addPart (item.type, item.style, { amp::faceW / 2, amp::faceH / 2 });
            return true;
        }

        return false;
    }

    void focusGained (FocusChangeType) override
    {
        if (focused < 0)
            focused = 1;   // the first part

        repaint();
    }

    void focusLost (FocusChangeType) override { repaint(); }

    int idealHeight() const { return rows.empty() ? 0 : rowBounds ((int) rows.size() - 1).getBottom() + 8; }

    /** The middle of a part's row (tests). */
    juce::Point<int> rowCentre (const juce::String& name) const
    {
        for (int i = 0; i < (int) rows.size(); ++i)
            if (! rows[(size_t) i].group && name == rows[(size_t) i].item.name)
                return rowBounds (i).getCentre();

        return {};
    }

private:
    struct Row { bool group; const char* groupName; LibItem item; };

    juce::Rectangle<int> rowBounds (int index) const
    {
        auto y = 0;

        for (int i = 0; i < index; ++i)
            y += rows[(size_t) i].group ? size::widgetGroupH : size::widgetRowH;

        return { 6, y, getWidth() - 12, rows[(size_t) index].group ? size::widgetGroupH : size::widgetRowH };
    }

    int rowAt (int y) const
    {
        for (int i = 0; i < (int) rows.size(); ++i)
            if (! rows[(size_t) i].group && rowBounds (i).contains (rowBounds (i).getX() + 1, y))
                return i;

        return -1;
    }

    void setHovered (int i)
    {
        if (i != hovered)
        {
            hovered = i;
            setMouseCursor (i >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    AmpBuilderView& owner;
    std::vector<Row> rows;
    std::map<juce::String, juce::Image> knobIcons;
    int hovered = -1, pressed = -1, focused = -1;
    bool dragging = false;
};

//==============================================================================
/** The face at 1:1 (scaled down to fit a small window): the composed image,
    and over it the selection, the bindings, the guides and the drop box. */
class AmpBuilderView::FaceCanvas final : public juce::Component, public juce::TooltipClient
{
public:
    explicit FaceCanvas (AmpBuilderView& o) : owner (o)
    {
        setSize (amp::faceW, amp::faceH);
        setWantsKeyboardFocus (true);
        setMouseClickGrabsKeyboardFocus (true);
        setTitle ("Amp face");
    }

    /** The amp changed: compose its face again (not in paint). */
    void refreshFace()
    {
        faceImage = kit::AmpFace::compose (owner.amp, kit::AmpFace::Layer::everything);
        repaint();
    }

    juce::String getTooltip() override
    {
        if (const auto* p = partAt (getMouseXYRelative()))
        {
            if (p->isKnob())
                return p->isFixed() ? p->fixedTooltip() : p->isNotSet() ? juce::String ("Not set: pick what it drives, or Fixed") : "Drives " + bindFor (*p).text.fromFirstOccurrenceOf (" ", false, false);

            if (p->isToggle())
                return p->isFixed() ? p->fixedTooltip() : juce::String ("Drives Level match");

            if (p->isLight())
                return p->lightsWhileOn() ? juce::String ("Lights while the amp block is on") : juce::String ("Always off: it was off when the capture was made");

            return typeName (*p);
        }

        return {};
    }

    const AmpPart* partAt (juce::Point<int> p) const
    {
        for (auto it = owner.amp.parts.rbegin(); it != owner.amp.parts.rend(); ++it)   // topmost first
            if (rectOf (*it).contains (p))
                return &*it;

        return nullptr;
    }

    //== Drop box (a new part from the list, or a part being moved) ============
    void showDrop (juce::Rectangle<int> r, const Landing& l)
    {
        dropBox = r;
        landing = l;
        repaint();
    }

    void hideDrop()
    {
        if (! dropBox.isEmpty())
        {
            dropBox = {};
            landing = {};
            repaint();
        }
    }

    bool isShowingDrop() const noexcept { return ! dropBox.isEmpty(); }

    void paint (juce::Graphics& g) override
    {
        if (moving.isNotEmpty() && faceWithout.isValid())
        {
            g.drawImage (faceWithout, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

            // The part where it was, faint (state::draggedAlpha), until it's let go.
            g.setOpacity (state::draggedAlpha);
            g.drawImage (movingImage, movingFrom.toFloat(), juce::RectanglePlacement::stretchToFit);
            g.setOpacity (1.0f);
        }
        else if (faceImage.isValid())
        {
            g.drawImage (faceImage, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
        }

        // Marks: NOT SET (amber, always), the selection (blue) with handles and bindings.
        for (const auto& p : owner.amp.parts)
        {
            const auto selected = owner.selection.contains (p.id);
            const auto bad = p.isNotSet();

            if (! selected && ! bad)
                continue;

            const auto r = rectOf (p).toFloat().expanded ((float) amp::outlineGap + 0.5f);
            juce::Path outline;
            outline.addRectangle (r);
            const float dash[] { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (outline, outline, dash, 2);
            g.setColour (paint::c (bad ? amp::notSetOutline : amp::selectedOutline));
            g.fillPath (outline);

            if (selected)
            {
                const auto h = (float) amp::handleSize;
                const auto box = rectOf (p).toFloat();

                for (auto corner : { box.getTopLeft(), box.getTopRight(), box.getBottomLeft(), box.getBottomRight() })
                {
                    const auto handle = juce::Rectangle<float> (h, h).withCentre (corner + juce::Point<float> (corner.x <= box.getX() ? -h * 0.5f : h * 0.5f,
                                                                                                                  corner.y <= box.getY() ? -h * 0.5f : h * 0.5f));
                    g.setColour (paint::c (amp::handleFill));
                    g.fillRoundedRectangle (handle, 2.0f);
                    g.setColour (paint::c (amp::handleBorder));
                    g.drawRoundedRectangle (handle.reduced (0.5f), 2.0f, 1.0f);
                }
            }

            if (p.isKnob() || (p.isToggle() && selected))
                paintBind (g, p);
        }

        // Smart guides and equal gaps (magenta), then the drop box.
        g.setColour (paint::c (amp::guide));

        for (const auto& guide : landing.guides)
            g.fillRect (guide.line);

        for (const auto& gap : landing.gaps)
        {
            g.setColour (paint::c (amp::guide));
            g.fillRect (gap.line);
            const auto text = juce::String (gap.px);
            const auto font = fontOfSize (type::Weight::bold, 9.0f);
            const auto w = textWidth (font, text) + 8.0f;
            const auto label = juce::Rectangle<float> (w, (float) amp::gapLabelH).withCentre ({ (float) gap.line.getCentreX(), (float) gap.line.getY() - 8.0f });
            g.fillRoundedRectangle (label, 3.0f);
            g.setColour (juce::Colours::white);
            g.setFont (font);
            g.drawText (text, label, juce::Justification::centred, false);
        }

        if (! dropBox.isEmpty())
        {
            const auto r = dropBox.toFloat();
            g.setColour (paint::c (amp::dropFill));
            g.fillRoundedRectangle (r, (float) amp::dropRadius);
            juce::Path outline;
            outline.addRoundedRectangle (r.reduced ((float) amp::dropBorder * 0.5f), (float) amp::dropRadius);
            const float dash[] { 6.0f, 4.0f };
            juce::PathStrokeType ((float) amp::dropBorder).createDashedStroke (outline, outline, dash, 2);
            g.setColour (paint::c (colour::accent));
            g.fillPath (outline);
        }

        if (hasKeyboardFocus (false) && owner.selection.isEmpty())
            paint::focusRing (g, getLocalBounds().toFloat().reduced (1.0f), 6.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto* p = partAt (e.getPosition());
        const auto add = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();

        if (p == nullptr)
        {
            if (! add)
                owner.select ({});   // the amp body: the amp's inspector

            return;
        }

        if (add)
        {
            owner.select (p->id, true);
            return;
        }

        if (! owner.selection.contains (p->id) || owner.selection.size() > 1)
            owner.select (p->id);

        pressedPart = p->id;
        grab = e.getPosition() - juce::Point<int> (p->x, p->y);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (pressedPart.isEmpty() || ! e.mouseWasDraggedSinceMouseDown())
            return;

        const auto* p = owner.amp.findPart (pressedPart);

        if (p == nullptr)
            return;

        if (moving.isEmpty())
            beginMove (*p);

        const auto l = owner.landingFor (*p, e.getPosition() - grab);
        showDrop (juce::Rectangle<int> (l.topLeft.x, l.topLeft.y, p->width(), p->height()), l);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (moving.isNotEmpty())
        {
            const auto id = moving;
            const auto to = dropBox.getPosition();
            endMove();

            if (const auto* p = owner.amp.findPart (id); p != nullptr && (p->x != to.x || p->y != to.y))
                owner.movePart (id, to);

            juce::ignoreUnused (e);
        }

        pressedPart.clear();
    }

    bool keyPressed (const juce::KeyPress& key) override { return owner.keyPressed (key); }

private:
    void beginMove (const AmpPart& p)
    {
        moving = p.id;
        movingFrom = kit::AmpFace::partBounds (p).expanded (8);

        // The face without the part, and the part alone (with its print), made once for the drag.
        auto without = owner.amp;
        without.parts.erase (std::remove_if (without.parts.begin(), without.parts.end(), [&p] (const AmpPart& q) { return q.id == p.id; }),
                             without.parts.end());
        faceWithout = kit::AmpFace::compose (without, kit::AmpFace::Layer::everything);

        movingImage = juce::Image (juce::Image::ARGB, movingFrom.getWidth() * 2, movingFrom.getHeight() * 2, true);
        juce::Graphics g (movingImage);
        g.addTransform (juce::AffineTransform::translation ((float) -movingFrom.getX(), (float) -movingFrom.getY()).scaled (2.0f));
        kit::AmpFace::paintPrint (g, owner.amp, p);
        kit::AmpFace::paintBody (g, owner.amp, p, p.isToggle() && ! p.isFixed() ? 1.0f : p.value, p.lightsWhileOn());
    }

    void endMove()
    {
        moving.clear();
        faceWithout = {};
        movingImage = {};
        hideDrop();
    }

    void paintBind (juce::Graphics& g, const AmpPart& p)
    {
        const auto b = bindFor (p);
        const auto font = fontOfSize (type::Weight::bold, 9.5f);
        const auto w = textWidth (font, b.text) + 2.0f * (float) amp::bindPadX;
        float top;

        if (p.isKnob())
        {
            const auto numbers = p.scale == "n10" || p.scale == "n12";
            top = (float) (p.y + p.height()) + (p.labelPos == "above" ? (numbers ? 8.0f : 4.0f) : (numbers ? 22.0f : p.scale == "ticks" ? 24.0f : 18.0f));
        }
        else
        {
            top = (float) (p.y + p.h) + (p.h < 40 ? 16.0f : 34.0f);
        }

        const auto r = juce::Rectangle<float> (w, (float) amp::bindH).withCentre ({ (float) p.x + (float) p.width() * 0.5f, top + (float) amp::bindH * 0.5f });
        g.setColour (paint::c (b.fill));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (paint::c (b.ink));
        g.setFont (font);
        g.drawText (b.text, r, juce::Justification::centred, false);
    }

    AmpBuilderView& owner;
    juce::Image faceImage, faceWithout, movingImage;
    juce::String moving, pressedPart;
    juce::Rectangle<int> movingFrom, dropBox;
    juce::Point<int> grab;
    Landing landing;
};

//==============================================================================
/** The inspector (300): the selected part's options, Arrange for several,
    the whole amp's look when nothing is selected; then the sound. Rebuilt
    when the selection or the amp changes. */
class AmpBuilderView::Inspector final : public juce::Component
{
public:
    explicit Inspector (AmpBuilderView& o) : owner (o)
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        setTitle ("Inspector");
    }

    ~Inspector() override { viewport.setViewedComponent (nullptr, false); }

    juce::String title, hint;

    void rebuild()
    {
        rows.clear();
        content.removeAllChildren();
        const auto& sel = owner.selection;
        const auto* part = sel.size() == 1 ? owner.amp.findPart (sel[0]) : nullptr;

        if (part != nullptr)
        {
            title = typeName (*part);
            hint = "click the amp body for the amp";
            buildPart (*part);
        }
        else if (sel.size() > 1)
        {
            title = "Arrange";
            hint = {};
            buildArrange();
        }
        else
        {
            title = "Amp" + middot() + owner.amp.name;
            hint = "the whole amp";
            buildAmp();
        }

        buildSound();
        layoutRows();
        getParentComponent()->repaint();
    }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        layoutRows();
    }

private:
    //== Pieces =================================================================
    struct Row
    {
        juce::String label;                  // empty: full width
        std::unique_ptr<juce::Component> editor;
        int height = amp::rowMinH;
    };

    /** A note in a box (NOT SET amber, Fixed grey) or plain (the hints). */
    struct Note final : public juce::Component
    {
        Note (juce::String t, juce::uint32 f, juce::uint32 b, juce::uint32 i) : text (std::move (t)), fill (f), border (b), ink (i) {}

        int heightFor (int width) const
        {
            juce::AttributedString s;
            s.append (text, fontOfSize (type::Weight::regular, 11.5f), paint::c (ink));
            juce::TextLayout l;
            l.createLayout (s, (float) juce::jmax (10, width - (fill != 0 ? 20 : 0)));
            return juce::roundToInt (l.getHeight() * 1.1f) + (fill != 0 ? 16 : 2);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();

            if (fill != 0)
            {
                g.setColour (paint::c (fill));
                g.fillRoundedRectangle (r, 6.0f);

                if (border != 0)
                {
                    g.setColour (paint::c (border));
                    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
                }

                r = r.reduced (10.0f, 8.0f);
            }

            g.setColour (paint::c (ink));
            g.setFont (fontOfSize (type::Weight::regular, fill != 0 ? 11.5f : 11.0f));
            g.drawFittedText (text, r.toNearestInt(), juce::Justification::topLeft, 8, 1.0f);
        }

        juce::String text;
        juce::uint32 fill, border, ink;
    };

    /** Buttons in a grid, one of them chosen (radio). */
    struct Choice final : public juce::Component
    {
        Choice (const juce::StringArray& names, int chosen, int columns, std::function<void (int)> pick, const juce::StringArray& tips = {})
            : cols (juce::jmax (1, columns))
        {
            for (int i = 0; i < names.size(); ++i)
            {
                auto* b = buttons.add (std::make_unique<juce::TextButton> (names[i]));
                b->setToggleState (i == chosen, juce::dontSendNotification);
                b->setMouseClickGrabsKeyboardFocus (false);
                b->setTooltip (tips[i]);
                b->getProperties().set ("encoreCompact", true);
                b->onClick = [pick, i] { pick (i); };
                addAndMakeVisible (b);
            }
        }

        /** Pictures in place of words (the knob styles), each button `height` tall. */
        void setIcons (const std::vector<juce::Image>& pictures, int height)
        {
            icons = pictures;
            rowH = height;
        }

        int heightFor() const { return ((buttons.size() + cols - 1) / cols) * (rowH + 4) - 4; }

        void resized() override
        {
            const auto w = (getWidth() - (cols - 1) * 4) / cols;

            for (int i = 0; i < buttons.size(); ++i)
                buttons[i]->setBounds ((i % cols) * (w + 4), (i / cols) * (rowH + 4), w, rowH);
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            for (int i = 0; i < buttons.size() && i < (int) icons.size(); ++i)
                g.drawImage (icons[(size_t) i], buttons[i]->getBounds().toFloat().withSizeKeepingCentre (24.0f, 24.0f), juce::RectanglePlacement::centred);
        }

        int cols, rowH = 22;
        juce::OwnedArray<juce::TextButton> buttons;
        std::vector<juce::Image> icons;
    };

    /** The five knob styles, drawn once (24 px at 2x). */
    static const std::vector<juce::Image>& knobIcons()
    {
        static const std::vector<juce::Image> icons = []
        {
            std::vector<juce::Image> list;

            for (const auto* style : { "chicken", "black", "cream", "silver", "plain" })
            {
                AmpPart k;
                k.type = "knob";
                k.style = style;
                k.size = "S";
                juce::Image icon (juce::Image::ARGB, 48, 48, true);
                juce::Graphics g (icon);
                g.addTransform (juce::AffineTransform::scale (48.0f / 26.0f));
                kit::AmpFace::paintBody (g, {}, k, 0.5f, false);
                list.push_back (icon);
            }

            return list;
        }();

        return icons;
    }

    /** Swatches (22 px), the chosen one ringed; each its own tooltip. */
    struct Swatches final : public juce::Component, public juce::TooltipClient
    {
        struct Item { juce::String name; std::function<void (juce::Graphics&, juce::Rectangle<float>)> draw; };

        Swatches (std::vector<Item> list, int chosen, std::function<void (int)> pick) : items (std::move (list)), selected (chosen), onPick (std::move (pick))
        {
            setWantsKeyboardFocus (false);
        }

        /** Wrapped into rows that fit the width. */
        int perRow() const { return juce::jmax (1, (getWidth() - 2 + 6) / (amp::swatch + 6)); }

        int heightFor (int width) const
        {
            const auto n = juce::jmax (1, (width - 2 + 6) / (amp::swatch + 6));
            const auto rowsNeeded = ((int) items.size() + n - 1) / n;
            return juce::jmax (amp::rowMinH, rowsNeeded * (amp::swatch + 6) - 6 + 2);
        }

        juce::Rectangle<float> box (int i) const
        {
            const auto n = perRow();
            return { (float) ((i % n) * (amp::swatch + 6)) + 1.0f, (float) ((i / n) * (amp::swatch + 6)) + 1.0f, (float) amp::swatch, (float) amp::swatch };
        }

        void paint (juce::Graphics& g) override
        {
            for (int i = 0; i < (int) items.size(); ++i)
            {
                const auto r = box (i);
                {
                    const juce::Graphics::ScopedSaveState state (g);
                    juce::Path clip;
                    clip.addRoundedRectangle (r, 5.0f);
                    g.reduceClipRegion (clip);
                    items[(size_t) i].draw (g, r);
                }
                g.setColour (paint::c (colour::paneBorder));
                g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);

                if (i == selected)
                {
                    g.setColour (paint::c (amp::swatchRing));
                    g.drawRoundedRectangle (r.expanded (1.5f), 6.0f, 2.0f);
                }
            }
        }

        juce::String getTooltip() override
        {
            const auto p = getMouseXYRelative();

            for (int i = 0; i < (int) items.size(); ++i)
                if (box (i).contains (p.toFloat()))
                    return items[(size_t) i].name;

            return {};
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            for (int i = 0; i < (int) items.size(); ++i)
                if (box (i).contains (e.position) && onPick != nullptr)
                    onPick (i);
        }

        std::vector<Item> items;
        int selected;
        std::function<void (int)> onPick;
    };

    /** A field (text or a number): applied on Enter or when it loses focus. */
    std::unique_ptr<juce::TextEditor> field (const juce::String& value, std::function<void (const juce::String&)> apply)
    {
        auto f = std::make_unique<juce::TextEditor>();
        setVariant (*f, Variant::field);
        f->setText (value, false);
        f->setSelectAllWhenFocused (true);
        auto* raw = f.get();
        auto commit = [raw, apply, last = value] () mutable
        {
            if (raw->getText() != last)
            {
                last = raw->getText();
                apply (last);
            }
        };
        f->onReturnKey = commit;
        f->onFocusLost = commit;
        return f;
    }

    void add (const juce::String& label, std::unique_ptr<juce::Component> editor, int height = amp::rowMinH)
    {
        content.addAndMakeVisible (*editor);
        rows.push_back ({ label, std::move (editor), height });
    }

    void addChoice (const juce::String& label, const juce::StringArray& names, int chosen, int columns, std::function<void (int)> pick,
                    const juce::StringArray& tips = {})
    {
        auto c = std::make_unique<Choice> (names, chosen, columns, std::move (pick), tips);
        const auto h = juce::jmax (amp::rowMinH, c->heightFor());
        add (label, std::move (c), h);
    }

    void addSeg (const juce::String& label, const juce::StringArray& names, int chosen, std::function<void (int)> pick)
    {
        auto seg = std::make_unique<kit::SegmentedControl>();
        seg->setSegments (names);
        seg->setSelected (chosen);
        seg->onClick = std::move (pick);
        add (label, std::move (seg));
    }

    void addNote (const juce::String& text, juce::uint32 fill, juce::uint32 border, juce::uint32 ink)
    {
        auto n = std::make_unique<Note> (text, fill, border, ink);
        add ({}, std::move (n), 0);   // its height from the width, in layoutRows()
    }

    void addSwatches (const juce::String& label, std::vector<Swatches::Item> items, int chosen, std::function<void (int)> pick)
    {
        add (label, std::make_unique<Swatches> (std::move (items), chosen, std::move (pick)));
    }

    static Swatches::Item imageSwatch (const juce::String& name, const juce::String& file, float fx = 0.5f, float fy = 0.5f)
    {
        return { name, [file, fx, fy] (juce::Graphics& g, juce::Rectangle<float> r)
        {
            const auto image = kit::ampArt::get (file);

            if (image.isValid())
            {
                const auto src = juce::Rectangle<int> (64, 64).withCentre ({ juce::roundToInt ((float) image.getWidth() * fx), juce::roundToInt ((float) image.getHeight() * fy) });
                g.drawImage (image, r.getSmallestIntegerContainer().getX(), r.getSmallestIntegerContainer().getY(), (int) r.getWidth(), (int) r.getHeight(),
                             src.getX(), src.getY(), src.getWidth(), src.getHeight());
            }
        } };
    }

    static Swatches::Item colourSwatch (const juce::String& name, juce::uint32 colour)
    {
        return { name, [colour] (juce::Graphics& g, juce::Rectangle<float> r) { g.setColour (paint::c (colour)); g.fillRect (r); } };
    }

    //== Part ===================================================================
    void buildPart (const AmpPart& p)
    {
        auto& o = owner;

        if (p.isKnob())
        {
            static const juce::StringArray drives { "Input", "Bass", "Mid", "Treble", "Output", "Fixed" };
            static const juce::StringArray keys { "input", "bass", "mid", "treble", "output", "fixed" };
            addChoice ("Drives", drives, keys.indexOf (p.drives), 3, [&o] (int i) { o.changeSelected ([i] (AmpPart& q) { q.drives = keys[i]; }); });

            if (p.isFixed())
            {
                auto row = std::make_unique<juce::Component>();
                auto slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
                slider->setRange (0.0, 10.0, 0.1);
                slider->setValue (p.value * 10.0f, juce::dontSendNotification);
                slider->setTooltip ("The setting the capture was made at");
                auto* s = slider.get();
                slider->onDragEnd = [&o, s] { o.changeSelected ([v = (float) s->getValue() / 10.0f] (AmpPart& q) { q.value = v; }); };
                auto value = field (ampPreset::tenth (p.value), [&o] (const juce::String& t)
                {
                    o.changeSelected ([v = juce::jlimit (0.0f, 10.0f, t.getFloatValue()) / 10.0f] (AmpPart& q) { q.value = v; });
                });
                struct Pair final : public juce::Component
                {
                    std::unique_ptr<juce::Slider> a;
                    std::unique_ptr<juce::TextEditor> b;
                    void resized() override
                    {
                        auto r = getLocalBounds();
                        b->setBounds (r.removeFromRight (44).withSizeKeepingCentre (44, 22));
                        r.removeFromRight (8);
                        a->setBounds (r);
                    }
                };
                auto pair = std::make_unique<Pair>();
                pair->addAndMakeVisible (*slider);
                pair->addAndMakeVisible (*value);
                pair->a = std::move (slider);
                pair->b = std::move (value);
                add ("Fixed at", std::move (pair));
                addNote (juce::String (juce::CharPointer_UTF8 ("Shows the setting the capture was made at. It can\xe2\x80\x99t be turned; in Edit its tooltip says \xe2\x80\x9c"))
                             + "fixed at " + ampPreset::tenth (p.value) + " in this capture" + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x9d.")),
                         amp::fixedNoteFill, 0, colour::textTertiary);
            }

            if (p.isNotSet())
                addNote (juce::String (juce::CharPointer_UTF8 ("Not set yet. Pick what it drives, or Fixed. A knob that is neither can\xe2\x80\x99t be saved.")),
                         amp::notSetNoteFill, amp::notSetNoteBorder, amp::notSetNoteText);

            static const juce::StringArray styles { "chicken", "black", "cream", "silver", "plain" };
            static const juce::StringArray styleNames { "Chickenhead", "Black knurled", "Cream dome", "Silver", "Black, white line" };
            auto knobs = std::make_unique<Choice> (juce::StringArray { "", "", "", "", "" }, styles.indexOf (p.style), 4,
                                                   [&o] (int i) { o.changeSelected ([i] (AmpPart& q) { q.style = styles[i]; }); }, styleNames);
            knobs->setIcons (knobIcons(), 34);
            const auto knobsH = knobs->heightFor();
            add ("Knob", std::move (knobs), knobsH);

            static const juce::StringArray sizes { "S", "M", "L", "XL" };
            addSeg ("Size", sizes, sizes.indexOf (p.size), [&o] (int i) { o.changeSelected ([i] (AmpPart& q) { q.size = sizes[i]; }); });
            addSeg ("Label", { "Above", "Below" }, p.labelPos == "above" ? 0 : 1,
                    [&o] (int i) { o.changeSelected ([i] (AmpPart& q) { q.labelPos = i == 0 ? "above" : "below"; }); });

            static const juce::StringArray scales { "ticks", "n10", "n12", "none" };
            addSeg ("Scale", { "Ticks", juce::String (juce::CharPointer_UTF8 ("1\xe2\x80\x93" "10")), juce::String (juce::CharPointer_UTF8 ("1\xe2\x80\x93" "12")), "None" },
                    juce::jmax (0, scales.indexOf (p.scale.isNotEmpty() ? p.scale : juce::String ("none"))),
                    [&o] (int i) { o.changeSelected ([i] (AmpPart& q) { q.scale = scales[i]; }); });
        }
        else if (p.isToggle())
        {
            const auto push = p.switchStyle == "push";
            addSeg ("Switch", { "Bat toggle", "Push button" }, push ? 1 : 0, [&o] (int i)
            {
                o.changeSelected ([i] (AmpPart& q)
                {
                    q.switchStyle = i == 1 ? "push" : "bat";
                    q.w = i == 1 ? 18 : 14;
                    q.h = i == 1 ? 18 : 26;
                });
            });
            addSeg ("Drives", { "Level match", "Fixed" }, p.isFixed() ? 1 : 0,
                    [&o] (int i) { o.changeSelected ([i] (AmpPart& q) { q.drives = i == 1 ? "fixed" : "levelMatch"; }); });

            if (p.isFixed())
            {
                addSeg ("Fixed", push ? juce::StringArray { "Out", "In" } : juce::StringArray { "Up", "Down" }, p.value >= 0.5f ? 0 : 1,
                        [&o] (int i) { o.changeSelected ([i] (AmpPart& q) { q.value = i == 0 ? 1.0f : 0.0f; }); });
                addNote (juce::String (juce::CharPointer_UTF8 ("Shows the switch as it was when the capture was made. It can\xe2\x80\x99t be switched.")),
                         amp::fixedNoteFill, 0, colour::textTertiary);
            }

            add ("Top label", field (p.top, [&o] (const juce::String& t) { o.changeSelected ([t] (AmpPart& q) { q.top = t; }); }));
        }
        else if (p.isLight())
        {
            addSeg ("Style", { "Jewel", "LED" }, p.style == "led" ? 1 : 0, [&o] (int i)
            {
                o.changeSelected ([i] (AmpPart& q)
                {
                    q.style = i == 1 ? "led" : "jewel";
                    q.w = q.h = i == 1 ? 8 : 28;
                });
            });
            addSeg ("Lights", { "Lights while on", "Always off" }, p.lights == "off" ? 1 : 0,
                    [&o] (int i) { o.changeSelected ([i] (AmpPart& q) { q.lights = i == 1 ? "off" : "whileOn"; }); });

            static const juce::StringArray colours { "red", "amber", "green", "blue" };
            addSwatches ("Colour", { colourSwatch ("Red", amp::lightRed), colourSwatch ("Amber", amp::lightAmber), colourSwatch ("Green", amp::lightGreen),
                                     colourSwatch ("Blue", amp::lightBlue) },
                         juce::jmax (0, colours.indexOf (p.colour)), [&o] (int i) { o.changeSelected ([i] (AmpPart& q) { q.colour = colours[i]; }); });
        }
        else if (p.type == "symbol")
        {
            static const juce::StringArray icons { "volume", "treble", "bass", "mid", "gain", "reverb", "power", "input", "clean", "dirty" };
            static const juce::StringArray names { "Volume", "Treble", "Bass", "Mid / shape", "Gain", "Reverb", "Power", "Input", "Clean", "Dirty" };
            addChoice ("Symbol", names, icons.indexOf (p.icon), 2, [&o] (int i) { o.changeSelected ([i] (AmpPart& q) { q.icon = icons[i]; }); });
        }
        else if (p.type == "text")
        {
            addSeg ("Style", { "Caps", "Serif" }, p.font == "serif" ? 1 : 0,
                    [&o] (int i) { o.changeSelected ([i] (AmpPart& q) { q.font = i == 1 ? "serif" : "caps"; }); });
        }

        if (p.type == "plate")
            addSeg ("Shape", { "Rectangle", "Oval" }, p.shape == "oval" ? 1 : 0,
                    [&o] (int i) { o.changeSelected ([i] (AmpPart& q) { q.shape = i == 1 ? "oval" : "rect"; }); });

        if (p.type == "section" || p.type == "plate" || p.type == "trim")
        {
            std::vector<Swatches::Item> items;
            juce::StringArray keys;
            const auto panel = [&] (const juce::String& k, const juce::String& n) { keys.add (k); items.push_back (imageSwatch (n, "panel_tall_" + k + ".webp")); };

            if (p.type == "section")
            {
                panel ("black", "Black"); panel ("gold", "Gold"); panel ("silver", "Silver"); panel ("orange", "Orange"); panel ("white", "White");
            }
            else if (p.type == "plate")
            {
                const auto plate = [&] (const juce::String& k, const juce::String& n, juce::uint32 top, juce::uint32 bottom)
                {
                    keys.add (k);
                    items.push_back ({ n, [top, bottom] (juce::Graphics& g, juce::Rectangle<float> r)
                    {
                        g.setGradientFill (juce::ColourGradient::vertical (paint::c (top), r.getY(), paint::c (bottom), r.getBottom()));
                        g.fillRect (r);
                    } });
                };
                plate ("gold", "Gold", amp::plateGoldTop, amp::plateGoldBottom);
                plate ("silver", "Silver", amp::plateSilverTop, amp::plateSilverBottom);
                plate ("black", "Black", amp::plateBlackTop, amp::plateBlackBottom);
                keys.add ("blackgold");
                items.push_back ({ "Black, gold rim", [] (juce::Graphics& g, juce::Rectangle<float> r)
                {
                    g.setColour (paint::c (amp::plateRimEdge));
                    g.fillRect (r);
                    g.setColour (paint::c (amp::plateRim));
                    g.drawRect (r, 3.0f);
                } });
            }
            else
            {
                panel ("black", "Black"); panel ("gold", "Gold"); panel ("silver", "Silver");
            }

            addSwatches ("Material", std::move (items), keys.indexOf (p.mat.isNotEmpty() ? p.mat : keys[0]),
                         [&o, keys] (int i) { o.changeSelected ([i, keys] (AmpPart& q) { q.mat = keys[i]; }); });
        }

        if (p.type == "logo")
        {
            static const juce::StringArray colours { "cream", "white", "gold", "black" };
            addSwatches ("Colour", { colourSwatch ("Cream", amp::logoCream), colourSwatch ("White", amp::logoWhite), colourSwatch ("Gold", amp::logoGold),
                                     colourSwatch ("Black", amp::logoBlack) },
                         juce::jmax (0, colours.indexOf (p.colour)), [&o] (int i) { o.changeSelected ([i] (AmpPart& q) { q.colour = colours[i]; }); });
            addNote (juce::String (juce::CharPointer_UTF8 ("Every amp carries the Encore logo. Its colour and size can change; the logo itself can\xe2\x80\x99t.")),
                     0, 0, colour::textMuted);
        }

        if (p.isKnob() || p.isToggle())
            add ("Text", field (p.label, [&o] (const juce::String& t) { o.changeSelected ([t] (AmpPart& q) { q.label = t; }); }));
        else if (p.type == "text" || p.type == "plate" || p.type == "title")
            add ("Text", field (p.text, [&o] (const juce::String& t) { o.changeSelected ([t] (AmpPart& q) { q.text = t; }); }));

        // Position and size (a knob's size is its S / M / L / XL).
        const auto pairOfFields = [this] (const juce::String& a, int va, std::function<void (int)> setA, const juce::String& b, int vb,
                                          std::function<void (int)> setB, bool enabledB)
        {
            struct Pair final : public juce::Component
            {
                std::unique_ptr<juce::TextEditor> l, r;
                void resized() override
                {
                    auto area = getLocalBounds();
                    l->setBounds (area.removeFromLeft ((area.getWidth() - 6) / 2).withSizeKeepingCentre (area.getWidth(), 24));
                    area.removeFromLeft (6);
                    r->setBounds (area.withSizeKeepingCentre (area.getWidth(), 24));
                }
            };
            auto pair = std::make_unique<Pair>();
            pair->l = field (juce::String (va), [setA] (const juce::String& t) { setA (t.getIntValue()); });
            pair->r = field (juce::String (vb), [setB] (const juce::String& t) { setB (t.getIntValue()); });
            pair->l->setTooltip (a);
            pair->r->setTooltip (b);
            pair->r->setEnabled (enabledB);
            pair->addAndMakeVisible (*pair->l);
            pair->addAndMakeVisible (*pair->r);
            return pair;
        };

        add ("Position", pairOfFields ("x", p.x, [&o] (int v) { o.changeSelected ([v] (AmpPart& q) { q.x = snapTo (juce::jlimit (0, amp::faceW - q.width(), v)); }); },
                                       "y", p.y, [&o] (int v) { o.changeSelected ([v] (AmpPart& q) { q.y = snapTo (juce::jlimit (0, amp::faceH - q.height(), v)); }); },
                                       true));

        if (! p.isKnob())
            add ("Size", pairOfFields ("w", p.w, [&o] (int v) { o.changeSelected ([v] (AmpPart& q) { q.w = juce::jlimit (2, amp::faceW, v); }); },
                                       "h", p.h, [&o] (int v) { o.changeSelected ([v] (AmpPart& q)
                                       {
                                           q.h = juce::jlimit (2, amp::faceH, v);

                                           if (q.type == "logo")
                                               q.logoSize = q.h;
                                       }); },
                                       true));

        struct Buttons final : public juce::Component
        {
            juce::TextButton duplicate { "Duplicate" }, remove { "Remove" };
            Buttons()
            {
                setVariant (remove, Variant::danger);
                addAndMakeVisible (duplicate);
                addAndMakeVisible (remove);
            }
            void resized() override
            {
                auto r = getLocalBounds().withTrimmedTop (6);
                duplicate.setBounds (r.removeFromLeft ((r.getWidth() - 6) / 2).withHeight (24));
                r.removeFromLeft (6);
                remove.setBounds (r.withHeight (24));
            }
        };
        auto buttons = std::make_unique<Buttons>();
        buttons->duplicate.setTooltip ("Duplicate (Ctrl+D)");
        buttons->remove.setTooltip ("Remove (Delete)");
        buttons->duplicate.onClick = [&o] { o.duplicateSelected(); };
        buttons->remove.onClick = [&o] { o.removeSelected(); };
        add ({}, std::move (buttons), 32);
    }

    //== Several parts ==========================================================
    void buildArrange()
    {
        auto& o = owner;
        addNote (juce::String (owner.selection.size()) + " parts selected", 0, 0, colour::textSoft);
        addChoice ({}, { "Align tops", "Align middles", "Align bottoms", "Space evenly", "Same size" }, -1, 2, [&o] (int i)
        {
            static const Arrange kinds[] { Arrange::tops, Arrange::middles, Arrange::bottoms, Arrange::spaceEvenly, Arrange::sameSize };
            o.arrange (kinds[i]);
        });
        addNote ("Shift-click parts to select several. Align middles lines them up on the first one you picked; Space evenly keeps the outer two in place.",
                 0, 0, colour::textMuted);
    }

    //== The amp ================================================================
    void buildAmp()
    {
        auto& o = owner;
        const auto& look = owner.amp.look;
        juce::StringArray names, keys;

        for (const auto& t : kit::ampTemplates::all())
        {
            names.add (t.name);
            keys.add (t.key);
        }

        juce::StringArray tips;

        for (int i = 0; i < names.size(); ++i)
            tips.add ("Start over from this template: its look and its parts");

        addChoice ("Template", names, keys.indexOf (owner.templateKey()), 2, [&o, keys] (int i) { o.applyTemplate (keys[i]); }, tips);

        static const juce::StringArray bodies { "tall", "thin", "top", "head", "headg" };
        // 2 columns (the board has 3): at this inspector's width "Grille head" fits whole only in 2.
        addChoice ("Body", { "Tall", "Thin", "Top panel", "Head", "Grille head" }, bodies.indexOf (look.body), 2,
                   [&o] (int i) { o.changeLook ([i] (AmpLook& l) { l.body = bodies[i]; }); });

        static const juce::StringArray tolex { "tweed", "black", "orange", "white", "cream", "brown", "red", "blonde" };
        static const juce::StringArray tolexNames { "Tweed", "Black", "Orange", "White", "Cream", "Brown", "Red", "Blonde" };
        std::vector<Swatches::Item> tolexItems;

        for (int i = 0; i < tolex.size(); ++i)
            tolexItems.push_back (imageSwatch (tolexNames[i], "body_tall_" + tolex[i] + "_none.webp", 0.02f, 0.6f));

        addSwatches ("Tolex", std::move (tolexItems), tolex.indexOf (look.tolex), [&o] (int i) { o.changeLook ([i] (AmpLook& l) { l.tolex = tolex[i]; }); });

        static const juce::StringArray piping { "gold", "white", "black", "none" };
        std::vector<Swatches::Item> pipingItems { colourSwatch ("Gold", amp::pipingGold), colourSwatch ("White", amp::pipingWhite),
                                                  colourSwatch ("Black", amp::pipingBlack),
                                                  { "None", [] (juce::Graphics& g, juce::Rectangle<float> r)
                                                    {
                                                        g.setColour (paint::c (amp::pipingNone));
                                                        g.fillRect (r);
                                                        g.setColour (paint::c (amp::pipingNoneSlash));
                                                        g.drawLine ({ r.getBottomLeft(), r.getTopRight() }, 2.5f);
                                                    } } };
        addSwatches ("Piping", std::move (pipingItems), piping.indexOf (look.piping), [&o] (int i) { o.changeLook ([i] (AmpLook& l) { l.piping = piping[i]; }); });

        static const juce::StringArray panels { "gold", "silver", "black", "plexi", "dark", "white", "orange" };
        static const juce::StringArray panelNames { "Brushed gold", "Brushed silver", "Black", "Plexi gold", "Dark steel", "White", "Orange" };
        std::vector<Swatches::Item> panelItems;

        for (int i = 0; i < panels.size(); ++i)
            panelItems.push_back (imageSwatch (panelNames[i], "panel_tall_" + panels[i] + ".webp"));

        addSwatches ("Panel", std::move (panelItems), panels.indexOf (look.panel), [&o] (int i) { o.changeLook ([i] (AmpLook& l) { l.panel = panels[i]; }); });

        if (kit::AmpFace::layoutFor (look.body).grille)
        {
            static const juce::StringArray grilles { "oxblood", "silver", "wheat", "salt", "black", "brass" };
            static const juce::StringArray grilleNames { "Oxblood weave", "Silver-blue weave", "Wheat weave", "Salt & pepper", "Black mesh", "Perforated brass" };
            std::vector<Swatches::Item> grilleItems;

            for (int i = 0; i < grilles.size(); ++i)
                grilleItems.push_back (imageSwatch (grilleNames[i], "grille_tall_" + grilles[i] + ".webp"));

            addSwatches ("Grille", std::move (grilleItems), grilles.indexOf (look.grille), [&o] (int i) { o.changeLook ([i] (AmpLook& l) { l.grille = grilles[i]; }); });
        }

        static const juce::StringArray corners { "chrome", "black", "none" };
        addSeg ("Corners", { "Chrome", "Black", "None" }, corners.indexOf (look.corners), [&o] (int i) { o.changeLook ([i] (AmpLook& l) { l.corners = corners[i]; }); });
        addSeg ("Studs", { "On", "Off" }, look.studs ? 0 : 1, [&o] (int i) { o.changeLook ([i] (AmpLook& l) { l.studs = i == 0; }); });
    }

    //== The sound ==============================================================
    void buildSound()
    {
        struct Divider final : public juce::Component
        {
            void paint (juce::Graphics& g) override
            {
                g.setColour (paint::c (colour::paneHeaderLine));
                g.fillRect (getLocalBounds().withHeight (1).withY (10).expanded (amp::inspectorPadX, 0));
                paint::text (g, paint::caps (type::groupTitle, "Sound"), getLocalBounds().toFloat().withTrimmedTop (18.0f), type::groupTitle, colour::textMuted);
            }
        };
        add ({}, std::make_unique<Divider>(), 34);

        auto name = std::make_unique<juce::TextEditor>();
        setVariant (*name, Variant::field);
        name->setReadOnly (true);
        name->setText (owner.amp.capture.isEmpty() ? juce::String ("(none)") : juce::File::createFileWithoutCheckingPath (owner.amp.capture).getFileName(), false);
        name->setTooltip (owner.amp.capture);
        add ("NAM capture", std::move (name));

        if (owner.captureInfo.isNotEmpty())
            addNote (owner.captureInfo, 0, 0, colour::textMuted);

        struct Load final : public juce::Component
        {
            juce::TextButton button { "Load capture" + ellipsis() };
            Load() { addAndMakeVisible (button); }
            void resized() override { button.setBounds (0, 1, button.getBestWidthForHeight (size::buttonHeight), size::buttonHeight); }
        };
        auto load = std::make_unique<Load>();
        load->button.setTooltip ("The NAM capture this amp plays (the amp block loads it)");
        load->button.onClick = [&o = owner] { o.loadCapture(); };
        add (" ", std::move (load));
        addNote ("The knobs' positions here are the amp's starting settings. Each patch that uses the amp keeps its own.", 0, 0, colour::textMuted);
    }

    void layoutRows()
    {
        const auto width = juce::jmax (100, getWidth() - 2 * amp::inspectorPadX - (viewport.isVerticalScrollBarShown() ? 8 : 0));
        auto y = amp::inspectorPadY;

        for (auto& row : rows)
        {
            const auto full = row.label.isEmpty();
            const auto editorW = full ? width : width - amp::rowLabelW - amp::rowGap;
            auto h = row.height;

            if (auto* note = dynamic_cast<Note*> (row.editor.get()))
                h = note->heightFor (editorW);
            else if (auto* swatches = dynamic_cast<Swatches*> (row.editor.get()))
                h = swatches->heightFor (editorW);

            row.editor->setBounds (amp::inspectorPadX + (full ? 0 : amp::rowLabelW + amp::rowGap), y + (full ? 0 : juce::jmax (0, (h - row.height) / 2)),
                                   editorW, h);
            y += h + 6;
        }

        content.setBounds (0, 0, juce::jmax (1, getWidth()), y + amp::inspectorPadY);
        content.labelRows.clear();

        for (auto& row : rows)
            if (row.label.isNotEmpty() && row.label != " ")
                content.labelRows.push_back ({ row.label, row.editor->getBounds().withX (amp::inspectorPadX).withWidth (amp::rowLabelW) });

        content.repaint();
    }

    struct Content final : public juce::Component
    {
        std::vector<std::pair<juce::String, juce::Rectangle<int>>> labelRows;

        void paint (juce::Graphics& g) override
        {
            for (const auto& [text, r] : labelRows)
                paint::text (g, text, r.toFloat().withHeight ((float) amp::rowMinH), type::label, colour::textTertiary);
        }
    };

    AmpBuilderView& owner;
    juce::Viewport viewport;
    Content content;
    std::vector<Row> rows;
};

//==============================================================================
AmpBuilderView::AmpBuilderView (ShowSession& s, AmpLibrary& l) : session (s), library (l)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setTitle ("Amp builder");

    ampPicker.setTooltip ("The amp you are building. Pick another amp of this show or the library.");
    ampPicker.onClick = [this] { showAmpMenu(); };
    addAndMakeVisible (ampPicker);

    savedChip.chip.setText (paint::caps (type::tagChip, "In this show" + middot() + "In library"));
    savedChip.setTooltip ("Saved in this show, with a copy in this computer's amp library");
    addAndMakeVisible (savedChip);

    undoButton.setTooltip ("Undo (Ctrl+Z)");
    undoButton.onClick = [this] { undo(); };
    saveButton.onClick = [this] { save(); };
    backButton.setTooltip ("Back to the patches (F8)");
    setButtonKeyCap (backButton, "F8");
    backButton.onClick = [this] { if (onClose != nullptr) onClose(); };

    for (auto* b : { &undoButton, &saveButton, &backButton })
    {
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    parts = std::make_unique<PartsList> (*this);
    face = std::make_unique<FaceCanvas> (*this);
    inspector = std::make_unique<Inspector> (*this);
    partsViewport.setViewedComponent (parts.get(), false);
    partsViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (partsViewport);
    addAndMakeVisible (*face);
    addAndMakeVisible (*inspector);

    edit (kit::ampTemplates::defaultFace());
}

AmpBuilderView::~AmpBuilderView()
{
    partsViewport.setViewedComponent (nullptr, false);
}

const juce::Component* AmpBuilderView::getDropBox() const noexcept { return face.get(); }
juce::Component* AmpBuilderView::getFace() const noexcept           { return face.get(); }
juce::Component* AmpBuilderView::getInspector() const noexcept      { return inspector.get(); }

void AmpBuilderView::edit (const AmpPreset& preset)
{
    amp = preset;
    selection.clear();
    undoStack.clear();
    redoStack.clear();
    dirty = false;
    nextId = 1;
    refreshAll();
}

void AmpBuilderView::setCaptureInfo (const juce::String& formatLine)
{
    captureInfo = formatLine;
    inspector->rebuild();
}

juce::String AmpBuilderView::newPartId()
{
    for (;; ++nextId)
        if (amp.findPart ("n" + juce::String (nextId)) == nullptr)
            return "n" + juce::String (nextId++);
}

juce::String AmpBuilderView::templateKey() const
{
    // The template whose look and parts the amp still has (the board shows it chosen).
    for (const auto& t : kit::ampTemplates::all())
        if (t.amp.look == amp.look && t.amp.parts == amp.parts)
            return t.key;

    for (const auto& t : kit::ampTemplates::all())
        if (t.amp.look == amp.look)
            return t.key;

    return {};
}

//==============================================================================
void AmpBuilderView::changed (AmpPreset before)
{
    if (before == amp)
        return;

    undoStack.push_back (std::move (before));

    if ((int) undoStack.size() > amp::undoDepth)
        undoStack.erase (undoStack.begin());

    redoStack.clear();
    dirty = true;
    refreshAll();
}

void AmpBuilderView::refreshAll()
{
    // The selection keeps only parts that still exist.
    for (int i = selection.size(); --i >= 0;)
        if (amp.findPart (selection[i]) == nullptr)
            selection.remove (i);

    face->refreshFace();
    inspector->rebuild();
    refreshToolbar();
    repaint();
}

void AmpBuilderView::refreshToolbar()
{
    ampPicker.setText (amp.name, {});

    juce::String why;
    const auto ok = amp.canSave (&why);
    saveButton.setEnabled (ok);
    saveButton.setTooltip (ok ? juce::String ("Save the amp in this show and update the copy in the library") : why);
    undoButton.setEnabled (canUndo());

    const auto inShow = session.getAmp (amp.id) != nullptr;
    const auto inLibrary = library.fileFor (amp.id).existsAsFile();
    savedChip.setVisible (inShow || inLibrary);
    savedChip.chip.setText (paint::caps (type::tagChip, inShow && inLibrary ? "In this show" + middot() + "In library"
                                                      : inShow ? juce::String ("In this show") : juce::String ("In library")));
    resized();
}

juce::String AmpBuilderView::addPart (const juce::String& type, const juce::String& style, juce::Point<int> centre)
{
    const auto before = amp;
    const auto& lay = kit::AmpFace::layoutFor (amp.look.body);
    AmpPart p;
    p.id = newPartId();
    p.type = type;

    if (type == "knob")
    {
        p.style = style.isNotEmpty() ? style : juce::String ("chicken");
        p.size = amp.look.body == "tall" ? "L" : "S";
        p.drives = {};   // NOT SET until it drives something, or is Fixed
        p.value = 0.5f;
        p.label = "KNOB";
        p.labelPos = amp.look.body == "thin" || amp.look.body == "top" ? "above" : "below";
        p.scale = amp.look.body != "thin" ? "ticks" : "none";
    }
    else
    {
        const auto s = newSize (type);
        p.w = s.x;
        p.h = s.y;
    }

    if (type == "text")    { p.text = "LABEL"; p.font = "caps"; }
    if (type == "plate")   { p.text = "MODEL"; p.mat = "gold"; }
    if (type == "title")   p.text = "SECTION";
    if (type == "section") p.mat = "gold";
    if (type == "trim")    p.mat = "silver";
    if (type == "logo")    { p.logoSize = 26; p.colour = "cream"; }
    if (type == "symbol")  p.icon = "volume";
    if (type == "toggle")
    {
        p.label = "MATCH";
        p.drives = "levelMatch";
        p.value = 1.0f;
        p.switchStyle = style == "push" ? "push" : "bat";

        if (style == "push")
            p.w = p.h = 18;
    }
    if (type == "jewel")
    {
        p.colour = "red";
        p.style = style == "led" ? "led" : "jewel";
        p.lights = "whileOn";

        if (style == "led")
            p.w = p.h = 8;
    }

    const auto l = landingFor (p, centre - juce::Point<int> (p.width() / 2, p.height() / 2));
    p.x = type == "trim" ? lay.px : l.topLeft.x;
    p.y = l.topLeft.y;
    amp.parts.push_back (p);
    selection = { p.id };
    changed (before);
    return p.id;
}

AmpBuilderView::Landing AmpBuilderView::landingFor (const AmpPart& part, juce::Point<int> wanted) const
{
    // The board's pos() and smart(): the 2 px grid inside the face, then guides.
    const auto w = part.width(), h = part.height();
    auto x = juce::jlimit (0, juce::jmax (0, amp::faceW - w), snapTo (wanted.x));
    auto y = juce::jlimit (24, juce::jmax (24, amp::faceH - h), snapTo (wanted.y));
    Landing result;

    struct Box { int x, y, w, h; };
    std::vector<Box> others;

    for (const auto& q : amp.parts)
        if (q.id != part.id && q.type != "trim" && q.type != "section")
            others.push_back ({ q.x, q.y, q.width(), q.height() });

    const auto reach = (float) amp::guideReach;
    auto cy = (float) y + (float) h * 0.5f, cx = (float) x + (float) w * 0.5f;
    std::optional<std::pair<float, float>> bestY, bestX;   // (distance, centre)

    for (const auto& q : others)
    {
        const auto qcy = (float) q.y + (float) q.h * 0.5f, qcx = (float) q.x + (float) q.w * 0.5f;
        const auto dy = std::abs (qcy - cy), dx = std::abs (qcx - cx);

        if (dy <= reach && (! bestY || dy < bestY->first)) bestY = std::make_pair (dy, qcy);
        if (dx <= reach && (! bestX || dx < bestX->first)) bestX = std::make_pair (dx, qcx);
    }

    if (bestY) { y = (int) std::round (bestY->second - (float) h * 0.5f); cy = bestY->second; }
    if (bestX) { x = (int) std::round (bestX->second - (float) w * 0.5f); cx = bestX->second; }

    if (bestY)
    {
        // The row it lines up with: repeat the gap of the two parts beside it.
        std::vector<Box> row;

        for (const auto& q : others)
            if (std::abs ((float) q.y + (float) q.h * 0.5f - cy) < 1.5f)
                row.push_back (q);

        std::sort (row.begin(), row.end(), [] (const Box& a, const Box& b) { return a.x < b.x; });
        std::vector<Box> left, right;

        for (const auto& q : row)
        {
            if (q.x + q.w <= x + 1) left.push_back (q);
            if (q.x >= x + w - 1)   right.push_back (q);
        }

        const auto gapY = (int) std::round (cy - (float) h * 0.5f - 6.0f);

        if (left.size() >= 2)
        {
            const auto& l1 = left[left.size() - 1];
            const auto& l2 = left[left.size() - 2];
            const auto gap = l1.x - (l2.x + l2.w), current = x - (l1.x + l1.w);

            if (std::abs ((float) (current - gap)) <= reach)
            {
                x = l1.x + l1.w + gap;
                result.gaps.push_back ({ { l2.x + l2.w, gapY, juce::jmax (1, l1.x - (l2.x + l2.w)), 1 }, gap });
                result.gaps.push_back ({ { l1.x + l1.w, gapY, juce::jmax (1, x - (l1.x + l1.w)), 1 }, gap });
            }
        }

        if (result.gaps.empty() && right.size() >= 2)
        {
            const auto& r1 = right[0];
            const auto& r2 = right[1];
            const auto gap = r2.x - (r1.x + r1.w), current = r1.x - (x + w);

            if (std::abs ((float) (current - gap)) <= reach)
            {
                x = r1.x - gap - w;
                result.gaps.push_back ({ { x + w, gapY, juce::jmax (1, r1.x - (x + w)), 1 }, gap });
                result.gaps.push_back ({ { r1.x + r1.w, gapY, juce::jmax (1, r2.x - (r1.x + r1.w)), 1 }, gap });
            }
        }

        auto minX = x, maxX = x + w;

        for (const auto& q : row)
        {
            minX = juce::jmin (minX, q.x);
            maxX = juce::jmax (maxX, q.x + q.w);
        }

        result.guides.push_back ({ { minX - 6, (int) std::round (cy), maxX - minX + 12, 1 } });
    }

    if (bestX)
        result.guides.push_back ({ { (int) std::round (cx), 28, 1, 398 } });

    result.topLeft = { x, y };
    return result;
}

void AmpBuilderView::movePart (const juce::String& partId, juce::Point<int> topLeft)
{
    const auto before = amp;

    for (auto& p : amp.parts)
    {
        if (p.id == partId)
        {
            const auto l = landingFor (p, topLeft);
            p.x = l.topLeft.x;
            p.y = l.topLeft.y;
        }
    }

    selection = { partId };
    changed (before);
}

void AmpBuilderView::select (const juce::String& partId, bool addOrToggle)
{
    if (partId.isEmpty())
        selection.clear();
    else if (addOrToggle)
    {
        if (selection.contains (partId))
            selection.removeString (partId);
        else
            selection.add (partId);
    }
    else
        selection = { partId };

    face->repaint();
    inspector->rebuild();
}

void AmpBuilderView::arrange (Arrange kind)
{
    if (selection.size() < 2)
        return;

    const auto before = amp;
    struct Box { AmpPart* p; int x, y, w, h; };
    std::vector<Box> boxes;

    for (const auto& id : selection)
        for (auto& p : amp.parts)
            if (p.id == id)
                boxes.push_back ({ &p, p.x, p.y, p.width(), p.height() });

    if (boxes.size() < 2)
        return;

    const auto& first = boxes.front();   // the first picked
    auto top = boxes[0].y, bottom = boxes[0].y + boxes[0].h;

    for (const auto& b : boxes)
    {
        top = juce::jmin (top, b.y);
        bottom = juce::jmax (bottom, b.y + b.h);
    }

    switch (kind)
    {
        case Arrange::tops:    for (auto& b : boxes) b.p->y = top; break;
        case Arrange::bottoms: for (auto& b : boxes) b.p->y = bottom - b.h; break;
        case Arrange::middles:
        {
            const auto middle = (float) first.y + (float) first.h * 0.5f;

            for (auto& b : boxes)
                b.p->y = (int) std::round (middle - (float) b.h * 0.5f);

            break;
        }
        case Arrange::spaceEvenly:
        {
            std::sort (boxes.begin(), boxes.end(), [] (const Box& a, const Box& b) { return a.x < b.x; });
            const auto span = boxes.back().x + boxes.back().w - boxes.front().x;
            auto total = 0;

            for (const auto& b : boxes)
                total += b.w;

            const auto gap = (float) (span - total) / (float) (boxes.size() - 1);
            auto x = (float) boxes.front().x;

            for (auto& b : boxes)
            {
                b.p->x = (int) std::round (x);
                x += (float) b.w + gap;
            }

            break;
        }
        case Arrange::sameSize:
        {
            const auto* f = first.p;

            for (auto& b : boxes)
            {
                if (b.p == f || b.p->type != f->type)
                    continue;

                if (f->isKnob())
                    b.p->size = f->size;
                else
                {
                    b.p->w = f->w;
                    b.p->h = f->h;
                }
            }

            break;
        }
    }

    changed (before);
}

void AmpBuilderView::changeSelected (const std::function<void (AmpPart&)>& edit)
{
    const auto before = amp;

    for (auto& p : amp.parts)
        if (selection.contains (p.id))
            edit (p);

    changed (before);
}

void AmpBuilderView::changeLook (const std::function<void (AmpLook&)>& edit)
{
    const auto before = amp;
    edit (amp.look);
    changed (before);
}

void AmpBuilderView::applyTemplate (const juce::String& key)
{
    // Asks nothing: the look and the parts are replaced (Undo brings them back).
    if (const auto* t = kit::ampTemplates::find (key))
    {
        const auto before = amp;
        amp.look = t->amp.look;
        amp.parts = t->amp.parts;
        selection.clear();
        changed (before);
    }
}

void AmpBuilderView::nudge (int dx, int dy)
{
    if (selection.isEmpty())
        return;

    changeSelected ([dx, dy] (AmpPart& p)
    {
        p.x = juce::jlimit (0, juce::jmax (0, amp::faceW - p.width()), p.x + dx);
        p.y = juce::jlimit (0, juce::jmax (0, amp::faceH - p.height()), p.y + dy);
    });
}

void AmpBuilderView::removeSelected()
{
    if (selection.isEmpty())
        return;

    const auto before = amp;
    amp.parts.erase (std::remove_if (amp.parts.begin(), amp.parts.end(), [this] (const AmpPart& p) { return selection.contains (p.id); }),
                     amp.parts.end());
    selection.clear();
    changed (before);
}

void AmpBuilderView::duplicateSelected()
{
    if (selection.isEmpty())
        return;

    const auto before = amp;
    juce::StringArray copies;

    for (const auto& id : juce::StringArray (selection))
    {
        if (const auto* p = amp.findPart (id))
        {
            auto copy = *p;
            copy.id = newPartId();
            copy.x = juce::jlimit (0, juce::jmax (0, amp::faceW - copy.width()), copy.x + 10);
            copy.y = juce::jlimit (0, juce::jmax (0, amp::faceH - copy.height()), copy.y + 10);
            amp.parts.push_back (copy);
            copies.add (copy.id);
        }
    }

    selection = copies;
    changed (before);
}

void AmpBuilderView::selectNext (bool backwards)
{
    if (amp.parts.empty())
        return;

    auto index = -1;

    if (selection.size() == 1)
        for (int i = 0; i < (int) amp.parts.size(); ++i)
            if (amp.parts[(size_t) i].id == selection[0])
                index = i;

    const auto n = (int) amp.parts.size();
    index = index < 0 ? (backwards ? n - 1 : 0) : (index + (backwards ? n - 1 : 1)) % n;
    select (amp.parts[(size_t) index].id);
}

void AmpBuilderView::undo()
{
    if (undoStack.empty())
        return;

    redoStack.push_back (amp);
    amp = undoStack.back();
    undoStack.pop_back();
    dirty = true;
    refreshAll();
}

void AmpBuilderView::redo()
{
    if (redoStack.empty())
        return;

    undoStack.push_back (amp);
    amp = redoStack.back();
    redoStack.pop_back();
    dirty = true;
    refreshAll();
}

bool AmpBuilderView::save()
{
    if (! amp.canSave())
        return false;   // the button says why

    session.saveAmp (amp);
    const auto copied = library.save (amp);
    juce::ignoreUnused (copied);   // the show has it either way; the chip says whether the library does
    dirty = false;
    refreshToolbar();

    if (onSaved != nullptr)
        onSaved (amp);

    return true;
}

juce::String AmpBuilderView::countText() const
{
    auto turnable = 0, fixed = 0;

    for (const auto& p : amp.parts)
    {
        if (p.isKnob() && p.drivesParameter())
            ++turnable;

        if (p.isFixed())
            ++fixed;
    }

    const auto key = templateKey();
    const auto* t = kit::ampTemplates::find (key);
    return juce::String ((int) amp.parts.size()) + " parts" + middot() + juce::String (turnable) + " turnable knobs" + middot()
         + juce::String (fixed) + " fixed" + (t != nullptr ? middot() + "template: " + t->name : juce::String());
}

std::vector<AmpPreset> AmpBuilderView::libraryChoices() const
{
    std::vector<AmpPreset> choices;

    for (const auto& a : library.loadAll())
        if (session.getAmp (a.id) == nullptr)
            choices.push_back (a);

    // The examples ship built in, so a first start has amps to pick.
    for (const auto& e : kit::ampTemplates::examples())
        if (session.getAmp (e.amp.id) == nullptr && ! library.fileFor (e.amp.id).existsAsFile())
            choices.push_back (e.amp);

    return choices;
}

juce::String AmpBuilderView::usedByText() const
{
    juce::StringArray names;

    for (const auto& entry : session.getAllPatches())
        if (entry.patch != nullptr)
            for (const auto& lane : entry.patch->lanes)
                for (const auto& b : lane.blocks)
                    if (b.ampPreset == amp.id)
                        names.addIfNotAlreadyThere (entry.patch->name);

    return names.isEmpty() ? juce::String ("Not used by a patch yet") : "Used by " + names.joinIntoString (", ");
}

void AmpBuilderView::dragNewPart (const juce::String& type, const juce::String& style, juce::Point<int> facePoint, bool released)
{
    if (! face->getLocalBounds().contains (facePoint))
    {
        face->hideDrop();   // off the face: let go there and nothing happens
        return;
    }

    AmpPart probe;
    probe.type = type;
    probe.style = style;
    probe.size = amp.look.body == "tall" ? "L" : "S";

    if (type != "knob")
    {
        const auto s = type == "toggle" && style == "push" ? juce::Point<int> (18, 18)
                     : type == "jewel" && style == "led"   ? juce::Point<int> (8, 8) : newSize (type);
        probe.w = s.x;
        probe.h = s.y;
    }

    if (! released)
    {
        const auto l = landingFor (probe, facePoint - juce::Point<int> (probe.width() / 2, probe.height() / 2));
        face->showDrop ({ type == "trim" ? kit::AmpFace::layoutFor (amp.look.body).px : l.topLeft.x, l.topLeft.y, probe.width(), probe.height() }, l);
        return;
    }

    face->hideDrop();
    addPart (type, style, facePoint);
}

void AmpBuilderView::showAmpMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Amps in this show");
    juce::Component::SafePointer<AmpBuilderView> safe (this);

    for (const auto& a : session.getAmps())
        menu.addItem (a.name, true, a.id == amp.id, [safe, a] { if (safe != nullptr) safe->edit (a); });

    if (session.getAmps().empty())
        menu.addItem ("(none yet)", false, false, nullptr);

    menu.addSeparator();
    juce::PopupMenu templates;

    for (const auto& t : kit::ampTemplates::all())
        templates.addItem (t.name, [safe, key = t.key, name = t.name]
        {
            if (safe != nullptr)
                safe->edit (kit::ampTemplates::makeFrom (key, name));
        });

    menu.addSubMenu (juce::String (juce::CharPointer_UTF8 ("New amp from a template\xe2\x80\xa6")), templates);

    juce::PopupMenu fromLibrary;

    for (const auto& a : libraryChoices())
        fromLibrary.addItem (a.name, [safe, a] { if (safe != nullptr) safe->edit (a); });

    if (fromLibrary.getNumItems() == 0)
        fromLibrary.addItem ("(every amp is in this show)", false, false, nullptr);

    menu.addSubMenu (juce::String (juce::CharPointer_UTF8 ("Add from the library\xe2\x80\xa6")), fromLibrary);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&ampPicker).withMinimumWidth (260));
}

void AmpBuilderView::loadCapture()
{
    if (onLoadCapture == nullptr)
        return;

    juce::Component::SafePointer<AmpBuilderView> safe (this);
    onLoadCapture ([safe] (const juce::File& file, const juce::String& formatLine)
    {
        if (safe == nullptr || file == juce::File())
            return;

        const auto before = safe->amp;
        safe->amp.capture = file.getFullPathName();
        safe->captureInfo = formatLine;
        safe->changed (before);
    });
}

//==============================================================================
bool AmpBuilderView::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();
    const auto step = mods.isShiftDown() ? 10 : amp::snap;

    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { undo(); return true; }
    if (key == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0)
        || key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { redo(); return true; }
    if (key == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0)) { duplicateSelected(); return true; }
    if (code == juce::KeyPress::tabKey) { selectNext (mods.isShiftDown()); return true; }
    if (code == juce::KeyPress::escapeKey && selection.size() > 0) { select ({}); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { removeSelected(); return true; }
    if (code == juce::KeyPress::leftKey)  { nudge (-step, 0); return true; }
    if (code == juce::KeyPress::rightKey) { nudge (step, 0); return true; }
    if (code == juce::KeyPress::upKey)    { nudge (0, -step); return true; }
    if (code == juce::KeyPress::downKey)  { nudge (0, step); return true; }

    return false;
}

void AmpBuilderView::paint (juce::Graphics& g)
{
    g.fillAll (paint::c (colour::window));
    auto area = getLocalBounds().reduced (size::outerPadding).toFloat();
    paint::pane (g, area);

    // The sub-toolbar (40): the title with its icon.
    g.setGradientFill (juce::ColourGradient::vertical (paint::c (colour::subToolbarTop), (float) toolbarArea.getY(),
                                                       paint::c (colour::subToolbarBot), (float) toolbarArea.getBottom()));
    g.fillRect (toolbarArea.toFloat().reduced (1.0f, 0.0f).withTrimmedTop (1.0f));
    g.setColour (paint::c (colour::paneBorder));
    g.fillRect (toolbarArea.getX(), toolbarArea.getBottom() - 1, toolbarArea.getWidth(), 1);
    drawIcon (g, Icon::amp, titleArea.removeFromLeft (15).toFloat().withSizeKeepingCentre (15.0f, 15.0f), paint::c (colour::text));
    paint::text (g, "Amp builder", titleArea.toFloat().withTrimmedLeft (8.0f), type::subToolbarTitle, colour::text);
    paint::text (g, usedByText(), usedByArea.toFloat(), type::bodySmall, colour::textTertiary, juce::Justification::centredRight);

    // The three columns, their headers and the face's work area.
    const auto body = getLocalBounds().reduced (size::outerPadding).withTrimmedTop (size::subToolbar).reduced (1, 0).withTrimmedBottom (1);
    auto columns = body;
    const auto partsCol = columns.removeFromLeft (amp::partsW);
    const auto inspectorCol = columns.removeFromRight (amp::inspectorW);
    const auto faceCol = columns;

    const auto header = [&g] (juce::Rectangle<int> col, const juce::String& left, const juce::String& right)
    {
        const auto r = col.withHeight (size::paneHeader).toFloat();
        paint::paneHeader (g, r, 0.0f);
        paint::text (g, paint::caps (type::paneHeader, left), r.withTrimmedLeft ((float) size::paneHeaderPadL), type::paneHeader, colour::textMuted);
        paint::text (g, right, r.withTrimmedRight ((float) size::paneHeaderPadR + 2.0f), type::paneHeaderNote, colour::textMuted, juce::Justification::centredRight);
    };

    header (partsCol, "Parts", "drag onto the amp");
    header (faceCol, "Face" + middot() + "600 " + juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) + " 430",
            "Drag to move" + middot() + "lines up with other parts" + middot() + "shift-click to select several");
    header (inspectorCol, inspector->title, inspector->hint);

    g.setColour (paint::c (colour::paneBorder));
    g.fillRect (partsCol.getRight(), partsCol.getY(), 1, partsCol.getHeight());
    g.fillRect (inspectorCol.getX() - 1, inspectorCol.getY(), 1, inspectorCol.getHeight());

    // The parts' note at the bottom of their column.
    const auto note = partsCol.withTrimmedTop (partsCol.getHeight() - 64);
    g.setColour (paint::c (colour::paneHeaderLine));
    g.fillRect (note.withHeight (1));
    g.setColour (paint::c (colour::textMuted));
    g.setFont (fontOfSize (type::Weight::regular, 11.0f));
    g.drawFittedText ("A knob either drives Input, Bass, Mid, Treble or Output, or is fixed at the setting the capture was made at. Fixed knobs can't be turned.",
                      note.reduced (10, 8), juce::Justification::topLeft, 4, 1.0f);

    // The work area: dots every 16 px, the face's shadow.
    const auto work = faceCol.withTrimmedTop (size::paneHeader).withTrimmedBottom (amp::footerH);
    g.setColour (paint::c (amp::workArea));
    g.fillRect (work);
    g.setColour (paint::c (amp::workDots));

    for (int y = work.getY() + amp::dotPitch / 2; y < work.getBottom(); y += amp::dotPitch)
        for (int x = work.getX() + amp::dotPitch / 2; x < work.getRight(); x += amp::dotPitch)
            g.fillRect (x, y, 1, 1);

    const auto faceBox = face->getBoundsInParent().toFloat();
    juce::DropShadow (paint::c (amp::faceShadow), amp::faceShadowBlur, { 0, amp::faceShadowY }).drawForRectangle (g, faceBox.toNearestInt().reduced (20, 30));

    // The footer.
    const auto footer = faceCol.withTrimmedTop (faceCol.getHeight() - amp::footerH);
    g.setColour (paint::c (colour::stageFooter));
    g.fillRect (footer);
    g.setColour (paint::c (colour::paneBorder));
    g.fillRect (footer.withHeight (1));
    paint::text (g, countText(), footer.toFloat().reduced (12.0f, 0.0f), type::caption, colour::textMuted);
    paint::text (g, "Rendered to images when saved, so it costs nothing on stage", footer.toFloat().reduced (12.0f, 0.0f), type::caption,
                 colour::textMuted, juce::Justification::centredRight);
}

void AmpBuilderView::resized()
{
    const auto area = getLocalBounds().reduced (size::outerPadding);
    toolbarArea = area.withHeight (size::subToolbar);

    auto bar = toolbarArea.reduced (12, 0).withTrimmedRight (-4);
    titleArea = bar.removeFromLeft (120);
    bar.removeFromLeft (8);
    ampPicker.setBounds (bar.removeFromLeft (amp::ampPickerW).withSizeKeepingCentre (amp::ampPickerW, 24));
    bar.removeFromLeft (10);
    savedChip.setBounds (bar.removeFromLeft (savedChip.chip.getIdealWidth()).withSizeKeepingCentre (savedChip.chip.getIdealWidth(), 18));

    const auto buttonY = (size::subToolbar - size::buttonHeight) / 2;
    auto right = bar.getRight();
    const auto place = [&] (juce::Button& b, int w)
    {
        right -= w;
        b.setBounds (right, toolbarArea.getY() + buttonY, w, size::buttonHeight);
        right -= 6;
    };

    place (backButton, amp::backButtonW);
    place (saveButton, saveButton.getBestWidthForHeight (size::buttonHeight));
    place (undoButton, undoButton.getBestWidthForHeight (size::buttonHeight));
    usedByArea = { bar.getX(), toolbarArea.getY(), juce::jmax (0, right - 6 - bar.getX()), toolbarArea.getHeight() };

    const auto body = area.withTrimmedTop (size::subToolbar).reduced (1, 0).withTrimmedBottom (1);
    auto columns = body;
    auto partsCol = columns.removeFromLeft (amp::partsW);
    auto inspectorCol = columns.removeFromRight (amp::inspectorW);
    const auto faceCol = columns;

    partsViewport.setBounds (partsCol.withTrimmedTop (size::paneHeader).withTrimmedBottom (64).withTrimmedRight (1));
    parts->setSize (partsViewport.getWidth() - (partsViewport.getHeight() < parts->idealHeight() ? partsViewport.getScrollBarThickness() : 0),
                    parts->idealHeight());
    inspector->setBounds (inspectorCol.withTrimmedTop (size::paneHeader));

    // The face at 1:1, centred; smaller when the window is (never bigger).
    const auto work = faceCol.withTrimmedTop (size::paneHeader).withTrimmedBottom (amp::footerH).reduced (12);
    const auto scale = juce::jmax (0.1f, juce::jmin (1.0f, (float) work.getWidth() / (float) amp::faceW, (float) work.getHeight() / (float) amp::faceH));
    face->setTransform (juce::AffineTransform::scale (scale));
    const auto centre = work.getCentre().toFloat() / scale;
    face->setBounds (juce::roundToInt (centre.x - (float) amp::faceW * 0.5f), juce::roundToInt (centre.y - (float) amp::faceH * 0.5f), amp::faceW, amp::faceH);
}

} // namespace encore
