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
// PHI's microtonal staff is the default for every system (owner, 2026-10-29), spelled by the
// config that fits it (suggested()); each tradition's own notation (traditional()) and any other
// can be picked beside it. Plain C++, no Qt.
#include "NotationGrid.h"
#include "NoteEditor.h"
#include "../core/XenTuner.h"

#include <array>
#include <string>
#include <vector>

namespace notation
{
    // The staff, PR #85's cipher line, and the beat-by-beat notations (NotationGrid.h).
    enum class Kind { staff, cipher, bhatkhande, carnatic, jianpu, angka, jeongganbo };
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
        Script script = Script::roman;   // the svaras' script (bhatkhande, carnatic)
        // The staff's key signature (LilyPond's maqams, makams and dastgahs, keySignatures()):
        // empty for the system's own when LilyPond has it, "none", or "<tradition>:<name>@<tonic>"
        // ("turkish:hicaz@a", the tonic a letter, with "s" or "f" after it for a sharp or flat).
        std::string key;
        bool operator== (const Choice&) const = default;
    };

    // PHI's staff for a system, the default: half-flats for maqams, koron and sori for dastgahs,
    // HEJI for modes in just intonation (ragas' shrutis among them), Sagittal or ups and downs for
    // equal divisions, sharps and flats on the keys. `tunings` is Xen Tuner's folder, looked in for
    // a Sagittal file of the division.
    Choice suggested (const noteedit::PitchSystem&, const std::string& tunings);
    // The notation the system's tradition writes in, picked beside the default: cipher for ragas
    // (sargam), gamelan (kepatihan) and Chinese modes (jianpu), else as suggested().
    Choice traditional (const noteedit::PitchSystem&, const std::string& tunings);

    // The notations, for the menus (owner, 2026-10-29: PHI's microtonal staff the default, every
    // other an option beside it): an id and a title, PHI's staff first. The Compose menu's New Score
    // In opens one of them (MidiEditor::Host::notation).
    struct System { std::string id, title; };
    const std::vector<System>& systems();
    // A notation by its id, for a system (the staff's config the one that fits it); false for an
    // id there is none of.
    bool fromId (const std::string& id, const noteedit::PitchSystem&, const std::string& tunings, Choice&);

    // Key signatures, LilyPond's (ly/arabic.ly, ly/turkish-makam.ly, ly/persian.ly, GPL-3.0-or-later,
    // read into KeySignatures.inc by scripts/lilypond-keys.py): what each letter C..B is altered by in
    // cents, for the mode on C; LilyPond moves it to the key's tonic.
    struct KeySignature
    {
        std::string tradition, name;   // "turkish", "hicaz"
        std::string tonic;             // the tonic LilyPond's file gives it ("a"), or empty
        std::array<double, 7> cents {};
    };
    const std::vector<KeySignature>& keySignatures();
    const KeySignature* findKeySignature (const std::string& tradition, const std::string& name);
    // The order a tradition's key signature is written in: the letters (C 0 .. B 6) and how much
    // they are altered by, flats first.
    const std::vector<std::pair<int, double>>& keyOrder (const std::string& tradition);
    // A key signature moved to a tonic (its letter, C 0 .. B 6, and what it is altered by): what
    // each letter is altered by in that key. `naturals` are the letters' cents above C: 12-TET's,
    // or the 53 commas' (Pythagorean) for Turkish.
    std::array<double, 7> transpose (const KeySignature&, int tonicLetter, double tonicCents, const std::array<double, 7>& naturals);
    std::array<double, 7> naturals (const std::string& tradition);
    // The key signature a staff choice writes for a system, as alterations of C..B (all 0: none).
    std::array<double, 7> keyFor (const Choice&, const noteedit::PitchSystem&);

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
