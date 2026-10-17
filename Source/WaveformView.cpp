// SPDX-License-Identifier: AGPL-3.0-only
#include "WaveformView.h"

WaveformView::WaveformView (juce::AudioFormatManager& formatManager, juce::AudioTransportSource& t)
    : transport (t), thumbnail (512, formatManager, thumbnailCache)
{
    setWantsKeyboardFocus (true);
    setTitle ("Waveform");
    setDescription ("Audio waveform. Use the arrow keys to move the cursor and Space to play.");
    thumbnail.addChangeListener (this);
    startTimerHz (30);
}

WaveformView::~WaveformView()
{
    thumbnail.removeChangeListener (this);
}

void WaveformView::setFile (const juce::File& file)
{
    thumbnail.setSource (new juce::FileInputSource (file));
    visibleRange = { 0.0, thumbnail.getTotalLength() };
    repaint();
}

void WaveformView::clear()
{
    thumbnail.clear();
    visibleRange = {};
    repaint();
}

double WaveformView::getLengthSeconds() const { return transport.getLengthInSeconds(); }
double WaveformView::getCursorSeconds() const { return transport.getCurrentPosition(); }

void WaveformView::setCursorSeconds (double seconds)
{
    transport.setPosition (juce::jlimit (0.0, getLengthSeconds(), seconds));
    keepCursorVisible();
    repaint();
}

void WaveformView::moveCursorBy (double seconds)
{
    setCursorSeconds (getCursorSeconds() + seconds);
}

void WaveformView::zoomBy (double factor)
{
    const auto total = getLengthSeconds();
    if (total <= 0.0)
        return;

    const auto newLength = juce::jlimit (0.001, total, visibleRange.getLength() * factor);
    const auto start = juce::jlimit (0.0, total - newLength, getCursorSeconds() - newLength * 0.5);
    visibleRange = { start, start + newLength };
    repaint();
}

void WaveformView::zoomToFit()
{
    visibleRange = { 0.0, getLengthSeconds() };
    repaint();
}

void WaveformView::keepCursorVisible()
{
    const auto cursorPos = getCursorSeconds();
    if (visibleRange.isEmpty() || visibleRange.contains (cursorPos))
        return;

    const auto length = visibleRange.getLength();
    const auto start = juce::jlimit (0.0, juce::jmax (0.0, getLengthSeconds() - length), cursorPos - length * 0.1);
    visibleRange = { start, start + length };
}

void WaveformView::timerCallback()
{
    if (transport.isPlaying())
    {
        keepCursorVisible();
        repaint();
    }
}

float WaveformView::timeToX (double seconds) const
{
    if (visibleRange.isEmpty())
        return 0.0f;

    return (float) ((seconds - visibleRange.getStart()) / visibleRange.getLength() * getWidth());
}

double WaveformView::xToTime (float x) const
{
    return visibleRange.getStart() + (double) x / juce::jmax (1, getWidth()) * visibleRange.getLength();
}

void WaveformView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15171c));
    auto area = getLocalBounds().reduced (2);

    if (thumbnail.getTotalLength() <= 0.0)
    {
        g.setColour (juce::Colours::grey);
        g.drawFittedText ("Press Ctrl+O to open a file, or Ctrl+Shift+P to search all commands",
                          area, juce::Justification::centred, 2);
    }
    else
    {
        g.setColour (juce::Colour (0xff4fc3f7));
        thumbnail.drawChannels (g, area, visibleRange.getStart(), visibleRange.getEnd(), 1.0f);

        const auto x = timeToX (getCursorSeconds());
        g.setColour (juce::Colours::white);
        g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
    }

    if (hasKeyboardFocus (true))
    {
        g.setColour (juce::Colour (0xffffb74d));
        g.drawRect (getLocalBounds(), 2);
    }
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    setCursorSeconds (xToTime ((float) e.x));
}
