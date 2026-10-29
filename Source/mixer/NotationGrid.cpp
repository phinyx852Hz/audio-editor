// SPDX-License-Identifier: AGPL-3.0-only
// The conventions are the traditions'; where a rule or table comes from one of the programs named
// in NotationGrid.h it says so. Jianpu's durations follow jianpu-ly (Copyright Silas S. Brown,
// Apache License 2.0: its NOTICE is kept in third_party/README.md).
#include "NotationGrid.h"

#include "Rhythms.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

using namespace noteedit;

namespace notation
{
    namespace
    {
        int mod (int a, int b) { return ((a % b) + b) % b; }
        int64_t floorDiv (int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

        // Digits in the scripts that have their own (Bhatkhande's tālī numbers in Hindi books are
        // Devanagari's).
        std::string digits (const std::string& s, Script script)
        {
            static const char* deva[10] { "०", "१", "२", "३", "४", "५", "६", "७", "८", "९" };
            static const char* beng[10] { "০", "১", "২", "৩", "৪", "৫", "৬", "৭", "৮", "৯" };
            if (script != Script::devanagari && script != Script::bengali)
                return s;
            std::string out;
            for (const auto c : s)
                out += c >= '0' && c <= '9' ? (script == Script::devanagari ? deva : beng)[c - '0'] : std::string (1, c);
            return out;
        }

        std::string subscript (int n)
        {
            static const char* sub[10] { "₀", "₁", "₂", "₃", "₄", "₅", "₆", "₇", "₈", "₉" };
            std::string out;
            for (const auto c : std::to_string (n))
                out += sub[c - '0'];
            return out;
        }

        // A note in the system it starts in: its semitone above the tonic (0..11), the periods up or
        // down, its degree and the cents it is off the degree.
        struct Placed { int semitone = 0, period = 0, degree = 0; double off = 0.0, degreeCents = 0.0; };
        Placed place (const PitchSystem& s, double pitch)
        {
            Placed p;
            if (s.size() == 0)
            {
                const auto rel = pitch - s.tonic;
                const auto semis = (int) std::lround (rel / 100.0);
                p.semitone = mod (semis, 12);
                p.period = (int) floorDiv (semis, 12);
                p.off = rel - semis * 100.0;
                p.degreeCents = p.semitone * 100.0;
                return p;
            }
            const auto step = s.nearestStep (pitch);
            p.degree = s.degreeOf (step);
            p.period = s.periodOf (step);
            p.degreeCents = s.degrees[(size_t) p.degree];
            p.semitone = mod ((int) std::lround (p.degreeCents / 100.0), 12);
            p.off = pitch - s.pitchOf (step);
            return p;
        }

        std::string offText (double off) { return std::abs (off) >= 0.5 ? centsText (off) : std::string(); }

        //======================================================================
        // The beats, each cut into as many equal parts as its notes need: a note, a hold of the note
        // before, or a rest in each part (as Bhatkhande, the Karnatak books and jeongganbo write a
        // beat: its svaras share it equally).
        struct Slot { Sign::Kind kind = Sign::Kind::rest; int note = -1; int64_t tick = 0; };
        struct RawBeat { int64_t tick = 0, length = 0; std::vector<Slot> slots; };

        std::vector<RawBeat> rawBeats (const Sequence& seq, const Counting& c, int64_t from, int64_t to)
        {
            const auto mel = melody (seq);
            // Each melody note's end, cut at the next one's start.
            std::vector<int64_t> ends (mel.size());
            for (size_t i = 0; i < mel.size(); ++i)
            {
                const auto& n = seq.notes[(size_t) mel[i]];
                ends[i] = i + 1 < mel.size() ? std::min (n.end(), seq.notes[(size_t) mel[i + 1]].start) : n.end();
            }
            const auto soundingAt = [&] (int64_t t) -> int
            {
                // The melody note sounding at t (started at or before it), else -1.
                auto it = std::upper_bound (mel.begin(), mel.end(), t, [&] (int64_t x, int i) { return x < seq.notes[(size_t) i].start; });
                if (it == mel.begin())
                    return -1;
                const auto k = (size_t) (it - mel.begin() - 1);
                return ends[k] > t ? (int) k : -1;
            };

            std::vector<RawBeat> out;
            const auto beat = std::max<int64_t> (1, c.beat);
            auto t = c.start + floorDiv (from - c.start, beat) * beat;
            for (; t < to; t += beat)
            {
                if (t + beat <= 0)
                    continue;
                RawBeat b { t, beat, {} };
                // The times things happen in it: starts, and ends that leave a real silence.
                std::vector<int64_t> times;
                for (size_t k = 0; k < mel.size(); ++k)
                {
                    const auto s = seq.notes[(size_t) mel[k]].start;
                    if (s >= t && s < t + beat)
                        times.push_back (s - t);
                    const auto e = ends[k];
                    const auto next = k + 1 < mel.size() ? seq.notes[(size_t) mel[k + 1]].start : INT64_MAX;
                    if (e > t && e < t + beat && std::min (next, t + beat) - e >= beat / 8)
                        times.push_back (e - t);
                }
                // The fewest equal parts that hold them all, within a 48th of the beat.
                int parts = 8;
                for (const int n : { 1, 2, 3, 4, 6, 8 })
                    if (std::all_of (times.begin(), times.end(), [&] (int64_t x)
                    {
                        const auto unit = (double) beat / n;
                        return std::abs (x - std::round (x / unit) * unit) <= beat / 48.0;
                    }))
                    {
                        parts = n;
                        break;
                    }
                std::vector<int> startsAt ((size_t) parts, -1);
                for (size_t k = 0; k < mel.size(); ++k)
                {
                    const auto s = seq.notes[(size_t) mel[k]].start;
                    if (s >= t && s < t + beat)
                    {
                        const auto p = std::min<int64_t> (parts - 1, std::llround ((s - t) * parts / (double) beat));
                        startsAt[(size_t) p] = (int) k;   // the last at a part wins: the shorter ones before are grace notes
                    }
                }
                for (int p = 0; p < parts; ++p)
                {
                    const auto at = t + beat * p / parts;
                    Slot s;
                    s.tick = at;
                    if (startsAt[(size_t) p] >= 0)
                    {
                        s.kind = Sign::Kind::note;
                        s.note = mel[(size_t) startsAt[(size_t) p]];
                    }
                    else if (const auto k = soundingAt (at + beat / (parts * 4)); k >= 0)
                    {
                        s.kind = Sign::Kind::hold;
                        s.note = mel[(size_t) k];
                    }
                    b.slots.push_back (s);
                }
                out.push_back (std::move (b));
            }
            return out;
        }

        // Each beat's place in the cycle: its section's start and sign.
        void markCycle (Writing& w, const Counting& c, Script script, bool carnaticSigns)
        {
            for (auto& b : w.beats)
            {
                const auto i = mod ((int) floorDiv (b.tick - c.start, std::max<int64_t> (1, c.beat)), c.beats);
                b.cycleStart = i == 0;
                int at = 0;
                for (size_t k = 0; k < c.sections.size(); ++k)
                {
                    if (i == at)
                    {
                        b.sectionStart = true;
                        auto mark = k < c.marks.size() ? c.marks[k] : std::string();
                        if (carnaticSigns)
                        {
                            // The aṅgas' signs: laghu I with its count, drutam O, anudrutam U.
                            if (mark == "laghu")
                                mark = "I" + subscript (c.sections[k]);
                            else if (mark == "drutam")
                                mark = "O";
                            else if (mark == "anudrutam")
                                mark = "U";
                        }
                        else if (mark == "x" || mark == "X")
                            mark = "×";
                        b.sign = digits (mark, script);
                    }
                    at += c.sections[k];
                }
            }
        }

        // The note's lyric on its first sign; a hold of a note with words carries ऽ, the syllable drawn out.
        void words (Sign& s, const Note& n, bool first, const char* drawnOut)
        {
            if (first)
                s.lyric = n.lyric;
            else if (! n.lyric.empty() && drawnOut != nullptr)
                s.lyric = drawnOut;
        }
    }

