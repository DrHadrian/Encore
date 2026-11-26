// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/theme/ShapedShadow.h"

#include <cmath>
#include <optional>
#include <vector>

namespace encore::theme
{

namespace
{
    const juce::Identifier shadowId ("encoreShapedShadow");

    /** Keeps the shadow alive as long as its owner (it sits in the owner's properties). */
    struct Holder final : juce::ReferenceCountedObject
    {
        std::unique_ptr<ShapedShadow> shadow;
    };

    Holder* findHolder (const juce::Component& owner)
    {
        return dynamic_cast<Holder*> (owner.getProperties()[shadowId].getObject());
    }
}

//==============================================================================
/** The window (or component) the shadow is painted in. Never takes a click. */
struct ShapedShadow::Layer final : juce::Component
{
    Layer (juce::Component& o, const Look& l) : owner (&o), look (l)
    {
        setOpaque (false);
        setInterceptsMouseClicks (false, false);
        setWantsKeyboardFocus (false);
        setAccessible (false);
    }

    // On the owner's screen scale (as JUCE's own shadow windows).
    float getDesktopScaleFactor() const override
    {
        return owner != nullptr ? owner->getDesktopScaleFactor() : juce::Component::getDesktopScaleFactor();
    }

    void paint (juce::Graphics& g) override
    {
        ++paints;
        lastPaintClip = g.getClipBounds();

        if (owner == nullptr)
            return;

        const auto scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
        const juce::Point<int> ownerSize { owner->getWidth(), owner->getHeight() };

        if (image.isNull() || ownerSize != imageOwnerSize || ! juce::approximatelyEqual (scale, imageScale))
        {
            // As a native image: a software image drawn into a semi-transparent
            // window (Direct2D) never reached the screen (CI, 26 Nov: a plain fill
            // did, the same fill as a software image didn't).
            image = juce::NativeImageType().convert (render (ownerSize, look, scale));
            imageOwnerSize = ownerSize;
            imageScale = scale;
        }

        g.drawImage (image, getLocalBounds().toFloat());
    }

    static inline int paints = 0;
    static inline juce::Rectangle<int> lastPaintClip;

    juce::Component::SafePointer<juce::Component> owner;
    Look look;
    juce::Image image;
    juce::Point<int> imageOwnerSize;
    float imageScale = 0.0f;
};

//==============================================================================
void ShapedShadow::attach (juce::Component& owner, const Look& look, std::function<void()> onOwnerDeleted)
{
    if (findHolder (owner) != nullptr)
        return;

    auto* holder = new Holder();
    holder->shadow.reset (new ShapedShadow (owner, look, std::move (onOwnerDeleted)));
    owner.getProperties().set (shadowId, juce::var (holder));
    holder->shadow->update();
}

juce::Component* ShapedShadow::findLayer (const juce::Component& owner)
{
    if (auto* holder = findHolder (owner); holder != nullptr && holder->shadow != nullptr)
        return holder->shadow->layer.get();

    return nullptr;
}

juce::BorderSize<int> ShapedShadow::marginsFor (const Look& look)
{
    // A CSS blur of B is a Gaussian of standard deviation B / 2; past 3 of those
    // (1.5 B) nothing shows.
    const auto reach = (int) std::ceil (1.5 * look.blur) + 1;
    return { juce::jmax (0, reach - look.offset.y), juce::jmax (0, reach - look.offset.x),
             juce::jmax (0, reach + look.offset.y), juce::jmax (0, reach + look.offset.x) };
}

namespace
{
    /** One box blur of `radius` along rows (step 1) or columns (step = width);
        outside the image counts as clear. */
    void boxBlur (const std::vector<float>& in, std::vector<float>& out, int lines, int length, int lineStep, int step, int radius)
    {
        const auto window = (float) (2 * radius + 1);

        for (int line = 0; line < lines; ++line)
        {
            const auto base = (size_t) (line * lineStep);
            const auto get = [&] (int i) { return i >= 0 && i < length ? in[base + (size_t) (i * step)] : 0.0f; };
            auto sum = 0.0f;

            for (int i = -radius; i <= radius; ++i)
                sum += get (i);

            for (int i = 0; i < length; ++i)
            {
                out[base + (size_t) (i * step)] = sum / window;
                sum += get (i + radius + 1) - get (i - radius);
            }
        }
    }

