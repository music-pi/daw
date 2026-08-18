#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../control/HardwareConstants.h"

// Design tokens — single source of truth, aligned to Figma MASCHINEPIMASTER.
// Every visual constant in the codebase must reference this namespace.
namespace UiTheme
{
    // ── Screen Layout ──
    constexpr int kPanelWidth = 480;
    constexpr int kPanelHeight = 272;
    constexpr int kTotalWidth = kPanelWidth * 2;

    // ── Colors: Backgrounds ──
    const juce::Colour kBackgroundDark = juce::Colour(0xff0c0e11);
    const juce::Colour kContentGradientTop = juce::Colour(77, 77, 77).withAlpha(0.14f);
    const juce::Colour kContentGradientBottom = juce::Colours::transparentWhite;

    // ── Colors: Option Bar ──
    const juce::Colour kOptionFillActive = juce::Colours::white;
    const juce::Colour kOptionFillEnabled = juce::Colour(0xff2c2c2c);
    const juce::Colour kOptionFillDisabled = juce::Colour(0xff222222);
    const juce::Colour kOptionFillEmpty = juce::Colour(0xff111111);
    const juce::Colour kOptionBorderActive = juce::Colour(0xffb1b1b1);
    const juce::Colour kOptionBorderEnabled = juce::Colours::white;
    const juce::Colour kOptionBorderDisabled = juce::Colour(0xff5a5a5a);
    const juce::Colour kOptionBorderEmpty = juce::Colour(0xff262626);
    const juce::Colour kOptionTextEnabled = juce::Colours::white;
    const juce::Colour kOptionTextActive = juce::Colour(0xffb1b1b1);
    const juce::Colour kOptionTextDisabled = juce::Colour(0xff5a5a5a);

    // ── Colors: Titlebar ──
    const juce::Colour kTitlebarBackground = juce::Colour(0xff2c2c2c);
    const juce::Colour kTitlebarAccent = juce::Colour(0xffa49700);

    // ── Accent Palette ── widgets pick from these for per-screen titlebar accents
    const juce::Colour kAccentGold = juce::Colour(0xffa49700);   // default
    const juce::Colour kAccentPurple = juce::Colour(0xff8b5cf6);
    const juce::Colour kAccentOrange = juce::Colour(0xffff9100);
    const juce::Colour kAccentBlue = juce::Colour(0xff50b4ff);
    const juce::Colour kAccentGreen = juce::Colour(0xff30a060);
    const juce::Colour kAccentRed = juce::Colour(0xffff4545);

    // ── Colors: Knob Bar ──
    const juce::Colour kKnobBarBackground = juce::Colour(0xff111111);
    const juce::Colour kKnobBarBorder = juce::Colour(0xff262626);
    const juce::Colour kKnobLabelText = juce::Colour(0xff8f8e8e);
    const juce::Colour kKnobValueText = juce::Colours::white;
    const juce::Colour kKnobSeparator = juce::Colour(0xffd9d9d9);

    // ── Colors: Pads ──
    const juce::Colour kPadEmpty = juce::Colour(0xff2d373f);
    const juce::Colour kPadWithSample = juce::Colour(0xff50b4ff);
    const juce::Colour kPadBorder = juce::Colour(0xff4d5560);
    const juce::Colour kPadText = juce::Colours::white;

    // ── Colors: Mixer ──
    const juce::Colour kMeterGreen = juce::Colour::fromRGB(0x21, 0xff, 0x62);
    const juce::Colour kMeterYellow = juce::Colour::fromRGB(0xff, 0xd8, 0x45);
    const juce::Colour kMeterRed = juce::Colour::fromRGB(0xff, 0x45, 0x45);
    const juce::Colour kMeterBackground = juce::Colour::fromRGB(38, 43, 52);
    const juce::Colour kStripBackground = juce::Colour::fromRGB(46, 52, 60);
    const juce::Colour kStripBorder = juce::Colour::fromRGB(18, 22, 26);
    const juce::Colour kHeaderBackground = juce::Colour::fromRGB(71, 76, 86);
    const juce::Colour kFooterBackground = juce::Colour::fromRGB(28, 32, 38);
    const juce::Colour kFooterDivider = juce::Colour::fromRGB(18, 20, 24);
    const juce::Colour kMixerExpandedBackground = juce::Colour::fromRGB(18, 22, 28); // mixer background when a strip is expanded
    const juce::Colour kIndicatorBase = juce::Colour::fromRGB(60, 66, 76);
    const juce::Colour kIndicatorHighlight = juce::Colour::fromRGB(205, 210, 220);
    const juce::Colour kIndicatorAccent = juce::Colour::fromRGB(120, 176, 255);
    const juce::Colour kTextPrimary = juce::Colours::white;
    const juce::Colour kTextSecondary = juce::Colour::fromRGB(180, 186, 194);

    // ── Colors: Lists ──
    const juce::Colour kRowHighlight = juce::Colour::fromRGB(70, 110, 180).withAlpha(0.55f);

    // ── Colors: Dialogs/Overlays ──
    const juce::Colour kDialogBackground = juce::Colour(0xff1a1a1a);
    const juce::Colour kDialogPanel = juce::Colour(0xff111111);
    const juce::Colour kDialogDivider = juce::Colour(0xff333333);
    const juce::Colour kOverlayBackground = juce::Colour::fromFloatRGBA(0.02f, 0.02f, 0.02f, 0.82f);
    const juce::Colour kSnackbarBackground = juce::Colour(0xe61a1a1a);

