// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QImage>
#include <QWidget>
#include <array>
#include <functional>
#include <vector>

class QKeyEvent;

// Where the sound is in an ambisonic file (AmbiX, up to 10th order), for the
// Analysis Meters window. Every 5 degrees around the sphere a max-rE beam
// measures the level, and six views show it, each after what one meter or
// panner does best:
// - Sphere: a glass globe with the levels as a heat map (3D Meter), the near
//   side bright and the far side through it. Drag it or use the arrows to
//   turn it, the wheel or Ctrl+Up/Down to go closer or further.
// - Four Views: from the left, the front and the top, and the globe as above.
// - Panorama: the whole sphere at once, equal-area (Hammer), front in the
//   middle, as ab Visualiser draws it.
// - Head: the listener from above and from the right with the energy around
//   them as outlines (SoundField).
// - Energy: three polar plots (from above, behind and the right), meters for
//   front, back, left, right, top and bottom, and where the energy points on
//   a pad with a height slider (ab Visualiser).
// - Scope: from above with the speakers of a 7.1.4 layout and a glowing cloud
//   where the sound is (PRISM); Height puts what is above nearer the middle.
// - Speakers: a bar for each speaker of 7.1.4, the level the scene has there
//   with a peak hold, beside the energy around the listener with the speakers
//   marked on it (Kaizen).
// Levels are in dB of a full-scale plane wave, or against the loudest place
// (Relative). Left/Right turn the scene in the flat views. Home or a double
// click puts the camera back. Hovering shows the direction and level there.
class AmbiVisualizer final : public QWidget
{
public:
    struct Vec { double x = 0.0, y = 0.0, z = 1.0; };   // x to the front, y to the left, z up

    // How a disc shows the sphere: screen right and up, and the axis towards the viewer.
    // With glass, the far side shows through the near one.
    struct Projection
    {
        Vec right, up, normal;
        bool glass = false;
    };

    enum class View { sphere, fourViews, panorama, head, energy, scope, speakers };
    static constexpr int numViews = 7;
    static constexpr int maxOrder = 10;
    static constexpr int cols = 72, rows = 37;   // azimuth -180 to 175, elevation 90 to -90, every 5 degrees

    explicit AmbiVisualizer (QWidget* parent);

    // The order of an ambisonic file: from its channel layout ("Ambisonics (2nd order)"),
    // or from (order + 1)^2 channels when the layout says Ambisonics without an order, or
    // for 25 channels and more (4th order up), which nothing else has. With `assumeAny`,
    // also 4, 9 and 16 unmarked channels. 0 when it is not ambisonic.
    static int orderFor (int channels, int layoutOrder, bool layoutIsAmbisonic, bool assumeAny);

    // Channels of an ambisonic file around the playhead, in ACN order; order 0 means
    // there is none (the views say how to get one).
    void analyse (const std::vector<std::vector<float>>& channels, int order);
    void step (double dt);   // on the UI timer: the levels move towards the last analysis
    void reset();
    bool handleKey (QKeyEvent*);   // arrows, Ctrl+arrows, Home; false when not its key
    bool wantsSignal() const { return isVisible(); }

    void setView (View);
    void nextView();
    View view() const { return current; }
    static QString viewName (View);

    // Options (remembered).
    void setRange (double dB);
    double range() const { return rangeDb; }
    void setRelative (bool);
    bool relative() const { return relativeScale; }
    void setResponse (int index);   // 0 fast, 1 medium, 2 slow
    int response() const { return responseIndex; }
    void setOrderLimit (int order);  // show at most this order of the file
    int orderLimit() const { return limit; }
    void setHeight (bool);
    bool height() const { return showHeight; }
    void resetCamera();

    std::function<void (View)> viewChanged;   // when the view changes here (a key)

    QSize sizeHint() const override        { return { 360, 300 }; }
    QSize minimumSizeHint() const override { return { 180, 160 }; }

protected:
    void paintEvent (QPaintEvent*) override;
    void mousePressEvent (QMouseEvent*) override;
    void mouseMoveEvent (QMouseEvent*) override;
    void mouseReleaseEvent (QMouseEvent*) override;
    void mouseDoubleClickEvent (QMouseEvent*) override;
    void wheelEvent (QWheelEvent*) override;
    void leaveEvent (QEvent*) override;
    void resizeEvent (QResizeEvent*) override;

private:
    struct Disc
    {
        QRectF area;       // the square the disc fills (before zoom)
        Projection projection;
        double zoom = 1.0;
        QString title;
        std::array<QString, 4> edges;   // right, top, left, bottom
    };

    void buildBeams (int order);
    void saveSettings() const;
    std::vector<Disc> discs() const;
    QRectF plotArea() const;
    QRectF panoramaArea() const;
    double levelAt (double azimuth, double elevation) const;   // smoothed, dB
    double floorDb() const { return topDb() - rangeDb; }
    double topDb() const;
    bool directionAt (QPointF, double& azimuth, double& elevation) const;
    void turn (double degreesAcross, double degreesUp);
    void zoomBy (double factor);

    struct Lookup   // where each pixel of a disc or the map looks on the grid
    {
        QSize size;
        QRectF visible;
        double radius = 0.0;
        Projection projection;
        std::vector<float> nearC, nearR, farC, farR, shade;
    };

    void paintDisc (QPainter&, const Disc&, int index);
    void paintPanorama (QPainter&);
    void paintHead (QPainter&);
    void paintEnergy (QPainter&);
    void paintScope (QPainter&);
    void paintReadout (QPainter&, const QRectF&);
    // Returns where its circle is (centre and radius; radius 0 when there was no room).
    std::pair<QPointF, double> paintPolar (QPainter&, const QRectF&, int plane, const QString& title, const std::array<QString, 4>& edges, bool head);
    void paintSpeakers (QPainter&);
    void paintHint (QPainter&);
    void emitScope (double dt);

    // The beams: K weights per direction, for the order they were made for.
    int beamOrder = 0;
    std::vector<float> beams;             // rows * cols * K
    std::vector<float> covariance;        // K * K
    std::array<std::vector<float>, 3> planeAngle, planeWeight;   // per direction: where it falls in each polar plot

    std::vector<float> target, shown;     // dB per direction
    int order = 0, fileOrderShown = 0, channelCount = 0, analyses = 0;
    double planeWaveSpread = 1.0;         // |rE| of one plane wave at this order
    static constexpr int numSpeakers = 11;
    std::array<double, numSpeakers> speakerPeak {}, speakerHold {};
    double diffuseness = 1.0, shownDiffuseness = 1.0;
    Vec energy, shownEnergy;              // the energy vector (rE), shorter when spread
    bool hasSignal = false;

    View current = View::sphere;
    double rangeDb = 24.0;
    bool relativeScale = true;
    int responseIndex = 1;
    int limit = maxOrder;
    bool showHeight = true;
    double cameraAzimuth = 180.0, cameraElevation = 28.0, cameraZoom = 1.0;
    double heading = 0.0;                 // the flat views: what is in front

    QPoint dragFrom;
    bool dragging = false;
    QPointF hover { -1.0, -1.0 };

    QImage pixels, scopeTrace;
    std::vector<Lookup> caches;
    double smoothedTop = 0.0;
};
