// SPDX-License-Identifier: AGPL-3.0-only
#include "Scala.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>

namespace phi::scala
{
    namespace
    {
        std::string trim (const std::string& s)
        {
            const auto a = s.find_first_not_of (" \t\r\n");
            if (a == std::string::npos)
                return {};
            const auto b = s.find_last_not_of (" \t\r\n");
            return s.substr (a, b - a + 1);
        }

        // The lines that are not comments ("!" first), the first of them kept even when empty (a
        // .scl's description may be).
        std::vector<std::string> lines (const std::string& text)
        {
            std::vector<std::string> out;
            std::istringstream in (text);
            std::string line;
            while (std::getline (in, line))
            {
                if (! line.empty() && line.back() == '\r')
                    line.pop_back();
                if (! line.empty() && line[0] == '!')
                    continue;
                out.push_back (line);
            }
            return out;
        }

        // The first word of a line: "701.955 fifth" is 701.955.
        std::string firstWord (const std::string& line)
        {
            const auto t = trim (line);
            const auto end = t.find_first_of (" \t");
            return end == std::string::npos ? t : t.substr (0, end);
        }

        std::optional<long long> wholeNumber (const std::string& s)
        {
            if (s.empty())
                return std::nullopt;
            size_t at = s[0] == '-' || s[0] == '+' ? 1 : 0;
            if (at == s.size())
                return std::nullopt;
            for (size_t i = at; i < s.size(); ++i)
                if (s[i] < '0' || s[i] > '9')
                    return std::nullopt;
            try { return std::stoll (s); } catch (...) { return std::nullopt; }
        }

        // A pitch: cents when it has a period, else a ratio or a whole number.
        std::optional<double> pitch (const std::string& word)
        {
            if (word.find ('.') != std::string::npos)
            {
                char* end = nullptr;
                const auto c = std::strtod (word.c_str(), &end);
                if (end == word.c_str() || *end != '\0' || ! std::isfinite (c))
                    return std::nullopt;
                return c;
            }
            const auto slash = word.find ('/');
            const auto num = wholeNumber (slash == std::string::npos ? word : word.substr (0, slash));
            const auto den = slash == std::string::npos ? std::optional<long long> (1) : wholeNumber (word.substr (slash + 1));
            if (! num || ! den || *num <= 0 || *den <= 0)
                return std::nullopt;
            return Tuning::ratioToCents ((double) *num / (double) *den);
        }

        int floorDiv (int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
        int mod (int a, int b) { return ((a % b) + b) % b; }

        bool readFile (const std::string& path, std::string& text, std::string& error)
        {
            std::ifstream in (path, std::ios::binary);
            if (! in)
            {
                error = "could not be opened";
                return false;
            }
            std::ostringstream s;
            s << in.rdbuf();
            text = s.str();
            return true;
        }

        std::string number (double c)
        {
            char buffer[64];
            std::snprintf (buffer, sizeof buffer, "%.5f", c);
            return buffer;
        }

        // A degree's cents counted from the tonic: degree 0 is the tonic, degree size() the period,
        // and on round the periods for any degree above or below.
        double degreeCents (const Scale& s, int degree)
        {
            const auto n = s.size();
            if (n == 0)
                return degree * 100.0;
            const auto periods = floorDiv (degree, n);
            const auto at = mod (degree, n);
            return periods * s.period() + (at == 0 ? 0.0 : s.cents[(size_t) at - 1]);
        }

        // The cents of `key` above the middle key, or nothing when the map plays no note there.
        std::optional<double> keyCents (const Scale& s, const KeyboardMap& m, int key)
        {
            const auto d = key - m.middleKey;
            if (m.size <= 0)
                return degreeCents (s, d);
            const auto periods = floorDiv (d, m.size);
            const auto at = mod (d, m.size);
            if ((size_t) at >= m.mapping.size() || m.mapping[(size_t) at] < 0)
                return std::nullopt;
            const auto octave = degreeCents (s, m.octaveDegree > 0 ? m.octaveDegree : s.size());
            return periods * octave + degreeCents (s, m.mapping[(size_t) at]);
        }
    }

