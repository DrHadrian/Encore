// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/AmpArt.h"

#include "EncoreAmpArt.h"

#include <cmath>
#include <map>

namespace encore::kit::ampArt
{

namespace
{
    /** The decoded images; let go at JUCE's shutdown (before the leak checks), as the fonts are. */
    struct Cache final : private juce::DeletedAtShutdown
    {
        ~Cache() override { clearSingletonInstance(); }

        std::map<juce::String, juce::Image> images;

        JUCE_DECLARE_SINGLETON_INLINE (Cache, false)
    };

    std::map<juce::String, juce::Image>& cache()
    {
        return Cache::getInstance()->images;
    }

    const void* dataFor (const juce::String& fileName, int& size)
    {
        for (int i = 0; i < EncoreAmpArt::namedResourceListSize; ++i)
            if (fileName == EncoreAmpArt::originalFilenames[i])
                return EncoreAmpArt::getNamedResource (EncoreAmpArt::namedResourceList[i], size);

        size = 0;
        return nullptr;
    }

    /** The CSS filters hue-rotate(deg) then saturate(s) (Filter Effects spec), on unpremultiplied colour. */
    juce::Image filtered (const juce::Image& source, float degrees, float saturation)
    {
        auto image = source.createCopy();
        const auto a = juce::degreesToRadians (degrees);
        const auto c = std::cos (a), s = std::sin (a);
        const float hue[3][3] {
            { 0.213f + c * 0.787f - s * 0.213f, 0.715f - c * 0.715f - s * 0.715f, 0.072f - c * 0.072f + s * 0.928f },
            { 0.213f - c * 0.213f + s * 0.143f, 0.715f + c * 0.285f + s * 0.140f, 0.072f - c * 0.072f - s * 0.283f },
            { 0.213f - c * 0.213f - s * 0.787f, 0.715f - c * 0.715f + s * 0.715f, 0.072f + c * 0.928f + s * 0.072f },
        };
        const auto k = saturation;
        const float sat[3][3] {
            { 0.213f + 0.787f * k, 0.715f - 0.715f * k, 0.072f - 0.072f * k },
            { 0.213f - 0.213f * k, 0.715f + 0.285f * k, 0.072f - 0.072f * k },
            { 0.213f - 0.213f * k, 0.715f - 0.715f * k, 0.072f + 0.928f * k },
        };

        juce::Image::BitmapData bits (image, juce::Image::BitmapData::readWrite);

        for (int y = 0; y < bits.height; ++y)
        {
            for (int x = 0; x < bits.width; ++x)
            {
                const auto px = bits.getPixelColour (x, y);

                if (px.getAlpha() == 0)
                    continue;

                const float in[3] { px.getFloatRed(), px.getFloatGreen(), px.getFloatBlue() };
                float mid[3], out[3];

                for (int r = 0; r < 3; ++r)
                    mid[r] = juce::jlimit (0.0f, 1.0f, hue[r][0] * in[0] + hue[r][1] * in[1] + hue[r][2] * in[2]);

                for (int r = 0; r < 3; ++r)
                    out[r] = juce::jlimit (0.0f, 1.0f, sat[r][0] * mid[0] + sat[r][1] * mid[1] + sat[r][2] * mid[2]);

                bits.setPixelColour (x, y, juce::Colour::fromFloatRGBA (out[0], out[1], out[2], px.getFloatAlpha()));
            }
        }

        return image;
    }
}

juce::Image get (const juce::String& fileName)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto& images = cache();

    if (const auto found = images.find (fileName); found != images.end())
        return found->second;

    int size = 0;
    juce::Image image;

    if (const auto* data = dataFor (fileName, size); data != nullptr && size > 0)
        image = juce::ImageFileFormat::loadFrom (data, (size_t) size);

    if (image.isValid())
        image = image.convertedToFormat (juce::Image::ARGB);   // premultiplied ARGB, as Graphics draws fastest

    images[fileName] = image;   // a missing file is remembered too (asked once)
    return image;
}

juce::Image jewel (const juce::String& colour, bool lit)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (! lit)
        return get ("jewel_off.png");

    if (colour.isEmpty() || colour == "red")
        return get ("jewel_on.png");

    const auto key = "jewel_on_" + colour;
    auto& images = cache();

    if (const auto found = images.find (key); found != images.end())
        return found->second;

    // The board's filters: amber hue-rotate(28deg) saturate(1.3), green hue-rotate(95deg), blue hue-rotate(215deg).
    const auto base = get ("jewel_on.png");
    juce::Image made;

    if (base.isValid())
    {
        if (colour == "amber")      made = filtered (base, 28.0f, 1.3f);
        else if (colour == "green") made = filtered (base, 95.0f, 1.0f);
        else if (colour == "blue")  made = filtered (base, 215.0f, 1.0f);
        else                        made = base;
    }

    images[key] = made;
    return made;
}

juce::StringArray names()
{
    juce::StringArray all;

    for (int i = 0; i < EncoreAmpArt::namedResourceListSize; ++i)
        all.add (EncoreAmpArt::originalFilenames[i]);

    all.sort (false);
    return all;
}

void clearCache()
{
    JUCE_ASSERT_MESSAGE_THREAD
    cache().clear();
}

int numCached()
{
    return (int) cache().size();
}

} // namespace encore::kit::ampArt
