// SPDX-License-Identifier: AGPL-3.0-only
#include "Nyquist.h"

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <locale>
#include <mutex>
#include <sstream>

#include <locale.h>

#include "nyx.h"
#include "xlisp.h"

namespace nyquist
{
    namespace
    {
        std::string runtimeFolder;
        std::vector<std::string> pluginPaths;
        std::string userPlugins;

        bool startsWith (const std::string& s, const char* prefix) { return s.rfind (prefix, 0) == 0; }

        double toDouble (const std::string& s)
        {
            std::istringstream in (s);
            in.imbue (std::locale::classic());
            double v = 0.0;
            in >> v;
            return in.fail() ? 0.0 : v;
        }

        std::string number (double v)
        {
            std::ostringstream out;
            out.imbue (std::locale::classic());
            out.precision (14);
            out << v;
            auto s = out.str();
            if (s.find_first_of (".eEn") == std::string::npos)   // keep it a float for Lisp
                s += ".0";
            return s;
        }

        // Splits a header line into tokens: words, "strings" and (lists),
        // following Audacity's NyquistBase::Tokenizer.
        struct Tokenizer
        {
            bool sl = false, q = false;
            int paren = 0;
            std::string tok;
            std::vector<std::string> tokens;

            bool tokenize (const std::string& line, bool eof, size_t trimStart, size_t trimEnd)
            {
                auto endToken = [&]
                {
                    if (! tok.empty())
                    {
                        tokens.push_back (tok);
                        tok.clear();
                    }
                };

                const auto last = line.size() > trimEnd ? line.size() - trimEnd : 0;
                for (size_t i = std::min (trimStart, line.size()); i < last; ++i)
                {
                    auto c = line[i];
                    if (q && ! sl && c == '\\')
                    {
                        sl = true;
                        continue;
                    }

                    if (! sl && c == '"')
                    {
                        if (! q)
                        {
                            if (! paren)
                                endToken();
                            tok += c;
                            q = true;
                        }
                        else
                        {
                            tok += c;
                            if (! paren)
                                endToken();
                            q = false;
                        }
                    }
                    else if (! q && ! paren && (c == ' ' || c == '\t'))
                        endToken();
                    else if (! q && c == ';')
                        break;
                    else if (! q && c == '(')
                    {
                        if (++paren == 1)
                            endToken(), tok += c;
                        else
                            tok += c;
                    }
                    else if (! q && c == ')')
                    {
                        if (--paren == 0)
                            tok += c, endToken();
                        else if (paren < 0)
                            paren = 0, endToken();
                        else
                            tok += c;
                    }
                    else
                    {
                        if (sl && paren)
                            tok += '\\';
                        if (sl && ! paren && c == 'n')
                            c = '\n';
                        tok += c;
                    }
                    sl = false;
                }

                if (eof || (! q && ! paren))
                {
                    endToken();
                    return true;
                }
                if (q)
                    tok += '\n';
                return false;
            }
        };

        // "text", (_ "text") or text. A choice may be ("Id" (_ "Shown")): `extra` gets the id.
        std::string unquote (const std::string& s, bool allowParens = true, std::string* extra = nullptr)
        {
            if (extra != nullptr)
                extra->clear();
            const auto n = s.size();
            if (n >= 2 && s.front() == '"' && s.back() == '"')
                return s.substr (1, n - 2);
            if (allowParens && n >= 2 && s.front() == '(' && s.back() == ')')
            {
                Tokenizer t;
                t.tokenize (s, true, 1, 1);
                if (t.tokens.size() < 2)
                    return {};
                if (extra != nullptr && t.tokens[1][0] == '(')
                {
                    *extra = unquote (t.tokens[0], false);
                    return unquote (t.tokens[1]);
                }
                return unquote (t.tokens[1], false);
            }
            return s;
        }

        std::vector<std::string> parseChoices (const std::string& text)
        {
            std::vector<std::string> result;
            if (text.empty())
                return result;
            if (text[0] == '(')
            {
                Tokenizer t;
                t.tokenize (text, true, 1, 1);
                std::string id;   // ("Id" (_ "Shown")): the id is not needed, the variable gets the index
                for (const auto& c : t.tokens)
                    result.push_back (unquote (c, true, &id));
                return result;
            }
            // Old style: comma separated, perhaps quoted as a whole.
            auto body = text[0] == '"' && text.size() >= 2 ? text.substr (1, text.size() - 2) : text;
            std::stringstream in (body);
            for (std::string item; std::getline (in, item, ',');)
            {
                const auto a = item.find_first_not_of (" \t"), b = item.find_last_not_of (" \t");
                result.push_back (a == std::string::npos ? std::string() : item.substr (a, b - a + 1));
            }
            return result;
        }