    // ── Toast accent colors ──
    namespace ToastColors
    {
        const juce::Colour kInfo    = juce::Colour(0xff4aa3e6); // light blue
        const juce::Colour kSuccess = juce::Colour(0xff37c871); // green
        const juce::Colour kWarning = juce::Colour(0xffe6a420); // amber
        const juce::Colour kError   = juce::Colour(0xffd94747); // red
        const juce::Colour kLoading = juce::Colour(0xff4aa3e6); // same blue as info

        constexpr int kStripeWidth  = 4;   // px — accent stripe on left edge
        constexpr int kIconGutter   = 40;  // px — gutter between stripe and text
    }

    // ── Colors: Arranger ──
    const juce::Colour kArrangerMeterColour = juce::Colour::fromRGB(0, 200, 140);
    const juce::Colour kArrangerIndicator = juce::Colour::fromRGB(255, 145, 0);

    // ── Typography ──
    namespace Fonts
    {
        constexpr float kHeadingLarge = 20.0f;   // Bold — large headers
        constexpr float kHeading = 18.0f;        // Bold — dialog/section headers
        constexpr float kBody = 14.0f;           // Plain — list items, descriptions
        constexpr float kBodySmall = 12.0f;      // Plain — secondary text
        constexpr float kTitlebarTitle = 14.0f;  // Extra Bold — titlebar main text
        constexpr float kTitlebarSubtitle = 12.0f; // Semi Bold — titlebar right info
        constexpr float kOptionLabel = 11.0f;    // Medium — option button text
        constexpr float kKnobLabel = 8.0f;       // Medium — knob parameter name
        constexpr float kKnobValue = 12.0f;      // Bold — knob parameter value
        constexpr float kKnobBarTitle = 10.0f;   // Bold — knob bar title row
        constexpr float kSmallLabel = 9.0f;      // Plain — captions, footnotes
        constexpr float kIndicator = 12.5f;      // Bold — mixer indicator buttons
        constexpr float kIndicatorSmall = 10.0f; // Bold — mixer badge text
        constexpr float kSnackbar = 13.0f;       // Bold — snackbar message
        constexpr float kScale = 11.0f;          // Bold — meter/gain scale labels
    }

    // ── Spacing ──
    constexpr int kOptionHeight = 20;
    constexpr int kTitlebarHeight = 25;
    constexpr int kKnobBarHeight = 25;
    constexpr int kKnobBarWithTitleHeight = 45;
    constexpr int kKnobBarTitleAreaHeight = 18;

    // ── Knob Encoder Sensitivity ──
    // Accumulated raw-delta threshold before a list-knob advances one entry.
    // The Mk3 encoder reports multiple units per detent, and short discrete
    // lists (e.g. slice count) feel very sensitive at low thresholds. Medium
    // suits lists with only a handful of entries where accidental steps are
    // costly.
    constexpr int kMediumKnobTicks = 64;
    constexpr int kAccentWidth = 10;
    constexpr float kOptionCornerRadius = 0.0f;
    constexpr int kPadding = 4;
    constexpr int kPadGridGap = 8;
    constexpr int kHorizontalPadding = 12;
    constexpr int kVerticalPadding = 4;
    constexpr int kSlotPadding = 6;
    constexpr int kSlotLeftPadding = 5;
    constexpr float kDisabledAlpha = 0.25f;
    constexpr float kPadCornerRadius = 5.0f;
    constexpr float kPadBorderWidth = 1.0f;

    // ── Mixer Layout ──
    constexpr float kBackgroundAlpha = 0.95f;
    constexpr float kStripPadding = 0.0f;
    constexpr float kStripCornerRadius = 12.0f;
    constexpr float kMixerHeaderHeight = 44.0f;
    constexpr float kMixerFooterHeight = 56.0f;
    constexpr float kMixerIndicatorSpacing = 8.0f;
    constexpr float kMixerIndicatorCornerRadius = 8.0f;

    // ── Mixer Audio ──
    constexpr float kMeterDbMin = -60.0f;
    constexpr float kMeterDbMax = 0.0f;
    constexpr float kFaderDbMin = -48.0f;
    constexpr float kFaderDbMax = 6.0f;
    constexpr float kFaderDbDisplayMin = -80.0f;
    constexpr float kFaderDbVisualMax = 12.0f;
    constexpr double kUiMeterUpdateRateHz = 60.0;
    constexpr double kPeakHoldSeconds = 1.2;
    constexpr float kPeakDecayDbPerSec = 18.0f;
    constexpr float kHoldDecayDbPerSec = 24.0f;

    // ── Mk3 LED palette ──
    // MK3 firmware palette: indices 4-67 are 16 hues with four brightness
    // tiers each. This wheel was validated on physical firmware 1.45.
    inline juce::Colour ledIndexToColour(uint8_t index)
    {
        if (index >= 4 && index <= 67)
        {
            const auto offset = static_cast<int>(index) - 4;
            const auto hue = static_cast<float>(offset / 4) / 16.0f;
            const auto brightness = 0.55f + static_cast<float>(offset % 4) * 0.15f;
            return juce::Colour::fromHSV(hue, 0.75f, brightness, 1.0f);
        }

        switch (index)
        {
            case 76: return juce::Colour::fromRGB(92, 86, 76);
            case 77: return juce::Colour::fromRGB(170, 160, 145);
            case 78: return juce::Colour::fromRGB(255, 244, 220);
            default: return kPadEmpty;
        }
    }

    inline uint8_t brightestHueVariant(uint8_t colorIndex)
    {
        return HardwareConstants::brightestIndexedVariant(colorIndex);
    }

    inline uint8_t dimmestHueVariant(uint8_t colorIndex)
    {
        return HardwareConstants::dimmestIndexedVariant(colorIndex);
    }
}