    /** Three box blurs that together make a Gaussian of `sigma` (pixels). */
    void gaussianBlur (std::vector<float>& pixels, int width, int height, double sigma)
    {
        if (sigma <= 0.0)
            return;

        constexpr int passes = 3;
        const auto ideal = std::sqrt (12.0 * sigma * sigma / passes + 1.0);
        auto lower = (int) std::floor (ideal);

        if (lower % 2 == 0)
            --lower;

        const auto upper = lower + 2;
        const auto lowerCount = (int) std::round ((12.0 * sigma * sigma - passes * lower * lower - 4.0 * passes * lower - 3.0 * passes)
                                                  / (-4.0 * lower - 4.0));
        std::vector<float> scratch (pixels.size());

        for (int pass = 0; pass < passes; ++pass)
        {
            const auto radius = ((pass < lowerCount ? lower : upper) - 1) / 2;
            boxBlur (pixels, scratch, height, width, width, 1, radius);   // rows
            boxBlur (scratch, pixels, width, height, 1, width, radius);   // columns
        }
    }

    /** Coverage (0..1) of `shape` per pixel, row by row. */
    std::vector<float> coverage (const juce::Path& shape, int width, int height)
    {
        juce::Image image (juce::Image::SingleChannel, width, height, true, juce::SoftwareImageType());
        {
            juce::Graphics g (image);
            g.setColour (juce::Colours::white);
            g.fillPath (shape);
        }

        std::vector<float> values ((size_t) width * (size_t) height);
        const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);

        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                values[(size_t) (y * width + x)] = (float) *data.getPixelPointer (x, y) / 255.0f;

        return values;
    }
}

juce::Image ShapedShadow::render (juce::Point<int> ownerSize, const Look& look, float scale)
{
    const auto margins = marginsFor (look);
    const auto w = ownerSize.x + margins.getLeftAndRight();
    const auto h = ownerSize.y + margins.getTopAndBottom();
    const auto pw = juce::jmax (1, juce::roundToInt ((float) w * scale));
    const auto ph = juce::jmax (1, juce::roundToInt ((float) h * scale));

    // The owner's shape, in the image's physical pixels.
    const auto ownerArea = juce::Rectangle<float> ((float) margins.getLeft(), (float) margins.getTop(),
                                                   (float) ownerSize.x, (float) ownerSize.y) * scale;
    juce::Path shape, shadow;
    shape.addRoundedRectangle (ownerArea, look.cornerRadius * scale);
    shadow.addRoundedRectangle (ownerArea + look.offset.toFloat() * scale, look.cornerRadius * scale);

    // As CSS draws box-shadow: the shape moved by the offset, blurred by a Gaussian
    // of half the blur, then left out inside the owner (the owner shows what is
    // behind it, not its own shadow).
    auto amount = coverage (shadow, pw, ph);
    gaussianBlur (amount, pw, ph, 0.5 * look.blur * scale);
    const auto inside = coverage (shape, pw, ph);

    juce::Image image (juce::Image::ARGB, pw, ph, true, juce::SoftwareImageType());
    const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::writeOnly);
    const auto baseAlpha = look.colour.getFloatAlpha();

    for (int y = 0; y < ph; ++y)
    {
        for (int x = 0; x < pw; ++x)
        {
            const auto i = (size_t) (y * pw + x);
            const auto alpha = juce::jlimit (0.0f, 1.0f, baseAlpha * amount[i] * (1.0f - inside[i]));
            auto* p = reinterpret_cast<juce::PixelARGB*> (pixels.getPixelPointer (x, y));
            p->setARGB ((juce::uint8) juce::roundToInt (alpha * 255.0f),
                        (juce::uint8) juce::roundToInt (look.colour.getRed() * alpha),
                        (juce::uint8) juce::roundToInt (look.colour.getGreen() * alpha),
                        (juce::uint8) juce::roundToInt (look.colour.getBlue() * alpha));   // premultiplied
        }
    }

    return image;
}

//==============================================================================
ShapedShadow::ShapedShadow (juce::Component& o, const Look& l, std::function<void()> deleted)
    : owner (&o), look (l), onOwnerDeleted (std::move (deleted))
{
    o.addComponentListener (this);
}

ShapedShadow::~ShapedShadow()
{
    if (owner != nullptr)
        owner->removeComponentListener (this);

    layer = nullptr;
}

