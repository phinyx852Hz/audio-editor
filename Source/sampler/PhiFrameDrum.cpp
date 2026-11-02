// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiFrameDrum.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <map>

namespace phiframedrum
{
    int gridPad (int row, int column)
    {
        const auto bank = (row >= 4 ? 2 : 0) + (column >= 4 ? 1 : 0);
        return bank * slicer::padsPerBank + (row % 4) * 4 + column % 4;
    }

    const std::vector<Pad>& pads()
    {
        // The owner's files, by their 2019 names (FrameDrumSamples7Aug19): OneShots-FD 01 to 30, less
        // 03_Bass3 and 04_Bass4 (the same recording as 02_Bass2); the QuarterNotePhrases, Fives,
        // Sevens and Fours&Eights loops at 120 BPM, less 06_balladi3 (the same as 05_balladi2).
        // The bottom half of Push's grid: a row a stroke. The top half: the loops and phrases.
        static const std::vector<Pad> all {
            // Row 1: dum, and the flat hand's pa.
            { gridPad (0, 0), "Bass1", "dum", { "bass-1" } },
            { gridPad (0, 1), "Bass2", "dum", { "bass-2" } },
            { gridPad (0, 2), "Bass5", "dum", { "bass-5" } },
            { gridPad (0, 3), "Bass6", "dum", { "bass-6" } },
            { gridPad (0, 4), "Bass7", "dum", { "bass-7" } },
            { gridPad (0, 5), "Bass8", "dum", { "bass-8" } },
            { gridPad (0, 6), "FlatHand1", "pa", { "flat-hand-1" } },
            { gridPad (0, 7), "FlatHand2", "pa", { "flat-hand-2" } },
            // Row 2: tak, a flat hand and the finger snaps.
            { gridPad (1, 0), "Rimshot1", "tak", { "rimshot-1" } },
            { gridPad (1, 1), "Rimshot2", "tak", { "rimshot-2" } },
            { gridPad (1, 2), "Rimshot3", "tak", { "rimshot-3" } },
            { gridPad (1, 3), "Rimshot4", "tak", { "rimshot-4" } },
            { gridPad (1, 4), "FlatHand3", "pa", { "flat-hand-3" } },
            { gridPad (1, 5), "FingerSnap1", "snap", { "finger-snap-1" } },
            { gridPad (1, 6), "FingerSnap2", "snap", { "finger-snap-2" } },
            { gridPad (1, 7), "FingerSnap3", "snap", { "finger-snap-3" } },
            // Row 3: ka, and the hand sweeps.
            { gridPad (2, 0), "SideKnock1", "ka", { "side-knock-1" } },
            { gridPad (2, 1), "SideKnock2", "ka", { "side-knock-2" } },
            { gridPad (2, 2), "SideKnock3", "ka", { "side-knock-3" } },
            { gridPad (2, 3), "SideKnock4", "ka", { "side-knock-4" } },
            { gridPad (2, 4), "SideKnock5", "ka", { "side-knock-5" } },
            { gridPad (2, 5), "HandSweep1", "sweep", { "hand-sweep-1" } },
            { gridPad (2, 6), "HandSweep2", "sweep", { "hand-sweep-2" } },
            { gridPad (2, 7), "HandSweep3", "sweep", { "hand-sweep-3" } },
            // Row 4: the rest of the sweeps.
            { gridPad (3, 0), "HandSweep4", "sweep", { "hand-sweep-4" } },
            { gridPad (3, 1), "HandSweep5", "sweep", { "hand-sweep-5" } },
            { gridPad (3, 2), "HandSweep6", "sweep", { "hand-sweep-6" } },
            { gridPad (3, 3), "HandSweep7", "sweep", { "hand-sweep-7" } },
            // Rows 5 to 7: the loops (fours and eights, fives, sevens) and the quarter-note phrases.
            { gridPad (4, 0), "mousmodi1", "", { "loop-maqsum-1" } },
            { gridPad (4, 1), "mousmodi2", "", { "loop-maqsum-2" } },
            { gridPad (4, 2), "balladi1", "", { "loop-baladi-1" } },
            { gridPad (4, 3), "balladi2", "", { "loop-baladi-2" } },
            { gridPad (4, 4), "balladi4", "", { "loop-baladi-4" } },
            { gridPad (4, 5), "chiftitelli", "", { "loop-chiftetelli" } },
            { gridPad (4, 6), "Fives1", "", { "loop-fives-1" } },
            { gridPad (4, 7), "Fives2", "", { "loop-fives-2" } },
            { gridPad (5, 0), "Fives x2", "", { "loop-fives-x2" } },
            { gridPad (5, 1), "Sevens", "", { "loop-sevens-1" } },
            { gridPad (5, 2), "Sevens2", "", { "loop-sevens-2" } },
            { gridPad (5, 3), "Sevens3", "", { "loop-sevens-3" } },
            { gridPad (5, 4), "quarters alt two fingers", "", { "phrase-quarters-alt-two-fingers" } },
            { gridPad (5, 5), "quarters three fingers", "", { "phrase-quarters-three-fingers" } },
            { gridPad (5, 6), "quarters four fingers", "", { "phrase-quarters-four-fingers" } },
            { gridPad (5, 7), "quarters x2 alt two fingers", "", { "phrase-quarters-x2-alt-two-fingers" } },
            { gridPad (6, 0), "quarters x2 one hand", "", { "phrase-quarters-x2-two-fingers-one-hand" } },
            { gridPad (6, 1), "triplet roll", "", { "phrase-triplet-roll" } },
            { gridPad (6, 2), "triplet roll alt two fingers", "", { "phrase-triplet-roll-alt-two-fingers" } },
            { gridPad (6, 3), "1-2-3-1-2-3-1-2", "", { "phrase-3-3-2" } },
            { gridPad (6, 4), "1-2-3-1-2-3-1-2 x2", "", { "phrase-3-3-2-x2" } },
        };
        return all;
    }

