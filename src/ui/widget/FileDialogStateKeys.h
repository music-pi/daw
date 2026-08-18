// src/ui/widget/FileDialogStateKeys.h
#pragma once
#include <juce_data_structures/juce_data_structures.h>

namespace FileDialogStateKeys
{
inline const juce::Identifier kRoot         { "fileDialog" };
inline const juce::Identifier kMode         { "mode" };         // "browse" | "search" | "saveAs" | "saveAsOverwrite"
inline const juce::Identifier kSelectedFile { "selectedFile" }; // absolute path string
inline const juce::Identifier kQuery        { "query" };
inline const juce::Identifier kSaveAsName   { "saveAsName" };
}