    const char* scriptName (Script s)
    {
        switch (s)
        {
            case Script::roman: return "Roman";
            case Script::devanagari: return "Devanagari";
            case Script::bengali: return "Bengali";
            case Script::kannada: return "Kannada";
            case Script::tamil: return "Tamil";
            case Script::telugu: return "Telugu";
            case Script::malayalam: return "Malayalam";
        }
        return "";
    }

    Counting counting (const Sequence& seq, int64_t tick)
    {
        Counting c;
        c.beat = seq.ticksPerQuarter;
        c.legend = "4/4";
        const auto* cycle = cycleAt (seq, tick);
        if (cycle == nullptr)
            return c;
        const auto r = rhythm::findRhythm (cycle->rhythm);
        if (r < 0)
            return c;
        const auto& rh = rhythm::rhythms()[(size_t) r];
        c.start = cycle->tick;
        c.beat = cycle->beat > 0 ? cycle->beat : seq.ticksPerQuarter;
        c.beats = std::max (1, rh.beats);
        c.sections = rh.sections.empty() ? std::vector<int> { c.beats } : rh.sections;
        c.marks = rh.marks;
        c.legend = rh.name + ": " + std::to_string (c.beats);
        for (size_t k = 0; k < c.sections.size(); ++k)
            c.legend += (k == 0 ? ", " : "+") + std::to_string (c.sections[k]);
        return c;
    }

