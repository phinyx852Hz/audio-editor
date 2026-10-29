// SPDX-License-Identifier: AGPL-3.0-only
#include "Notation.h"
#include "Tunings.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>

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

        Choice of (Kind k, std::string config = "phi:12-TET", CipherStyle cipher = CipherStyle::tradition, Script script = Script::roman)
        {
            Choice c;
            c.kind = k;
            c.config = std::move (config);
            c.cipher = cipher;
            c.script = script;
            return c;
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
            // Turkish makam (owner, 2026-10-29, after LilyPond's turkish-makam.ly): the
            // Arel-Ezgi-Uzdilek system, the whole tone of nine commas of 53 to the octave, the
            // naturals Pythagorean, and its accidentals: the koma (1 comma), bakiye (4), küçük
            // mücenneb (5) and büyük mücenneb (8), flat and sharp.
            b.push_back ({ "turkish aeu", "Turkish makam: Arel-Ezgi-Uzdilek",
                           std::string (middleC)
                               + "0c 9*1200/53c 18*1200/53c 22*1200/53c 31*1200/53c 40*1200/53c 49*1200/53c 1200c\n"
                                 "b\\/\\/(-4*1200/53c) aeu-5(-2*1200/53c) b\\/(-2*1200/53c) aeu-1 (1200/53c) aeu+1 aeu+4(2*1200/53c) E(2*1200/53c) E|(4*1200/53c)\n" });
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

    Choice traditional (const noteedit::PitchSystem& s, const std::string& tunings)
    {
        const Choice c;
        const auto family = lower (s.family);
        // Bhatkhande's sargam for Hindustani ragas, the Karnatak svaras for melakartas, not angka
        // (kepatihan) for gamelan, jianpu for Chinese modes, jeongganbo for Korean ones.
        if (has (family, "melakarta") || has (family, "karnatak") || has (family, "carnatic"))
            return of (Kind::carnatic, c.config, CipherStyle::tradition);
        if (has (family, "raga"))
            return of (Kind::bhatkhande, c.config, CipherStyle::tradition, Script::devanagari);
        if (has (family, "gamelan"))
            return of (Kind::angka, c.config, has (family, "sundanese") ? CipherStyle::degrees : CipherStyle::tradition);
        if (has (family, "chinese"))
            return of (Kind::jianpu, c.config, CipherStyle::jianpu);
        if (has (family, "korean"))
            return of (Kind::jeongganbo, c.config, CipherStyle::tradition);
        return suggested (s, tunings);
    }

    Choice suggested (const noteedit::PitchSystem& s, const std::string& tunings)
    {
        Choice c;
        const auto family = lower (s.family);
        if (has (family, "maqam"))
            return of (Kind::staff, "phi:arabic quarter tones");
        if (has (family, "dastgah") || has (family, "avaz"))
            return of (Kind::staff, "phi:persian koron and sori");
        if (has (family, "makam"))
            return of (Kind::staff, "phi:turkish aeu");   // Turkish makam's 53 commas, in AEU's accidentals
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
                    return of (Kind::staff, file);
            }
            return of (Kind::staff, "edo:" + std::to_string (n));
        }
        // On the keys: sharps and flats. Off them (just intonation, Pythagorean modes, a Scala
        // scale, an mbira's measured cents): HEJI, its comma arrows for 5-limit, the rest in cents.
        const auto onKeys = std::all_of (s.degrees.begin(), s.degrees.end(), [] (double d) { return std::abs (d - 100.0 * std::round (d / 100.0)) < 1.0; });
        if (onKeys || has (family, "mbira") || has (family, "nyunga") || has (family, "matepe"))
            return c;
        return of (Kind::staff, "heji/5 limit.txt");
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
        const auto script = c.script == Script::roman ? std::string() : std::string (" (") + scriptName (c.script) + ")";
        switch (c.kind)
        {
            case Kind::bhatkhande: return "Bhatkhande sargam" + script;
            case Kind::carnatic: return "Karnatak svaras" + script;
            case Kind::jianpu: return "jianpu";
            case Kind::angka: return "not angka (kepatihan)";
            case Kind::jeongganbo: return "jeongganbo";
            case Kind::staff: case Kind::cipher: break;
        }
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

    //==========================================================================
    const std::vector<System>& systems()
    {
        static const std::vector<System> all {
            { "phi", "PHI Microtonal Staff" },
            { "tradition", "Each Tradition's Own" },
            { "bhatkhande", "Hindustani Sargam (Bhatkhande)" },
            { "carnatic", "Carnatic Swaras" },
            { "jianpu", "Jianpu (Numbered)" },
            { "kepatihan", "Kepatihan (Gamelan)" },
            { "jeongganbo", "Jeongganbo (Korean)" },
            { "arabic", "Arabic Maqam Staff" },
            { "turkish", "Turkish Makam Staff (AEU)" },
            { "persian", "Persian Dastgah Staff (Koron and Sori)" },
            { "degrees", "Degree Numbers" },
        };
        return all;
    }

    bool fromId (const std::string& id, const noteedit::PitchSystem& s, const std::string& tunings, Choice& out)
    {
        Choice c;
        if (id == "phi" || id.empty())
            c = suggested (s, tunings);
        else if (id == "tradition")
            c = traditional (s, tunings);
        else if (id == "bhatkhande")
            c = of (Kind::bhatkhande, c.config, CipherStyle::tradition, Script::devanagari);
        else if (id == "carnatic")
            c.kind = Kind::carnatic;
        else if (id == "jianpu")
            c.kind = Kind::jianpu;
        else if (id == "kepatihan")
            c.kind = Kind::angka;
        else if (id == "jeongganbo")
            c.kind = Kind::jeongganbo;
        else if (id == "arabic")
            c.config = "phi:arabic quarter tones";
        else if (id == "turkish")
            c.config = "phi:turkish aeu";
        else if (id == "persian")
            c.config = "phi:persian koron and sori";
        else if (id == "degrees")
            c = of (Kind::cipher, c.config, CipherStyle::degrees);
        else
            return false;
        out = c;
        return true;
    }

    //==========================================================================
    namespace
    {
        struct OrderRow { const char* tradition; int letter; double cents; };
#define PHI_KEY(tradition, name, tonic, ...) KeySignature { tradition, name, tonic, std::array<double, 7> __VA_ARGS__ },
#define PHI_ORDER(tradition, letter, cents)
        const std::vector<KeySignature> keyRows {
#include "KeySignatures.inc"
        };
#undef PHI_KEY
#undef PHI_ORDER
#define PHI_KEY(tradition, name, tonic, ...)
#define PHI_ORDER(tradition, letter, cents) OrderRow { tradition, letter, cents },
        const OrderRow orderRows[] {
#include "KeySignatures.inc"
        };
#undef PHI_KEY
#undef PHI_ORDER

        // The tradition a staff config writes, for its key signatures.
        std::string traditionOf (const std::string& config)
        {
            if (config == "phi:arabic quarter tones")
                return "arabic";
            if (config == "phi:turkish aeu")
                return "turkish";
            if (config == "phi:persian koron and sori")
                return "persian";
            return {};
        }

        // ASCII for matching a system's name with LilyPond's: "ḥijāz kār" is "hijazkar".
        std::string fold (const std::string& name)
        {
            static const std::pair<const char*, char> marks[] {
                { "ā", 'a' }, { "ī", 'i' }, { "ū", 'u' }, { "ē", 'e' }, { "ō", 'o' }, { "ḥ", 'h' }, { "ṣ", 's' },
                { "ḍ", 'd' }, { "ṭ", 't' }, { "ẓ", 'z' }, { "ğ", 'g' }, { "ş", 's' }, { "ç", 'c' }, { "ü", 'u' },
                { "ö", 'o' }, { "ı", 'i' }, { "â", 'a' }, { "î", 'i' }, { "û", 'u' }, { "š", 's' }, { "ž", 'z' } };
            std::string out;
            for (size_t i = 0; i < name.size();)
            {
                bool done = false;
                for (const auto& [from, to] : marks)
                {
                    const std::string f (from);
                    if (name.compare (i, f.size(), f) == 0)
                    {
                        out += to;
                        i += f.size();
                        done = true;
                        break;
                    }
                }
                if (done)
                    continue;
                const auto ch = (unsigned char) name[i++];
                if (std::isalnum (ch))
                    out += (char) std::tolower (ch);
            }
            return out;
        }
    }

    const std::vector<KeySignature>& keySignatures() { return keyRows; }

    const KeySignature* findKeySignature (const std::string& tradition, const std::string& name)
    {
        const auto want = fold (name);
        for (const auto& k : keyRows)
            if (k.tradition == tradition && fold (k.name) == want)
                return &k;
        return nullptr;
    }

    const std::vector<std::pair<int, double>>& keyOrder (const std::string& tradition)
    {
        static std::map<std::string, std::vector<std::pair<int, double>>> orders = []
        {
            std::map<std::string, std::vector<std::pair<int, double>>> m;
            for (const auto& r : orderRows)
                m[r.tradition].push_back ({ r.letter, r.cents });
            // Turkish key signatures in LilyPond keep its default order; PHI's: the flats as
            // Arabic's (B E A D G C F), then the sharps (F C G D A E B).
            for (const auto l : { 6, 2, 5, 1, 4, 0, 3 })
                m["turkish"].push_back ({ l, -1.0 });
            for (const auto l : { 3, 0, 4, 1, 5, 2, 6 })
                m["turkish"].push_back ({ l, 1.0 });
            return m;
        }();
        static const std::vector<std::pair<int, double>> none;
        const auto it = orders.find (tradition);
        return it == orders.end() ? none : it->second;
    }

    std::array<double, 7> naturals (const std::string& tradition)
    {
        if (tradition == "turkish")
        {
            const auto comma = 1200.0 / 53.0;
            return { 0.0, 9 * comma, 18 * comma, 22 * comma, 31 * comma, 40 * comma, 49 * comma };
        }
        return { 0.0, 200.0, 400.0, 500.0, 700.0, 900.0, 1100.0 };
    }

    std::array<double, 7> transpose (const KeySignature& k, int tonicLetter, double tonicCents, const std::array<double, 7>& nat)
    {
        // As LilyPond's \key moves a mode on C: each letter's note moved by the tonic's interval
        // above C, the letters along with it.
        std::array<double, 7> out {};
        tonicLetter = ((tonicLetter % 7) + 7) % 7;
        const auto interval = nat[(size_t) tonicLetter] + tonicCents;
        for (int i = 0; i < 7; ++i)
        {
            const auto letter = (i + tonicLetter) % 7;
            auto cents = nat[(size_t) i] + k.cents[(size_t) i] + interval - nat[(size_t) letter];
            cents -= 1200.0 * std::round (cents / 1200.0);
            out[(size_t) letter] = std::abs (cents) < 0.01 ? 0.0 : cents;
        }
        return out;
    }

    std::array<double, 7> keyFor (const Choice& c, const noteedit::PitchSystem& s)
    {
        std::array<double, 7> none {};
        if (c.kind != Kind::staff || c.key == "none")
            return none;
        const auto tradition = traditionOf (c.config);
        const KeySignature* k = nullptr;
        int letter = 0;
        double alter = 0.0;
        const auto nat = naturals (tradition.empty() ? std::string ("arabic") : tradition);
        if (! c.key.empty())
        {
            // "<tradition>:<name>@<tonic>"
            const auto colon = c.key.find (':'), at = c.key.find ('@');
            if (colon == std::string::npos)
                return none;
            k = findKeySignature (c.key.substr (0, colon), c.key.substr (colon + 1, at == std::string::npos ? std::string::npos : at - colon - 1));
            const auto tonic = at == std::string::npos ? std::string ("c") : c.key.substr (at + 1);
            if (k == nullptr || tonic.empty())
                return none;
            letter = (int) std::string ("cdefgab").find ((char) std::tolower ((unsigned char) tonic[0]));
            if (letter < 0)
                return none;
            for (size_t i = 1; i < tonic.size(); ++i)
                alter += tonic[i] == 's' ? 100.0 : tonic[i] == 'f' ? -100.0 : 0.0;
            return transpose (*k, letter, alter, nat);
        }
        // The system's own, when LilyPond has a key of its name in the staff's tradition: on the
        // system's tonic, spelled on the nearest letter.
        if (tradition.empty())
            return none;
        std::string plain = s.name;
        for (const auto& t : mixer::tuningSystems())
            if (t.name == s.name)
                plain = t.plain;
        k = findKeySignature (tradition, plain);
        if (k == nullptr)
            return none;
        const auto rel = std::fmod (std::fmod (s.tonic, 1200.0) + 1200.0, 1200.0);
        double best = 1e9;
        for (int l = 0; l < 7; ++l)
            for (const auto oct : { -1200.0, 0.0, 1200.0 })
                if (std::abs (rel - (nat[(size_t) l] + oct)) < std::abs (best))
                {
                    best = rel - (nat[(size_t) l] + oct);
                    letter = l;
                }
        return transpose (*k, letter, std::abs (best) < 1.0 ? 0.0 : best, nat);
    }
}