        bool lower (const std::string& s, const char* word)
        {
            std::string l = s;
            std::transform (l.begin(), l.end(), l.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
            return l == word;
        }

        // One header line (after its ; or $), following NyquistBase::Parse.
        bool parseLine (Script& s, Tokenizer& tzer, const std::string& line, bool eof, bool first)
        {
            if (! tzer.tokenize (line, eof, first ? 1 : 0, 0))
                return false;

            const auto& t = tzer.tokens;
            const auto len = t.size();
            if (len < 1)
                return true;

            if (len == 2 && t[0] == "nyquist" && (t[1] == "plug-in" || t[1] == "plugin"))
            {
                s.ok = true;
                return true;
            }

            if (len >= 2 && t[0] == "type")
            {
                auto kind = t[1];
                s.typeGiven = true;
                s.inToolsMenu = false;
                if (kind == "tool")
                {
                    s.inToolsMenu = true;
                    s.kind = Kind::tool;
                    if (len >= 3)
                        kind = t[2];
                }
                if (kind == "process")
                    s.kind = Kind::process;
                else if (kind == "generate")
                    s.kind = Kind::generate;
                else if (kind == "analyze")
                    s.kind = Kind::analyze;
                if (len >= 3 && t[2] == "spectral")
                    s.spectral = true;
                return true;
            }

            if (len == 2 && t[0] == "codetype")
            {
                s.sal = t[1] == "sal";
                return true;
            }

            if (len >= 2 && t[0] == "debugflags")
            {
                for (size_t i = 1; i < len; ++i)
                    if (t[i] == "trace")
                        s.trace = true;
                    else if (t[i] == "notrace")
                        s.trace = false;
                return true;
            }

            if (len >= 2 && t[0] == "version")
            {
                const auto v = (int) toDouble (t[1]);
                if (v < 1 || v > 4)
                {
                    s.ok = false;
                    s.error = "Nyquist plug-in version " + t[1] + " is not supported";
                    return true;
                }
                s.version = v;
            }

            if (len >= 2 && t[0] == "name")
            {
                s.name = unquote (t[1]);
                if (s.name.size() > 3 && s.name.compare (s.name.size() - 3, 3, "...") == 0)
                    s.name.resize (s.name.size() - 3);
                return true;
            }
            if (len >= 2 && t[0] == "action")    { s.action = unquote (t[1]); return true; }
            if (len >= 2 && t[0] == "info")      { s.info = unquote (t[1]); return true; }
            if (len >= 2 && t[0] == "author")    { s.author = unquote (t[1]); return true; }
            if (len >= 2 && t[0] == "release")   { s.release = unquote (t[1]); return true; }
            if (len >= 2 && t[0] == "copyright") { s.copyright = unquote (t[1]); return true; }
            if (len >= 2 && t[0] == "maxlen")
            {
                s.maxLength = (long long) toDouble (t[1]);
                return true;
            }

            if (len >= 3 && t[0] == "control")
            {
                Control c;
                if (len == 3 && t[1] == "text")
                {
                    c.var = t[1];
                    c.label = unquote (t[2]);
                    c.type = Control::Type::text;
                }
                else if (len >= 5)
                {
                    c.var = t[1];
                    c.name = unquote (t[2]);
                    c.label = t[4];
                    auto valueText = len > 5 ? t[5] : std::string();
                    c.value = toDouble (valueText);
                    if (! valueText.empty() && (valueText[0] == '(' || valueText[0] == '"'))
                        valueText = unquote (valueText);
                    c.text = valueText;

                    const auto& type = t[3];
                    if (type == "string")
                    {
                        c.type = Control::Type::string;
                        c.label = unquote (c.label);
                    }
                    else if (type == "choice")
                    {
                        c.type = Control::Type::choice;
                        c.choices = parseChoices (c.label);
                        c.label.clear();
                        c.high = std::max (0.0, (double) c.choices.size() - 1.0);
                        c.value = std::clamp (c.value, 0.0, c.high);
                    }
                    else if (type == "file")
                    {
                        c.type = Control::Type::file;
                        c.fileTypes = len > 6 ? unquote (t[6]) : std::string();
                        c.fileFlags = len > 7 ? unquote (t[7]) : std::string();
                        c.label = unquote (c.label);
                    }
                    else
                    {
                        c.label = unquote (c.label);
                        if (len < 8)
                            return true;
                        if (type == "float" || type == "real")
                            c.type = Control::Type::real;
                        else if (type == "int")
                            c.type = Control::Type::integer;
                        else if (type == "float-text")
                            c.type = Control::Type::realText;
                        else if (type == "int-text")
                            c.type = Control::Type::intText;
                        else if (type == "time")
                            c.type = Control::Type::time;
                        else
                            return true;   // unknown control type: left out, as Audacity does

                        const auto lowText = unquote (t[6]), highText = unquote (t[7]);
                        const bool text = c.type == Control::Type::realText || c.type == Control::Type::intText;
                        c.low = lower (lowText, "nil") ? (c.type == Control::Type::time ? 0.0 : text ? -1.0e9 : 0.0) : toDouble (lowText);
                        c.high = lower (highText, "nil") ? (text || c.type == Control::Type::time ? 1.0e9 : 1.0) : toDouble (highText);
                        c.high = std::max (c.high, c.low);
                        c.value = std::clamp (c.value, c.low, c.high);
                    }
                }
                else
                    return true;
                s.controls.push_back (c);
            }
            return true;
        }

        //======================================================================
        // Lisp functions PHI adds, as Audacity does: (_ "text") and friends
        // return their text (no translations yet), and AUD-DO reports that
        // Audacity's scripting commands are not available.
        LVAL lispGettext()
        {
            auto* s = getstring (xlgastring());
            if (moreargs())
                nextarg();
            xllastarg();
            return cvstring ((const char*) s);
        }

        LVAL lispNgettext()
        {
            auto* one = getstring (xlgastring());
            auto* many = getstring (xlgastring());
            const auto n = getfixnum (xlgafixnum());
            if (moreargs())
                nextarg();
            xllastarg();
            return cvstring ((const char*) (n == 1 ? one : many));
        }

        LVAL lispAudDo()
        {
            xlgastring();
            xllastarg();
            return cons (cvstring ("AUD-DO (Audacity scripting) is not available in PHI"), NIL);
        }

        void registerFunctions()
        {
            static bool done = false;
            if (done)
                return;
            done = true;
            static const FUNDEF functions[] = {
                { "_", SUBR, lispGettext }, { "_C", SUBR, lispGettext },
                { "NGETTEXT", SUBR, lispNgettext }, { "NGETTEXTC", SUBR, lispNgettext },
                { "AUD-DO", SUBR, lispAudDo },
            };
            xlbindfunctions (functions, sizeof (functions) / sizeof (functions[0]));
        }

        std::mutex running;   // libnyquist keeps global state: one script at a time

        //======================================================================
        // What one pass over a group of channels (mono or a stereo pair) needs.
        struct Pass
        {
            const AudioData* audio = nullptr;
            std::vector<int> channels;
            Frame start = 0, length = 0;
            const Progress* progress = nullptr;
            float base = 0.0f, share = 1.0f;   // of the whole job's progress
            float inDone = 0.0f, outDone = 0.0f;

            // Input, read a chunk at a time.
            Frame cacheStart = -1;
            int cacheLength = 0;
            std::vector<std::vector<float>> cache;

            // Output, a block per channel in lock step.
            std::vector<std::vector<float>> out;

            std::string* output = nullptr;
            bool cancelled = false;

            void report()
            {
                if (progress != nullptr && progress->fraction != nullptr)
                    progress->fraction->store (base + share * 0.5f * (std::min (inDone, 1.0f) + std::min (outDone, 1.0f)));
            }

            bool stopRequested() const { return progress != nullptr && progress->cancel != nullptr && progress->cancel->load(); }

            int get (float* buffer, int channel, std::int64_t offset, std::int64_t len)
            {
                if (channel < 0 || channel >= (int) channels.size() || len < 0)
                    return -1;
                const auto from = start + (Frame) offset;
                if (cacheStart < 0 || from < cacheStart || from + len > cacheStart + cacheLength)
                {
                    constexpr int chunk = 1 << 16;
                    cacheStart = from;
                    cacheLength = (int) std::max<std::int64_t> (len, chunk);
                    const auto everything = audio->numChannels();
                    cache.assign ((size_t) everything, std::vector<float> ((size_t) cacheLength));
                    std::vector<float*> ptrs;
                    for (auto& c : cache)
                        ptrs.push_back (c.data());
                    // Nothing past the selection: Nyquist sees exactly the selection.
                    const auto inside = (int) std::clamp<Frame> (start + length - from, 0, cacheLength);
                    audio->read (from, inside, ptrs.data());
                    for (auto& c : cache)
                        std::fill (c.begin() + inside, c.end(), 0.0f);
                }
                const auto& src = cache[(size_t) channels[(size_t) channel]];
                std::copy_n (src.begin() + (from - cacheStart), len, buffer);
                if (channel == 0 && length > 0)
                {
                    inDone = (float) (offset + len) / (float) length;
                    report();
                }
                return stopRequested() ? -1 : 0;
            }

            int put (const float* buffer, int channel, std::int64_t offset, std::int64_t len, std::int64_t total)
            {
                if (channel < 0 || channel >= (int) out.size())
                    return -1;
                auto& o = out[(size_t) channel];
                o.insert (o.end(), buffer, buffer + len);
                if (channel == 0)
                {
                    outDone = total > 0 ? (float) (offset + len) / (float) total : 0.5f;
                    report();
                }
                return stopRequested() ? -1 : 0;
            }

            static int getCallback (float* buffer, int channel, std::int64_t start, std::int64_t len, std::int64_t, void* user)
            {
                return static_cast<Pass*> (user)->get (buffer, channel, start, len);
            }

            static int putCallback (float* buffer, int channel, std::int64_t start, std::int64_t len, std::int64_t total, void* user)
            {
                return static_cast<Pass*> (user)->put (buffer, channel, start, len, total);
            }

            static void outputCallback (int c, void* user)
            {
                auto* self = static_cast<Pass*> (user);
                if (self->output != nullptr && self->output->size() < (1u << 20))
                    *self->output += (char) c;
            }

            static void osCallback (void* user)
            {
                auto* self = static_cast<Pass*> (user);
                if (self->stopRequested() && ! self->cancelled)
                {
                    self->cancelled = true;
                    nyx_stop();
                }
            }
        };

        std::string home()
        {
            const char* h = std::getenv ("HOME");
            return h != nullptr ? h : "/";
        }

        std::string documents()
        {
            namespace fs = std::filesystem;
            const auto d = home() + "/Documents";
            std::error_code ec;
            return fs::is_directory (d, ec) ? d : home();
        }

        // The variables Audacity sets for version 4 plug-ins (see the Nyquist plug-in reference).
        std::string systemProps (const Script& script, const Input& in, int channels)
        {
            std::string p;
            p += "(putprop '*AUDACITY* (list 3 7 9) 'VERSION)\n";   // what the plug-ins were written against
            p += "(putprop '*AUDACITY* \"en\" 'LANGUAGE)\n";
            p += "(setf *DECIMAL-SEPARATOR* #\\.)\n";
            p += "(putprop '*SYSTEM-DIR* \"" + escape (runtimeFolder) + "\" 'BASE)\n";
            p += "(putprop '*SYSTEM-DIR* \"" + escape (home() + "/.local/share/phi") + "\" 'DATA)\n";
            p += "(putprop '*SYSTEM-DIR* \"\" 'HELP)\n";
            p += "(putprop '*SYSTEM-DIR* \"/tmp\" 'TEMP)\n";
            p += "(putprop '*SYSTEM-DIR* \"/tmp\" 'SYS-TEMP)\n";
            p += "(putprop '*SYSTEM-DIR* \"" + escape (documents()) + "\" 'DOCUMENTS)\n";
            p += "(putprop '*SYSTEM-DIR* \"" + escape (home()) + "\" 'HOME)\n";
            std::string list;
            for (const auto& f : pluginPaths)
                list += "\"" + escape (f) + "\" ";
            p += "(putprop '*SYSTEM-DIR* (list " + list + ") 'PLUGIN)\n";
            p += "(putprop '*SYSTEM-DIR* (list " + list + ") 'PLUG-IN)\n";
            p += "(putprop '*SYSTEM-DIR* \"" + escape (userPlugins) + "\" 'USER-PLUG-IN)\n";

            const auto now = std::time (nullptr);
            std::tm t {};
            localtime_r (&now, &t);
            char date[64], clock[64], iso[64], isoTime[64], month[64], day[64];
            std::strftime (date, sizeof date, "%x", &t);
            std::strftime (clock, sizeof clock, "%X", &t);
            std::strftime (iso, sizeof iso, "%Y-%m-%d", &t);
            std::strftime (isoTime, sizeof isoTime, "%H:%M:%S", &t);
            std::strftime (month, sizeof month, "%B", &t);
            std::strftime (day, sizeof day, "%A", &t);
            p += "(setf *SYSTEM-TIME* (list " + std::to_string (t.tm_year + 1900) + " " + std::to_string (t.tm_yday + 1) + " "
               + std::to_string (t.tm_hour) + " " + std::to_string (t.tm_min) + " " + std::to_string (t.tm_sec) + "))\n";
            p += "(putprop '*SYSTEM-TIME* \"" + escape (date) + "\" 'DATE)\n";
            p += "(putprop '*SYSTEM-TIME* \"" + escape (clock) + "\" 'TIME)\n";
            p += "(putprop '*SYSTEM-TIME* \"" + std::string (iso) + "\" 'ISO-DATE)\n";
            p += "(putprop '*SYSTEM-TIME* \"" + std::string (isoTime) + "\" 'ISO-TIME)\n";
            p += "(putprop '*SYSTEM-TIME* " + std::to_string (t.tm_year + 1900) + " 'YEAR)\n";
            p += "(putprop '*SYSTEM-TIME* " + std::to_string (t.tm_mday) + " 'DAY)\n";
            p += "(putprop '*SYSTEM-TIME* " + std::to_string (t.tm_mon) + " 'MONTH)\n";
            p += "(putprop '*SYSTEM-TIME* \"" + escape (month) + "\" 'MONTH-NAME)\n";
            p += "(putprop '*SYSTEM-TIME* \"" + escape (day) + "\" 'DAY-NAME)\n";

            const auto rate = in.audio != nullptr ? in.audio->sampleRate() : in.sampleRate;
            p += "(putprop '*PROJECT* 1 'PROJECTS)\n";
            p += "(putprop '*PROJECT* \"" + escape (in.projectName) + "\" 'NAME)\n";
            p += "(putprop '*PROJECT* (float " + number (rate) + ") 'RATE)\n";
            p += "(putprop '*PROJECT* " + std::string (in.audio != nullptr ? "1" : "0") + " 'TRACKS)\n";
            p += "(putprop '*PROJECT* " + std::string (in.audio != nullptr ? "1" : "0") + " 'WAVETRACKS)\n";
            p += "(putprop '*PROJECT* 0 'LABELTRACKS)\n(putprop '*PROJECT* 0 'MIDITRACKS)\n(putprop '*PROJECT* 0 'TIMETRACKS)\n";
            p += "(putprop '*PROJECT* (float 6.0) 'PREVIEW-DURATION)\n";
            p += "(setf *PREVIEWP* NIL)\n";
            p += "(putprop '*SELECTION* (list " + std::string (in.audio != nullptr && script.kind != Kind::tool ? "1" : "") + ") 'TRACKS)\n";
            p += "(putprop '*SELECTION* " + std::to_string (channels) + " 'CHANNELS)\n";
            return p;
        }

        std::string selectionProps (const Input& in, double rate)
        {
            auto floatOrNil = [] (bool has, double v) { return has ? "(float " + number (v) + ")" : std::string ("nil"); };
            const bool hasLow = in.lowHz >= 0.0, hasHigh = in.highHz >= 0.0;
            std::string p;
            p += "(putprop '*SELECTION* " + floatOrNil (hasLow, in.lowHz) + " 'LOW-HZ)\n";
            p += "(putprop '*SELECTION* " + floatOrNil (hasLow && hasHigh, std::sqrt (std::max (0.0, in.lowHz * in.highHz))) + " 'CENTER-HZ)\n";
            p += "(putprop '*SELECTION* " + floatOrNil (hasHigh, in.highHz) + " 'HIGH-HZ)\n";
            const bool bandwidth = in.lowHz > 0.0 && in.highHz >= in.lowHz;
            p += "(putprop '*SELECTION* " + floatOrNil (bandwidth, bandwidth ? std::log2 (in.highHz / in.lowHz) : 0.0) + " 'BANDWIDTH)\n";
            p += "(putprop '*SELECTION* (float " + number ((double) in.start / rate) + ") 'START)\n";
            p += "(putprop '*SELECTION* (float " + number ((double) in.end / rate) + ") 'END)\n";
            return p;
        }

        std::string trackProps (const Input& in, const Pass& pass, int index)
        {
            const auto& a = *in.audio;
            const auto rate = a.sampleRate();
            const auto n = (int) pass.channels.size();
            std::string p;
            p += "(putprop '*TRACK* " + std::to_string (index) + " 'INDEX)\n";
            p += "(putprop '*TRACK* \"" + escape (in.trackName) + "\" 'NAME)\n";
            p += "(putprop '*TRACK* \"wave\" 'TYPE)\n";
            p += "(putprop '*TRACK* \"Waveform\" 'VIEW)\n";
            p += "(putprop '*TRACK* " + std::to_string (n) + " 'CHANNELS)\n";
            p += std::string ("(putprop '*TRACK* ") + (in.lowHz >= 0.0 || in.highHz >= 0.0 ? "T" : "NIL") + " 'SPECTRAL-EDIT-ENABLED)\n";
            p += "(putprop '*TRACK* (float 0.0) 'START-TIME)\n";
            p += "(putprop '*TRACK* (float " + number (a.lengthSeconds()) + ") 'END-TIME)\n";
            p += "(putprop '*TRACK* (float 1.0) 'GAIN)\n(putprop '*TRACK* (float 0.0) 'PAN)\n";
            p += "(putprop '*TRACK* (float " + number (rate) + ") 'RATE)\n";
            p += "(putprop '*TRACK* 32.0 'FORMAT)\n";

            // One clip, the whole file.
            const auto clip = "(list (float 0.0) (float " + number (a.lengthSeconds()) + "))";
            std::string clips;
            for (int i = 0; i < n; ++i)
                clips += n > 1 ? "(list " + clip + " )" : clip;
            const auto open = n == 1 ? std::string ("(list ") : std::string ("(vector ");
            p += "(putprop '*TRACK* " + open + clips + " ) 'INCLIPS)\n";
            p += "(putprop '*TRACK* " + open + clips + " ) 'CLIPS)\n";

            // Peak and RMS of each channel over the selection.
            std::vector<double> peak ((size_t) n, 0.0), sum ((size_t) n, 0.0);
            std::vector<std::vector<float>> block ((size_t) a.numChannels(), std::vector<float> (AudioChunk::maxFrames));
            std::vector<float*> ptrs;
            for (auto& b : block)
                ptrs.push_back (b.data());
            for (Frame pos = in.start; pos < in.end; pos += AudioChunk::maxFrames)
            {
                const auto count = (int) std::min<Frame> (AudioChunk::maxFrames, in.end - pos);
                a.read (pos, count, ptrs.data());
                for (int i = 0; i < n; ++i)
                    for (int k = 0; k < count; ++k)
                    {
                        const auto v = (double) block[(size_t) pass.channels[(size_t) i]][(size_t) k];
                        peak[(size_t) i] = std::max (peak[(size_t) i], std::abs (v));
                        sum[(size_t) i] += v * v;
                    }
            }
            std::string peaks, rmss;
            double peakLevel = 0.0;
            for (int i = 0; i < n; ++i)
            {
                peaks += "(float " + number (peak[(size_t) i]) + ") ";
                rmss += "(float " + number (std::sqrt (sum[(size_t) i] / (double) std::max<Frame> (1, in.end - in.start))) + ") ";
                peakLevel = std::max (peakLevel, peak[(size_t) i]);
            }
            p += n > 1 ? "(putprop '*SELECTION* (vector " + peaks + ") 'PEAK)\n" : "(putprop '*SELECTION* " + peaks + " 'PEAK)\n";
            p += "(putprop '*SELECTION* (float " + number (peakLevel) + ") 'PEAK-LEVEL)\n";
            p += n > 1 ? "(putprop '*SELECTION* (vector " + rmss + ") 'RMS)\n" : "(putprop '*SELECTION* " + rmss + " 'RMS)\n";
            return p;
        }

        std::string controlValues (const Script& script)
        {
            std::string p;
            for (const auto& c : script.controls)
            {
                using T = Control::Type;
                switch (c.type)
                {
                    case T::real: case T::realText: case T::time:
                        p += "(setf " + c.var + " " + number (c.value) + ")\n";
                        break;
                    case T::integer: case T::intText: case T::choice:
                        p += "(setf " + c.var + " " + std::to_string ((long long) std::lround (c.value)) + ")\n";
                        break;
                    case T::string: case T::file:
                        p += "(setf " + c.var + " \"" + escape (c.text) + "\")\n";
                        break;
                    case T::text:
                        break;
                }
            }
            return p;
        }

        // Switches this thread's number formatting to "C" while Nyquist runs:
        // libnyquist reads and prints numbers with the C library.
        struct CLocale
        {
            locale_t c = newlocale (LC_NUMERIC_MASK, "C", (locale_t) 0);
            locale_t previous = c != (locale_t) 0 ? uselocale (c) : (locale_t) 0;
            ~CLocale()
            {
                if (c != (locale_t) 0)
                {
                    uselocale (previous);
                    freelocale (c);
                }
            }
        };
    }

