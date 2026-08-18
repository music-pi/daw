#pragma once

/**
 * Shared ValueTree property keys used by the `pad_mixer` state node.
 *
 * MixerWidget writes these on activation, drill up/down, active-channel
 * change, and horizontal scroll. ChannelDetailsWidget reads them to stay
 * in sync with the currently-active channel.
 */
namespace MixerStateKeys
{
    constexpr const char* Level = "level";
    constexpr const char* FocusedGroup = "focusedGroup";
    constexpr const char* ActiveChannel = "activeChannelId";
}
