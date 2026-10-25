// SPDX-License-Identifier: AGPL-3.0-only
#include "PhiRaga.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace phi
{
    namespace
    {
        constexpr const char* letters = "SrRgGmMPdDnN";

        std::string trim (const std::string& s)
        {
            const auto a = s.find_first_not_of (" \t\r");
            if (a == std::string::npos)
                return {};
            return s.substr (a, s.find_last_not_of (" \t\r") - a + 1);
        }

        std::vector<std::string> split (const std::string& s, char by)
        {
            std::vector<std::string> out;
            std::string part;
            std::istringstream in (s);
            while (std::getline (in, part, by))
                if (auto t = trim (part); ! t.empty())
                    out.push_back (t);
            return out;
        }

        int letterIndex (char c)
        {
            for (int i = 0; i < 12; ++i)
                if (letters[i] == c)
                    return i;
            return -1;
        }

        // "32/27", "294c" or "294.1c" -> cents; false when neither.
        bool tuningCents (const std::string& s, double& cents)
        {
            try
            {
                if (! s.empty() && (s.back() == 'c' || s.back() == 'C'))
                {
                    cents = std::stod (s.substr (0, s.size() - 1));
                    return true;
                }
                const auto slash = s.find ('/');
                if (slash == std::string::npos)
                    return false;
                const auto p = std::stod (s.substr (0, slash)), q = std::stod (s.substr (slash + 1));
                if (p <= 0.0 || q <= 0.0)
                    return false;
                cents = 1200.0 * std::log2 (p / q);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        std::string number (double v)
        {
            std::ostringstream o;
            o.precision (12);
            o << v;
            return o.str();
        }
    }

    double justCents (int semitone)
    {
        static const double ratio[12] { 1.0, 16.0 / 15, 9.0 / 8, 6.0 / 5, 5.0 / 4, 4.0 / 3,
                                        45.0 / 32, 3.0 / 2, 8.0 / 5, 5.0 / 3, 9.0 / 5, 15.0 / 8 };
        return 1200.0 * std::log2 (ratio[((semitone % 12) + 12) % 12]);
    }

    bool parsePhrase (const std::string& text, Phrase& out, std::string* bad)
    {
        out.clear();
        size_t i = 0;
        while (i < text.size())
        {
            const auto c = text[i];
            if (c == ' ' || c == '\t' || c == '-')
            {
                ++i;
                continue;
            }
            const auto at = letterIndex (c);
            if (at < 0)
            {
                if (bad != nullptr)
                    *bad = text.substr (i, text.find (' ', i) - i);
                return false;
            }
            Svara s { at, 0 };
            ++i;
            // Marks after the letter: , or a combining dot below (U+0323) lower; ' or a dot above (U+0307) upper.
            for (;;)
            {
                if (i < text.size() && text[i] == ',')
                    --s.octave, ++i;
                else if (i < text.size() && text[i] == '\'')
                    ++s.octave, ++i;
                else if (i + 1 < text.size() && (unsigned char) text[i] == 0xcc && (unsigned char) text[i + 1] == 0xa3)
                    --s.octave, i += 2;
                else if (i + 1 < text.size() && (unsigned char) text[i] == 0xcc && (unsigned char) text[i + 1] == 0x87)
                    ++s.octave, i += 2;
                else
                    break;
            }
            out.push_back (s);
        }
        return true;
    }

    std::string phraseText (const Phrase& phrase)
    {
        std::string out;
        for (const auto& s : phrase)
        {
            if (! out.empty())
                out += ' ';
            out += letters[s.semitone];
            for (int o = s.octave; o < 0; ++o)
                out += ',';
            for (int o = 0; o < s.octave; ++o)
                out += '\'';
        }
        return out;
    }

    std::vector<double> Raga::scaleCents() const
    {
        std::vector<double> out;
        for (int s = 0; s < 12; ++s)
            if (uses[(size_t) s])
                out.push_back (cents[(size_t) s]);
        return out;
    }

    std::array<bool, 12> Raga::upSet() const
    {
        if (aroha.empty())
            return uses;
        std::array<bool, 12> out {};
        for (const auto& s : aroha)
            out[(size_t) s.semitone] = true;
        return out;
    }

    std::array<bool, 12> Raga::downSet() const
    {
        if (avaroha.empty())
            return uses;
        std::array<bool, 12> out {};
        for (const auto& s : avaroha)
            out[(size_t) s.semitone] = true;
        return out;
    }

    int Raga::kanBefore (int semitone) const
    {
        for (const auto& [grace, target] : kan)
            if (target == semitone)
                return grace;
        return -1;
    }

    const Andolan* Raga::andolanOn (int semitone) const
    {
        for (const auto& a : andolan)
            if (a.semitone == semitone)
                return &a;
        return nullptr;
    }

    std::vector<Raga> parseRagas (const std::string& text, std::vector<std::string>& errors)
    {
        std::vector<Raga> out;
        Raga raga;
        bool open = false, broken = false;
        int startLine = 0;
        auto finish = [&]
        {
            if (! open)
                return;
            if (! broken)
            {
                if (raga.plain.empty())
                    errors.push_back ("line " + std::to_string (startLine) + ": " + raga.name + " has no plain name");
                else if (! raga.uses[0])
                    errors.push_back ("line " + std::to_string (startLine) + ": " + raga.name + " has no svaras");
                else
                    out.push_back (raga);
            }
            raga = {};
            open = broken = false;
        };

        std::istringstream in (text);
        std::string line;
        for (int n = 1; std::getline (in, line); ++n)
        {
            line = trim (line);
            if (line.empty() || line[0] == '#')
                continue;
            const auto colon = line.find (':');
            if (colon == std::string::npos)
            {
                errors.push_back ("line " + std::to_string (n) + ": no field name");
                broken = true;
                continue;
            }
            const auto key = trim (line.substr (0, colon)), value = trim (line.substr (colon + 1));
            auto fail = [&] (const std::string& why)
            {
                errors.push_back ("line " + std::to_string (n) + ": " + why);
                broken = true;
            };
            if (key == "raga")
            {
                finish();
                open = true;
                startLine = n;
                raga.name = value;
                continue;
            }
            if (! open)
            {
                fail ("\"" + key + "\" before the first raga:");
                broken = false;   // nothing to drop yet
                continue;
            }
            // Svaras outside the raga's own are a mistake in a phrase.
            auto phrase = [&] (const std::string& s, Phrase& p)
            {
                std::string bad;
                if (! parsePhrase (s, p, &bad))
                    return fail ("\"" + bad + "\" is not a svara"), false;
                for (const auto& sv : p)
                    if (! raga.uses[(size_t) sv.semitone])
                        return fail (std::string (1, letters[sv.semitone]) + " is not one of " + raga.name + "'s svaras"), false;
                return true;
            };
            auto oneSvara = [&] (const std::string& s, int& to)
            {
                Phrase p;
                if (! phrase (s, p) || p.size() != 1)
                    return fail ("\"" + s + "\" should be one svara"), false;
                to = p[0].semitone;
                return true;
            };

            if (key == "plain")
                raga.plain = value;
            else if (key == "thaat")
                raga.thaat = value;
            else if (key == "svaras")
            {
                raga.uses = {};
                for (const auto& token : split (value, ' '))
                {
                    const auto eq = token.find ('=');
                    const auto at = token.size() > 0 ? letterIndex (token[0]) : -1;
                    if (at < 0 || (eq == std::string::npos ? token.size() != 1 : eq != 1))
                    {
                        fail ("\"" + token + "\" is not a svara");
                        continue;
                    }
                    double c = justCents (at);
                    if (eq != std::string::npos && ! tuningCents (token.substr (eq + 1), c))
                        fail ("\"" + token.substr (eq + 1) + "\" is not a ratio (32/27) or cents (294c)");
                    raga.uses[(size_t) at] = true;
                    raga.cents[(size_t) at] = at == 0 ? 0.0 : c;
                }
                if (! raga.uses[0])
                    fail ("the svaras need Sa");
            }
            else if (key == "aroha")
                phrase (value, raga.aroha);
            else if (key == "avaroha")
                phrase (value, raga.avaroha);
            else if (key == "vadi")
                oneSvara (value, raga.vadi);
            else if (key == "samvadi")
                oneSvara (value, raga.samvadi);
            else if (key == "pakad" || key == "chalan")
                for (const auto& part : split (value, '|'))
                {
                    Phrase p;
                    if (phrase (part, p))
                        (key == "pakad" ? raga.pakad : raga.chalan).push_back (p);
                }
            else if (key == "andolan")
                for (const auto& part : split (value, ','))
                {
                    const auto words = split (part, ' ');
                    Andolan a;
                    int semitone = -1;
                    if (words.empty() || ! oneSvara (words[0], semitone))
                        continue;
                    a.semitone = semitone;
                    try
                    {
                        if (words.size() > 1)
                            a.depthCents = std::stod (words[1]);
                        if (words.size() > 2)
                            a.rateHz = std::stod (words[2]);
                    }
                    catch (...)
                    {
                        fail ("andolan is a svara, its depth in cents and its rate in Hz: \"g 20 0.5\"");
                        continue;
                    }
                    raga.andolan.push_back (a);
                }
            else if (key == "kan")
                for (const auto& part : split (value, ','))
                {
                    const auto arrow = part.find ('>');
                    int grace = -1, target = -1;
                    if (arrow == std::string::npos)
                        fail ("kan is a grace and the svara it graces: \"m>g\"");
                    else if (oneSvara (trim (part.substr (0, arrow)), grace) && oneSvara (trim (part.substr (arrow + 1)), target))
                        raga.kan.emplace_back (grace, target);
                }
            else if (key == "time")
                raga.time = value;
            else if (key == "rasa")
                raga.rasa = value;
            else
                fail ("\"" + key + "\" is not a field this sheet knows");
        }
        finish();
        return out;
    }

    std::string writeRagas (const std::vector<Raga>& ragas)
    {
        std::string out;
        for (const auto& r : ragas)
        {
            if (! out.empty())
                out += '\n';
            out += "raga: " + r.name + "\nplain: " + r.plain + "\n";
            if (! r.thaat.empty())
                out += "thaat: " + r.thaat + "\n";
            out += "svaras:";
            for (int s = 0; s < 12; ++s)
                if (r.uses[(size_t) s])
                {
                    out += ' ';
                    out += letters[s];
                    if (std::abs (r.cents[(size_t) s] - justCents (s)) > 1.0e-6 && s != 0)
                        out += "=" + number (r.cents[(size_t) s]) + "c";
                }
            out += "\n";
            auto phrases = [&] (const char* key, const std::vector<Phrase>& list)
            {
                if (list.empty())
                    return;
                out += std::string (key) + ": ";
                for (size_t i = 0; i < list.size(); ++i)
                    out += (i > 0 ? " | " : "") + phraseText (list[i]);
                out += "\n";
            };
            if (! r.aroha.empty())
                out += "aroha: " + phraseText (r.aroha) + "\n";
            if (! r.avaroha.empty())
                out += "avaroha: " + phraseText (r.avaroha) + "\n";
            if (r.vadi >= 0)
                out += "vadi: " + phraseText ({ Svara { r.vadi, 0 } }) + "\n";
            if (r.samvadi >= 0)
                out += "samvadi: " + phraseText ({ Svara { r.samvadi, 0 } }) + "\n";
            phrases ("pakad", r.pakad);
            phrases ("chalan", r.chalan);
            if (! r.andolan.empty())
            {
                out += "andolan: ";
                for (size_t i = 0; i < r.andolan.size(); ++i)
                    out += (i > 0 ? ", " : "") + phraseText ({ Svara { r.andolan[i].semitone, 0 } }) + " "
                           + number (r.andolan[i].depthCents) + " " + number (r.andolan[i].rateHz);
                out += "\n";
            }
            if (! r.kan.empty())
            {
                out += "kan: ";
                for (size_t i = 0; i < r.kan.size(); ++i)
                    out += (i > 0 ? ", " : "") + phraseText ({ Svara { r.kan[i].first, 0 } }) + ">" + phraseText ({ Svara { r.kan[i].second, 0 } });
                out += "\n";
            }
            if (! r.time.empty())
                out += "time: " + r.time + "\n";
            if (! r.rasa.empty())
                out += "rasa: " + r.rasa + "\n";
        }
        return out;
    }

    void RagaPlayer::setRaga (const Raga* r, int sa, double hz)
    {
        raga = r;
        saKey = sa;
        saHz = hz;
        up = down = {};
        if (raga == nullptr)
            return;
        up = raga->upSet();
        down = raga->downSet();
    }

    RagaPlayer::Voice RagaPlayer::noteOn (int key)
    {
        Voice v;
        v.inKey = key;
        if (lastKey >= 0 && key != lastKey)
            rising = key > lastKey;
        lastKey = key;
        if (raga == nullptr)
        {
            v.outKey = key;
            v.svara = { ((key - saKey) % 12 + 12) % 12, (int) std::floor ((key - saKey) / 12.0) };
            v.hz = saHz * std::pow (2.0, (key - saKey) / 12.0);
            return v;
        }
        const auto& allowed = rising ? up : down;
        const int step = rising ? 1 : -1;
        int rel = key - saKey;
        for (int distance = 0; distance < 12; ++distance)
        {
            const auto first = rel + distance * step, second = rel - distance * step;
            if (allowed[(size_t) (((first % 12) + 12) % 12)])
            {
                rel = first;
                break;
            }
            if (allowed[(size_t) (((second % 12) + 12) % 12)])
            {
                rel = second;
                break;
            }
        }
        v.outKey = saKey + rel;
        v.svara = { ((rel % 12) + 12) % 12, (int) std::floor (rel / 12.0) };
        v.cents = 1200.0 * v.svara.octave + raga->cents[(size_t) v.svara.semitone];
        v.hz = saHz * std::pow (2.0, v.cents / 1200.0);
        if (const auto* swing = raga->andolanOn (v.svara.semitone))
        {
            v.swingCents = swing->depthCents;
            v.swingHz = swing->rateHz;
        }
        return v;
    }

    RagaPlayer::Voice RagaPlayer::glide (const Voice& from, int key, double seconds)
    {
        auto v = noteOn (key);
        if (raga == nullptr || ornaments <= 0.0)
            return v;
        // The path: from where the string is, through each svara of the raga between, to the new one.
        const auto& allowed = rising ? up : down;
        v.path[0] = from.cents;
        v.pathCount = 1;
        const int a = from.outKey - saKey, b = v.outKey - saKey, step = b > a ? 1 : -1;
        for (int k = a + step; k != b && v.pathCount < Voice::maxPath - 1; k += step)
        {
            const auto semitone = ((k % 12) + 12) % 12;
            if (allowed[(size_t) semitone])
                v.path[(size_t) v.pathCount++] = 1200.0 * std::floor (k / 12.0) + raga->cents[(size_t) semitone];
        }
        v.path[(size_t) v.pathCount++] = v.cents;
        v.stepSeconds = std::max (0.0, seconds) / (v.pathCount - 1);
        v.hold = 0.25;   // a touch on each svara on the way
        return v;
    }

    void RagaPlayer::addKan (Voice& v, double seconds) const
    {
        if (raga == nullptr || ornaments <= 0.0 || v.pathCount > 0)
            return;
        const auto grace = raga->kanBefore (v.svara.semitone);
        if (grace < 0)
            return;
        // The grace's octave: the one nearest the svara.
        auto cents = 1200.0 * v.svara.octave + raga->cents[(size_t) grace];
        if (cents - v.cents > 600.0)
            cents -= 1200.0;
        else if (v.cents - cents > 600.0)
            cents += 1200.0;
        v.path[0] = cents;
        v.path[1] = v.cents;
        v.pathCount = 2;
        v.stepSeconds = seconds;
        v.hold = 0.7;
    }

    double RagaPlayer::hzAt (const Voice& v, double seconds) const
    {
        const auto approach = v.approachSeconds();
        if (seconds < approach && v.stepSeconds > 0.0)
        {
            const auto position = seconds / v.stepSeconds;
            const auto i = std::min ((int) position, v.pathCount - 2);
            auto f = std::clamp ((position - i - v.hold) / (1.0 - v.hold), 0.0, 1.0);
            f = f * f * (3.0 - 2.0 * f);
            const auto cents = v.path[(size_t) i] + (v.path[(size_t) i + 1] - v.path[(size_t) i]) * f;
            return saHz * std::pow (2.0, cents / 1200.0);
        }
        seconds -= approach;
        if (v.swingCents <= 0.0 || ornaments <= 0.0)
            return v.hz;
        // Āndolan: from the svara down by its depth and back, slowly, as long as it is held.
        constexpr double twoPi = 6.283185307179586;
        const auto below = ornaments * v.swingCents * 0.5 * (1.0 - std::cos (twoPi * v.swingHz * seconds));
        return v.hz * std::pow (2.0, -below / 1200.0);
    }

    const std::vector<Raga>& builtInRagas()
    {
        static const auto all = []
        {
            std::vector<std::string> errors;
            return parseRagas (builtInRagaSheets(), errors);
        }();
        return all;
    }
}