    std::vector<int> melody (const Sequence& seq)
    {
        std::map<int64_t, int> top;
        for (size_t i = 0; i < seq.notes.size(); ++i)
        {
            const auto& n = seq.notes[i];
            if (n.channel == 9)
                continue;
            auto [it, fresh] = top.emplace (n.start, (int) i);
            if (! fresh && n.pitch > seq.notes[(size_t) it->second].pitch)
                it->second = (int) i;
        }
        std::vector<int> out;
        for (const auto& [t, i] : top)
            out.push_back (i);
        return out;
    }

    Ornament ornamentOf (const Note& n)
    {
        Ornament o;
        if (n.curve.size() < 2 || n.length <= 0)
            return o;
        // A glide: the curve ends far from the note, late in it.
        const auto& last = n.curve.back();
        if (std::abs (last.second) >= 50.0 && last.first >= n.length / 3)
            o.glide = true;
        // A kaṇ: it starts off the note and comes to it within the first quarter.
        const auto& first = n.curve.front();
        if (first.first <= n.length / 16 && std::abs (first.second) >= 50.0 && std::abs (n.bendAt (n.length / 4)) < 20.0)
            o.grace = first.second;
        // A shake: the curve crosses back and forth more than twice.
        int turns = 0;
        double was = 0.0;
        for (const auto& [t, c] : n.curve)
        {
            if (std::abs (c) < 10.0)
                continue;
            if (was != 0.0 && (c > 0) != (was > 0))
                ++turns;
            was = c;
        }
        o.shake = turns >= 3;
        return o;
    }

    //==========================================================================
    // Hindustani: Bhatkhande's svaras (Swara Notebook's sargam-spec: S r R g G m M P d D n N, the
    // lower case komal, M tīvra).
    Svara hindustani (int semitone, Script script)
    {
        static const int letter[12] { 0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6 };
        static const bool komal[12] { false, true, false, true, false, false, false, false, true, false, true, false };
        static const char* names[][7] {
            { "S", "R", "G", "M", "P", "D", "N" },
            { "सा", "रे", "ग", "म", "प", "ध", "नि" },
            { "সা", "রে", "গা", "মা", "পা", "ধা", "নি" },
            { "ಸ", "ರಿ", "ಗ", "ಮ", "ಪ", "ಧ", "ನಿ" },
            { "ஸ", "ரி", "க", "ம", "ப", "த", "நி" },
            { "స", "రి", "గ", "మ", "ప", "ధ", "ని" },
            { "സ", "രി", "ഗ", "മ", "പ", "ധ", "നി" },
        };
        semitone = mod (semitone, 12);
        Svara s;
        s.name = names[(int) script][letter[semitone]];
        s.komal = komal[semitone];
        s.tivra = semitone == 6;
        return s;
    }