    std::vector<double> Scale::degrees() const
    {
        std::vector<double> out { 0.0 };
        const auto p = period();
        for (size_t i = 0; i + 1 < cents.size(); ++i)
        {
            auto c = p > 0 ? std::fmod (cents[i], p) : cents[i];
            if (c < 0)
                c += p;
            if (std::abs (c) > 1.0e-6 && std::abs (c - p) > 1.0e-6)
                out.push_back (c);
        }
        std::sort (out.begin(), out.end());
        out.erase (std::unique (out.begin(), out.end(), [] (double a, double b) { return std::abs (a - b) < 1.0e-6; }), out.end());
        return out;
    }

    bool parseScl (const std::string& text, Scale& out, std::string& error)
    {
        const auto l = lines (text);
        if (l.size() < 2)
        {
            error = "the file has no description and count";
            return false;
        }
        Scale s;
        s.description = trim (l[0]);
        const auto count = wholeNumber (firstWord (l[1]));
        if (! count || *count < 0 || *count > 100000)
        {
            error = "the number of notes is not a number: " + trim (l[1]);
            return false;
        }
        for (size_t i = 2; i < l.size() && (long long) s.cents.size() < *count; ++i)
        {
            const auto word = firstWord (l[i]);
            if (word.empty())
                continue;
            const auto c = pitch (word);
            if (! c)
            {
                error = "not a pitch: " + trim (l[i]);
                return false;
            }
            s.cents.push_back (*c);
            s.text.push_back (word);
        }
        if ((long long) s.cents.size() != *count)
        {
            error = "the file says " + std::to_string (*count) + " notes and has " + std::to_string (s.cents.size());
            return false;
        }
        out = std::move (s);
        return true;
    }

    bool parseKbm (const std::string& text, KeyboardMap& out, std::string& error)
    {
        std::vector<std::string> l;
        for (const auto& line : lines (text))
            if (! trim (line).empty())
                l.push_back (line);
        if (l.size() < 7)
        {
            error = "a keyboard map needs seven lines before its keys";
            return false;
        }
        KeyboardMap m;
        int* whole[] { &m.size, &m.firstKey, &m.lastKey, &m.middleKey, &m.referenceKey };
        for (size_t i = 0; i < 5; ++i)
        {
            const auto v = wholeNumber (firstWord (l[i]));
            if (! v || *v < 0 || *v > 100000)
            {
                error = "not a whole number: " + trim (l[i]);
                return false;
            }
            *whole[i] = (int) *v;
        }
        char* end = nullptr;
        const auto hzWord = firstWord (l[5]);
        m.referenceHz = std::strtod (hzWord.c_str(), &end);
        if (end == hzWord.c_str() || ! (m.referenceHz > 0.0))
        {
            error = "the reference frequency is not a frequency: " + trim (l[5]);
            return false;
        }
        const auto octave = wholeNumber (firstWord (l[6]));
        if (! octave || *octave < 0)
        {
            error = "the octave degree is not a whole number: " + trim (l[6]);
            return false;
        }
        m.octaveDegree = (int) *octave;
        for (size_t i = 7; i < l.size() && (int) m.mapping.size() < m.size; ++i)
        {
            const auto word = firstWord (l[i]);
            if (word == "x" || word == "X")
            {
                m.mapping.push_back (-1);
                continue;
            }
            const auto v = wholeNumber (word);
            if (! v || *v < 0)
            {
                error = "not a degree or x: " + trim (l[i]);
                return false;
            }
            m.mapping.push_back ((int) *v);
        }
        // Keys the file leaves out at the end of the pattern are not played (the format allows it).
        m.mapping.resize ((size_t) m.size, -1);
        out = std::move (m);
        return true;
    }

