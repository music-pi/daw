#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace juce
{
class Image;
}

namespace DisplayFrameConversion
{

/** Converts a JUCE image to host-endian RGB565 pixels.

    The destination must contain at least width * height entries. The function
    reads the image through one BitmapData lock rather than calling
    Image::getPixelAt for every pixel.
*/
bool toRgb565(const juce::Image& image, std::span<uint16_t> destination);

/** Converts a JUCE image to big-endian RGB565 bytes. */
bool toRgb565BigEndian(const juce::Image& image, std::span<uint8_t> destination);

} // namespace DisplayFrameConversion
