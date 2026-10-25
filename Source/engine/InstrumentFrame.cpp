// SPDX-License-Identifier: AGPL-3.0-only
#include "InstrumentFrame.h"
#include "StockFrame.h"
#include "X11Windows.h"

#include <algorithm>
#include <cmath>

std::function<double (const std::string&)> InstrumentFrame::loadOpacity;
std::function<void (const std::string&, double)> InstrumentFrame::saveOpacity;

//==============================================================================
// The opacity bar: a track filled up to the opacity. Click or drag along it,
// the mouse wheel nudges it, a double-click makes the window solid again.
class InstrumentFrame::Bar final : public juce::Component
{
public:
    explicit Bar (InstrumentFrame& f) : frame (f)
    {
        setTitle ("Opacity");
        setRepaintsOnMouseActivity (true);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& c = StockFrame::currentColours();
        g.fillAll (juce::Colour (c.window));
        auto area = getLocalBounds().toFloat().reduced (6.0f, 0.0f);
        const auto label = area.removeFromLeft (92.0f);
        g.setColour (juce::Colour (c.dimText));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Opacity " + juce::String (juce::roundToInt (frame.opacity * 100.0)) + "%", label, juce::Justification::centredLeft);

        track = area.withSizeKeepingCentre (area.getWidth(), 4.0f);
        g.setColour (juce::Colour (c.mid));
        g.fillRoundedRectangle (track, 2.0f);
        const auto fraction = (frame.opacity - minOpacity) / (1.0 - minOpacity);
        g.setColour (juce::Colour (c.accent).withMultipliedAlpha (isMouseOverOrDragging() ? 1.0f : 0.8f));
        g.fillRoundedRectangle (track.withWidth (track.getWidth() * (float) fraction), 2.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override { setFrom (e.position.x); }
    void mouseDrag (const juce::MouseEvent& e) override { setFrom (e.position.x); }
    void mouseDoubleClick (const juce::MouseEvent&) override { frame.setOpacityValue (1.0); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        frame.setOpacityValue (frame.opacity + (w.deltaY > 0 ? 0.05 : w.deltaY < 0 ? -0.05 : 0.0));
    }

private:
    void setFrom (float x)
    {
        if (track.getWidth() > 0.0f)
            frame.setOpacityValue (minOpacity + (1.0 - minOpacity) * std::clamp ((x - track.getX()) / track.getWidth(), 0.0f, 1.0f));
    }

    InstrumentFrame& frame;
    juce::Rectangle<float> track;
};

//==============================================================================
InstrumentFrame::InstrumentFrame (juce::AudioProcessorEditor* e, const stock::Info& i)
    : editor (e), scaler (*e), bar (std::make_unique<Bar> (*this)), info (i)
{
    setOpaque (true);
    getProperties().set ("ownOpacity", true);   // its bar sets it, not PHINYX's slider
    if (loadOpacity)
        opacity = std::clamp (loadOpacity (info.key), minOpacity, 1.0);

    addAndMakeVisible (view);
    view.addAndMakeVisible (editor.get());
    view.childResized = [this] { fitToEditor(); };
    addAndMakeVisible (*bar);
    setSize (editor->getWidth(), editor->getHeight() + barHeight);
}

InstrumentFrame::~InstrumentFrame()
{
    view.childResized = nullptr;
    editor.reset();
}

void InstrumentFrame::setOpacityValue (double value)
{
    value = std::clamp (value, minOpacity, 1.0);
    if (std::abs (value - opacity) < 1.0e-4)
        return;
    opacity = value;
    applyOpacity();
    bar->repaint();
    if (saveOpacity)
        saveOpacity (info.key, opacity);
}

void InstrumentFrame::applyOpacity()
{
    if (auto* peer = getPeer())
        x11::setOpacity (peer->getNativeHandle(), opacity, getTopLevelComponent()->getName().toStdString());
}

void InstrumentFrame::parentHierarchyChanged()
{
    applyOpacity();
    // Again once the window manager has the window (Hyprland finds it by its title).
    juce::Timer::callAfterDelay (400, [f = juce::Component::SafePointer<InstrumentFrame> (this)]
    {
        if (f != nullptr)
            f->applyOpacity();
    });
}

void InstrumentFrame::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (StockFrame::currentColours().window));
}

void InstrumentFrame::resized()
{
    auto area = getLocalBounds();
    bar->setBounds (area.removeFromBottom (barHeight));
    view.setBounds (area);
    if (fitting)
        return;
    // A view that resizes fills the space; one that cannot sits in the middle of it.
    juce::ScopedValueSetter<bool> busy (fitting, true);
    if (editor->isResizable())
        editor->setBounds (view.getLocalBounds());
    else
    {
        scaler.fit (view.getWidth(), view.getHeight());
        editor->setCentrePosition (view.getLocalBounds().getCentre());
    }
    embedded::follow (*editor);
}

// The view changed its own size (a resize button, a page with another size): the window follows.
void InstrumentFrame::fitToEditor()
{
    if (fitting || scaler.isScaling())
        return;
    if (scaler.isScaled())
    {
        resized();   // scaled to the window: only placed again
        return;
    }
    juce::ScopedValueSetter<bool> busy (fitting, true);
    setSize (editor->getWidth(), editor->getHeight() + barHeight);
    editor->setTopLeftPosition (0, 0);
}
