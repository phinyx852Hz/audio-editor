// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The MIDI editor's notations (owner, 2026-10-29: "an optional microtonal staff notation ... based on
// several musical notation traditions, going beyond the western staff notation"): what a clip's
// notes are written in, beside the piano roll. Two kinds:
//
// - The staff, spelled by a Xen Tuner tuning config (core/XenTuner.h): any of Xen Tuner's library
//   (HEJI, HEWM, Sagittal, ups and downs, Kite, Fokker, Stein-Zimmermann, chromatic staves) or a
//   user's own file, and PHI's own configs for the traditions that write on the staff with
//   accidentals of their own: Arabic maqam's half-flats and half-sharps, Persian dastgah's koron and
//   sori, and ups and downs for any equal division Xen Tuner has no file for.
// - Cipher notation, a number or syllable a note with dots for the octave, as the traditions that
//   write that way do: sargam (Bhatkhande's) for ragas, kepatihan numbers for gamelan, and jianpu,
//   China's numbered notation.
//
// Each system's family has its tradition's notation (suggested()); any other can be picked. Plain
// C++, no Qt.
#include "NoteEditor.h"
#include "../core/XenTuner.h"

#include <string>
#include <vector>

namespace notation
{
    enum class Kind { staff, cipher };
    enum class CipherStyle
    {
        tradition,   // the tradition's names, with dots: sargam, kepatihan
        jianpu,      // 1 to 7 by the semitone above the tonic, ♯ and ♭ before: 1 ♭3 4 5 ♭7
        degrees      // the system's degrees counted from the tonic: 1 to n
    };

    struct Choice
    {
        Kind kind = Kind::staff;
        // The staff's config: "phi:<name>" for one of PHI's (builtins()), "edo:<n>" for ups and
        // downs in n equal steps, else a path in Xen Tuner's tunings folder ("heji/5 limit.txt"),
        // or a file's full path.
        std::string config = "phi:12-TET";
        CipherStyle cipher = CipherStyle::tradition;
        bool operator== (const Choice&) const = default;
    };

    // The notation a system's tradition writes in: cipher for ragas, gamelan and Chinese modes,
    // the staff for the rest, with half-flats for maqams, koron and sori for dastgahs, HEJI for
    // modes in just intonation, Sagittal or ups and downs for equal divisions. `tunings` is Xen
    // Tuner's folder, looked in for a Sagittal file of the division.
    Choice suggested (const noteedit::PitchSystem&, const std::string& tunings);

    // PHI's own configs, by name ("12-TET", "arabic quarter tones", "persian koron and sori").
    struct Builtin { std::string name, title, text; };
    const std::vector<Builtin>& builtins();
    // Ups and downs (Kite's arrows on the chain of fifths) for `divisions` equal steps to the octave.
    std::string upsAndDowns (int divisions);
    // The equal division the system is, else 0.
    int equalDivisions (const noteedit::PitchSystem&);

    // The config of a staff choice, read: from PHI's, generated, or from the file.
    bool load (const Choice&, const std::string& tunings, phi::xen::Config&, std::string& error);
    // What a choice is called, for the menu and the view's corner.
    std::string title (const Choice&);

    // A note in cipher notation: its name, dots above (periods up) or below, and the cents it is
    // off its degree (empty when on it).
    struct Cipher
    {
        std::string name;
        int period = 0;
        std::string off;
    };
    Cipher cipher (const noteedit::PitchSystem&, double pitch, CipherStyle);
}
