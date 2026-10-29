// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023 euwbah (Xen Tuner); the C++ port (C) 2026 phinyx852Hz (ΦNYX) and contributors.
// Ported from Xen Tuner's `Xen Tuner/fns.js` (parseTuningConfig and the functions it calls).
#include "XenTuner.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>

namespace phi::xen
{
    namespace
    {
        struct SymbolRow { char32_t point; const char* name; const char* text; };
        struct TextRow { const char* text; int code; };

#define XEN_SYMBOL(point, name, text) SymbolRow { point, name, text },
#define XEN_TEXT(text, code)
        const SymbolRow symbolRows[] = {
#include "XenSymbols.inc"
        };
#undef XEN_SYMBOL
#undef XEN_TEXT
#define XEN_SYMBOL(point, name, text)
#define XEN_TEXT(text, code) TextRow { text, code },
        const TextRow textRows[] = {
#include "XenSymbols.inc"
        };
#undef XEN_SYMBOL
#undef XEN_TEXT

        const std::map<std::string, int>& textToCode()
        {
            static const auto map = []
            {
                std::map<std::string, int> m;
                for (const auto& row : textRows)
                    m.emplace (row.text, row.code);
                return m;
            }();
            return map;
        }

        std::string trim (const std::string& s)
        {
            const auto a = s.find_first_not_of (" \t\r\n");
            if (a == std::string::npos)
                return {};
            const auto b = s.find_last_not_of (" \t\r\n");
            return s.substr (a, b - a + 1);
        }

        bool endsWith (const std::string& s, const std::string& end)
        {
            return s.size() >= end.size() && s.compare (s.size() - end.size(), end.size(), end) == 0;
        }

        std::vector<std::string> words (const std::string& line)
        {
            std::vector<std::string> out;
            std::string w;
            for (const auto c : line)
            {
                if (c == ' ' || c == '\t')
                {
                    if (! w.empty())
                        out.push_back (w);
                    w.clear();
                }
                else
                    w += c;
            }
            if (! w.empty())
                out.push_back (w);
            return out;
        }

        //======================================================================
        // The arithmetic a config writes where Xen Tuner evaluates JavaScript: numbers, + - * /,
        // brackets, and Math's pow, sqrt, cbrt, log, log2, log10, exp, abs, floor, ceil, round,
        // min, max, PI and E.
        class Expression
        {
        public:
            explicit Expression (const std::string& t) : text (t) {}

            std::optional<double> value()
            {
                const auto v = sum();
                space();
                if (! v || at != text.size())
                    return std::nullopt;
                return v;
            }

        private:
            void space() { while (at < text.size() && std::isspace ((unsigned char) text[at])) ++at; }
            bool take (char c)
            {
                space();
                if (at < text.size() && text[at] == c) { ++at; return true; }
                return false;
            }

            std::optional<double> sum()
            {
                auto v = product();
                while (v)
                {
                    if (take ('+')) { const auto r = product(); if (! r) return r; *v += *r; }
                    else if (take ('-')) { const auto r = product(); if (! r) return r; *v -= *r; }
                    else break;
                }
                return v;
            }

            std::optional<double> product()
            {
                auto v = unary();
                while (v)
                {
                    if (take ('*')) { const auto r = unary(); if (! r) return r; *v *= *r; }
                    else if (take ('/')) { const auto r = unary(); if (! r) return r; *v /= *r; }
                    else break;
                }
                return v;
            }

            std::optional<double> unary()
            {
                if (take ('-')) { auto v = unary(); if (v) *v = -*v; return v; }
                if (take ('+')) return unary();
                return atom();
            }

