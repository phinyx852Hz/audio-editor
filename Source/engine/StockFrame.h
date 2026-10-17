// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioEngine.h"
#include "StockPlugins.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>
#include <memory>

// The window every stock plugin opens in: a header bar (name, credit, A/B
// compare, on/off), input and output meters at the sides, the plugin's own
// view in the middle and a footer of knobs for its main controls. Coloured
// from the PHINYX theme.
class StockFrame final : public juce::Component, private juce::Timer
{
public:
    struct Hooks
    {
        std::function<bool()> isBypassed;
        std::function<void (bool)> setBypassed;
        std::function<int()> compareSide;            // 0 = A, 1 = B
        std::function<void (int)> setCompareSide;
        std::function<void()> copyCompareSide;       // the side being heard onto the other
        std::function<std::array<float, 4>()> takeLevels;   // in L, in R, out L, out R peaks since the last call
    };

    StockFrame (juce::AudioProcessorEditor* editor, juce::AudioPluginInstance&, const stock::Info&, const juce::String& name, Hooks);
    ~StockFrame() override;

    static void setColours (const PluginFrameColours&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void childBoundsChanged (juce::Component*) override;

private:
    class Knob;
    class Meter;

    void timerCallback() override;
    void fitToEditor();
    void refreshButtons();
    void applyColours();

    std::unique_ptr<juce::AudioProcessorEditor> editor;
    const stock::Info& info;
    juce::String name;
    Hooks hooks;

    juce::TextButton aButton { "A" }, bButton { "B" }, copyButton { "Copy" }, power { "On" };
    std::unique_ptr<Meter> inMeter, outMeter;
    juce::OwnedArray<Knob> knobs;
    std::uint32_t colourStamp = 0;
    bool fitting = false;

    static constexpr int headerHeight = 38, footerHeight = 100, sideWidth = 26;
};
