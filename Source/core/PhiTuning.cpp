// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiTuning.h"

#include <algorithm>
#include <cmath>

namespace phi
{
    namespace
    {
        double equalHz (double key) { return 440.0 * std::pow (2.0, (key - 69.0) / 12.0); }

        // MTS frequency data: a 12-TET key (xx) and a 14-bit fraction of a semitone above it (yy zz).
        std::array<std::uint8_t, 3> encode (double hz)
        {
            const auto maxKey = 127.0 + 16383.0 / 16384.0;
            const auto semis = std::clamp (69.0 + 12.0 * std::log2 (std::max (hz, 1.0e-3) / 440.0), 0.0, maxKey);
            auto whole = (int) std::floor (semis);
            auto fraction = (int) std::lround ((semis - whole) * 16384.0);
            if (fraction >= 16384)
            {
                if (whole < 127)
                {
                    ++whole;
                    fraction = 0;
                }
                else
                    fraction = 16383;
            }
            return { (std::uint8_t) whole, (std::uint8_t) (fraction >> 7), (std::uint8_t) (fraction & 0x7f) };
        }

        double decode (std::uint8_t xx, std::uint8_t yy, std::uint8_t zz)
        {
            return equalHz (xx + ((yy << 7) | zz) / 16384.0);
        }
    }

    Tuning::Tuning()
    {
        for (int k = 0; k < numKeys; ++k)
            keyHz[(size_t) k] = equalHz (k);
    }

    double Tuning::hz (int key) const { return keyHz[(size_t) std::clamp (key, 0, numKeys - 1)]; }

    void Tuning::setHz (int key, double v)
    {
        if (key >= 0 && key < numKeys && v > 0.0)
            keyHz[(size_t) key] = v;
    }

    Tuning Tuning::equal (int divisions, int rootKey, double rootHz)
    {
        divisions = std::clamp (divisions, 1, 128);   // Carrillo's 96, and on to a step a key
        Tuning t;
        for (int k = 0; k < numKeys; ++k)
            t.keyHz[(size_t) k] = rootHz * std::pow (2.0, (double) (k - rootKey) / divisions);
        return t;
    }

    Tuning Tuning::scale (const std::vector<double>& cents, double periodCents, int rootKey, double rootHz)
    {
        if (cents.empty() || periodCents <= 0.0)
            return equal (12, rootKey, rootHz * std::pow (2.0, (69 - rootKey) / 12.0));
        Tuning t;
        const auto n = (int) cents.size();
        for (int k = 0; k < numKeys; ++k)
        {
            const auto steps = k - rootKey;
            const auto period = (int) std::floor ((double) steps / n);
            const auto degree = steps - period * n;
            t.keyHz[(size_t) k] = rootHz * std::pow (2.0, (period * periodCents + cents[(size_t) degree]) / 1200.0);
        }
        return t;
    }

    double Tuning::ratioToCents (double ratio) { return ratio > 0.0 ? 1200.0 * std::log2 (ratio) : 0.0; }

    double Tuning::centsFromEqual (double v, int key) { return 1200.0 * std::log2 (v / equalHz (key)); }

    std::vector<std::vector<std::uint8_t>> Tuning::toSysEx (int program, int device) const
    {
        std::vector<std::vector<std::uint8_t>> out;
        constexpr int perMessage = 64;
        for (int first = 0; first < numKeys; first += perMessage)
        {
            std::vector<std::uint8_t> m { 0xf0, 0x7f, (std::uint8_t) (device & 0x7f), 0x08, 0x02,
                                          (std::uint8_t) (program & 0x7f), (std::uint8_t) perMessage };
            for (int k = first; k < first + perMessage; ++k)
            {
                const auto d = encode (keyHz[(size_t) k]);
                m.insert (m.end(), { (std::uint8_t) k, d[0], d[1], d[2] });
            }
            m.push_back (0xf7);
            out.push_back (std::move (m));
        }
        return out;
    }

    bool Tuning::applySysEx (const std::uint8_t* data, int size)
    {
        if (data == nullptr)
            return false;
        if (size > 0 && data[0] == 0xf0)
        {
            ++data;
            --size;
        }
        if (size > 0 && data[size - 1] == 0xf7)
            --size;
        // 7F (real time) dd 08 02 pp ll ..., or 7E (non-real time) dd 08 07 bb pp ll ...
        int at = 0;
        if (size >= 6 && data[0] == 0x7f && data[2] == 0x08 && data[3] == 0x02)
            at = 4 + 1;
        else if (size >= 7 && data[0] == 0x7e && data[2] == 0x08 && data[3] == 0x07)
            at = 4 + 2;
        else
            return false;
        const int count = data[at++];
        for (int i = 0; i < count && at + 4 <= size; ++i, at += 4)
        {
            const auto key = data[at];
            if (data[at + 1] == 0x7f && data[at + 2] == 0x7f && data[at + 3] == 0x7f)
                continue;   // "no change"
            if (key < numKeys)
                keyHz[key] = decode (data[at + 1], data[at + 2], data[at + 3]);
        }
        return true;
    }