            std::optional<double> atom()
            {
                space();
                if (take ('('))
                {
                    const auto v = sum();
                    if (! v || ! take (')'))
                        return std::nullopt;
                    return v;
                }
                if (at < text.size() && (std::isdigit ((unsigned char) text[at]) || text[at] == '.'))
                {
                    const char* begin = text.c_str() + at;
                    char* end = nullptr;
                    const auto v = std::strtod (begin, &end);
                    if (end == begin)
                        return std::nullopt;
                    at += (size_t) (end - begin);
                    return v;
                }
                if (text.compare (at, 5, "Math.") != 0)
                    return std::nullopt;
                at += 5;
                std::string name;
                while (at < text.size() && std::isalnum ((unsigned char) text[at]))
                    name += text[at++];
                if (name == "PI") return M_PI;
                if (name == "E") return M_E;
                if (! take ('('))
                    return std::nullopt;
                std::vector<double> args;
                if (! take (')'))
                {
                    do
                    {
                        const auto v = sum();
                        if (! v)
                            return std::nullopt;
                        args.push_back (*v);
                    } while (take (','));
                    if (! take (')'))
                        return std::nullopt;
                }
                const auto one = [&] (double (*f) (double)) -> std::optional<double>
                {
                    if (args.size() != 1) return std::nullopt;
                    return f (args[0]);
                };
                if (name == "pow") { if (args.size() != 2) return std::nullopt; return std::pow (args[0], args[1]); }
                if (name == "min" && ! args.empty()) return *std::min_element (args.begin(), args.end());
                if (name == "max" && ! args.empty()) return *std::max_element (args.begin(), args.end());
                if (name == "sqrt") return one ([] (double x) { return std::sqrt (x); });
                if (name == "cbrt") return one ([] (double x) { return std::cbrt (x); });
                if (name == "log") return one ([] (double x) { return std::log (x); });
                if (name == "log2") return one ([] (double x) { return std::log2 (x); });
                if (name == "log10") return one ([] (double x) { return std::log10 (x); });
                if (name == "exp") return one ([] (double x) { return std::exp (x); });
                if (name == "abs") return one ([] (double x) { return std::fabs (x); });
                if (name == "floor") return one ([] (double x) { return std::floor (x); });
                if (name == "ceil") return one ([] (double x) { return std::ceil (x); });
                if (name == "round") return one ([] (double x) { return std::floor (x + 0.5); });
                return std::nullopt;
            }

            const std::string& text;
            size_t at = 0;
        };

        std::optional<double> evaluate (const std::string& s) { return Expression (s).value(); }

        // parseCentsOrRatio: "100c" is cents, else a ratio ("3/2", "2"); a negative ratio is its
        // reciprocal downwards, and 0 is no interval.
        std::optional<double> centsOrRatio (const std::string& word)
        {
            const auto s = trim (word);
            if (s.empty())
                return std::nullopt;
            if (s.back() == 'c')
                return evaluate (s.substr (0, s.size() - 1));
            const auto ratio = evaluate (s);
            if (! ratio || std::isnan (*ratio))
                return std::nullopt;
            if (*ratio < 0.0)
                return -std::log2 (-*ratio) * 1200.0;
            if (*ratio == 0.0)
                return 0.0;
            return std::log2 (*ratio) * 1200.0;
        }

        // parseSymbolsDeclaration: symbols joined by periods, a quoted one text, `\` escaping
        // `\`, `'` and `/`.
        std::optional<std::vector<Symbol>> symbols (const std::string& s)
        {
            std::vector<Symbol> out;
            std::string current;
            bool quoted = false, escape = false, currentQuoted = false;
            const auto push = [&]() -> bool
            {
                if (currentQuoted)
                    out.push_back ({ 0, current });
                else
                {
                    const auto code = symbolCode (current);
                    if (code <= 0)
                        return false;
                    out.push_back ({ code, {} });
                }
                current.clear();
                currentQuoted = false;
                return true;
            };
            for (const auto c : s)
            {
                if (escape)
                {
                    if (c != '\\' && c != '\'' && c != '/')
                        return std::nullopt;
                    escape = false;
                    current += c;
                }
                else if (c == '\\')
                    escape = true;
                else if (c == '\'')
                {
                    quoted = ! quoted;
                    currentQuoted = true;
                }
                else if (c == '.' && ! quoted)
                {
                    if (! push())
                        return std::nullopt;
                }
                else
                    current += c;
            }
            if (quoted || escape || current.empty() || ! push())
                return std::nullopt;
            return out;
        }