    //==========================================================================
    std::string escape (const std::string& s)
    {
        std::string out;
        out.reserve (s.size());
        for (const auto c : s)
        {
            if (c == '\\' || c == '"')
                out += '\\';
            out += c;
        }
        return out;
    }

    Script parse (const std::string& text, bool prompt)
    {
        Script s;
        s.ok = prompt;
        if (prompt)
            s.version = 4;

        std::vector<std::string> lines;
        {
            std::stringstream in (text);
            for (std::string line; std::getline (in, line);)
            {
                if (! line.empty() && line.back() == '\r')
                    line.pop_back();
                lines.push_back (line);
            }
        }

        bool foundType = false;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            auto line = lines[i];
            if (line.size() > 1 && (line[0] == ';' || line[0] == '$'))
            {
                Tokenizer tzer;
                const bool control = line[0] == '$' || startsWith (line, ";control");
                size_t used = 1;
                bool first = true;
                while (! parseLine (s, tzer, line, ! control || i + 1 >= lines.size(), first))
                {
                    first = false;
                    line = lines[++i];
                    ++used;
                }
                // Header lines stay as blank lines, so SAL error line numbers are right.
                s.code.append (used, '\n');
                if (startsWith (lines[i + 1 - used], ";codetype") || startsWith (lines[i + 1 - used], "$codetype"))
                    foundType = true;
            }
            else
            {
                if (! foundType && ! line.empty())
                {
                    if (line[0] == '(' || (line[0] == '#' && line.size() > 1 && line[1] == '|'))
                    {
                        s.sal = false;
                        foundType = true;
                    }
                    else
                    {
                        auto upper = line;
                        std::transform (upper.begin(), upper.end(), upper.begin(), [] (unsigned char c) { return (char) std::toupper (c); });
                        if (upper.find ("RETURN") != std::string::npos)
                        {
                            s.sal = true;
                            foundType = true;
                        }
                    }
                }
                s.code += line + "\n";
            }
        }

