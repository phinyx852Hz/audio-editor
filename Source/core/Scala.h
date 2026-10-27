// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Scala tuning files (owner, 2026-10-26: "Scala looks amazing, but I will not be able to include
// it"): PHI's own reader and writer for the .scl scale and .kbm keyboard mapping formats, written
// from the published format descriptions (huygens-fokker.org/scala/scl_format.html and the
// keyboard mapping section of the Scala help). No Scala code is used or shipped. Plain C++ in
// phi_dsp, beside the tunings, so the PHI synth can read them too.
//
//     ! meantone.scl
//     !
//     1/4-comma meantone scale
//      12
//     !
//      76.04900
//      193.15686
//      ...
//      2/1
//
// A pitch with a period in it is in cents; otherwise it is a ratio ("3/2") or a whole number ("2").
// The last pitch is the period the scale repeats at (usually 2/1, the octave).
#include "PhiTuning.h"

#include <string>
#include <vector>

namespace phi::scala
{
    struct Scale
    {
        std::string description;
        // The degrees above the tonic in cents, the tonic itself (0) left out, so the last is the
        // period. As the file has them (unsorted, a degree may be negative).
        std::vector<double> cents;
        // Each as it was written ("3/2", "701.955"), kept so a ratio is written back as a ratio.
        // Empty, or the same size as cents.
        std::vector<std::string> text;

        double period() const { return cents.empty() ? 1200.0 : cents.back(); }
        int size() const { return (int) cents.size(); }
        // The degrees within the period from the tonic (0 first), sorted: what a piano roll lays out.
        std::vector<double> degrees() const;
    };

    // A keyboard mapping: which key plays which degree, and the key and frequency it is all tuned from.
    struct KeyboardMap
    {
        int size = 0;            // keys in the repeating pattern; 0 is a linear map, a degree a key
        int firstKey = 0, lastKey = 127;   // retuned; the keys outside keep 12-TET
        int middleKey = 60;      // plays the tonic (degree 0)
        int referenceKey = 69;   // tuned to referenceHz
        double referenceHz = 440.0;
        int octaveDegree = 0;    // the degree a pattern's repeat moves by; 0: the scale's own size
        std::vector<int> mapping;   // a degree for each key of the pattern, -1 for "x" (not played)
    };

    // Each fills `out` and says false (with the reason in `error`) when the text is not a valid file.
    bool parseScl (const std::string& text, Scale& out, std::string& error);
    bool parseKbm (const std::string& text, KeyboardMap& out, std::string& error);
    bool readScl (const std::string& path, Scale& out, std::string& error);
    bool readKbm (const std::string& path, KeyboardMap& out, std::string& error);

    // `name` goes in the first comment line, as Scala writes the file's name there.
    std::string writeScl (const Scale&, const std::string& name = {});
    std::string writeKbm (const KeyboardMap&, const std::string& name = {});
    bool saveText (const std::string& path, const std::string& text, std::string& error);

    // A scale from its degrees in cents within the period (the tonic, 0, may be there or not).
    Scale fromCents (const std::vector<double>& degrees, double periodCents = 1200.0, const std::string& description = {});
    // The usual keyboard map for a scale: linear, the tonic on `middleKey` at its 12-TET pitch.
    KeyboardMap linearMap (int middleKey = 60);

    // The frequency the map gives `key`, or 0 when it plays no note ("x", or outside first..last).
    double keyHz (const Scale&, const KeyboardMap&, int key);
    // Every key tuned as the map has it; keys it does not play keep 12-TET.
    Tuning toTuning (const Scale&, const KeyboardMap&);
}
