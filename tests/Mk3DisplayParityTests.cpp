#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

extern "C"
{
#include "mk3_display.h"
}

namespace
{
TEST(Mk3DisplayParityTest, EncodesPhysicallyValidatedBulkPacketLayout)
{
    constexpr std::array<uint16_t, 4> Pixels {
        0xf800, 0x07e0, 0x001f, 0xffff
    };
    std::vector<uint8_t> frame(mk3_display_frame_size(2, 2));

    ASSERT_EQ(mk3_display_encode_frame(
        frame.data(), frame.size(), 1, 300, 100, 2, 2, Pixels.data()), 0);
    ASSERT_EQ(frame.size(), 20u + 8u + 8u);

    const std::array<uint8_t, 20> expectedHeader {
        0x84, 0x00, 0x01, 0x60, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x2c, 0x00, 0x64, 0x00, 0x02, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x02
    };
    EXPECT_TRUE(std::equal(expectedHeader.begin(), expectedHeader.end(), frame.begin()));

    const std::array<uint8_t, 8> expectedPixels {
        0xf8, 0x00, 0x07, 0xe0, 0x00, 0x1f, 0xff, 0xff
    };
    EXPECT_TRUE(std::equal(expectedPixels.begin(), expectedPixels.end(), frame.begin() + 20));

    const std::array<uint8_t, 8> expectedFooter {
        0x03, 0x00, 0x00, 0x00,
        0x40, 0x00, 0x00, 0x00
    };
    EXPECT_TRUE(std::equal(expectedFooter.begin(), expectedFooter.end(), frame.end() - 8));
}

TEST(Mk3DisplayParityTest, FullFrameUsesExactDimensionsAndSizeField)
{
    constexpr int Width = 480;
    constexpr int Height = 272;
    std::vector<uint16_t> pixels(static_cast<std::size_t>(Width * Height));
    std::vector<uint8_t> frame(mk3_display_frame_size(Width, Height));

    ASSERT_EQ(mk3_display_encode_frame(
        frame.data(), frame.size(), 0, 0, 0, Width, Height, pixels.data()), 0);
    EXPECT_EQ(frame.size(), 261148u);
    EXPECT_EQ(frame[12], 0x01);
    EXPECT_EQ(frame[13], 0xe0);
    EXPECT_EQ(frame[14], 0x01);
    EXPECT_EQ(frame[15], 0x10);
    EXPECT_EQ(frame[16], 0x00);
    EXPECT_EQ(frame[17], 0x00);
    EXPECT_EQ(frame[18], 0xff);
    EXPECT_EQ(frame[19], 0x00);
}

TEST(Mk3DisplayParityTest, RejectsPacketsTheHardwareFormatCannotRepresent)
{
    std::array<uint16_t, 6> pixels {};
    std::array<uint8_t, 64> frame {};

    EXPECT_EQ(mk3_display_frame_size(1, 2), 0u);
    EXPECT_EQ(mk3_display_encode_frame(
        frame.data(), frame.size(), 0, 0, 0, 1, 2, pixels.data()), -1);
    EXPECT_EQ(mk3_display_encode_frame(
        frame.data(), mk3_display_frame_size(2, 2),
        0, 1, 0, 2, 2, pixels.data()), -1);
    EXPECT_EQ(mk3_display_encode_frame(
        frame.data(), mk3_display_frame_size(1, 1), 0, 0, 0, 1, 1, pixels.data()), -1);
    EXPECT_EQ(mk3_display_encode_frame(
        frame.data(), mk3_display_frame_size(2, 2), 2, 0, 0, 2, 2, pixels.data()), -1);
    EXPECT_EQ(mk3_display_encode_frame(
        frame.data(), mk3_display_frame_size(2, 2), 0, 479, 0, 2, 2, pixels.data()), -1);
}
} // namespace
