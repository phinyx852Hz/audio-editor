// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The MIDI editor's staff notation (owner, 2026-10-29: "a microtonal staff notation ... paired with
// the microtonal MIDI editor", "that would replace the space where the MIDI editor takes ... the
// option to toggle between"): the clip's notes in the roll's place, written in the notation picked
// (mixer/Notation.h): on a grand staff, each pitch spelled by a Xen Tuner config with its
// accidentals in Bravura's SMuFL glyphs and the cents it is off them above it, or in cipher
// notation (sargam, kepatihan, jianpu) with the octave in dots. Time runs across in proportion, as
// the roll has it: each note's head at its start and a line to its end, which follows its pitch
// curve (a mīṇḍ, a gamak) up and down the staff.
//
// Click picks a note (Shift adds), and on empty staff moves the step cursor; double-click on the
// staff writes a note on that line or space, without accidentals. Ctrl+Up and Ctrl+Down move the
// picked notes to the next pitch the notation writes (Xen Tuner's up and down); the editor's own
// keys (Up and Down by the system's degrees, Delete, Q, the arrows) work here as in the roll.
// Ctrl and the wheel zoom in time.
#include "MidiEditor.h"
#include "../mixer/Notation.h"

#include <QWidget>

#include <map>
#include <memory>

class QScrollBar;

class MidiEditor::Staff final : public QWidget
{
public:
    explicit Staff (MidiEditor&);

    // What it writes in; `automatic` takes PHI's staff for each system (notation::suggested), or
    // with `eachTradition` the tradition's own (notation::traditional).
    void setNotation (const notation::Choice&, bool automatic);
    const notation::Choice& notation() const { return choice; }
    bool automatic() const { return isAutomatic; }
    bool eachTradition = false;
    bool flats = false;   // flats before sharps, where both spell a pitch
    // The notation in force for a system, and its title for the corner.
    notation::Choice choiceFor (const noteedit::PitchSystem&) const;
    QString titleFor (const noteedit::PitchSystem&) const;

    void showNotes();     // scrolled to the first note
    void refresh();

    // Xen Tuner's tunings folder, as installed or in the source tree.
    static QString tuningsFolder();

protected:
    void paintEvent (QPaintEvent*) override;
    void resizeEvent (QResizeEvent*) override;
    void mousePressEvent (QMouseEvent*) override;
    void mouseDoubleClickEvent (QMouseEvent*) override;
    void mouseMoveEvent (QMouseEvent*) override;
    void wheelEvent (QWheelEvent*) override;
    void keyPressEvent (QKeyEvent*) override;

private:
    const phi::xen::Speller* spellerFor (const noteedit::PitchSystem&) const;
    double xOf (int64_t tick) const;
    int64_t tickAt (double x) const;
    double yOf (double staff) const;      // a staff position (C0 0, middle C 28)
    double staffAt (double y) const;
    int noteAt (QPointF) const;           // the note there, else -1
    std::vector<size_t> topDown() const;  // the notes by start, a chord's highest first
    void layoutScroll();
    void fit();                           // half and highest, for the notes and the height
    void paintStaff (QPainter&, int width);
    void paintCipher (QPainter&, int width);
    QString describe (const noteedit::Note&) const;

    MidiEditor& ed;
    QScrollBar* scroll = nullptr;
    double pixelsPerTick = 0.12;
    double half = 5.0;                    // pixels a staff step: half a space
    double highest = 44.0;                // the staff position at the top of the view
    notation::Choice choice;
    bool isAutomatic = true;
    QString musicFamily;                  // Bravura, when it loaded
    // The spellers made, by the choice and the tonic they are anchored on.
    mutable std::map<std::string, std::unique_ptr<phi::xen::Speller>> spellers;
    mutable QString loadError;
};