void ShapedShadow::update()
{
    if (updating)
        return;

    const juce::ScopedValueSetter<bool> guard (updating, true);
    auto* o = owner.getComponent();

    const auto canShow = o != nullptr && o->isShowing() && o->getWidth() > 0 && o->getHeight() > 0
                      && (juce::Desktop::canUseSemiTransparentWindows() || o->getParentComponent() != nullptr);

    // Gone while the owner is hidden, made again when it shows (as JUCE's own
    // shadow windows): a semi-transparent window that is shown after it was made
    // hidden may never be painted.
    if (! canShow)
    {
        layer = nullptr;
        return;
    }

    // Made, or made again after the owner moved between the desktop and a parent.
    const auto onDesktop = o->isOnDesktop();

    if (layer != nullptr && (layer->isOnDesktop() != onDesktop
                             || (! onDesktop && layer->getParentComponent() != o->getParentComponent())))
        layer = nullptr;

    if (layer == nullptr)
    {
        // Made hidden at its size and shown once placed (as a menu window is): a
        // layered window paints in full when it is shown; made visible at 1 x 1 and
        // then resized, it was never painted on screen (CI, 26 Nov).
        layer = std::make_unique<Layer> (*o, look);
        layer->setBounds (marginsFor (look).addedTo (o->getBounds()));

        if (onDesktop)
        {
            // (JUCE's own shadow windows also set the thread's DPI awareness to the
            // owner's: that matters only inside a plugin host. Encore is the
            // process, per-monitor aware throughout.)
            layer->setAlwaysOnTop (o->isAlwaysOnTop());
            // Not owned by the menu: Windows keeps an owned window above its owner,
            // and on CI an owned shadow window behind its menu was never shown.
            layer->addToDesktop (juce::ComponentPeer::windowIgnoresMouseClicks
                                 | juce::ComponentPeer::windowIsTemporary
                                 | juce::ComponentPeer::windowIgnoresKeyPresses);

            if (auto* ownerPeer = o->getPeer())
                if (auto* layerPeer = layer->getPeer())
                    layerPeer->setCustomPlatformScaleFactor (ownerPeer->getPlatformScaleFactor());
        }
        else
        {
            o->getParentComponent()->addChildComponent (*layer);
        }
    }

    layer->setAlwaysOnTop (o->isAlwaysOnTop());

    const auto margins = marginsFor (look);
    const auto bounds = margins.addedTo (o->getBounds());

    {
        // As JUCE places its own shadow windows: on a second screen with another
        // scale the window would otherwise land on the wrong screen.
        using Scope = decltype (std::declval<juce::ComponentPeer>().setMultimonitorPositionOverride ({}));
        std::optional<Scope> scope;

        if (auto* ownerPeer = onDesktop ? o->getPeer() : nullptr)
        {
            if (auto* layerPeer = layer->getPeer())
            {
                // The layer's top left relative to the owner, in the owner's unscaled pixels.
                const auto scale = o->getDesktopScaleFactor();
                const juce::Point<float> relative { (float) -margins.getLeft(), (float) -margins.getTop() };
                const auto unscaled = juce::approximatelyEqual (scale, 1.0f) ? relative : relative * scale;
                scope.emplace (layerPeer->setMultimonitorPositionOverride (ownerPeer->localToMultimonitor (unscaled).roundToInt()));
            }
        }

        layer->setBounds (bounds);
    }

    layer->setVisible (true);
    layer->toBehind (o);
    layer->repaint();
}

juce::String ShapedShadow::paintsForTests()
{
    return juce::String (Layer::paints) + " paints, last clip " + Layer::lastPaintClip.toString();
}

void ShapedShadow::componentMovedOrResized (juce::Component&, bool, bool) { update(); }
void ShapedShadow::componentVisibilityChanged (juce::Component&)         { update(); }
void ShapedShadow::componentBroughtToFront (juce::Component&)            { update(); }
void ShapedShadow::componentParentHierarchyChanged (juce::Component&)    { update(); }

void ShapedShadow::componentBeingDeleted (juce::Component& c)
{
    c.removeComponentListener (this);
    owner = nullptr;
    layer = nullptr;

    if (auto done = std::move (onOwnerDeleted))
        done();
}

} // namespace encore::theme