        if (prompt && ! foundType)
        {
            s.ok = false;
            s.error = "Your code looks like SAL, but there is no 'return' statement. For SAL, use a return statement "
                      "such as:\n    return *track* * 0.1\nFor Lisp, begin with an open parenthesis such as:\n    (mult *track* 0.1)";
        }
        if (! prompt && ! s.ok && s.error.empty())
            s.error = "This is not a Nyquist plug-in (no \";nyquist plug-in\" line)";
        return s;
    }

    Script load (const std::string& path)
    {
        std::ifstream in (path, std::ios::binary);
        if (! in)
        {
            Script s;
            s.path = path;
            s.error = "Could not open " + path;
            return s;
        }
        std::stringstream text;
        text << in.rdbuf();
        auto s = parse (text.str());
        s.path = path;
        if (s.name.empty())
            s.name = std::filesystem::path (path).stem().string();
        return s;
    }

    void setFolders (const std::string& runtime, const std::vector<std::string>& plugins, const std::string& user)
    {
        std::lock_guard lock (running);
        runtimeFolder = runtime;
        pluginPaths = plugins;
        userPlugins = user;
        // Nyquist's own (load "file") looks in these, the runtime first.
        std::string path = runtime;
        for (const auto& p : plugins)
            path += ";" + p;
        nyx_set_xlisp_path (path.c_str());
    }

