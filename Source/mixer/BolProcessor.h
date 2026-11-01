// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The Bol Processor (owner, 2026-10-31: "I would definitely want the full version ... with credit
// given to Bel/Kippen"). Bernard Bel and Jim Kippen's BP3 console (third_party/bolprocessor, BSD),
// built as phi-bp3, produces items from a grammar: a qāʿida's variations from Kippen and Bel's
// tablā grammars, or any other grammar's. This runs it and reads its items back: the bols, laid
// out in time by BP's polymetric notation (periods, speeds, {a, b} expressions), as a score on
// the tablā; or BP's own MIDI file when the grammar makes notes. Plain C++, no JUCE or Qt.
// docs/bol-processor.md.
#include "Compose.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bolprocessor
{
    // A grammar and the files it names in its header ("-se.dhin--", "-al.dhin--"), found beside it.
    struct Files
    {
        std::string grammar, alphabet, settings, soundObjects, tonality, csound;
    };
    Files companions (const std::string& grammarPath);

    // BP3's command line for producing items from `files`, with `seed` (0: BP's own), the items
    // written to `itemsOut` and the MIDI file to `midiOut`.
    std::vector<std::string> arguments (const Files&, uint32_t seed, const std::string& itemsOut, const std::string& midiOut);

    struct Run
    {
        bool ok = false;
        std::string items;               // the items as BP3 writes them, a blank line between
        std::vector<uint8_t> midi;       // its MIDI file (empty when the grammar makes no notes)
        std::string log;                 // what it said, errors included
    };
    // Runs `executable` (phi-bp3) in a folder of its own, with `strings` (console_strings.json)
    // copied there, for at most `seconds`; a data file's items are read without it.
    // A data file (-da.name, name.bpda): items already, read as they are rather than produced.
    bool isData (const std::string& path);
    Run run (const std::string& executable, const std::string& strings, const Files&, uint32_t seed, int seconds = 60);

    // An item laid out in time, in beats: each symbol and its length; '-' rests are not events and
    // '_' lengthens the symbol before it.
    struct Event
    {
        double beat = 0.0, length = 0.0;
        std::string symbol;
    };
    struct Item
    {
        std::string text;
        std::vector<Event> events;
        double beats = 0.0;
    };
    std::vector<std::string> splitItems (const std::string& output);
    Item parse (const std::string& item);

    // A bol's strokes as General MIDI drums: the dāyāṅ on the high congas and timbale, the bāyāṅ
    // open on the low conga and closed on the rim; dha is na with ge. Empty for what is no bol.
    std::vector<int> strokes (const std::string& bol);

    // What the bols play on: General MIDI drums (strokes()), PHI tabla's pads (sampler/PhiTabla.h),
    // or PHI frame drum's (sampler/PhiFrameDrum.h: dum, tak, ka, pa...), each stroke's takes in turn.
    enum class On { generalMidi, phiTabla, phiFrameDrum };
    // The items one after another as a score: one part, a section an item.
    composition::Result toScore (const std::vector<Item>&, double bpm, On on = On::generalMidi);
}
