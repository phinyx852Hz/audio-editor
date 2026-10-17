// SPDX-License-Identifier: AGPL-3.0-only
#include "StockFrame.h"

#include <algorithm>
#include <cmath>

namespace
{
    PluginFrameColours colours;
    std::uint32_t colourGeneration = 1;

    juce::Colour col (std::uint32_t argb) { return juce::Colour (argb); }
}

void StockFrame::setColours (const PluginFrameColours& c)
{
    colours = c;
    ++colourGeneration;
}

//==============================================================================
// A potentiometer like the Device Chain card's: a 270 degree arc filled up to
// the value, dragged up and down (200 px for the whole range, Shift ten times
// finer), the mouse wheel nudges it, a double-click types a value.
class StockFrame::Knob final : public juce::Component
{
public:
    explicit Knob (juce::AudioProcessorParameter& p) : param (p)
    {
        setRepaintsOnMouseActivity (false);
        setTitle (param.getName (64));
        shown = param.getValue();
    }

    void sync()
    {
        const auto v = param.getValue();
        if (! dragging && std::abs (v - shown) > 1.0e-6f)
        {
            shown = v;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto knobSide = (float) std::min (getWidth() - 8, 40);
        const auto arcBox = juce::Rectangle<float> (knobSide, knobSide).withCentre ({ getWidth() / 2.0f, 4.0f + knobSide / 2.0f });
        const auto centre = arcBox.getCentre();
        const auto radius = knobSide / 2.0f - 2.0f;
        constexpr auto start = -0.75f * juce::MathConstants<float>::pi, end = 0.75f * juce::MathConstants<float>::pi;
        const auto angle = start + (end - start) * std::clamp (shown, 0.0f, 1.0f);

        juce::Path track, fill;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
        g.setColour (col (colours.mid));
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        if (angle - start > 0.01f)
        {
            fill.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, angle, true);
            g.setColour (col (colours.accent));
            g.strokePath (fill, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }

        const juce::Point<float> tip (centre.x + std::sin (angle) * radius, centre.y - std::cos (angle) * radius);
        g.setColour (col (colours.text));
        g.drawLine ({ centre + (tip - centre) * 0.25f, tip }, 2.0f);

        auto text = getLocalBounds().withTrimmedTop ((int) arcBox.getBottom() + 4);
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (col (colours.dimText));
        g.drawFittedText (param.getName (32), text.removeFromTop (15), juce::Justification::centred, 1);
        g.setColour (col (colours.text));
        const auto value = (param.getText (shown, 32) + " " + param.getLabel()).trim();
        g.drawFittedText (value, text.removeFromTop (15), juce::Justification::centred, 1);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isLeftButtonDown())
            return;
        dragging = true;
        lastY = e.position.y;
        param.beginChangeGesture();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;
        const auto dy = lastY - e.position.y;
        lastY = e.position.y;
        set (shown + dy / 200.0f * (e.mods.isShiftDown() ? 0.1f : 1.0f));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging)
            param.endChangeGesture();
        dragging = false;
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const auto steps = param.getNumSteps();
        const auto stepSize = steps > 1 && steps < 1000 ? 1.0f / (float) (steps - 1) : (e.mods.isShiftDown() ? 0.002f : 0.02f);
        const auto direction = (std::abs (w.deltaY) > 0.0f ? w.deltaY : w.deltaX) > 0.0f ? 1.0f : -1.0f;
        param.beginChangeGesture();
        set (shown + direction * stepSize);
        param.endChangeGesture();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        entry = std::make_unique<juce::TextEditor>();
        entry->setJustification (juce::Justification::centred);
        entry->setColour (juce::TextEditor::backgroundColourId, col (colours.window));
        entry->setColour (juce::TextEditor::textColourId, col (colours.text));
        entry->setColour (juce::TextEditor::outlineColourId, col (colours.accent));
        entry->setColour (juce::TextEditor::focusedOutlineColourId, col (colours.accent));
        entry->setText (param.getText (shown, 32), false);
        entry->selectAll();
        addAndMakeVisible (*entry);
        entry->setBounds (getLocalBounds().removeFromBottom (18).reduced (2, 0));
        entry->onReturnKey = [this]
        {
            const auto typed = entry->getText();
            param.beginChangeGesture();
            set (param.getValueForText (typed.trim()));
            param.endChangeGesture();
            closeEntry();
        };
        entry->onEscapeKey = [this] { closeEntry(); };
        entry->onFocusLost = [this] { closeEntry(); };
        entry->grabKeyboardFocus();
    }