    //==========================================================================
    Mpe::Mpe() { reset(); }

    void Mpe::reset()
    {
        channels = {};
        lowerMembers = 15;
        upperMembers = 0;
        configureZone (0, 15);
    }

    void Mpe::configureZone (int manager, int members)
    {
        members = std::clamp (members, 0, 15);
        if (manager == 0)
        {
            lowerMembers = members;
            upperMembers = std::min (upperMembers, 14 - std::min (14, members));
        }
        else
        {
            upperMembers = members;
            lowerMembers = std::min (lowerMembers, 14 - std::min (14, members));
        }
        // MPE's defaults: member channels bend ±48 semitones, manager channels ±2.
        for (int ch = 0; ch < 16; ++ch)
            channels[(size_t) ch].bendRange = isMember (ch) ? 48.0 : 2.0;
    }

    bool Mpe::isMember (int ch) const
    {
        if (ch >= 1 && ch <= lowerMembers)
            return true;
        return upperMembers > 0 && ch >= 15 - upperMembers && ch <= 14;
    }

    int Mpe::managerOf (int ch) const
    {
        if (lowerMembers > 0 && ch >= 0 && ch <= lowerMembers)
            return 0;
        if (upperMembers > 0 && ch >= 15 - upperMembers && ch <= 15)
            return 15;
        return -1;
    }

    Mpe::Message Mpe::handle (std::uint8_t status, std::uint8_t data1, std::uint8_t data2)
    {
        Message m;
        m.channel = status & 0x0f;
        auto& c = channels[(size_t) m.channel];
        switch (status & 0xf0)
        {
            case 0x90:
                if (data2 > 0)
                {
                    m.kind = Kind::noteOn;
                    m.key = data1;
                    m.velocity = data2 / 127.0;
                    c.polyPressure[data1 & 0x7f] = 0;
                    break;
                }
                [[fallthrough]];
            case 0x80:
                m.kind = Kind::noteOff;
                m.key = data1;
                m.velocity = data2 / 127.0;
                break;
            case 0xa0:
                c.polyPressure[data1 & 0x7f] = data2;
                m.kind = Kind::expression;
                m.key = data1;
                break;
            case 0xd0:
                c.pressure = data1 / 127.0;
                m.kind = Kind::expression;
                break;
            case 0xe0:
                c.bend = (data2 << 7) | data1;
                m.kind = Kind::expression;
                break;
            case 0xb0:
                switch (data1)
                {
                    case 74: c.timbre = data2 / 127.0; m.kind = Kind::expression; break;
                    case 101: c.rpnMsb = data2; break;
                    case 100: c.rpnLsb = data2; break;
                    case 6:   // data entry: the selected RPN's value
                        if (c.rpnMsb == 0 && c.rpnLsb == 0)
                            c.bendRange = data2;   // pitch bend sensitivity, in semitones
                        else if (c.rpnMsb == 0 && c.rpnLsb == 6 && (m.channel == 0 || m.channel == 15))
                            configureZone (m.channel, data2);   // MPE Configuration Message
                        break;
                    case 38:   // data entry LSB: cents of pitch bend sensitivity
                        if (c.rpnMsb == 0 && c.rpnLsb == 0)
                            c.bendRange = std::floor (c.bendRange) + data2 / 100.0;
                        break;
                    case 120: case 123: m.kind = Kind::allNotesOff; break;
                    default: break;
                }
                break;
            default: break;
        }
        return m;
    }

    Expression Mpe::expression (int ch, int key) const
    {
        Expression e;
        ch = std::clamp (ch, 0, 15);
        const auto& c = channels[(size_t) ch];
        const auto bendOf = [] (const Channel& x) { return (x.bend - 8192) / 8192.0 * x.bendRange; };
        e.bendSemitones = bendOf (c);
        e.pressure = c.pressure;
        e.timbre = c.timbre;
        if (key >= 0 && key < Tuning::numKeys && c.polyPressure[(size_t) key] > 0)
            e.pressure = std::max (e.pressure, c.polyPressure[(size_t) key] / 127.0);
        // A member channel's note also follows its zone's manager channel.
        const auto manager = managerOf (ch);
        if (isMember (ch) && manager >= 0)
        {
            const auto& z = channels[(size_t) manager];
            e.bendSemitones += bendOf (z);
            e.pressure = std::max (e.pressure, z.pressure);
        }
        return e;
    }

    double Mpe::noteHz (const Tuning& tuning, int ch, int key) const
    {
        return tuning.hz (key) * std::pow (2.0, expression (ch, key).bendSemitones / 12.0);
    }
}