    std::string strokeOf (const std::string& syllable)
    {
        std::string s;
        for (char c : syllable)
            s += (char) std::tolower ((unsigned char) c);
        while (! s.empty() && (s.back() == '\'' || std::isdigit ((unsigned char) s.back())))
            s.pop_back();
        static const std::map<std::string, std::string> as {
            { "dum", "dum" }, { "doum", "dum" }, { "doom", "dum" }, { "dm", "dum" }, { "d", "dum" }, { "bass", "dum" },
            { "tak", "tak" }, { "tek", "tak" }, { "te", "tak" }, { "t", "tak" }, { "rim", "tak" }, { "rimshot", "tak" },
            { "ka", "ka" }, { "kah", "ka" }, { "k", "ka" }, { "knock", "ka" },
            { "pa", "pa" }, { "sak", "pa" }, { "slap", "pa" }, { "flat", "pa" },
            { "snap", "snap" }, { "sn", "snap" },
            { "sweep", "sweep" }, { "sh", "sweep" }, { "shh", "sweep" },
        };
        const auto f = as.find (s);
        return f == as.end() ? std::string() : f->second;
    }

    int padFor (const std::string& syllable, int use)
    {
        const auto stroke = strokeOf (syllable);
        if (stroke.empty())
            return -1;
        std::vector<int> takes;
        for (const auto& p : pads())
            if (stroke == p.stroke)
                takes.push_back (p.pad);
        if (takes.empty())
            return -1;
        const auto n = (int) takes.size();
        return takes[(size_t) ((use % n + n) % n)];
    }

    int noteFor (const std::string& syllable, int use)
    {
        const auto pad = padFor (syllable, use);
        return pad < 0 ? -1 : slicer::noteForPad (pad, slicer::Layout::push, 0);
    }

    std::string strokeForDrum (int note, int velocity)
    {
        switch (note)
        {
            case 35: case 36: case 41: case 43: case 45: case 61: case 64: case 66: case 58:
                return "dum";
            case 38: case 40: case 47: case 48: case 50: case 60: case 63: case 65:
                return velocity < 70 ? "ka" : "tak";
            case 62:
                return "pa";
            case 39:
                return "snap";
            case 49: case 52: case 55: case 57: case 69: case 70: case 82: case 54: case 73: case 74:
                return "sweep";
            default:
                return note >= 27 && note <= 87 ? "ka" : "";
        }
    }

    midifile::SongPtr fromGeneralMidi (const midifile::Song& in, int* moved)
    {
        auto out = std::make_shared<midifile::Song> (in);
        out->events.clear();
        out->channelsUsed = 0;
        std::map<std::string, int> uses;
        std::map<int, std::deque<int>> sounding;   // a drum note held: the pads' notes it was put on, in turn
        int count = 0;
        for (const auto& e : in.events)
        {
            const bool drum = e.channel() == 9 && (e.kind() == 0x90 || e.kind() == 0x80);
            if (! drum)
            {
                out->events.push_back (e);
                if (e.kind() == 0x90 || e.kind() == 0x80)
                    out->channelsUsed |= (uint16_t) (1u << e.channel());
                continue;
            }
            auto x = e;
            if (e.kind() == 0x90 && e.data2 > 0)
            {
                const auto stroke = strokeForDrum (e.data1, e.data2);
                const auto note = stroke.empty() ? -1 : noteFor (stroke, uses[stroke]++);
                if (note < 0)
                    continue;
                sounding[e.data1].push_back (note);
                x.data1 = (uint8_t) note;
                ++count;
            }
            else
            {
                auto& held = sounding[e.data1];
                if (held.empty())
                    continue;
                x.data1 = (uint8_t) held.front();
                held.pop_front();
            }
            out->events.push_back (x);
            out->channelsUsed |= (uint16_t) (1u << 9);
        }
        if (moved != nullptr)
            *moved = count;
        return out;
    }

    slicer::Kit kit (const std::string& folder, const std::function<sampler::SamplePtr (const std::string&)>& load, std::vector<std::string>* missing)
    {
        slicer::Kit k;
        k.layout = slicer::Layout::push;
        k.velocity = 0.8;
        k.velocityTone = 0.5;   // a soft hit darker as well as quieter, as PHI tabla
        k.voices = 32;
        for (const auto& p : pads())
        {
            const auto path = folder + "/" + p.sound.file + ".wav";
            const auto s = load (path);
            if (s == nullptr)
            {
                if (missing != nullptr)
                    missing->push_back (path);
                continue;
            }
            slicer::placeSound (k, p.pad, 0, s, path);
            auto& l = k.pads[(size_t) p.pad].layers[0];
            l.start = std::clamp<Frame> (p.sound.start, 0, s->length());
            l.end = p.sound.end < 0 ? s->length() : std::clamp<Frame> (p.sound.end, l.start, s->length());
            l.gainDb = p.sound.gainDb;
            auto& pad = k.pads[(size_t) p.pad];
            pad.name = p.name;
            const std::string stroke = p.stroke;
            pad.colour = stroke == "dum" ? 6 : stroke == "tak" ? 3 : stroke == "ka" ? 9 : stroke.empty() ? 12 : 1;
            pad.release = 0.08;
            // The bass rings for seconds: a dum stops the dum before it, and a loop the loop before.
            pad.choke = stroke == "dum" ? 1 : stroke.empty() ? 2 : 0;
        }
        return k;
    }
}