    bool readScl (const std::string& path, Scale& out, std::string& error)
    {
        std::string text;
        return readFile (path, text, error) && parseScl (text, out, error);
    }

    bool readKbm (const std::string& path, KeyboardMap& out, std::string& error)
    {
        std::string text;
        return readFile (path, text, error) && parseKbm (text, out, error);
    }

    std::string writeScl (const Scale& s, const std::string& name)
    {
        std::ostringstream out;
        out << "! " << (name.empty() ? std::string ("scale.scl") : name) << "\n!\n";
        out << s.description << "\n " << s.cents.size() << "\n!\n";
        for (size_t i = 0; i < s.cents.size(); ++i)
        {
            const auto useText = s.text.size() == s.cents.size() && ! s.text[i].empty();
            out << " " << (useText ? s.text[i] : number (s.cents[i])) << "\n";
        }
        return out.str();
    }

    std::string writeKbm (const KeyboardMap& m, const std::string& name)
    {
        std::ostringstream out;
        out << "! " << (name.empty() ? std::string ("keyboard.kbm") : name) << "\n!\n";
        out << "! Size of map:\n" << m.size << "\n";
        out << "! First MIDI note number to retune:\n" << m.firstKey << "\n";
        out << "! Last MIDI note number to retune:\n" << m.lastKey << "\n";
        out << "! Middle note where the first entry of the mapping is mapped to:\n" << m.middleKey << "\n";
        out << "! Reference note for which frequency is given:\n" << m.referenceKey << "\n";
        out << "! Frequency to tune the above note to:\n" << number (m.referenceHz) << "\n";
        out << "! Scale degree to consider as formal octave:\n" << m.octaveDegree << "\n";
        out << "! Mapping.\n";
        for (int i = 0; i < m.size; ++i)
        {
            const auto d = (size_t) i < m.mapping.size() ? m.mapping[(size_t) i] : -1;
            out << (d < 0 ? std::string ("x") : std::to_string (d)) << "\n";
        }
        return out.str();
    }

    bool saveText (const std::string& path, const std::string& text, std::string& error)
    {
        std::ofstream out (path, std::ios::binary);
        if (! out || ! (out << text))
        {
            error = "could not be written";
            return false;
        }
        return true;
    }

    Scale fromCents (const std::vector<double>& degrees, double periodCents, const std::string& description)
    {
        Scale s;
        s.description = description;
        auto d = degrees;
        std::sort (d.begin(), d.end());
        for (auto c : d)
            if (c > 1.0e-6 && c < periodCents - 1.0e-6)
                s.cents.push_back (c);
        s.cents.push_back (periodCents);
        s.text.assign (s.cents.size(), {});
        if (std::abs (periodCents - 1200.0) < 1.0e-6)
            s.text.back() = "2/1";
        return s;
    }

    KeyboardMap linearMap (int middleKey)
    {
        KeyboardMap m;
        m.middleKey = m.referenceKey = std::clamp (middleKey, 0, 127);
        m.referenceHz = 440.0 * std::pow (2.0, (m.referenceKey - 69) / 12.0);
        return m;
    }

    double keyHz (const Scale& s, const KeyboardMap& m, int key)
    {
        if (key < m.firstKey || key > m.lastKey)
            return 0.0;
        const auto c = keyCents (s, m, key);
        if (! c)
            return 0.0;
        // A reference key the map does not play is placed a semitone a key from the middle key.
        const auto middle = keyCents (s, m, m.middleKey).value_or (0.0);
        const auto ref = keyCents (s, m, m.referenceKey).value_or (middle + (m.referenceKey - m.middleKey) * 100.0);
        return m.referenceHz * std::pow (2.0, (*c - ref) / 1200.0);
    }

    Tuning toTuning (const Scale& s, const KeyboardMap& m)
    {
        Tuning t;
        for (int key = 0; key < Tuning::numKeys; ++key)
            if (const auto hz = keyHz (s, m, key); hz > 0.0)
                t.setHz (key, hz);
        return t;
    }
}
