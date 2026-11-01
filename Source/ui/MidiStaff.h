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
//
// The beat-by-beat notations (owner, 2026-10-29; mixer/NotationGrid.h) are written in the same
// place: Bhatkhande's sargam and the Karnatak svaras a cycle a line under the tāl's signs, jianpu
// and not angka with their lines and dashes, jeongganbo in boxes down columns read right to left
// (in the TteoiJeonggan font). The staff for maqams, makams and dastgahs carries LilyPond's key
// signature. A right-click on a note gives it a lyric or a mark (a stroke, a technique, a
// sigimsae).
#include "MidiEditor.h"
#include "../mixer/Notation.h"

#include <QElapsedTimer>
#include <QWidget>

#include <map>
#include <memory>
#include <optional>
#include <tuple>

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
    // The svaras' script and the staff's key signature, when picked (else the choice's own).
    std::optional<notation::Script> script;
    std::optional<std::string> key;
    // The notation in force for a system, and its title for the corner.
    notation::Choice choiceFor (const noteedit::PitchSystem&) const;
    QString titleFor (const noteedit::PitchSystem&) const;

    void showNotes();     // scrolled to the first note
    void refresh();
    // The playhead moved (or the notes changed): while it plays, the view follows it (the page
    // slides under it once it is a third across, or the lines and columns turn with it) and is
    // drawn at most at the screen's rate, so the clock that sends the notes is not held up.
    void playheadMoved();

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
    void contextMenuEvent (QContextMenuEvent*) override;

private:
    const phi::xen::Speller* spellerFor (const noteedit::PitchSystem&) const;
    const phi::xen::Speller* makeSpeller (const noteedit::PitchSystem&) const;
    // A pitch's spelling, kept: spelling is slow and the staff spells every note at each frame.
    const phi::xen::Spelling& spelled (const phi::xen::Speller&, double pitch) const;
    bool followPlayhead();                // scrolled to keep the playhead in view; true if it moved
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
    // The beat-by-beat notations: the writing for the choice, laid out in lines or in columns.
    notation::Writing writing (const notation::Choice&) const;
    void paintLines (QPainter&, const notation::Choice&, const notation::Writing&, int width);
    void paintColumns (QPainter&, const notation::Writing&, int width);
    // The staff's key signature after the clefs (and the letters it alters, for the notes).
    void paintKey (QPainter&, const phi::xen::Speller&, const notation::Choice&, const std::array<double, 7>&);
    std::array<std::vector<phi::xen::Symbol>, 7> keySymbols (const phi::xen::Speller&, const std::array<double, 7>&) const;
    QString describe (const noteedit::Note&) const;

    MidiEditor& ed;
    QScrollBar* scroll = nullptr;
    QScrollBar* lines = nullptr;          // down the lines of a beat-by-beat writing
    double pixelsPerTick = 0.12;
    double half = 5.0;                    // pixels a staff step: half a space
    double highest = 44.0;                // the staff position at the top of the view
    notation::Choice choice;
    bool isAutomatic = true;
    QString musicFamily;                  // Bravura, when it loaded
    QString jeongganFamily;               // TteoiJeonggan, when it loaded
    double keyWidth = 0.0;                // the key signature's, after the clefs
    // Where the beat-by-beat writing put each note, for clicks and hovering.
    mutable std::vector<std::pair<QRectF, int>> hits;
    bool writingShown = false;            // the last paint was a beat-by-beat writing
    // The spellers made, by the choice and the tonic they are anchored on.
    mutable std::map<std::string, std::unique_ptr<phi::xen::Speller>> spellers;
    mutable QString loadError;
    // While a frame is painted: each system's speller, found once (choiceFor looks for files).
    mutable bool painting = false;
    mutable std::map<const noteedit::PitchSystem*, const phi::xen::Speller*> paintSpellers;
    mutable std::map<std::tuple<const phi::xen::Speller*, double, bool>, phi::xen::Spelling> spellings;
    // How the last beat-by-beat writing was laid out, for following the playhead down its lines
    // (or across its columns).
    struct GridLayout
    {
        bool shown = false, columns = false;
        int64_t start = 0, beat = 1, first = 0;   // the writing's start and beat; its first line (column)
        int per = 1;                              // beats a line (a column)
        double top = 0.0, step = 1.0;             // the first line's top; a line's height (a column's width)
    } grid;
    QElapsedTimer sinceFrame;             // since the last frame was painted
    bool following = false;               // the view is being scrolled to the playhead
};