private:
    void set (float v)
    {
        v = std::clamp (v, 0.0f, 1.0f);
        const auto steps = param.getNumSteps();
        if (steps > 1 && steps < 1000)
            v = std::round (v * (float) (steps - 1)) / (float) (steps - 1);
        shown = v;
        param.setValueNotifyingHost (v);
        repaint();
    }

    void closeEntry()
    {
        // Called from the editor's own callbacks: delete it afterwards.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Knob> (this)]
        {
            if (safe != nullptr)
                safe->entry.reset();
        });
    }

    juce::AudioProcessorParameter& param;
    std::unique_ptr<juce::TextEditor> entry;
    float shown = 0.0f, lastY = 0.0f;
    bool dragging = false;
};

//==============================================================================
// Two bars (left and right) from -60 to 0 dBFS, falling back at 20 dB a second,
// with a peak line that holds for a second.
class StockFrame::Meter final : public juce::Component
{
public:
    void push (float left, float right)
    {
        const std::array<float, 2> now { left, right };
        for (size_t i = 0; i < 2; ++i)
        {
            const auto db = now[i] > 1.0e-6f ? 20.0f * std::log10 (now[i]) : -100.0f;
            level[i] = std::max (db, level[i] - 20.0f / 30.0f);
            if (db >= hold[i])
            {
                hold[i] = db;
                holdTicks[i] = 30;
            }
            else if (--holdTicks[i] <= 0)
                hold[i] = std::max (level[i], hold[i] - 20.0f / 30.0f);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        const auto barWidth = (area.getWidth() - 2.0f) / 2.0f;
        auto yOf = [&] (float db) { return area.getY() + area.getHeight() * (1.0f - std::clamp ((db + 60.0f) / 60.0f, 0.0f, 1.0f)); };

        for (int i = 0; i < 2; ++i)
        {
            const auto bar = juce::Rectangle<float> (area.getX() + (float) i * (barWidth + 2.0f), area.getY(), barWidth, area.getHeight());
            g.setColour (col (colours.meterBackground));
            g.fillRect (bar);

            // Green below -18 dB, yellow to -6 dB, red above.
            const auto top = yOf (level[(size_t) i]);
            const std::array<std::pair<float, std::uint32_t>, 3> bands {{ { -18.0f, colours.meterLow }, { -6.0f, colours.meterMid }, { 0.0f, colours.meterHigh } }};
            auto lower = bar.getBottom();
            for (const auto& [upTo, c] : bands)
            {
                const auto upper = std::max (yOf (upTo), top);
                if (upper < lower)
                {
                    g.setColour (col (c));
                    g.fillRect (bar.withTop (upper).withBottom (lower));
                }
                lower = std::min (lower, yOf (upTo));
            }

            if (hold[(size_t) i] > -60.0f)
            {
                g.setColour (col (hold[(size_t) i] > -0.1f ? colours.meterHigh : colours.text));
                g.fillRect (bar.withTop (yOf (hold[(size_t) i])).withHeight (1.5f));
            }
        }
    }

private:
    std::array<float, 2> level { -100.0f, -100.0f }, hold { -100.0f, -100.0f };
    std::array<int, 2> holdTicks {};
};

//==============================================================================
StockFrame::StockFrame (juce::AudioProcessorEditor* e, juce::AudioPluginInstance& plugin, const stock::Info& i,
                        const juce::String& pluginName, Hooks h)
    : editor (e), info (i), name (pluginName), hooks (std::move (h)),
      inMeter (std::make_unique<Meter>()), outMeter (std::make_unique<Meter>())
{
    setOpaque (true);

    for (auto* b : { &aButton, &bButton, &copyButton, &power })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    aButton.setTooltip ("Listen to settings A");
    bButton.setTooltip ("Listen to settings B (starts as a copy of A)");
    copyButton.setTooltip ("Copy the settings being heard onto the other side");
    power.setTooltip ("Plugin on or bypassed");
    aButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    bButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    aButton.onClick = [this] { hooks.setCompareSide (0); refreshButtons(); };
    bButton.onClick = [this] { hooks.setCompareSide (1); refreshButtons(); };
    copyButton.onClick = [this] { hooks.copyCompareSide(); };
    power.onClick = [this] { hooks.setBypassed (! hooks.isBypassed()); refreshButtons(); };

    addAndMakeVisible (*inMeter);
    addAndMakeVisible (*outMeter);
    inMeter->setTitle ("Input level");
    outMeter->setTitle ("Output level");

    for (const auto* wanted : info.footer)
        for (auto* p : plugin.getParameters())
            if (p->getName (64) == wanted)
            {
                addAndMakeVisible (knobs.add (std::make_unique<Knob> (*p)));
                break;
            }

    addAndMakeVisible (*editor);
    applyColours();
    refreshButtons();
    fitToEditor();
    startTimerHz (30);
}

StockFrame::~StockFrame()
{
    stopTimer();
    editor.reset();   // before the plugin it belongs to
}

void StockFrame::fitToEditor()
{
    const juce::ScopedValueSetter<bool> busy (fitting, true);
    const auto minimumWidth = 120 + 84 * std::max (4, knobs.size());   // room for the header buttons and the knobs
    setSize (std::max (editor->getWidth(), minimumWidth) + 2 * sideWidth, editor->getHeight() + headerHeight + footerHeight);
    resized();
}

void StockFrame::childBoundsChanged (juce::Component* child)
{
    // The plugin resized its own view.
    if (child == editor.get() && ! fitting)
        fitToEditor();
}

void StockFrame::resized()
{
    const juce::ScopedValueSetter<bool> busy (fitting, true);
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerHeight).reduced (8, 7);
    power.setBounds (header.removeFromRight (48));
    header.removeFromRight (12);
    copyButton.setBounds (header.removeFromRight (52));
    header.removeFromRight (6);
    bButton.setBounds (header.removeFromRight (30));
    aButton.setBounds (header.removeFromRight (30));