        // parseSymbolOffsetPair: "b(-113.685c)" is the symbols and an offset of their own.
        std::pair<std::string, double> symbolAndOffset (const std::string& s)
        {
            if (s.empty() || s.back() != ')')
                return { s, 0.0 };
            int depth = 1;
            size_t split = 0;
            for (size_t i = s.size() - 1; i-- > 0;)
            {
                if (s[i] == ')') ++depth;
                else if (s[i] == '(') --depth;
                if (depth == 0) { split = i; break; }
            }
            if (split == 0)
                return { s, 0.0 };
            const auto offset = centsOrRatio (s.substr (split + 1, s.size() - split - 2));
            if (! offset)
                return { s, 0.0 };
            return { s.substr (0, split), *offset };
        }

        int letterIndex (char letter)   // C = 0 .. B = 6
        {
            static const std::string order = "cdefgab";
            const auto i = order.find ((char) std::tolower ((unsigned char) letter));
            return i == std::string::npos ? -1 : (int) i;
        }

        std::string lower (std::string s)
        {
            for (auto& c : s)
                c = (char) std::tolower ((unsigned char) c);
            return s;
        }
    }

    //==========================================================================
    int symbolCount() { return (int) std::size (symbolRows); }

    char32_t codePoint (int code)
    {
        return code > 0 && code < symbolCount() ? symbolRows[code].point : 0;
    }

    std::string smuflName (int code)
    {
        return code > 0 && code < symbolCount() ? symbolRows[code].name : "";
    }

    std::string textCode (int code)
    {
        return code > 0 && code < symbolCount() ? symbolRows[code].text : "";
    }

    int symbolCode (const std::string& codeOrText)
    {
        const auto s = trim (codeOrText);
        if (s.empty())
            return -1;
        if (const auto it = textToCode().find (s); it != textToCode().end())
            return it->second;
        if (! std::all_of (s.begin(), s.end(), [] (char c) { return std::isdigit ((unsigned char) c); }))
            return -1;
        const auto code = std::atoi (s.c_str());
        return code > 0 && code < symbolCount() ? code : -1;
    }

    std::string asText (const std::vector<Symbol>& list)
    {
        std::string out;
        for (const auto& s : list)
        {
            if (s.code == 0)
                out += s.text;
            else if (const auto t = textCode (s.code); ! t.empty())
                out += t;
            else
                out += "[" + smuflName (s.code) + "]";
        }
        return out;
    }

    double Secondary::centsOn (int nominal) const
    {
        if (cents.empty())
            return 0.0;
        if (cents.size() == 1)
            return cents[0];
        return cents[(size_t) std::clamp (nominal, 0, (int) cents.size() - 1)];
    }

    int Config::referenceStaff() const
    {
        return octave * 7 + std::max (0, letterIndex (letter));
    }

    double Config::referencePitch() const
    {
        // The reference note is named for its place on the staff (A4, C4); its pitch is the frequency given.
        return 6900.0 + 1200.0 * std::log2 (hz / 440.0);
    }

