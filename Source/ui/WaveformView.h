// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioDocument.h"
#include "../core/Pitch.h"
#include "../core/Repair.h"
#include "Spectrogram.h"

#include <QFutureWatcher>
#include <QImage>
#include <QPolygonF>
#include <QWidget>
#include <memory>
#include <optional>
#include <vector>

// Waveform display with cursor, selection and markers. Keyboard focusable;
// all navigation is driven through commands so every key can be remapped.
// Mouse: click places the cursor, drag selects, Shift+click extends.
class WaveformView final : public QWidget
{
    Q_OBJECT

public:
    explicit WaveformView (QWidget* parent = nullptr);

    // Keeps the current zoom when the length is unchanged, otherwise fits.
    void setData (AudioDataPtr, bool keepView);
    void setMarkers (std::vector<Marker>);
    // Warp markers (Warp menu): while editing, hits show as ticks along the top and
    // warp markers as flags. Dragging a tick or a flag moves that point of the audio
    // (snapped to sixteenths of the grid; Alt: free); Shift+drag moves a flag without
    // moving the audio; double-click adds or removes a flag.
    void setWarp (std::shared_ptr<const WarpEdit>, bool editing);
    // The beat grid: bar lines, beats and sixteenths as far as the zoom leaves room, bars numbered.
    void setGrid (const BeatGrid&, bool shown);
    void clear();
    void setLoading (bool, float progress = 0.0f);

    void setCursorFrame (Frame);
    void setSelection (Frame start, Frame end);  // empty when start == end

    // Visible range in frames, so each editor tab can keep its own zoom.
    std::pair<double, double> view() const { return { viewStart, viewLength }; }
    void setView (double start, double length);

    void zoomBy (double factor);
    void zoomToFit();
    void zoomToSelection();

    // Spectral display instead of the waveform. There a drag selects a box of
    // time and frequency; a drag over nearly the whole height selects every
    // frequency shown. Alt+wheel zooms the frequencies around the mouse.
    void setSpectral (bool);
    bool isSpectral() const { return spectral; }      // the spectrogram is shown at least half way (its tools apply)
    // A blend of the two: 0 waveform only, 1 spectrogram only, between both drawn over each other.
    void setBlend (double);
    void setLayoutName (const std::string& name) { layoutName = name; update(); }   // lane labels (dsp::layoutChoices)
    double blend() const { return mix; }
    void setSpectrumSettings (const spectrum::Settings&);
    const spectrum::Settings& spectrumSettings() const { return spectrumSet; }
    // The frequencies shown, low to high (high 0 = half the sample rate).
    void setFrequencyRange (double low, double high);
    std::pair<double, double> frequencyRange() const;
    void zoomFrequency (double factor, double around = 0.5);   // factor > 1 zooms in; around: 0 bottom to 1 top
    bool showsAllFrequencies() const;

    // The pitch line: the main voice's or instrument's pitch over time, traced
    // over the spectrogram (worked out in the background, once per take).
    void setPitchLine (bool);
    bool pitchLine() const { return pitchShown; }

    // Vertical zoom of the waveform: 1 shows full scale, 4 makes it four times taller
    // (+12 dB), so quiet passages and fades can be seen. Alt+wheel over the waveform.
    void setAmplitudeZoom (double);
    double amplitudeZoom() const { return ampZoom; }
    // Zooms whichever display is shown: amplitude on the waveform, frequencies on the spectrogram.
    void zoomVertically (double factor);
    void resetVerticalZoom();

    // The selection's frequency band in Hz (none when high <= low), drawn as a box in the spectral view.
    void setBand (double lowHz, double highHz);

    // How a drag in the spectral view selects: a time and frequency box, a
    // lasso, a brush, or the Spot Healing Brush (heals what it paints). Shift
    // with the lasso or a brush adds to the shape already drawn.
    enum class Tool { box, lasso, brush, spotHeal };
    void setTool (Tool);
    Tool tool() const { return currentTool; }
    void setBrushSize (int pixels);
    int brushSize() const { return brushRadius; }

    // A drawn shape selection, shown as a tint over the spectral view.
    void setShape (std::shared_ptr<const repair::SpectralMask>);