    std::vector<int> karnatakLetters (const std::vector<int>& semis)
    {
        // A semitone's commonest name: R₁ R₂ G₂ G₃ M₁ M₂ P D₁ D₂ N₂ N₃.
        static const int common[12] { 0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6 };
        // The semitones each letter can take: R 1..3, G 2..4, M 5..6, D 8..10, N 9..11.
        const auto fits = [] (int letter, int s)
        {
            switch (letter)
            {
                case 0: return s == 0;
                case 1: return s >= 1 && s <= 3;
                case 2: return s >= 2 && s <= 4;
                case 3: return s == 5 || s == 6;
                case 4: return s == 7;
                case 5: return s >= 8 && s <= 10;
                case 6: return s >= 9 && s <= 11;
                default: return false;
            }
        };
        std::vector<int> out (semis.size());
        bool inOrder = semis.size() == 7;
        for (size_t i = 0; inOrder && i < semis.size(); ++i)
            inOrder = fits ((int) i, semis[i]);
        for (size_t i = 0; i < semis.size(); ++i)
            out[i] = inOrder ? (int) i : common[mod (semis[i], 12)];
        return out;
    }

    std::string karnatak (int semitone, int letter, Script script)
    {
        static const char* names[][7] {
            { "S", "R", "G", "M", "P", "D", "N" },
            { "स", "रि", "ग", "म", "प", "ध", "नि" },
            { "স", "রি", "গ", "ম", "প", "ধ", "নি" },
            { "ಸ", "ರಿ", "ಗ", "ಮ", "ಪ", "ದ", "ನಿ" },
            { "ஸ", "ரி", "க", "ம", "ப", "த", "நி" },
            { "స", "రి", "గ", "మ", "ప", "ద", "ని" },
            { "സ", "രി", "ഗ", "മ", "പ", "ധ", "നി" },
        };
        // The variant number: the semitone counted from the letter's lowest.
        static const int lowest[7] { 0, 1, 2, 5, 7, 8, 9 };
        letter = std::clamp (letter, 0, 6);
        std::string name = names[(int) script][letter];
        if (letter != 0 && letter != 4)
            name += subscript (mod (semitone, 12) - lowest[letter] + 1);
        return name;
    }

    Writing bhatkhande (const Sequence& seq, const PitchSystem& before, Script script, int64_t from, int64_t to)
    {
        Writing w;
        const auto c = counting (seq, from);
        w.legend = c.legend;
        w.start = c.start;
        w.beat = c.beat;
        w.beatsPerLine = c.beats <= 16 ? c.beats : 16;
        w.holdSign = "ऽ";
        std::vector<bool> lyricSeen (seq.notes.size(), false);
        for (const auto& raw : rawBeats (seq, c, from, to))
        {
            Beat b;
            b.tick = raw.tick;
            b.length = raw.length;
            for (const auto& slot : raw.slots)
            {
                Sign s;
                s.kind = slot.kind;
                s.tick = slot.tick;
                if (slot.kind == Sign::Kind::rest)
                    s.text = "–";
                else if (slot.kind == Sign::Kind::hold)
                {
                    s.text = "ऽ";
                    words (s, seq.notes[(size_t) slot.note], false, "ऽ");
                }
                else
                {
                    const auto& n = seq.notes[(size_t) slot.note];
                    const auto& sys = systemAt (seq, before, n.start);
                    const auto p = place (sys, n.pitch);
                    const auto sv = hindustani (p.semitone, script);
                    s.text = sv.name;
                    s.lowered = sv.komal;
                    s.raised = sv.tivra;
                    s.octave = p.period;
                    s.off = offText (p.off);
                    s.note = slot.note;
                    s.mark = n.mark;
                    words (s, n, ! lyricSeen[(size_t) slot.note], nullptr);
                    lyricSeen[(size_t) slot.note] = true;
                    const auto o = ornamentOf (n);
                    s.glide = o.glide;
                    if (o.grace != 0.0)
                        s.grace = hindustani (place (sys, n.pitch + o.grace).semitone, script).name;
                }
                b.signs.push_back (std::move (s));
            }
            b.grouped = b.signs.size() > 1;
            w.beats.push_back (std::move (b));
        }
        markCycle (w, c, script, false);
        return w;
    }

