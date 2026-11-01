// SPDX-License-Identifier: AGPL-3.0-only
#include "BolProcessor.h"
#include "../sampler/PhiFrameDrum.h"
#include "../sampler/PhiTabla.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <thread>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace bolprocessor
{
    namespace fs = std::filesystem;

    namespace
    {
        std::string slurp (const fs::path& p)
        {
            std::ifstream in (p, std::ios::binary);
            std::ostringstream s;
            s << in.rdbuf();
            return s.str();
        }

        std::string trim (std::string s)
        {
            while (! s.empty() && std::isspace ((unsigned char) s.back()))
                s.pop_back();
            size_t i = 0;
            while (i < s.size() && std::isspace ((unsigned char) s[i]))
                ++i;
            return s.substr (i);
        }
    }

    //==========================================================================
    Files companions (const std::string& grammarPath)
    {
        Files f;
        f.grammar = grammarPath;
        const auto folder = fs::path (grammarPath).parent_path();
        std::istringstream in (slurp (grammarPath));
        std::string line;
        // The header names its files before the first rule or subgrammar type.
        for (int n = 0; std::getline (in, line) && n < 40; ++n)
        {
            line = trim (line);
            if (line.size() < 4 || line[0] != '-' || line[3] != '.')
                continue;
            auto kind = line.substr (1, 2);
            auto path = folder / line;
            // BP1 and BP2 called the alphabet a homomorphism file (-ho.dhati); BP3 keeps it as -al.
            if (kind == "ho")
            {
                kind = "al";
                if (! fs::exists (path))
                    path = folder / ("-al" + line.substr (3));
            }
            if (! fs::exists (path))
                continue;
            if (kind == "al") f.alphabet = path.string();
            else if (kind == "se" && slurp (path.string()).find ('{') != std::string::npos) f.settings = path.string();   // BP3's JSON; BP2's settings it cannot read
            else if (kind == "so") f.soundObjects = path.string();
            else if (kind == "to") f.tonality = path.string();
            else if (kind == "cs") f.csound = path.string();
        }
        return f;
    }

    std::vector<std::string> arguments (const Files& f, uint32_t seed, const std::string& itemsOut, const std::string& midiOut)
    {
        std::vector<std::string> a { "produce", "-gr", f.grammar };
        for (const auto& [flag, path] : { std::pair { "-al", f.alphabet }, { "-se", f.settings }, { "-so", f.soundObjects },
                                          { "-to", f.tonality }, { "-cs", f.csound } })
            if (! path.empty())
                a.insert (a.end(), { flag, path });
        a.insert (a.end(), { "-o", itemsOut, "--midiout", midiOut });
        if (seed != 0)
            a.insert (a.end(), { "--seed", std::to_string (seed) });
        return a;
    }

    bool isData (const std::string& path)
    {
        const auto name = fs::path (path).filename().string();
        return name.rfind ("-da.", 0) == 0 || fs::path (path).extension() == ".bpda";
    }

    Run run (const std::string& executable, const std::string& strings, const Files& files, uint32_t seed, int seconds)
    {
        Run out;
        if (isData (files.grammar))
        {
            // A data file is items already: its blocks, without the header, comments and [remarks].
            std::istringstream in (slurp (files.grammar));
            std::string line;
            while (std::getline (in, line))
            {
                if (const auto c = line.find ("//"); c != std::string::npos)
                    line.erase (c);
                for (size_t a; (a = line.find ('[')) != std::string::npos;)
                    line.erase (a, line.find (']', a) == std::string::npos ? std::string::npos : line.find (']', a) - a + 1);
                const auto t = trim (line);
                if (t.size() >= 4 && t[0] == '-' && t[3] == '.' && std::isalpha ((unsigned char) t[1]))
                    continue;
                out.items += t + "\n";
            }
            out.ok = ! splitItems (out.items).empty();
            out.log = out.ok ? "Read the items in " + files.grammar : "No items in " + files.grammar;
            (void) executable; (void) strings; (void) seed; (void) seconds;
            return out;
        }
        std::error_code ec;
        auto base = fs::temp_directory_path (ec) / "phi-bp3-XXXXXX";
        std::string pattern = base.string();
        if (mkdtemp (pattern.data()) == nullptr)
        {
            out.log = "Could not make a folder for the Bol Processor to work in.";
            return out;
        }
        const fs::path work (pattern);
        // BP3 writes its own files beside the folder it runs in ("../temp_bolprocessor"): both inside `work`.
        const auto here = work / "run";
        fs::create_directories (here, ec);
        fs::copy_file (strings, here / "console_strings.json", fs::copy_options::overwrite_existing, ec);
        const auto items = (work / "items.txt").string(), midi = (work / "items.mid").string(), log = (work / "log.txt").string();
        auto args = arguments (files, seed, items, midi);
        args.insert (args.begin(), executable);

        const auto pid = fork();
        if (pid == 0)
        {
            if (chdir (here.c_str()) != 0)
                _exit (126);
            const int fd = open (log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0)
            {
                dup2 (fd, 1);
                dup2 (fd, 2);
                close (fd);
            }
            const int null = open ("/dev/null", O_RDONLY);
            if (null >= 0)
                dup2 (null, 0);
            std::vector<char*> argv;
            for (auto& a : args)
                argv.push_back (a.data());
            argv.push_back (nullptr);
            execv (executable.c_str(), argv.data());
            _exit (127);
        }
        int status = 0;
        bool finished = false;
        if (pid > 0)
        {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds (std::max (1, seconds));
            while (! finished)
            {
                const auto r = waitpid (pid, &status, WNOHANG);
                if (r == pid || r < 0)
                    finished = r == pid;
                if (r < 0)
                    break;
                if (! finished && std::chrono::steady_clock::now() > until)
                {
                    kill (pid, SIGKILL);
                    waitpid (pid, &status, 0);
                    break;
                }
                if (! finished)
                    std::this_thread::sleep_for (std::chrono::milliseconds (20));
            }
        }
        out.log = slurp (log);
        out.items = slurp (items);
        const auto m = slurp (midi);
        out.midi.assign (m.begin(), m.end());
        out.ok = finished && WIFEXITED (status) && WEXITSTATUS (status) == 0 && ! trim (out.items).empty();
        if (pid < 0)
            out.log = "Could not start the Bol Processor.";
        else if (! finished)
            out.log += "\nThe Bol Processor took longer than " + std::to_string (seconds) + " seconds and was stopped.";
        else if (WIFEXITED (status) && WEXITSTATUS (status) == 127)
            out.log += "\nThe Bol Processor (" + executable + ") could not be run.";
        fs::remove_all (work, ec);
        return out;
    }

    //==========================================================================
    std::vector<std::string> splitItems (const std::string& output)
    {
        std::vector<std::string> out;
        std::string current, line;
        std::istringstream in (output);
        auto flush = [&]
        {
            if (! trim (current).empty())
                out.push_back (trim (current));
            current.clear();
        };
        while (std::getline (in, line))
        {
            if (! line.empty() && line.back() == '\r')
                line.pop_back();
            if (trim (line).empty())
                flush();
            else if (trim (line).rfind ("//", 0) != 0)
                current += line + "\n";
        }
        flush();
        return out;
    }

    namespace
    {
        // The item as a tree: symbols, rests and prolongations of a length (in beats at the speed
        // they were written at), sequences, and polymetric expressions.
        struct Node
        {
            enum Kind { symbol, rest, prolong, sequence, poly } kind = sequence;
            std::string text;
            double length = 0.0;     // the time it takes in its parent
            double natural = 0.0;    // the time its own children take
            std::vector<Node> kids;
        };

        std::vector<std::string> tokens (const std::string& text)
        {
            std::vector<std::string> out;
            std::string w;
            int depth = 0;   // inside a control's parentheses, "_vel(64)", nothing splits
            auto flush = [&] { if (! w.empty()) out.push_back (w); w.clear(); };
            for (char c : text)
            {
                if (depth > 0)
                {
                    w += c;
                    depth += c == '(' ? 1 : c == ')' ? -1 : 0;
                    continue;
                }
                if (std::isspace ((unsigned char) c))
                    flush();
                else if (c == '{' || c == '}' || c == ',')
                {
                    flush();
                    out.push_back (std::string (1, c));
                }
                else
                {
                    // A speed written onto the bols around it, "dha-/4 taki", "kite/6takite": apart.
                    const bool speedHere = (c == '/' || c == '*') && w.find_first_not_of ("0123456789+") != std::string::npos;
                    const bool speedDone = std::isalpha ((unsigned char) c) && ! w.empty() && std::isdigit ((unsigned char) w.back())
                                           && (w[0] == '/' || w[0] == '*');
                    if (speedHere || speedDone)
                        flush();
                    w += c;
                    if (c == '(')
                        ++depth;
                }
            }
            flush();
            return out;
        }

        // A speed marker: "/6", "*2", or a beat pattern with one ("4+4+4+4/6"). 0 if not one.
        double speedOf (const std::string& t, double current)
        {
            const auto slash = t.rfind ('/'), star = t.rfind ('*');
            const auto at = slash != std::string::npos ? slash : star;
            if (at == std::string::npos || at + 1 >= t.size())
                return 0.0;
            for (size_t i = 0; i < at; ++i)
                if (! std::isdigit ((unsigned char) t[i]) && t[i] != '+')
                    return 0.0;
            for (size_t i = at + 1; i < t.size(); ++i)
                if (! std::isdigit ((unsigned char) t[i]) && t[i] != '.')
                    return 0.0;
            const auto v = std::atof (t.c_str() + at + 1);
            if (v <= 0.0)
                return 0.0;
            (void) current;
            return slash != std::string::npos ? v : 1.0 / v;
        }

        Node sequenceOf (const std::vector<std::string>& t, size_t& i, double speed);

        bool isBol (const std::string& s) { return ! strokes (s).empty(); }

        // A word that is bols written together, split into them (rests and prolongations too), the
        // longest bol first; empty when it is one bol or not bols at all.
        std::vector<std::string> syllables (const std::string& w)
        {
            if (w.size() < 3 || isBol (w))
                return {};
            std::vector<std::string> out;
            std::function<bool (size_t)> from = [&] (size_t at)
            {
                if (at == w.size())
                    return true;
                if (w[at] == '-' || w[at] == '_')
                {
                    out.push_back (std::string (1, w[at]));
                    if (from (at + 1))
                        return true;
                    out.pop_back();
                    return false;
                }
                for (size_t n = std::min<size_t> (5, w.size() - at); n >= 2; --n)
                {
                    const auto x = w.substr (at, n);
                    if (! isBol (x))
                        continue;
                    out.push_back (x);
                    if (from (at + n))
                        return true;
                    out.pop_back();
                }
                return false;
            };
            return from (0) ? out : std::vector<std::string> {};
        }

        // A duration written first in a polymetric expression, "{3/2, ...}": its beats, else 0.
        double durationOf (const std::string& w)
        {
            if (w.empty() || ! std::isdigit ((unsigned char) w[0]) || w.find_first_not_of ("0123456789/.") != std::string::npos)
                return 0.0;
            const auto slash = w.find ('/');
            const auto n = std::atof (w.c_str()), d = slash == std::string::npos ? 1.0 : std::atof (w.c_str() + slash + 1);
            return d > 0.0 ? n / d : 0.0;
        }

        Node polyOf (const std::vector<std::string>& t, size_t& i, double speed)
        {
            Node p;
            p.kind = Node::poly;
            double written = 0.0;
            if (i + 1 < t.size() && t[i + 1] == "," && (written = durationOf (t[i])) > 0.0)
                i += 2;
            while (i < t.size())
            {
                p.kids.push_back (sequenceOf (t, i, speed));
                if (i < t.size() && t[i] == ",")
                {
                    ++i;
                    continue;
                }
                if (i < t.size() && t[i] == "}")
                    ++i;
                break;
            }
            // The first field gives the expression its length; the others fit it.
            p.natural = p.length = written > 0.0 ? written / speed : p.kids.empty() ? 0.0 : p.kids.front().length;
            return p;
        }

        Node sequenceOf (const std::vector<std::string>& t, size_t& i, double speed)
        {
            std::vector<Node> fields (1);
            for (; i < t.size(); ++i)
            {
                const auto& w = t[i];
                if (w == "}" || w == ",")
                    break;
                auto& field = fields.back();
                if (w == "{")
                {
                    ++i;
                    field.kids.push_back (polyOf (t, i, speed));
                    --i;
                    continue;
                }
                if (w == ".")
                {
                    fields.emplace_back();
                    continue;
                }
                if (const auto s = speedOf (w, speed); s > 0.0)
                {
                    speed = s;
                    continue;
                }
                // Bols written together, as BP's alphabets let them be ("dhatidhage", "dhin--dhagena"): one a unit.
                if (const auto pieces = syllables (w); pieces.size() > 1)
                {
                    for (const auto& x : pieces)
                    {
                        Node n;
                        n.length = n.natural = 1.0 / speed;
                        n.kind = x == "-" ? Node::rest : x == "_" ? Node::prolong : Node::symbol;
                        if (n.kind == Node::symbol)
                            n.text = x;
                        field.kids.push_back (n);
                    }
                    continue;
                }
                Node n;
                n.length = n.natural = 1.0 / speed;
                if (w == "-")
                    n.kind = Node::rest;
                else if (w == "_")
                    n.kind = Node::prolong;
                else if (w[0] == '_' || w[0] == '(' || w == "&")   // a control ("_vel(64)", "_tempo(2)"): no time
                    continue;
                else
                {
                    n.kind = Node::symbol;
                    n.text = w;
                }
                field.kids.push_back (n);
            }
            for (auto& f : fields)
            {
                f.natural = 0.0;
                for (const auto& k : f.kids)
                    f.natural += k.length;
                f.length = f.natural;
            }
            if (fields.size() == 1)
                return fields.front();
            // Period notation: fields between periods take the same time, a beat each.
            Node s;
            for (auto& f : fields)
                if (! f.kids.empty())
                {
                    f.length = 1.0;
                    s.kids.push_back (std::move (f));
                }
            s.natural = s.length = (double) s.kids.size();
            return s;
        }

        void layOut (const Node& n, double start, double length, std::vector<Event>& out)
        {
            switch (n.kind)
            {
                case Node::symbol:
                    out.push_back ({ start, length, n.text });
                    return;
                case Node::rest:
                    return;
                case Node::prolong:
                    // The symbol that ends here goes on.
                    for (auto e = out.rbegin(); e != out.rend(); ++e)
                        if (std::abs (e->beat + e->length - start) < 1e-9)
                        {
                            e->length += length;
                            break;
                        }
                    return;
                case Node::poly:
                    for (const auto& k : n.kids)
                        layOut (k, start, length, out);
                    return;
                case Node::sequence:
                {
                    const auto scale = n.natural > 0.0 ? length / n.natural : 0.0;
                    auto x = start;
                    for (const auto& k : n.kids)
                    {
                        layOut (k, x, k.length * scale, out);
                        x += k.length * scale;
                    }
                    return;
                }
            }
        }
    }

    Item parse (const std::string& item)
    {
        Item out;
        out.text = item;
        const auto t = tokens (item);
        size_t i = 0;
        Node root;
        root.kind = Node::sequence;
        while (i < t.size())   // a stray '}' or ',' at the top: go on after it
        {
            auto s = sequenceOf (t, i, 1.0);
            root.kids.push_back (std::move (s));
            ++i;
        }
        for (const auto& k : root.kids)
            root.natural += k.length;
        layOut (root, 0.0, root.natural, out.events);
        out.beats = root.natural;
        std::stable_sort (out.events.begin(), out.events.end(), [] (const Event& a, const Event& b) { return a.beat < b.beat; });
        return out;
    }

    //==========================================================================
    std::vector<int> strokes (const std::string& bolIn)
    {
        std::string bol;
        for (char c : bolIn)
            bol += (char) std::tolower ((unsigned char) c);
        while (! bol.empty() && (bol.back() == '\'' || std::isdigit ((unsigned char) bol.back())))
            bol.pop_back();
        constexpr int na = 63, tin = 62, tu = 65, ge = 64, ke = 37;   // GM: open and muted high conga, high timbale, low conga, rim
        static const std::map<std::string, std::vector<int>> table {
            { "dha", { na, ge } }, { "dhaa", { na, ge } }, { "dhin", { tin, ge } }, { "dhi", { tin, ge } }, { "dhee", { tin, ge } },
            { "dhe", { tin, ge } }, { "dhet", { tin, ge } }, { "dhun", { tu, ge } },
            { "na", { na } }, { "ne", { na } }, { "naa", { na } }, { "ta", { na } }, { "taa", { na } },
            { "tin", { tin } }, { "ti", { tin } }, { "tee", { tin } }, { "te", { tin } }, { "tet", { tin } }, { "ra", { tin } }, { "re", { tin } },
            { "tu", { tu } }, { "tun", { tu } }, { "thun", { tu } },
            { "ge", { ge } }, { "ghe", { ge } }, { "ga", { ge } }, { "gi", { ge } },
            { "ke", { ke } }, { "ka", { ke } }, { "ki", { ke } }, { "kat", { ke } }, { "kran", { ke, na } },
            { "tr", { tin } }, { "kt", { ke } }, { "tak", { na } }, { "trika", { tin, ke } },
        };
        const auto f = table.find (bol);
        return f == table.end() ? std::vector<int> {} : f->second;
    }

    composition::Result toScore (const std::vector<Item>& items, double bpm, On on)
    {
        const bool phiTabla = on == On::phiTabla, frameDrum = on == On::phiFrameDrum;
        std::map<std::string, int> uses;   // PHI tabla, PHI frame drum: how often each stroke has been played, for its next take
        composition::Result out;
        out.bpm = bpm;
        out.parts.push_back ({ frameDrum ? "frame drum" : "tablā", {}, true });
        constexpr int64_t Q = composition::ticksPerQuarter;
        int64_t at = 0;
        std::map<std::string, int> unknown;
        size_t bols = 0;
        for (size_t k = 0; k < items.size(); ++k)
        {
            const auto& item = items[k];
            std::string start;
            for (size_t e = 0; e < item.events.size() && e < 8; ++e)
                start += (e > 0 ? " " : "") + item.events[e].symbol;
            out.sections.push_back ({ at, "item " + std::to_string (k + 1) + (start.empty() ? "" : ": " + start + " ...") });
            for (const auto& e : item.events)
            {
                auto drums = strokes (e.symbol);
                if (phiTabla)
                {
                    const auto note = phitabla::noteFor (e.symbol, uses[phitabla::bolOf (e.symbol)]++);
                    drums = note < 0 ? std::vector<int> {} : std::vector<int> { note };
                }
                else if (frameDrum)
                {
                    const auto note = phiframedrum::noteFor (e.symbol, uses[phiframedrum::strokeOf (e.symbol)]++);
                    drums = note < 0 ? std::vector<int> {} : std::vector<int> { note };
                }
                if (drums.empty())
                    ++unknown[e.symbol];
                else
                    ++bols;
                const auto t = at + (int64_t) std::llround (e.beat * Q);
                const auto len = std::max<int64_t> (1, (int64_t) std::llround (std::min (e.length, 1.0) * Q));
                for (auto d : drums)
                    out.parts[0].notes.push_back ({ t, t + len, d, std::abs (e.beat - std::round (e.beat)) < 1e-6 ? 96 : 80, 0.0 });
            }
            at += (int64_t) std::ceil (item.beats - 1e-9) * Q;
        }
        out.end = at;
        out.summary = std::to_string (items.size()) + (items.size() == 1 ? " item, " : " items, ") + std::to_string (bols) + (phiTabla ? " bols on PHI tabla" : frameDrum ? " strokes on PHI frame drum" : " bols on the tablā");
        if (! unknown.empty())
        {
            out.summary += "; not bols, so not played:";
            int shown = 0;
            for (const auto& [s, n] : unknown)
                if (shown++ < 8)
                    out.summary += " " + s;
        }
        return out;
    }
}
