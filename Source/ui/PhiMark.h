// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QPixmap>
#include <QToolButton>
#include <QWidget>

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

// The gold mark on a black tile.
class PhiMark final : public QWidget
{
public:
    explicit PhiMark (int height, QWidget* parent = nullptr);

protected:
    void paintEvent (QPaintEvent*) override;
};

// The Φ button used across the DAW to turn a golden-ratio option on: the compact
// white mark, a line of text high, so it takes no more room than a checkbox.
class PhiButton final : public QToolButton
{
public:
    explicit PhiButton (const QString& accessibleName, QWidget* parent = nullptr);
};
