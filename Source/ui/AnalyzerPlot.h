// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

#include <vector>

// A plot for the Plugin Analyzer: curves (or stems) over a log or linear axis, with
// labelled markers, a grid in the theme's colours and a readout under the mouse.
class AnalyzerPlot final : public QWidget
{
    Q_OBJECT

public:
    struct Series
    {
        QString name;
        std::vector<double> x, y;
        QColor colour;
        bool dashed = false;
        bool stems = false;   // a line from the bottom up to each point
    };

    struct Marker
    {
        double x = 0.0;
        QString label;
        QColor colour;
    };

    explicit AnalyzerPlot (QWidget* parent = nullptr);

    // x0..x1 on a log axis (Hz) or a linear one; y0..y1, or fitted to the data when `fitY`
    // (never narrower than `minimumSpan`, never outside y0..y1).
    void setAxes (bool logX, double x0, double x1, double y0, double y1, const QString& xUnit, const QString& yUnit,
                  bool fitY = false, double minimumSpan = 12.0);
    void setSeries (std::vector<Series>);
    void setMarkers (std::vector<Marker>);
    void setEmptyText (const QString&);   // shown while there is nothing to draw
    void clear();

    QSize sizeHint() const override { return { 520, 220 }; }
    QSize minimumSizeHint() const override { return { 200, 110 }; }

protected:
    void paintEvent (class QPaintEvent*) override;
    void mouseMoveEvent (class QMouseEvent*) override;
    void leaveEvent (class QEvent*) override;

private:
    QRectF plotArea() const;
    double toX (double v, const QRectF&) const;
    double toY (double v, const QRectF&) const;
    double fromX (double px, const QRectF&) const;
    void fit();

    bool logX = true, fitY = false;
    double x0 = 20.0, x1 = 20000.0, y0 = -60.0, y1 = 12.0, yLow = -60.0, yHigh = 12.0, minimumSpan = 12.0;
    QString xUnit, yUnit, emptyText;
    std::vector<Series> series;
    std::vector<Marker> markers;
    double mouseX = -1.0;
};
