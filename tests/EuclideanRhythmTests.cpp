#include <gtest/gtest.h>

#include "../src/engine/EuclideanRhythm.h"

namespace
{
int count(const std::vector<bool>& p)
{
    int n = 0; for (bool b : p) n += b ? 1 : 0; return n;
}
}

TEST(EuclideanRhythmTests, FamousPatterns)
{
    // Canonical euclidean rhythms from G. Toussaint "The Euclidean Algorithm
    // Generates Traditional Musical Rhythms":
    //   E(3, 8) = tresillo   = [x..x..x.]
    //   E(5, 8) = cinquillo  = [x.xx.xx.]
    //   E(2, 5) = flamenco   = [x.x.x]  (actually Bulgarian folk)
    auto tres = EuclideanRhythm::pattern(3, 8);
    ASSERT_EQ(tres.size(), 8u);
    EXPECT_EQ(count(tres), 3);
    EXPECT_TRUE(tres[0]);                     // starts with a hit

    auto cinq = EuclideanRhythm::pattern(5, 8);
    EXPECT_EQ(count(cinq), 5);

    auto p2of5 = EuclideanRhythm::pattern(2, 5);
    EXPECT_EQ(count(p2of5), 2);
}

TEST(EuclideanRhythmTests, PulseCountClampsToSteps)
{
    auto p = EuclideanRhythm::pattern(9, 4);
    EXPECT_EQ(p.size(), 4u);
    EXPECT_EQ(count(p), 4);   // all steps hit
}

TEST(EuclideanRhythmTests, ZeroPulsesYieldsAllFalse)
{
    auto p = EuclideanRhythm::pattern(0, 8);
    EXPECT_EQ(count(p), 0);
    EXPECT_EQ(p.size(), 8u);
}

TEST(EuclideanRhythmTests, StepsLessThanOneReturnsEmpty)
{
    EXPECT_TRUE(EuclideanRhythm::pattern(3, 0).empty());
    EXPECT_TRUE(EuclideanRhythm::pattern(3, -5).empty());
}

TEST(EuclideanRhythmTests, RotationShiftsSequenceLeft)
{
    // E(3, 8) starts with a hit; rotating by 1 should move the first hit
    // forward one step.
    auto base    = EuclideanRhythm::pattern(3, 8, 0);
    auto rotated = EuclideanRhythm::pattern(3, 8, 1);
    ASSERT_EQ(base.size(), rotated.size());
    for (int i = 0; i < (int) base.size(); ++i)
        EXPECT_EQ(rotated[(size_t) i], base[(size_t) ((i + 1) % 8)]);
}

TEST(EuclideanRhythmTests, RotationIsModularOnStepCount)
{
    auto r1 = EuclideanRhythm::pattern(3, 8, 1);
    auto r9 = EuclideanRhythm::pattern(3, 8, 9);     // == rotation 1
    auto rM7 = EuclideanRhythm::pattern(3, 8, -7);   // == rotation 1
    EXPECT_EQ(r1, r9);
    EXPECT_EQ(r1, rM7);
}

TEST(EuclideanRhythmTests, EightOfSixteenProducesStraightEighths)
{
    // E(8, 16) should yield every other step hit.
    auto p = EuclideanRhythm::pattern(8, 16);
    ASSERT_EQ(p.size(), 16u);
    EXPECT_EQ(count(p), 8);
    for (int i = 0; i < 16; ++i)
        EXPECT_EQ(p[(size_t) i], (i % 2 == 0)) << "step " << i;
}

TEST(EuclideanRhythmTests, PatternIsEvenlyDistributed)
{
    // For any non-trivial (pulses, steps) pair, the gaps between consecutive
    // hits must differ by at most 1 — this is the defining property of an
    // euclidean rhythm.
    const int pairs[][2] = {
        { 3, 8 }, { 5, 8 }, { 7, 16 }, { 5, 12 }, { 11, 24 }
    };
    for (auto& pr : pairs)
    {
        auto p = EuclideanRhythm::pattern(pr[0], pr[1]);
        std::vector<int> gaps;
        for (int i = 0, prev = -1; i < (int) p.size(); ++i)
        {
            if (! p[(size_t) i]) continue;
            if (prev >= 0) gaps.push_back(i - prev);
            prev = i;
        }
        if (gaps.empty()) continue;
        const int gmin = *std::min_element(gaps.begin(), gaps.end());
        const int gmax = *std::max_element(gaps.begin(), gaps.end());
        EXPECT_LE(gmax - gmin, 1)
            << "pulses=" << pr[0] << " steps=" << pr[1] << " gaps uneven";
    }
}