    Writing carnatic (const Sequence& seq, const PitchSystem& before, Script script, int64_t from, int64_t to)
    {
        Writing w;
        const auto c = counting (seq, from);
        w.legend = c.legend;
        w.start = c.start;
        w.beat = c.beat;
        w.beatsPerLine = c.beats <= 16 ? c.beats : 16;
        w.holdSign = ",";
        w.doubleBarAtCycle = true;
        // Each system's letters, worked out once.
        std::map<const PitchSystem*, std::vector<int>> letters;
        const auto letterOf = [&] (const PitchSystem& sys, const Placed& p)
        {
            auto [it, fresh] = letters.emplace (&sys, std::vector<int>());
            if (fresh)
            {
                std::vector<int> semis;
                for (const auto d : sys.degrees)
                    semis.push_back (mod ((int) std::lround (d / 100.0), 12));
                it->second = karnatakLetters (semis);
            }
            if (sys.size() > 0 && (size_t) p.degree < it->second.size())
                return it->second[(size_t) p.degree];
            return karnatakLetters ({ p.semitone }).front();
        };
        std::vector<bool> lyricSeen (seq.notes.size(), false);
        for (const auto& raw : rawBeats (seq, c, from, to))
        {
            Beat b;
            b.tick = raw.tick;
            b.length = raw.length;
            // The speed: a line over for two to the akṣara, two for four, three for eight.
            const auto parts = (int) raw.slots.size();
            const auto speed = parts >= 8 ? 3 : parts >= 4 ? 2 : parts >= 2 ? 1 : 0;
            for (const auto& slot : raw.slots)
            {
                Sign s;
                s.kind = slot.kind;
                s.tick = slot.tick;
                s.lines = speed;
                if (slot.kind == Sign::Kind::rest)
                    s.text = "–";
                else if (slot.kind == Sign::Kind::hold)
                {
                    s.text = ",";
                    words (s, seq.notes[(size_t) slot.note], false, nullptr);
                }
                else
                {
                    const auto& n = seq.notes[(size_t) slot.note];
                    const auto& sys = systemAt (seq, before, n.start);
                    const auto p = place (sys, n.pitch);
                    s.text = karnatak (p.semitone, letterOf (sys, p), script);
                    s.octave = p.period;
                    s.off = offText (p.off);
                    s.note = slot.note;
                    s.mark = n.mark;
                    words (s, n, ! lyricSeen[(size_t) slot.note], nullptr);
                    lyricSeen[(size_t) slot.note] = true;
                    s.glide = ornamentOf (n).glide;
                }
                b.signs.push_back (std::move (s));
            }
            w.beats.push_back (std::move (b));
        }
        markCycle (w, c, script, true);
        return w;
    }

