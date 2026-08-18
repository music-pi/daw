#include "EuclideanRhythm.h"

#include <algorithm>

namespace EuclideanRhythm
{

std::vector<bool> pattern(int pulses, int steps, int rotation)
{
    if (steps <= 0) return {};
    pulses = std::clamp(pulses, 0, steps);

    // Degenerate cases.
    std::vector<bool> out(static_cast<size_t>(steps), false);
    if (pulses == 0) return out;
    if (pulses == steps) { std::fill(out.begin(), out.end(), true); return out; }

    // Bjorklund iterative form: start with two groups — `pulses` ones and
    // `steps - pulses` zeros — then repeatedly distribute the smaller group
    // across the larger until only one group (or a remainder ≤ 1) remains.
    std::vector<std::vector<int>> groups;
    groups.reserve(static_cast<size_t>(steps));
    for (int i = 0; i < pulses; ++i)      groups.push_back({ 1 });
    for (int i = 0; i < steps - pulses; ++i) groups.push_back({ 0 });

    while (true)
    {
        // Count how many "one" groups (starting-with-1) and "zero" groups
        // (starting-with-0) live at the tail.
        int remainder = 0;
        const int lastOneIdx = static_cast<int>(groups.size()) - 1;
        for (int i = lastOneIdx; i >= 0 && groups[i].front() == 0; --i)
            ++remainder;

        if (remainder <= 1) break;       // done distributing
        const int pairs = static_cast<int>(groups.size()) - remainder;
        if (pairs <= 0) break;
        const int merges = std::min(pairs, remainder);

        for (int i = 0; i < merges; ++i)
        {
            auto& lhs = groups[static_cast<size_t>(i)];
            auto& rhs = groups[groups.size() - 1 - static_cast<size_t>(i)];
            lhs.insert(lhs.end(), rhs.begin(), rhs.end());
        }
        groups.resize(groups.size() - static_cast<size_t>(merges));
    }

    std::vector<bool> flat;
    flat.reserve(static_cast<size_t>(steps));
    for (const auto& g : groups)
        for (int b : g) flat.push_back(b != 0);

    // Apply rotation — positive values shift the sequence left by N steps.
    const int s = static_cast<int>(flat.size());
    if (s == 0) return out;
    const int r = ((rotation % s) + s) % s;
    for (int i = 0; i < s; ++i)
        out[static_cast<size_t>(i)] = flat[static_cast<size_t>((i + r) % s)];
    return out;
}

}