    // Fade handles at the top corners of the file, like Audition's: drag one
    // inwards for the fade's length, up or down to bend its curve.
    void setFadeHandles (bool shown);
    // Colours the waveform by level, cool when quiet to hot at full scale, from the
    // theme's level colours (levelQuiet ... levelPeak), blended in between.
    void setLevelColours (bool on) { levelColours = on; update(); }

Q_SIGNALS:
    void cursorRequested (Frame);
    void selectionRequested (Frame anchor, Frame end);
    void rangeRequested (Frame start, Frame end);   // exact edges (double-click between markers): not snapped
    void bandRequested (double lowHz, double highHz);   // high <= low: every frequency
    void shapeDrawn (std::shared_ptr<const repair::SpectralMask>, bool heal);   // heal: drawn with the Spot Healing Brush
    // While a fade handle is dragged, and when it is let go. Curve: -1 to +1 (see dsp::curvedFadeGain).
    void fadeDragged (bool fadeIn, Frame length, double curve);
    void fadeReleased (bool fadeIn, Frame length, double curve);
    void frequencyRangeChanged();
    // A warp point dragged: `source` (a frame of the original audio) now lands on `target`.
    // marker: the index of the flag dragged, or -1 for a tick; reanchor: Shift was held.
    void warpMoved (Frame source, Frame target, int marker, bool reanchor);
    void warpToggled (Frame source);

protected:
    void paintEvent (QPaintEvent*) override;
    void mousePressEvent (QMouseEvent*) override;
    void mouseMoveEvent (QMouseEvent*) override;
    void mouseReleaseEvent (QMouseEvent*) override;
    void wheelEvent (QWheelEvent*) override;
    void mouseDoubleClickEvent (QMouseEvent*) override;
    void leaveEvent (QEvent*) override { hover = QPointF (-1.0, -1.0); warpHover.reset(); update(); }
    void focusInEvent (QFocusEvent*) override  { update(); }
    void focusOutEvent (QFocusEvent*) override { update(); }

private:
    Frame length() const { return data ? data->length() : 0; }
    Frame xToFrame (double x) const;
    double frameToX (Frame) const;
    void keepVisible (Frame);
    void clampView();
    void requestSpectrogram();
    double laneHeight() const;
    double yToHz (double yInLane) const;
    double hzToY (double hz) const;   // within a lane
    bool drawing() const { return spectral && currentTool != Tool::box; }
    void brushAt (QPointF);           // paints the stroke at a point in the drag's lane
    void drawShape (QPainter&, double laneHeight);
    QRectF fadeHandle (bool fadeIn) const;   // where a handle is drawn (empty when its end is out of view)
    void drawFades (QPainter&);

    bool fadeHandles = true;
    int fadeDrag = 0;                // 1: the fade-in handle is being dragged, 2: the fade-out one
    Frame fadeLength = 0;
    double fadeCurve = 0.0;
    QPointF fadePress;

    struct SpectrogramKey
    {
        const AudioData* data = nullptr;
        double viewStart = 0.0, viewLength = 0.0;
        QSize size;
        QString theme;
        spectrum::Settings settings;
        double low = 0.0, high = 0.0;
        bool operator== (const SpectrogramKey&) const = default;
    };

    SpectrogramKey currentKey() const;

    AudioDataPtr data;
    std::vector<Marker> markers;
    BeatGrid grid;
    bool gridShown = false;
    void drawGrid (QPainter&);

    struct WarpHit
    {
        int marker = -1;          // index of a flag, or -1 for a tick
        Frame source = 0, target = 0;
    };
    std::shared_ptr<const WarpEdit> warp;
    bool warpEditing = false;
    std::optional<WarpHit> warpHover, warpDrag;
    Frame warpDragTo = -1;
    bool warpReanchor = false;
    std::optional<WarpHit> warpHitAt (double x) const;
    Frame warpSnap (double x, bool free) const;
    void drawWarp (QPainter&);
    bool levelColours = false;
    double viewStart = 0.0, viewLength = 0.0;  // in frames
    Frame cursor = 0, selStart = 0, selEnd = 0;
    Frame dragAnchor = -1;
    double dragAnchorY = 0.0;
    int dragLane = 0;
    double bandLow = 0.0, bandHigh = 0.0;

    Tool currentTool = Tool::box;
    int brushRadius = 12;
    std::shared_ptr<const repair::SpectralMask> shape;   // the selection
    std::shared_ptr<repair::SpectralMask> stroke;        // being painted
    QPolygonF lassoPoints;                               // being drawn, in the drag's lane
    QPointF lastBrush, hover { -1.0, -1.0 };
    bool adding = false;                                 // Shift held: add to the shape
    int shapeVersion = 0;                                // bumped whenever the shape or stroke changes
    QImage shapeImage;                                   // the tint for one lane, made when the shape or view changes
    int shapeImageVersion = -1;
    std::pair<double, double> shapeImageView;
    QSize shapeImageSize;

    bool loading = false;
    float loadProgress = 0.0f;

    bool spectral = false;
    double mix = 0.0;
    SpectrogramPicture spectrogram;
    SpectrogramKey spectrogramKey, pendingKey;
    QFutureWatcher<SpectrogramPicture> spectrogramWatcher;
    spectrum::Settings spectrumSet;
    double freqLow = 0.0, freqHigh = 0.0;   // as asked; high 0 = half the sample rate
    double ampZoom = 1.0;
    void drawAmplitudeScale (QPainter&, double laneHeight);
    void drawFrequencyAxis (QPainter&, double laneHeight);
    void drawReadout (QPainter&, double laneHeight);

    bool pitchShown = false;
    std::shared_ptr<const pitch::Track> pitchTrack;
    const AudioData* pitchFor = nullptr;       // the take pitchTrack (or the one being worked out) belongs to
    AudioDataPtr pitchData;                    // kept alive while it is worked out
    std::shared_ptr<std::atomic<bool>> pitchCancel;
    QFutureWatcher<std::shared_ptr<pitch::Track>> pitchWatcher;
    void requestPitch();
    void drawPitch (QPainter&, double laneHeight);
    std::string layoutName;
};