    const std::vector<std::string>& pluginFolders() { return pluginPaths; }
    const std::string& userPluginFolder()           { return userPlugins; }

    std::string resolveFilePath (const std::string& given, const std::string& extension)
    {
        namespace fs = std::filesystem;
        auto path = given;
        const auto a = path.find_first_not_of (" \t"), b = path.find_last_not_of (" \t");
        path = a == std::string::npos ? std::string() : path.substr (a, b - a + 1);

        const std::pair<const char*, std::string> keys[] {
            { "*home*", home() }, { "~", home() }, { "*default*", documents() },
            { "*export*", documents() }, { "*save*", documents() }, { "*config*", home() + "/.config/phi" },
        };
        auto lookup = [&] (const std::string& k) -> const std::string*
        {
            for (const auto& [key, value] : keys)
                if (k == key)
                    return &value;
            return nullptr;
        };

        const auto slash = path.find ('/');
        if (slash == std::string::npos)
        {
            if (path.empty())
                path = "*default*";
            if (const auto* dir = lookup (path))
                path = *dir + "/";
            else
                path = documents() + "/" + path;
        }
        else if (const auto* dir = lookup (path.substr (0, slash)))
            path = *dir + path.substr (slash);

        if (! path.empty() && path.back() == '/')
            path += extension.empty() ? "untitled" : "untitled." + extension;
        return path;
    }

