#include "DisplayFrameConversion.h"

#include <juce_graphics/juce_graphics.h>

namespace
{
inline uint16_t toRgb565(uint8_t red, uint8_t green, uint8_t blue) noexcept
{
    return static_cast<uint16_t>(((static_cast<uint16_t>(red) >> 3) << 11)
                               | ((static_cast<uint16_t>(green) >> 2) << 5)
                               | (static_cast<uint16_t>(blue) >> 3));
}

struct NativeWriter
{
    void write(size_t index, uint8_t red, uint8_t green, uint8_t blue) const noexcept
    {
        destination[index] = toRgb565(red, green, blue);
    }

    std::span<uint16_t> destination;
};

struct BigEndianWriter
{
    void write(size_t index, uint8_t red, uint8_t green, uint8_t blue) const noexcept
    {
        const auto pixel = toRgb565(red, green, blue);
        destination[index * 2] = static_cast<uint8_t>(pixel >> 8);
        destination[index * 2 + 1] = static_cast<uint8_t>(pixel & 0xff);
    }

    std::span<uint8_t> destination;
};

template<typename Pixel, typename Writer>
void convertPixels(const juce::Image::BitmapData& bitmap, const Writer& writer)
{
    size_t outputIndex = 0;
    for (int y = 0; y < bitmap.height; ++y)
    {
        const auto* line = bitmap.getLinePointer(y);
        for (int x = 0; x < bitmap.width; ++x)
        {
            const auto* pixel = reinterpret_cast<const Pixel*>(
                line + static_cast<ptrdiff_t>(x) * bitmap.pixelStride);
            writer.write(outputIndex++, pixel->getRed(), pixel->getGreen(), pixel->getBlue());
        }
    }
}

template<typename Writer>
bool convert(const juce::Image& image, size_t destinationPixels, const Writer& writer)
{
    if (image.isNull())
        return false;

    const auto requiredPixels = static_cast<size_t>(image.getWidth())
                              * static_cast<size_t>(image.getHeight());
    if (destinationPixels < requiredPixels)
        return false;

    const juce::Image::BitmapData bitmap(image, juce::Image::BitmapData::readOnly);
    switch (bitmap.pixelFormat)
    {
        case juce::Image::ARGB:
        {
            // JUCE stores ARGB RGB components premultiplied by alpha. Match
            // Image::getPixelAt exactly by unpremultiplying before conversion.
            size_t outputIndex = 0;
            for (int y = 0; y < bitmap.height; ++y)
            {
                const auto* line = bitmap.getLinePointer(y);
                for (int x = 0; x < bitmap.width; ++x)
                {
                    const auto* source = reinterpret_cast<const juce::PixelARGB*>(
                        line + static_cast<ptrdiff_t>(x) * bitmap.pixelStride);
                    const auto pixel = source->getUnpremultiplied();
                    writer.write(outputIndex++, pixel.getRed(), pixel.getGreen(), pixel.getBlue());
                }
            }
            return true;
        }
        case juce::Image::RGB:
            convertPixels<juce::PixelRGB>(bitmap, writer);
            return true;
        case juce::Image::SingleChannel:
            for (size_t index = 0; index < requiredPixels; ++index)
                writer.write(index, 0, 0, 0);
            return true;
        case juce::Image::UnknownFormat:
            break;
    }

    return false;
}
} // namespace

namespace DisplayFrameConversion
{

bool toRgb565(const juce::Image& image, std::span<uint16_t> destination)
{
    return convert(image, destination.size(), NativeWriter { destination });
}

bool toRgb565BigEndian(const juce::Image& image, std::span<uint8_t> destination)
{
    return convert(image, destination.size() / 2, BigEndianWriter { destination });
}

} // namespace DisplayFrameConversion