    //==========================================================================
    // Jianpu, as jianpu-ly writes it: a beat a crotchet, a note held a whole beat more a dash, the
    // parts of a beat with lines under by their length (a quaver one, a semiquaver two, a
    // demisemiquaver three), a dot after for half again (jianpu-ly's typesDot), the beams restarted
    // every beat ("jianpu printouts tend to restart beams every beat").
    Writing jianpu (const Sequence& seq, const PitchSystem& before, bool angka, int64_t from, int64_t to)
    {
        Writing w;
        const auto c = counting (seq, from);
        w.legend = c.legend;
        w.start = c.start;
        w.beat = c.beat;
        w.beatsPerLine = c.beats <= 16 ? c.beats : 16;
        w.holdSign = angka ? "·" : "–";
        static const char* numbers[12] { "1", "1", "2", "3", "3", "4", "4", "5", "6", "6", "7", "7" };
        static const char* sharpFlat[12] { "", "♯", "", "♭", "", "", "♯", "", "♭", "", "♭", "" };
        std::vector<bool> lyricSeen (seq.notes.size(), false);
        for (const auto& raw : rawBeats (seq, c, from, to))
        {
            Beat b;
            b.tick = raw.tick;
            b.length = raw.length;
            const auto parts = (int) raw.slots.size();
            // A whole beat held: the hold sign alone.
            if (parts == 1 && raw.slots.front().kind == Sign::Kind::hold)
            {
                Sign s;
                s.kind = Sign::Kind::hold;
                s.text = w.holdSign;
                s.tick = raw.tick;
                words (s, seq.notes[(size_t) raw.slots.front().note], false, nullptr);
                b.signs.push_back (s);
                w.beats.push_back (std::move (b));
                continue;
            }
            // Each sign with the holds after it in the beat: its length in parts.
            for (int p = 0; p < parts;)
            {
                const auto& slot = raw.slots[(size_t) p];
                int length = 1;
                while (p + length < parts && raw.slots[(size_t) (p + length)].kind == (slot.kind == Sign::Kind::rest ? Sign::Kind::rest : Sign::Kind::hold)
                       && (slot.kind == Sign::Kind::rest || raw.slots[(size_t) (p + length)].note == slot.note))
                    ++length;
                Sign s;
                s.kind = slot.kind;
                s.tick = slot.tick;
                // Its length as a fraction of the beat: n / parts, in quavers' lines and dots.
                const auto frac = (double) length / parts;
                int lines = 0;
                int dots = 0;
                if (frac < 1.0 - 1e-6)
                {
                    // Dotted (a quaver and a half: one line and a dot), else the lines of the plain
                    // length at or below it (a triplet quaver one line, as it is written).
                    const auto undotted = frac / 1.5;
                    const auto l = -std::log2 (undotted);
                    if (std::abs (l - std::round (l)) < 1e-6 && std::round (l) >= 1.0)
                    {
                        lines = (int) std::round (l);
                        dots = 1;
                    }
                    else
                        lines = std::max (1, (int) std::floor (-std::log2 (frac) + 1e-9));
                }
                s.lines = lines;
                s.dots = dots;
                if (slot.kind == Sign::Kind::rest)
                    s.text = "0";
                else if (slot.kind == Sign::Kind::hold)
                {
                    // A part of a beat held from the beat before: the dot that lengthens it.
                    s.text = "·";
                    s.lines = 0;
                    s.dots = 0;
                }
                else
                {
                    const auto& n = seq.notes[(size_t) slot.note];
                    const auto& sys = systemAt (seq, before, n.start);
                    const auto pl = place (sys, n.pitch);
                    if (angka && sys.size() > 0)
                    {
                        // Not angka: the system's own names (kepatihan numbers for gamelan).
                        auto name = (size_t) pl.degree < sys.names.size() ? sys.names[(size_t) pl.degree] : std::string();
                        if (name.empty())
                        {
                            name = label (sys, sys.nearestStep (n.pitch) - pl.period * sys.size(), Naming::tradition);
                            // label() puts the octave after a name it does not dot; the name alone.
                            if (const auto space = name.find (' '); space != std::string::npos)
                                name.resize (space);
                            // Combining dots it added go: the view draws its own.
                            for (const char* d : { "\xCC\x87", "\xCC\xA3" })
                                for (auto at = name.find (d); at != std::string::npos; at = name.find (d))
                                    name.erase (at, 2);
                        }
                        if (name.empty())
                            name = std::to_string (pl.degree + 1);
                        s.text = name;
                    }
                    else
                    {
                        s.text = numbers[pl.semitone];
                        s.before = sharpFlat[pl.semitone];
                    }
                    s.octave = pl.period;
                    s.off = offText (angka ? pl.off : pl.degreeCents + pl.off - 100.0 * std::lround (pl.degreeCents / 100.0));
                    s.note = slot.note;
                    s.mark = n.mark;
                    words (s, n, ! lyricSeen[(size_t) slot.note], nullptr);
                    lyricSeen[(size_t) slot.note] = true;
                    const auto o = ornamentOf (n);
                    s.glide = o.glide;
                    if (o.grace != 0.0)
                        s.grace = numbers[place (sys, n.pitch + o.grace).semitone];
                }
                b.signs.push_back (std::move (s));
                p += length;
            }
            b.grouped = b.signs.size() > 1;
            w.beats.push_back (std::move (b));
        }
        markCycle (w, c, Script::roman, false);
        for (auto& b : w.beats)
            if (b.sign == "×" || b.sign == "?")
                b.sign.clear();
        return w;
    }