    //==========================================================================
    bool parse (const std::string& source, Config& out, std::string& error)
    {
        // Comments off (the first "//" on a line: an escaped \/\/ is not one), blank lines out.
        std::vector<std::string> lines;
        {
            std::istringstream in (source);
            std::string line;
            while (std::getline (in, line))
            {
                if (const auto c = line.find ("//"); c != std::string::npos)
                    line.erase (c);
                line = trim (line);
                if (! line.empty())
                    lines.push_back (line);
            }
        }
        if (lines.size() < 2)
        {
            error = "A tuning config needs a reference note and the nominals.";
            return false;
        }

        Config conf;
        conf.name = out.name;

        // 1. The reference note: "A4: 440", "C4: 440*16/27", "!C4: 263".
        {
            const auto colon = lines[0].find (':');
            if (colon == std::string::npos || lines[0].find (':', colon + 1) != std::string::npos)
            {
                error = "The first line must name the reference note and its frequency, as \"A4: 440\".";
                return false;
            }
            auto note = trim (lines[0].substr (0, colon));
            if (! note.empty() && note[0] == '!')
                note.erase (0, 1);
            if (note.size() < 2 || letterIndex (note[0]) < 0)
            {
                error = "The reference note must be a letter A to G and an octave: " + note;
                return false;
            }
            conf.letter = (char) std::tolower ((unsigned char) note[0]);
            char* end = nullptr;
            conf.octave = (int) std::strtol (note.c_str() + 1, &end, 10);
            if (end == note.c_str() + 1 || *end != 0)
            {
                error = "The reference note's octave is not a number: " + note;
                return false;
            }
            const auto hz = evaluate (trim (lines[0].substr (colon + 1)));
            if (! hz || ! (*hz > 0.0))
            {
                error = "The reference frequency is not a number of hertz: " + lines[0];
                return false;
            }
            conf.hz = *hz;
        }

        // 2. The nominals and the equave: "0" (not "0c") leaves a nominal out.
        {
            const auto list = words (lines[1]);
            if (list.size() < 2)
            {
                error = "The nominals line needs at least one nominal and the equave.";
                return false;
            }
            for (size_t i = 0; i < list.size(); ++i)
            {
                if (list[i] == "0" && i + 1 < list.size())
                {
                    // The reference nominal is always there, at 0.
                    conf.nominals.push_back (i == 0 ? std::optional<double> (0.0) : std::nullopt);
                    continue;
                }
                const auto cents = centsOrRatio (list[i]);
                if (! cents)
                {
                    error = "Not a tuning in cents or a ratio: " + list[i];
                    return false;
                }
                if (i + 1 < list.size())
                    conf.nominals.push_back (*cents);
                else
                    conf.equave = *cents;
            }
            if (conf.equave == 0.0)
            {
                error = "The equave must not be 0.";
                return false;
            }
        }

        // 3. The accidental chains, up to the first declaration.
        static const std::regex declaration (R"((lig|aux|sec|explicit|nobold|override|displaycents|displaysteps|independent)\([0-9,a-zA-Z\s]*\))");
        size_t i = 2;
        for (; i < lines.size(); ++i)
        {
            if (std::regex_search (lines[i], declaration))
                break;
            Chain chain;
            std::vector<double> offsets;
            std::optional<double> increment;
            int central = -1;
            for (const auto& word : words (lines[i]))
            {
                if (word.size() > 2 && word.front() == '(' && word.back() == ')')
                {
                    if (const auto inc = centsOrRatio (word.substr (1, word.size() - 2)))
                    {
                        if (increment)
                        {
                            error = "An accidental chain has more than one step in brackets: " + lines[i];
                            return false;
                        }
                        increment = inc;
                        central = (int) chain.symbols.size();
                        chain.symbols.emplace_back();
                        offsets.push_back (0.0);
                        continue;
                    }
                }
                const auto [text, offset] = symbolAndOffset (word);
                const auto syms = symbols (text);
                if (! syms)
                {
                    error = "Not an accidental: " + word + " (line " + std::to_string (i + 1) + ")";
                    return false;
                }
                chain.symbols.push_back (*syms);
                offsets.push_back (offset);
            }
            if (! increment)
            {
                error = "An accidental chain needs its step in brackets, as \"bb b (100c) # x\": " + lines[i];
                return false;
            }
            chain.central = central;
            for (size_t j = 0; j < offsets.size(); ++j)
                chain.cents.push_back ((int) j == central ? 0.0 : ((int) j - central) * *increment + offsets[j]);
            conf.chains.push_back (std::move (chain));
        }

        // 4. The declarations, in any order.
        static const std::regex ligature (R"(^lig\(([0-9,\s]+)\)([?!]*))");
        static const std::regex displaySteps (R"(^displaysteps\(\s*([0-9]+)\s*,.*\))");
        enum class Section { none, lig, sec, override_, independent, skip } section = Section::none;
        const int numNominals = (int) conf.nominals.size();
        for (; i < lines.size(); ++i)
        {
            const auto& line = lines[i];
            std::smatch m;
            if (std::regex_search (line, m, ligature))
            {
                Ligature lig;
                std::stringstream list (m[1].str());
                std::string n;
                while (std::getline (list, n, ','))
                {
                    // A chain there is not (as some of Xen Tuner's Sagittal configs have, with one
                    // chain): Xen Tuner reads past it, and the ligature never applies.
                    lig.chains.push_back (std::atoi (trim (n).c_str()) - 1);
                }
                lig.important = m[2].str().find ('!') != std::string::npos;
                lig.weak = m[2].str().find ('?') != std::string::npos;
                conf.ligatures.push_back (lig);
                section = Section::lig;
                continue;
            }
            if (std::regex_search (line, m, displaySteps))
            {
                conf.displaySteps = std::atoi (m[1].str().c_str());
                section = Section::none;
                continue;
            }
            if (line == "sec()") { section = Section::sec; continue; }
            if (line == "override()") { section = Section::override_; continue; }
            if (line == "independent()") { section = Section::independent; continue; }
            if (std::regex_search (line, declaration))
            {
                section = Section::none;   // aux, explicit, nobold, displaycents: MuseScore's
                continue;
            }
            const auto list = words (line);
            switch (section)
            {
                case Section::none:
                case Section::skip:
                    error = "Expected lig(), aux(), sec() or another declaration, not: " + line;
                    return false;
                case Section::independent:
                    break;
                case Section::lig:
                {
                    auto& lig = conf.ligatures.back();
                    if (list.size() != lig.chains.size() + 1)
                    {
                        error = "A ligature line needs a degree for each of its chains and the symbols: " + line;
                        return false;
                    }
                    Ligature::Form form;
                    for (size_t k = 0; k + 1 < list.size(); ++k)
                        form.degrees.push_back (std::atoi (list[k].c_str()));
                    const auto syms = symbols (list.back());
                    if (! syms)
                    {
                        error = "Not an accidental: " + list.back();
                        return false;
                    }
                    form.symbols = *syms;
                    lig.forms.push_back (std::move (form));
                    break;
                }
                case Section::sec:
                {
                    if (list.size() < 2)
                    {
                        error = "A secondary accidental needs its symbols and tuning: " + line;
                        return false;
                    }
                    const auto first = symbols (list[0]);
                    if (! first)
                    {
                        error = "Not an accidental: " + list[0];
                        return false;
                    }
                    const bool firstIsText = first->size() == 1 && (*first)[0].code == 0;
                    const auto secondSymbols = list.size() > 1 ? symbols (list[1]) : std::nullopt;
                    const auto secondCents = list.size() > 1 ? centsOrRatio (list[1]) : std::nullopt;
                    const auto extra = (size_t) std::max (0, numNominals - 1);
                    Secondary sec;
                    size_t from = 1;
                    if (list.size() == 2 || (list.size() == 2 + extra && ! (list.size() == 3 && firstIsText && ! secondSymbols && secondCents)))
                        sec.symbols = *first;
                    else if (list.size() == 3 || list.size() == 3 + extra)
                    {
                        if (! secondSymbols)
                        {
                            error = "Not an accidental: " + list[1];
                            return false;
                        }
                        sec.symbols = *secondSymbols;
                        from = 2;
                    }
                    else
                    {
                        error = "A secondary accidental line has the wrong number of words: " + line;
                        return false;
                    }
                    for (size_t k = from; k < list.size(); ++k)
                    {
                        const auto c = centsOrRatio (list[k]);
                        if (! c)
                        {
                            error = "Not a tuning in cents or a ratio: " + list[k];
                            return false;
                        }
                        sec.cents.push_back (*c);
                    }
                    conf.secondary.push_back (std::move (sec));
                    break;
                }
                case Section::override_:
                {
                    if (list.size() != conf.chains.size() + 2)
                    {
                        error = "An override needs the nominal, a degree for each chain, and the tuning: " + line;
                        return false;
                    }
                    Config::Override o;
                    o.nominal = std::atoi (list[0].c_str());
                    for (size_t k = 1; k + 1 < list.size(); ++k)
                        o.degrees.push_back (std::atoi (list[k].c_str()));
                    const auto c = centsOrRatio (list.back());
                    if (! c || o.nominal < 0 || o.nominal >= numNominals)
                    {
                        error = "Not a valid override: " + line;
                        return false;
                    }
                    o.cents = *c;
                    conf.overrides.push_back (std::move (o));
                    break;
                }
            }
        }
        out = std::move (conf);
        return true;
    }

