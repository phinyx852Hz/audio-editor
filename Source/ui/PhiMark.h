// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QPixmap>
#include <QElapsedTimer>
#include <QToolButton>
#include <QWidget>

#include <functional>
#include <string>

// The Φ mark: PHI's own capital phi (U+03A6), drawn from golden-ratio geometry.
//
// Origin: an original drawing, not taken or traced from any font or existing
// artwork, so no attribution is owed. Created on 24 October 2026 for PHI by its
// author (phinyx852Hz), who holds the copyright, with Claude (Anthropic) doing
// the drawing. How it was made, in units where the mark is 1000 tall:
//   - the ring is 618 wide (1000/φ) and 382 tall (618/φ), so the stem is φ² times the ring;
//   - the stem is 1000/φ⁵ ≈ 90 wide, and so is the ring at its sides, thinning
//     by 1/φ at its top and bottom like a pen stroke;
//   - the serifs are φ² stems wide and 1/φ² of a stem tall.
// The same shapes are in packaging/phi-mark-*.svg and packaging/phi.svg (see packaging/PHI-MARK.md).
//
// The mark is gold, on black. The compact mark, for buttons, is white: as tall as
// the ring is wide (a square, 618 by 618), so it fits a button without making it
// any bigger, and without serifs.
namespace phimark
{
    enum class Style { goldOnBlack, white, compact };

    QPixmap pixmap (int height, Style = Style::goldOnBlack, qreal devicePixelRatio = 1.0);

    void makePhiWindow (QWidget* window);              // a Φ plugin's window: black, with gold highlights

    bool isPhiPlugin (const std::string& pluginId);   // one of PHI's own Φ effects
    QString withoutPhi (const QString& name);          // "Φ EQ" -> "EQ", when the mark stands in for the letter
}

// The window shown while PHI starts, in the middle of the screen (not full screen): a
// golden rectangle 618 x 382, the gold mark in its square, "phī" and the version in the
// rest, and the golden spiral through it. It opens by spiralling out of a dot: turning
// back a quarter turn and growing by φ for each square of the spiral, as the golden
// rectangle is made of itself turned and shrunk by 1/φ. It stays at least φ² (2.618)
// seconds, and closes once the main window is up; a click closes it at once.
class PhiSplash final : public QWidget
{
public:
    PhiSplash();
    void play();                         // shows it and waits while it spirals open (1.618 s)
    void finish (QWidget* mainWindow);   // closes when the 2.618 seconds are over

    // Played when the splash opens, if set: the Φ synth's sound for the start (a new random
    // patch each time). Set it before the splash is shown.
    static std::function<void()>& startupSound();

protected:
    void paintEvent (QPaintEvent*) override;
    void mousePressEvent (QMouseEvent*) override;   // a click closes it at once

private:
    void paintContent (QPainter&) const;   // the splash as it ends, unturned
    QPointF pole() const;                  // where the golden spiral winds in to

    QSizeF card;       // the splash itself, 618 x 382
    QPointF offset;    // where it sits in the window, which has room around it to turn in
    QElapsedTimer shown;
    QPixmap content;
    double progress = 0.0;   // 0 a dot, 1 open
};

// The gold mark on a black tile.
class PhiMark final : public QWidget
{
public:
    explicit PhiMark (int height, QWidget* parent = nullptr);

protected:
    void paintEvent (QPaintEvent*) override;
};

// The Φ button used across the DAW: the compact white mark, a line of text high, so it
// takes no more room than a checkbox. A switch (a golden-ratio option, faded when off)
// or, with isSwitch false, a plain button (the Φ random selector).
class PhiButton final : public QToolButton
{
public:
    explicit PhiButton (const QString& accessibleName, QWidget* parent = nullptr, bool isSwitch = true);
};