    //==========================================================================
    Result run (const Script& script, const Input& in, const Progress& progress)
    {
        Result result;
        if (! script.ok)
        {
            result.type = Result::Type::error;
            result.text = script.error;
            return result;
        }

        std::lock_guard lock (running);
        CLocale cLocale;
        registerFunctions();

        const bool tool = script.kind == Kind::tool;
        const bool generate = script.kind == Kind::generate;
        const bool hasAudio = in.audio != nullptr && in.audio->numChannels() > 0;
        const bool readsAudio = ! tool && ! generate;
        if (readsAudio && (! hasAudio || in.end <= in.start))
        {
            result.type = Result::Type::error;
            result.text = "Select some audio first.";
            return result;
        }

        const auto rate = hasAudio ? in.audio->sampleRate() : in.sampleRate;
        const auto fileChannels = hasAudio ? in.audio->numChannels() : 0;

        // Nyquist takes mono or stereo; more channels go through one at a time.
        std::vector<std::vector<int>> groups;
        if (tool || generate || fileChannels <= 2)
        {
            std::vector<int> all;
            for (int c = 0; c < (readsAudio ? fileChannels : 0); ++c)
                all.push_back (c);
            groups.push_back (all);
        }
        else
            for (int c = 0; c < fileChannels; ++c)
                groups.push_back ({ c });

        const auto props = script.version >= 4 ? systemProps (script, in, readsAudio ? fileChannels : 0) : std::string();
        auto selectionLength = in.end - in.start;
        if (script.maxLength > 0)
            selectionLength = std::min<Frame> (selectionLength, script.maxLength);

        std::vector<std::vector<float>> outputs ((size_t) std::max (1, fileChannels));
        int generatedChannels = 0;
        bool producedAudio = false;

        for (size_t g = 0; g < groups.size(); ++g)
        {
            Pass pass;
            pass.audio = in.audio;
            pass.channels = groups[g];
            pass.start = in.start;
            pass.length = readsAudio ? selectionLength : 0;
            pass.progress = &progress;
            pass.base = (float) g / (float) groups.size();
            pass.share = 1.0f / (float) groups.size();
            pass.output = &result.output;
            const auto n = (int) pass.channels.size();

            nyx_init();
            nyx_set_os_callback (Pass::osCallback, &pass);
            nyx_capture_output (Pass::outputCallback, &pass);

            std::string cmd = "(snd-set-latency  0.1)";
            if (tool)
                cmd += "(setf S 0.25)\n";
            else if (script.version >= 4)
            {
                nyx_set_audio_name ("*TRACK*");
                cmd += "(setf S 0.25)\n";
            }
            else
            {
                nyx_set_audio_name ("S");
                cmd += "(setf *TRACK* '*unbound*)\n";
            }

            if (script.version >= 4)
            {
                cmd += props;
                cmd += selectionProps (in, rate);
                if (readsAudio)
                    cmd += trackProps (in, pass, (int) g + 1);
            }

            if (tool)
                nyx_set_audio_params (44100, 0);
            else if (generate)
                nyx_set_audio_params (rate, 0);
            else
            {
                nyx_set_audio_params (rate, pass.length);
                nyx_set_input_audio (Pass::getCallback, &pass, n, pass.length, rate);
            }

            if (generate)
                cmd += "(setf s 0.25)\n";
            cmd += in.debug || script.trace ? "(setf *tracenable* T)\n" : "(setf *tracenable* NIL)\n";
            cmd += controlValues (script);

            if (script.sal)
            {
                auto sal = escape (script.code) + "\nset aud:result = main()\n";
                if (in.debug || script.trace)
                    cmd += "(setf *tracenable* nil)\n(setf *breakenable* nil)\n(setf *sal-traceback* t)\n";
                cmd += "(setf *sal-call-stack* nil)\n(setf aud:result nil)\n";
                cmd += "(sal-compile-audacity \"" + sal + "\" t t nil)\n";
                cmd += "(prog1 aud:result (setf aud:result nil))\n";
            }
            else
                cmd += script.code;

            const auto rval = nyx_eval_expression (cmd.c_str());
            bool stop = false;

            if (pass.cancelled)
            {
                result.type = Result::Type::cancelled;
                stop = true;
            }
            else if (tool && (rval == nyx_audio || rval == nyx_labels))
            {
                result.type = Result::Type::error;
                result.text = rval == nyx_audio ? "A ';type tool' effect cannot return audio." : "A ';type tool' effect cannot return labels.";
                stop = true;
            }
            else if (rval == nyx_error)
            {
                result.type = Result::Type::error;
                result.text = "Nyquist reported an error.";
                stop = true;
            }
            else if (rval == nyx_string)
            {
                const auto* s = nyx_get_string();
                result.text = s != nullptr ? s : "";
                result.type = result.text.empty() ? Result::Type::nothing : Result::Type::text;
                stop = true;   // a message ends the run, as in Audacity
            }
            else if (rval == nyx_double || rval == nyx_int)
            {
                result.type = Result::Type::number;
                result.text = rval == nyx_int ? std::to_string (nyx_get_int()) : number (nyx_get_double());
                stop = true;
            }
            else if (rval == nyx_list)
            {
                result.type = Result::Type::text;
                result.text = "Nyquist returned a list.";
                stop = true;
            }
            else if (rval == nyx_labels)
            {
                const auto offset = (double) in.start / rate;
                for (unsigned i = 0, count = nyx_get_num_labels(); i < count; ++i)
                {
                    double t0 = 0.0, t1 = 0.0;
                    const char* text = nullptr;
                    nyx_get_label (i, &t0, &t1, &text);
                    result.labels.push_back ({ offset + t0, offset + t1, text != nullptr ? text : "" });
                }
                result.type = Result::Type::labels;
            }
            else if (rval == nyx_audio)
            {
                const auto outChannels = nyx_get_audio_num_channels();
                if (outChannels <= 0 || (readsAudio && outChannels > n))
                {
                    result.type = Result::Type::error;
                    result.text = outChannels <= 0 ? "Nyquist returned audio it cannot hand back (an empty array, or one channel as an array)."
                                                   : "Nyquist returned more audio channels than it was given.";
                    stop = true;
                }
                else
                {
                    pass.out.assign ((size_t) outChannels, {});
                    const auto ok = nyx_get_audio (Pass::putCallback, &pass);
                    if (pass.cancelled)
                    {
                        result.type = Result::Type::cancelled;
                        stop = true;
                    }
                    else if (! ok || pass.out[0].empty())
                    {
                        result.type = Result::Type::error;
                        result.text = ok ? "Nyquist returned no audio." : "Nyquist stopped while computing the audio.";
                        stop = true;
                    }
                    else if (readsAudio)
                    {
                        // A mono result for a stereo pair goes to both channels.
                        for (int c = 0; c < n; ++c)
                            outputs[(size_t) pass.channels[(size_t) c]] = pass.out[(size_t) std::min (c, outChannels - 1)];
                        producedAudio = true;
                    }
                    else
                    {
                        outputs = std::move (pass.out);
                        generatedChannels = outChannels;
                        producedAudio = true;
                    }
                }
            }

            nyx_capture_output (nullptr, nullptr);
            nyx_set_os_callback (nullptr, nullptr);
            nyx_cleanup();
            if (stop)
                break;
        }

        if (producedAudio && (result.type == Result::Type::nothing || result.type == Result::Type::labels))
        {
            // Generated audio follows the file's channel count when there is a file.
            auto channels = readsAudio ? fileChannels : (hasAudio ? fileChannels : generatedChannels);
            std::vector<std::vector<float>> rows ((size_t) channels);
            if (readsAudio)
                rows = std::move (outputs);
            else if (generatedChannels >= channels)
            {
                for (int c = 0; c < channels; ++c)
                    rows[(size_t) c] = outputs[(size_t) c];
                if (generatedChannels > channels && channels == 1)   // stereo into a mono file: mixed
                    for (size_t i = 0; i < rows[0].size(); ++i)
                        rows[0][i] = 0.5f * (outputs[0][i] + outputs[1][i]);
            }
            else
                for (int c = 0; c < channels; ++c)
                    rows[(size_t) c] = outputs[(size_t) std::min (c, generatedChannels - 1)];

            size_t length = 0;
            for (const auto& r : rows)
                length = std::max (length, r.size());
            for (auto& r : rows)
                r.resize (length, 0.0f);

            AudioData::Builder builder (channels, rate);
            for (size_t pos = 0; pos < length; pos += AudioChunk::maxFrames)
            {
                const auto count = (int) std::min<size_t> (AudioChunk::maxFrames, length - pos);
                std::vector<const float*> ptrs;
                for (const auto& r : rows)
                    ptrs.push_back (r.data() + pos);
                builder.append (ptrs.data(), count);
            }
            result.audio = builder.finish();
            result.type = Result::Type::audio;
        }

        if (progress.fraction != nullptr)
            progress.fraction->store (1.0f);
        return result;
    }
}