    bool read (const std::string& path, Config& out, std::string& error)
    {
        std::ifstream in (path, std::ios::binary);
        if (! in)
        {
            error = "Cannot read " + path;
            return false;
        }
        std::stringstream text;
        text << in.rdbuf();
        if (out.name.empty())
        {
            auto name = path.substr (path.find_last_of ("/\\") + 1);
            if (endsWith (lower (name), ".txt"))
                name.resize (name.size() - 4);
            out.name = name;
        }
        return parse (text.str(), out, error);
    }

    //==========================================================================
    namespace
    {
        constexpr size_t maxEntries = 200000;
        constexpr double sameCents = 0.01;   // enharmonically the same (Xen Tuner's is 0.005)
    }

    Speller::Speller (Config c) : conf (std::move (c))
    {
        const auto& chains = conf.chains;
        // Every accidental vector: each chain's degree, the first chain the slowest.
        std::vector<std::vector<int>> vectors { {} };
        for (const auto& chain : chains)
        {
            std::vector<std::vector<int>> next;
            for (const auto& v : vectors)
                for (int d = chain.lowest(); d <= chain.highest(); ++d)
                {
                    auto w = v;
                    w.push_back (d);
                    next.push_back (std::move (w));
                }
            vectors = std::move (next);
            if (vectors.size() * conf.nominals.size() > maxEntries)
            {
                large = true;
                break;
            }
        }
        if (large)
            vectors.resize (std::min (vectors.size(), maxEntries / std::max<size_t> (1, conf.nominals.size())));

        for (int n = 0; n < (int) conf.nominals.size(); ++n)
        {
            if (! conf.nominals[(size_t) n])
                continue;
            for (const auto& v : vectors)
            {
                if (v.size() != chains.size())
                    continue;
                Entry e;
                e.nominal = n;
                e.degrees = v;
                e.cents = *conf.nominals[(size_t) n];
                // The first chain's symbols go right-most, the later chains' to the left of them.
                std::vector<Symbol> ordered;
                for (size_t k = 0; k < chains.size(); ++k)
                {
                    const auto d = v[k];
                    if (d == 0)
                        continue;
                    const auto index = (size_t) (d + chains[k].central);
                    const auto& syms = chains[k].symbols[index];
                    ordered.insert (ordered.begin(), syms.begin(), syms.end());
                    e.cents += chains[k].cents[index];
                    e.weight += 1.5 * d * d;
                }
                for (const auto& o : conf.overrides)
                    if (o.nominal == n && o.degrees == v)
                        e.cents = o.cents;
                if (! v.empty())
                    e.direction = v[0] > 0 ? 1 : v[0] < 0 ? -1 : 0;

                // The ligatures that give an accidental vector its usual form (strong or important ones),
                // by the precedence fns.js documents: weak 0, strong 1, weak and important 2, strong
                // and important 3. (Its code adds isWeak where the comment means strong.)
                int highest = 0;
                for (const auto& lig : conf.ligatures)
                {
                    const int precedence = (lig.weak ? 0 : 1) + (lig.important ? 2 : 0);
                    if (precedence < 1 || precedence < highest)
                        continue;
                    if (std::any_of (lig.chains.begin(), lig.chains.end(), [&] (int k) { return k < 0 || k >= (int) chains.size(); }))
                        continue;
                    std::vector<int> ligDegrees;
                    for (const auto k : lig.chains)
                        ligDegrees.push_back (v[(size_t) k]);
                    const auto form = std::find_if (lig.forms.begin(), lig.forms.end(),
                                                    [&] (const Ligature::Form& f) { return f.degrees == ligDegrees; });
                    if (form == lig.forms.end())
                        continue;
                    // The ligature takes the place of the right-most symbol it replaces.
                    auto replaced = ordered;
                    size_t place = 0;
                    bool found = true;
                    for (const auto k : lig.chains)
                    {
                        const auto d = v[(size_t) k];
                        if (d == 0)
                            continue;
                        for (const auto& s : chains[(size_t) k].symbols[(size_t) (d + chains[(size_t) k].central)])
                        {
                            const auto it = std::find (replaced.rbegin(), replaced.rend(), s);
                            if (it == replaced.rend()) { found = false; break; }
                            const auto at = (size_t) (std::distance (it, replaced.rend()) - 1);
                            replaced.erase (replaced.begin() + (long) at);
                            if (at > place) place = at;
                            else if (at < place) --place;
                        }
                        if (! found)
                            break;
                    }
                    if (! found)
                        continue;
                    place = std::min (place, replaced.size());
                    replaced.insert (replaced.begin() + (long) place, form->symbols.begin(), form->symbols.end());
                    ordered = std::move (replaced);
                    highest = precedence;
                }
                // And 2 a sign written: a sharp costs 3.5, a double sharp 8, and HEJI's flat with an
                // arrow, one sign by its ligature, 5.
                e.weight += 2.0 * (double) ordered.size();
                e.symbols = std::move (ordered);
                entries.push_back (std::move (e));
            }
        }
        std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.cents < b.cents; });

        if (conf.equave != 0.0)
        {
            const auto period = std::fabs (conf.equave);
            for (const auto& e : entries)
            {
                auto c = std::fmod (e.cents, period);
                if (c < 0.0) c += period;
                if (period - c < sameCents) c = 0.0;
                steps.push_back (c);
            }
            std::sort (steps.begin(), steps.end());
            steps.erase (std::unique (steps.begin(), steps.end(), [] (double a, double b) { return std::fabs (a - b) < sameCents; }), steps.end());
        }
    }

    Spelling Speller::make (const Entry& e, double pitch, int secondary) const
    {
        Spelling s;
        const auto rel = pitch - conf.referencePitch();
        auto cents = e.cents;
        if (secondary >= 0)
            cents += conf.secondary[(size_t) secondary].centsOn (e.nominal);
        s.equave = (int) std::lround ((rel - cents) / conf.equave);
        s.nominal = e.nominal;
        s.degrees = e.degrees;
        s.secondary = secondary;
        s.symbols = e.symbols;
        if (secondary >= 0)
        {
            const auto& more = conf.secondary[(size_t) secondary].symbols;
            s.symbols.insert (s.symbols.begin(), more.begin(), more.end());
        }
        s.staff = conf.referenceStaff() + e.nominal + s.equave * (int) conf.nominals.size();
        s.pitch = conf.referencePitch() + cents + s.equave * conf.equave;
        s.off = pitch - s.pitch;
        return s;
    }

    std::vector<Spelling> Speller::spell (double pitch, bool flats) const
    {
        struct Candidate { const Entry* entry; int secondary; double off; double weight; };
        std::vector<Candidate> all;
        const auto rel = pitch - conf.referencePitch();
        const auto offOf = [&] (double cents)
        {
            const auto e = std::lround ((rel - cents) / conf.equave);
            return rel - cents - (double) e * conf.equave;
        };
        const auto weigh = [&] (const Entry& e, double extra)
        {
            // Sharps before flats (or flats first), on a tie of plainness.
            const double lean = e.direction == 0 ? 0.0 : (e.direction > 0) == flats ? 0.01 : 0.0;
            return e.weight + lean + extra;
        };
        double nearest = 1e300;
        for (const auto& e : entries)
        {
            const auto off = offOf (e.cents);
            all.push_back ({ &e, -1, off, weigh (e, 0) });
            nearest = std::min (nearest, std::fabs (off));
        }
        if (nearest > 0.5)
            for (int k = 0; k < (int) conf.secondary.size(); ++k)
                for (const auto& e : entries)
                {
                    const auto off = offOf (e.cents + conf.secondary[(size_t) k].centsOn (e.nominal));
                    if (std::fabs (off) < nearest - sameCents)
                        all.push_back ({ &e, k, off, weigh (e, 4.0) });
                }
        if (all.empty())
            return {};
        // Nearness against plainness: an accidental costs as much as being a few cents further off
        // (a sharp 3.5, a double sharp 8, a secondary accidental 4 more), so a JI notation's plain
        // spelling a cent or two off beats a crowd of arrows that hits it, and a comma arrow that
        // hits it beats a plain letter 21.5 cents away.
        const auto score = [] (const Candidate& c) { return std::fabs (c.off) + c.weight; };
        const auto best = std::min_element (all.begin(), all.end(), [&] (const Candidate& a, const Candidate& b)
                                            { return score (a) < score (b); })->off;
        // The best's enharmonic spellings: the others that notate the same pitch.
        std::vector<Candidate> near;
        for (const auto& c : all)
            if (std::fabs (c.off - best) < sameCents)
                near.push_back (c);
        std::stable_sort (near.begin(), near.end(), [] (const Candidate& a, const Candidate& b) { return a.weight < b.weight; });
        std::vector<Spelling> out;
        for (const auto& c : near)
        {
            auto s = make (*c.entry, pitch, c.secondary);
            const bool same = std::any_of (out.begin(), out.end(), [&] (const Spelling& o)
                                           { return o.staff == s.staff && o.symbols == s.symbols; });
            if (! same)
                out.push_back (std::move (s));
            if (out.size() >= 8)
                break;
        }
        return out;
    }

    Spelling Speller::best (double pitch, bool flats) const
    {
        auto all = spell (pitch, flats);
        if (all.empty())
        {
            Spelling s;
            s.pitch = pitch;
            return s;
        }
        return all.front();
    }

    Speller Speller::anchoredAt (double pitch) const
    {
        const auto off = best (pitch).off;
        if (! valid() || std::fabs (off) < 1e-6)
            return *this;
        auto moved = conf;
        moved.hz *= std::exp2 (off / 1200.0);
        return Speller (std::move (moved));
    }

    double Speller::next (double pitch, int direction) const
    {
        if (steps.empty())
            return pitch;
        const auto period = std::fabs (conf.equave);
        const auto rel = pitch - conf.referencePitch();
        const auto base = std::floor (rel / period) * period;
        const auto within = rel - base;
        if (direction > 0)
        {
            for (const auto s : steps)
                if (s > within + sameCents)
                    return conf.referencePitch() + base + s;
            return conf.referencePitch() + base + period + steps.front();
        }
        for (auto it = steps.rbegin(); it != steps.rend(); ++it)
            if (*it < within - sameCents)
                return conf.referencePitch() + base + *it;
        return conf.referencePitch() + base - period + steps.back();
    }
}
