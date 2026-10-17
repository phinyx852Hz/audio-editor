// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// Waveform display with a playhead/cursor. Keyboard focusable; all navigation
// is driven through commands so it can be remapped.
class WaveformView final : public juce::Component,
                           private juce::ChangeListener,
                           private juce::Timer
{
public:
    WaveformView (juce::AudioFormatManager&, juce::AudioTransportSource&);
    ~WaveformView() override;

    void setFile (const juce::File&);
    void clear();

    double getLengthSeconds() const;
    double getCursorSeconds() const;
    void setCursorSeconds (double seconds);
    void moveCursorBy (double seconds);

    void zoomBy (double factor);
    void zoomToFit();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override   { repaint(); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void timerCallback() override;
    void keepCursorVisible();
    float timeToX (double seconds) const;
    double xToTime (float x) const;

    juce::AudioTransportSource& transport;
    juce::AudioThumbnailCache thumbnailCache { 8 };
    juce::AudioThumbnail thumbnail;
    juce::Range<double> visibleRange;
};