    auto footer = area.removeFromBottom (footerHeight);
    footer.removeFromBottom (14);   // IN and OUT labels
    const auto knobWidth = 84;
    auto row = footer.withSizeKeepingCentre (knobWidth * knobs.size(), footer.getHeight() - 8);
    for (auto* k : knobs)
        k->setBounds (row.removeFromLeft (knobWidth));

    inMeter->setBounds (area.removeFromLeft (sideWidth).reduced (6, 6));
    outMeter->setBounds (area.removeFromRight (sideWidth).reduced (6, 6));

    if (editor->isResizable())
        editor->setBounds (area);
    else
        editor->setBounds (area.withSizeKeepingCentre (editor->getWidth(), editor->getHeight()));
}

void StockFrame::paint (juce::Graphics& g)
{
    g.fillAll (col (colours.window));

    auto header = getLocalBounds().removeFromTop (headerHeight);
    g.setColour (col (colours.panel));
    g.fillRect (header);
    g.setColour (col (colours.mid));
    g.fillRect (header.removeFromBottom (1));

    auto text = header.reduced (12, 0).withRight (aButton.getX() - 12);
    g.setColour (col (colours.text));
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    const auto nameWidth = std::min (text.getWidth(), juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 4);
    g.drawFittedText (name, text.removeFromLeft (nameWidth), juce::Justification::centredLeft, 1);
    g.setColour (col (colours.dimText));
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText (juce::String ("   ") + info.credit + ", " + info.licence, text, juce::Justification::centredLeft, 1);

    auto footer = getLocalBounds().removeFromBottom (footerHeight);
    g.setColour (col (colours.panel));
    g.fillRect (footer);
    g.setColour (col (colours.mid));
    g.fillRect (footer.removeFromTop (1));
    g.setColour (col (colours.dimText));
    g.setFont (juce::FontOptions (11.0f));
    auto labels = footer.removeFromBottom (16);
    g.drawText ("IN", labels.removeFromLeft (sideWidth + 8), juce::Justification::centred);
    g.drawText ("OUT", labels.removeFromRight (sideWidth + 8), juce::Justification::centred);
}

void StockFrame::applyColours()
{
    colourStamp = colourGeneration;
    for (auto* b : { &aButton, &bButton, &copyButton, &power })
    {
        b->setColour (juce::TextButton::buttonColourId, col (colours.window));
        b->setColour (juce::TextButton::buttonOnColourId, col (colours.accent));
        b->setColour (juce::TextButton::textColourOffId, col (colours.text));
        b->setColour (juce::TextButton::textColourOnId, col (colours.text));
        b->setColour (juce::ComboBox::outlineColourId, col (colours.mid));
    }
    repaint();
}

void StockFrame::refreshButtons()
{
    const auto side = hooks.compareSide();
    aButton.setToggleState (side == 0, juce::dontSendNotification);
    bButton.setToggleState (side == 1, juce::dontSendNotification);
    const auto on = ! hooks.isBypassed();
    power.setToggleState (on, juce::dontSendNotification);
    power.setButtonText (on ? "On" : "Off");
}

void StockFrame::timerCallback()
{
    if (colourStamp != colourGeneration)
        applyColours();

    const auto levels = hooks.takeLevels();
    inMeter->push (levels[0], levels[1]);
    outMeter->push (levels[2], levels[3]);

    for (auto* k : knobs)
        k->sync();
    refreshButtons();   // the card or a key may have changed them
}
