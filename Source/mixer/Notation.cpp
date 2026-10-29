// SPDX-License-Identifier: AGPL-3.0-only
#include "Notation.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace notation
{
    namespace
    {
        bool has (const std::string& family, const char* word) { return family.find (word) != std::string::npos; }

        std::string lower (std::string s)
        {
            for (auto& c : s)
                c = (char) std::tolower ((unsigned char) c);
            return s;
        }

        bool exists (const std::string& path) { return std::ifstream (path).good(); }

        // The reference every PHI config starts from: middle C at its 12-TET pitch.
        constexpr const char* middleC = "C4: 261.6255653005986\n";
        constexpr const char* whiteKeys = "0c 200c 400c 500c 700c 900c 1100c 1200c\n";
    }

    const std::vector<Builtin>& builtins()
    {
        static const std::vector<Builtin> all = []
        {
            std::vector<Builtin> b;
            b.push_back ({ "12-TET", "12-TET, sharps and flats", std::string (middleC) + whiteKeys + "bbb bb b (100c) # x #x\n" });
            // Arabic maqam (owner, 2026-10-29): the half-flat (a flat struck through) and the
            // half-sharp (a sharp with one upright), and their sums with a flat or a sharp drawn as
            // one sign, as the Cairo Congress of 1932 and the printed maqam books write them.
            b.push_back ({ "arabic quarter tones", "Arabic maqam: half-flats and half-sharps",
                           std::string (middleC) + whiteKeys
                               + "bb b (100c) # x\n"
                                 "b/ (50c) +\n"
                                 "lig(1,2)!\n"
                                 "-1 -1 |b\n"
                                 "1 1 #+\n" });
            // Persian dastgah: Vaziri's koron (lowered by about a quarter tone) and sori (raised).
            b.push_back ({ "persian koron and sori", "Persian dastgah: koron and sori",
                           std::string (middleC) + whiteKeys
                               + "bb b (100c) # x\n"
                                 "|> (50c) >||\n" });
            return b;
        }();
        return all;
    }

    std::string upsAndDowns (int n)
    {
        n = std::clamp (n, 2, 999);
        // The chain of fifths in n steps: the fifth nearest 3/2, the sharp seven fifths less four
        // octaves, and arrows for the steps between (as Xen Tuner's generate-edo-updown.py).
        const auto fifth = (int) std::lround (n * std::log2 (1.5));
        const auto sharp = 7 * fifth - 4 * n;
        const auto steps = [n] (int s) { return std::to_string (s) + "*1200/" + std::to_string (n) + "c"; };
        const int nominal[7] { 0, 2 * fifth - n, 4 * fifth - 2 * n, n - fifth, fifth, 3 * fifth - n, 5 * fifth - 2 * n };
        std::string text = middleC;
        for (const auto s : nominal)
            text += steps (s) + " ";
        text += steps (n) + "\n";
        if (sharp > 0)
            text += "bb b (" + steps (sharp) + ") # x\n";
        // Arrows for single steps, enough to reach halfway to the next sharp (or to the next nominal
        // when the sharp is no step at all); none when the sharp is a step already.
        const auto span = sharp > 0 ? sharp : std::abs (2 * fifth - n);
        const auto reach = std::max (1, (span + 1) / 2);
        if (sharp != 1)
        {
            std::string down, up;
            for (int a = reach; a >= 1; --a)
            {
                std::string d, u;
                for (int k = 0; k < a; ++k)
                {
                    d += (k > 0 ? "." : "") + std::string ("\\\\");
                    u += (k > 0 ? "." : "") + std::string ("/");
                }
                down += d + " ";
                up = u + (up.empty() ? "" : " ") + up;
            }
            text += down + "(" + steps (1) + ") " + up + "\n";
        }
        return text;
    }

    int equalDivisions (const noteedit::PitchSystem& s)
    {
        if (s.size() < 2 || std::abs (s.period - 1200.0) > 1.0e-6)
            return 0;
        const auto step = s.period / s.size();
        for (int i = 0; i < s.size(); ++i)
            if (std::abs (s.degrees[(size_t) i] - i * step) > 0.01)
                return 0;
        return s.size();
    }

    Choice suggested (const noteedit::PitchSystem& s, const std::string& tunings)
    {
        Choice c;
        const auto family = lower (s.family);
        if (has (family, "raga") || has (family, "melakarta"))
            return { Kind::cipher, c.config, CipherStyle::tradition };
        if (has (family, "gamelan"))
            return { Kind::cipher, c.config, has (family, "sundanese") ? CipherStyle::degrees : CipherStyle::tradition };
        if (has (family, "chinese"))
            return { Kind::cipher, c.config, CipherStyle::jianpu };
        if (has (family, "maqam"))
            return { Kind::staff, "phi:arabic quarter tones", {} };
        if (has (family, "dastgah") || has (family, "avaz"))
            return { Kind::staff, "phi:persian koron and sori", {} };
        if (has (family, "makam"))
            return { Kind::staff, "sagittal/53edo revo.txt", {} };   // Turkish makam's 53 commas
        if (s.isTwelveTet() || s.size() == 0)
            return c;
        if (const auto n = equalDivisions (s); n > 0)
        {
            if (n == 12)
                return c;
            for (const auto* form : { " revo.txt", ".txt" })
            {
                const auto file = "sagittal/" + std::to_string (n) + "edo" + form;
                if (exists (tunings + "/" + file))
                    return { Kind::staff, file, {} };
            }
            return { Kind::staff, "edo:" + std::to_string (n), {} };
        }
        // On the keys: sharps and flats. Off them (just intonation, Pythagorean modes, a Scala
        // scale, an mbira's measured cents): HEJI, its comma arrows for 5-limit, the rest in cents.
        const auto onKeys = std::all_of (s.degrees.begin(), s.degrees.end(), [] (double d) { return std::abs (d - 100.0 * std::round (d / 100.0)) < 1.0; });
        if (onKeys || has (family, "mbira") || has (family, "nyunga") || has (family, "matepe"))
            return c;
        return { Kind::staff, "heji/5 limit.txt", {} };
    }

    bool load (const Choice& c, const std::string& tunings, phi::xen::Config& out, std::string& error)
    {
        out = {};
        out.name = title (c);
        if (c.config.rfind ("phi:", 0) == 0)
        {
            for (const auto& b : builtins())
                if (b.name == c.config.substr (4))
                    return phi::xen::parse (b.text, out, error);
            error = "No such notation: " + c.config;
            return false;
        }
        if (c.config.rfind ("edo:", 0) == 0)
            return phi::xen::parse (upsAndDowns (std::atoi (c.config.c_str() + 4)), out, error);
        const auto path = ! c.config.empty() && c.config[0] == '/' ? c.config : tunings + "/" + c.config;
        return phi::xen::read (path, out, error);
    }

    std::string title (const Choice& c)
    {
        if (c.kind == Kind::cipher)
            switch (c.cipher)
            {
                case CipherStyle::tradition: return "cipher: the tradition's names";
                case CipherStyle::jianpu: return "cipher: jianpu numbers";
                case CipherStyle::degrees: return "cipher: degree numbers";
            }
        if (c.config.rfind ("phi:", 0) == 0)
        {
            for (const auto& b : builtins())
                if (b.name == c.config.substr (4))
                    return b.title;
            return c.config.substr (4);
        }
        if (c.config.rfind ("edo:", 0) == 0)
            return c.config.substr (4) + "-EDO, ups and downs";
        // A folder's file by its path there ("heji/5 limit"), another file by its name.
        auto name = ! c.config.empty() && c.config[0] == '/' ? c.config.substr (c.config.find_last_of ('/') + 1) : c.config;
        if (name.size() > 4 && lower (name.substr (name.size() - 4)) == ".txt")
            name.resize (name.size() - 4);
        return name;
    }

    Cipher cipher (const noteedit::PitchSystem& s, double pitch, CipherStyle style)
    {
        Cipher c;
        if (s.size() == 0)
        {
            c.name = noteedit::letterName (pitch, false);
            return c;
        }
        const auto step = s.nearestStep (pitch);
        const auto degree = s.degreeOf (step);
        c.period = s.periodOf (step);
        const auto off = pitch - s.pitchOf (step);
        const auto cents = s.degrees[(size_t) degree];
        if (style == CipherStyle::jianpu)
        {
            // By the semitone above the tonic, as jianpu writes a mode on its gong (do): 1 to 7, a
            // sharp or flat before the number for the keys between, and the cents off the key.
            static const char* names[12] { "1", "♯1", "2", "♭3", "3", "4", "♯4", "5", "♭6", "6", "♭7", "7" };
            const auto semis = (int) std::lround (cents / 100.0);
            c.name = names[((semis % 12) + 12) % 12];
            const auto keyOff = cents - 100.0 * semis + off;
            if (std::abs (keyOff) >= 0.5)
                c.off = noteedit::centsText (keyOff);
            return c;
        }
        if (style == CipherStyle::tradition)
        {
            // The dotted traditions' names, as the editor's rows show them at the tonic's period.
            const auto name = noteedit::label (s, degree, noteedit::Naming::tradition);
            if (! name.empty() && name.find (' ') == std::string::npos)
                c.name = name;
        }
        if (c.name.empty())
            c.name = std::to_string (degree + 1);
        if (std::abs (off) >= 0.5)
            c.off = noteedit::centsText (off);
        return c;
    }
}
