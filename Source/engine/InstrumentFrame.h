// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "EmbeddedView.h"
#include "StockPlugins.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <string>

// The window a stock instrument opens in: its own view, untouched (its own
// look and fonts), with a thin opacity bar along the bottom. The bar sets the
// whole window's opacity, from Theme::minOpacity to 1, and it is kept per
// instrument. Coloured from the PHI theme like StockFrame.
class InstrumentFrame final : public juce::Component
{
public:
    InstrumentFrame (juce::AudioProcessorEditor* editor, const stock::Info&);
    ~InstrumentFrame() override;

    // Where each instrument's opacity is kept (MainWindow, in settings.ini), by its key.
    static std::function<double (const std::string& key)> loadOpacity;
    static std::function<void (const std::string& key, double)> saveOpacity;
    static constexpr double minOpacity = 0.2;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

private:
    class Bar;

    void fitToEditor();
    void setOpacityValue (double);
    void applyOpacity();

    struct View final : juce::Component
    {
        std::function<void()> childResized;
        void childBoundsChanged (juce::Component*) override { if (childResized) childResized(); }
    };

    View view;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    embedded::ScaleToFit scaler;   // an editor that cannot resize grows and shrinks with the window
    std::unique_ptr<Bar> bar;
    const stock::Info& info;
    double opacity = 1.0;
    bool fitting = false;

    static constexpr int barHeight = 16;
};
