// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QFontMetricsF>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <vector>

class QPainter;

// Text whose digits all take the width of the widest one, so a changing
// number (a running time, a counter) never pushes what follows it sideways,
// whatever the font.
namespace steady
{
    qreal width (const QFontMetricsF&, const QString&);
    // Draws from `left`, on `baseline`.
    void draw (QPainter&, qreal left, qreal baseline, const QString&);
}

// The line of file details along the bottom of the editor: position and
// length, selection, format, play state and, on the right, the audio device.
// Each field keeps its place: it is as wide as the longest text it has shown,
// so nothing after it moves. No background of its own.
class InfoStrip final : public QWidget
{
    Q_OBJECT

public:
    explicit InfoStrip (QWidget* parent);

    // Fields from the left (an empty one keeps its space), and the right-hand text.
    void setFields (const QStringList& fields, const QString& right);
    QString text() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent (QPaintEvent*) override;
    void changeEvent (QEvent*) override;

private:
    QStringList fields;
    QString right;
    std::vector<qreal> reserved;   // widest text seen in each field
};
