// SPDX-License-Identifier: AGPL-3.0-only
#include "BolProcessor.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
            const auto kind = line.substr (1, 2);
            const auto path = folder / line;
            if (! fs::exists (path))
                continue;
            if (kind == "al") f.alphabet = path.string();
            else if (kind == "se") f.settings = path.string();
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

    Run run (const std::string& executable, const std::string& strings, const Files& files, uint32_t seed, int seconds)
    {
        Run out;
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

        Node polyOf (const std::vector<std::string>& t, size_t& i, double speed)
        {
            Node p;
            p.kind = Node::poly;
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
            p.natural = p.length = p.kids.empty() ? 0.0 : p.kids.front().length;
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
        };
        const auto f = table.find (bol);
        return f == table.end() ? std::vector<int> {} : f->second;
    }

    composition::Result toScore (const std::vector<Item>& items, double bpm)
    {
        composition::Result out;
        out.bpm = bpm;
        out.parts.push_back ({ "tablā", {}, true });
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
                const auto drums = strokes (e.symbol);
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
        out.summary = std::to_string (items.size()) + (items.size() == 1 ? " item, " : " items, ") + std::to_string (bols) + " bols on the tablā";
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