    //==========================================================================
    // Jeongganbo. The yulmyeong and their octave forms are Jeongganbo Editor's (Dongha Hwang,
    // jgb_editor/pitch_name.py, GPL-3.0), in Source/mixer/Jeongganbo.inc.
    namespace
    {
#define PHI_YUL(name, minus2, minus1, middle, plus1, plus2) { minus2, minus1, middle, plus1, plus2 },
#define PHI_SIGIMSAE(key, text, where)
        const char* const yul[12][5] = {
#include "Jeongganbo.inc"
        };
#undef PHI_YUL
#undef PHI_SIGIMSAE
    }

    const std::vector<Sigimsae>& sigimsae()
    {
#define PHI_YUL(name, minus2, minus1, middle, plus1, plus2)
#define PHI_SIGIMSAE(key, text, where) Sigimsae { key, text, std::string (where) == "after" },
        static const std::vector<Sigimsae> all {
#include "Jeongganbo.inc"
        };
#undef PHI_YUL
#undef PHI_SIGIMSAE
        return all;
    }

    std::string yulmyeong (double pitch, int& octave)
    {
        // Hwangjong at E♭4 (MIDI 63), as the National Gugak Center tunes it.
        const auto steps = (int) std::lround ((pitch - 6300.0) / 100.0);
        const auto lu = mod (steps, 12);
        octave = (int) floorDiv (steps, 12);
        return yul[lu][std::clamp (octave, -2, 2) + 2];
    }

    Writing jeongganbo (const Sequence& seq, const PitchSystem&, int64_t from, int64_t to)
    {
        Writing w;
        const auto c = counting (seq, from);
        w.legend = c.legend;
        w.start = c.start;
        w.beat = c.beat;
        w.layout = Writing::columns;
        w.beatsPerLine = c.beats;
        w.holdSign = "－";
        std::vector<bool> lyricSeen (seq.notes.size(), false);
        for (const auto& raw : rawBeats (seq, c, from, to))
        {
            Beat b;
            b.tick = raw.tick;
            b.length = raw.length;
            const auto parts = (int) raw.slots.size();
            // A box's parts: up to three rows, then rows of two or three across (as Jeonggan's
            // cells hold rows and the rows columns).
            b.rows = parts <= 3 ? parts : parts % 3 == 0 ? 3 : 2;
            b.columns = std::max (1, parts / b.rows);
            for (int p = 0; p < parts; ++p)
            {
                const auto& slot = raw.slots[(size_t) p];
                Sign s;
                s.kind = slot.kind;
                s.tick = slot.tick;
                s.row = p / b.columns;
                s.column = p % b.columns;
                if (slot.kind == Sign::Kind::rest)
                    s.text = "△";
                else if (slot.kind == Sign::Kind::hold)
                {
                    s.text = "－";
                    words (s, seq.notes[(size_t) slot.note], false, nullptr);
                }
                else
                {
                    const auto& n = seq.notes[(size_t) slot.note];
                    int octave = 0;
                    s.text = yulmyeong (n.pitch, octave);
                    s.octave = octave;
                    s.off = offText (n.pitch - 100.0 * std::lround (n.pitch / 100.0));
                    s.note = slot.note;
                    s.mark = n.mark;
                    words (s, n, ! lyricSeen[(size_t) slot.note], nullptr);
                    lyricSeen[(size_t) slot.note] = true;
                }
                b.signs.push_back (std::move (s));
            }
            w.beats.push_back (std::move (b));
        }
        markCycle (w, c, Script::roman, false);
        return w;
    }
}
