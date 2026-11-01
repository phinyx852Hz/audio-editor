// SPDX-License-Identifier: AGPL-3.0-only
#include "DawProject.h"

#include "../core/MiniXml.h"
#include "../core/Zip.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>

namespace dawproject
{
namespace
{
    namespace fs = std::filesystem;
    using minixml::Node;

    // Numbers as XML writes them, whatever the locale (Qt sets it from the desktop's).
    double number (const std::string* text, double otherwise)
    {
        if (text == nullptr || text->empty())
            return otherwise;
        double v = otherwise;
        const auto* begin = text->data();
        if (*begin == '+')
            ++begin;
        const auto r = std::from_chars (begin, text->data() + text->size(), v);
        return r.ec == std::errc() && std::isfinite (v) ? v : otherwise;
    }

    double number (const Node& n, const char* name, double otherwise) { return number (n.attribute (name), otherwise); }

    bool flag (const Node& n, const char* name, bool otherwise)
    {
        const auto* t = n.attribute (name);
        return t == nullptr ? otherwise : (*t == "true" || *t == "1");
    }

    std::string text (const Node& n, const char* name)
    {
        const auto* t = n.attribute (name);
        return t == nullptr ? std::string() : *t;
    }

    std::string format (double v)
    {
        char buffer[64];
        const auto r = std::to_chars (buffer, buffer + sizeof (buffer), v);
        return std::string (buffer, r.ptr);
    }

    double toDb (double gain) { return gain > 1.0e-5 ? 20.0 * std::log10 (gain) : -100.0; }
    double toGain (double db) { return db <= -96.0 ? 0.0 : std::pow (10.0, db / 20.0); }

    // A Volume (or a send's): linear gain or decibels.
    double volumeDb (const Node* p, double otherwise)
    {
        if (p == nullptr)
            return otherwise;
        const auto unit = text (*p, "unit");
        const auto v = number (*p, "value", unit == "decibel" ? 0.0 : 1.0);
        return unit == "decibel" ? std::max (v, -100.0) : toDb (v);
    }

    // A Pan, onto -1..1 from its range (0..1 normalized as a rule).
    double panOf (const Node* p)
    {
        if (p == nullptr)
            return 0.0;
        const auto unit = text (*p, "unit");
        auto lo = number (*p, "min", unit == "percent" ? -100.0 : 0.0), hi = number (*p, "max", unit == "percent" ? 100.0 : 1.0);
        if (hi <= lo)
            hi = lo + 1.0;
        const auto v = number (*p, "value", (lo + hi) / 2.0);
        return std::clamp (2.0 * (v - lo) / (hi - lo) - 1.0, -1.0, 1.0);
    }

    const Node* firstOf (const Node& n, std::initializer_list<const char*> names)
    {
        for (const auto& c : n.children)
            for (const auto* name : names)
                if (c.name == name)
                    return &c;
        return nullptr;
    }

    bool holdsAudio (const Node& n)
    {
        if (n.name == "Audio")
            return true;
        if (n.name == "Notes")
            return false;
        for (const auto& c : n.children)
            if (holdsAudio (c))
                return true;
        return false;
    }

    // What a clip's content runs: the part [from, to) of the content, laid from `at` on.
    struct Segment { double from, to, at; };

    // The parts of a clip's content it plays over its length: once from playStart, or
    // from playStart to the loop's end, then the loop over and over.
    std::vector<Segment> segments (double playStart, double length, double loopStart, double loopEnd, bool loops)
    {
        std::vector<Segment> out;
        if (length <= 0.0)
            return out;
        if (! loops || loopEnd - loopStart <= 1.0e-9)
        {
            out.push_back ({ playStart, playStart + length, 0.0 });
            return out;
        }
        double at = 0.0, from = playStart >= loopEnd ? loopStart : playStart;
        while (at < length - 1.0e-9 && out.size() < 10000)
        {
            const auto to = std::min (loopEnd, from + (length - at));
            out.push_back ({ from, to, at });
            at += to - from;
            from = loopStart;
        }
        return out;
    }

    struct Reader
    {
        explicit Reader (Project& p) : project (p) {}

        Project& project;
        std::function<std::string (const std::string&, bool)> audioFile;          // a File's path to where it is
        std::function<std::vector<uint8_t> (const std::string&, bool)> stateFile;
        std::set<std::string> said;

        // Where a track's content goes: tracks or returns, by the Track's id and its Channel's.
        struct Where { bool isReturn = false; int index = -1; };
        std::map<std::string, Where> byId;
        struct PendingSend { Where from; int sendIndex; std::string destination; };
        std::vector<PendingSend> pendingSends;
        std::string masterChannel;

        void note (const std::string& s)
        {
            if (said.insert (s).second)
                project.notes.push_back (s);
        }

        double beats (double v, const std::string& unit) const { return unit == "seconds" ? v * project.bpm / 60.0 : v; }
        double seconds (double v, const std::string& unit) const { return unit == "seconds" ? v : v * 60.0 / project.bpm; }

        Track* track (const Where& w)
        {
            auto& list = w.isReturn ? project.returns : project.tracks;
            return w.index >= 0 && w.index < (int) list.size() ? &list[(size_t) w.index] : nullptr;
        }

        //======================================================================
        void devices (const Node& channel, Track& t)
        {
            const auto* list = channel.child ("Devices");
            if (list == nullptr)
                return;
            for (const auto& d : list->children)
            {
                Device device;
                device.element = d.name;
                device.role = text (d, "deviceRole");
                device.deviceId = text (d, "deviceID");
                device.name = text (d, "deviceName");
                if (device.name.empty())
                    device.name = text (d, "name");
                device.vendor = text (d, "deviceVendor");
                if (const auto* e = d.child ("Enabled"))
                    device.enabled = flag (*e, "value", true);
                if (const auto* s = d.child ("State"); s != nullptr && stateFile)
                    device.state = stateFile (text (*s, "path"), flag (*s, "external", false));
                if (! device.isPlugin())
                    note (t.name + ": " + (device.name.empty() ? device.element : device.name)
                          + " is a device of the other program, which PHI does not have");
                t.devices.push_back (std::move (device));
            }
            // The instrument first, as PHI has it.
            std::stable_partition (t.devices.begin(), t.devices.end(), [] (const Device& d) { return d.role == "instrument"; });
        }

        void mixer (const Node& channel, Track& t, const Where& where)
        {
            t.volumeDb = volumeDb (channel.child ("Volume"), 0.0);
            t.pan = panOf (channel.child ("Pan"));
            if (const auto* m = channel.child ("Mute"))
                t.mute = flag (*m, "value", false);
            t.solo = flag (channel, "solo", false);
            devices (channel, t);
            if (const auto* sends = channel.child ("Sends"))
                for (const auto& s : sends->children)
                {
                    if (s.name != "Send")
                        continue;
                    Send send;
                    send.db = volumeDb (s.child ("Volume"), -100.0);
                    send.pre = text (s, "type") == "pre";
                    if (const auto* e = s.child ("Enable"))
                        send.enabled = flag (*e, "value", true);
                    if (! send.enabled)
                        send.db = -100.0;
                    pendingSends.push_back ({ where, (int) t.sends.size(), text (s, "destination") });
                    t.sends.push_back (send);
                }
        }

        void structureTrack (const Node& n, const std::string& group)
        {
            const auto* channel = n.child ("Channel");
            const auto role = channel != nullptr ? text (*channel, "role") : std::string();
            std::vector<const Node*> inner;
            for (const auto& c : n.children)
                if (c.name == "Track")
                    inner.push_back (&c);
            const auto name = text (n, "name");
            if (role == "master")
            {
                if (channel != nullptr)
                {
                    project.masterDb = volumeDb (channel->child ("Volume"), 0.0);
                    masterChannel = text (*channel, "id");
                    if (const auto* d = channel->child ("Devices"); d != nullptr && ! d->children.empty())
                        note ("The master's devices are not carried over; PHI's Master Section is its own");
                }
                return;
            }
            if (! inner.empty())
            {
                note ("The group " + (name.empty() ? std::string ("without a name") : name)
                      + " is not kept as a group (PHI has none); its tracks are, without the group's own volume and devices");
                for (const auto* c : inner)
                    structureTrack (*c, name);
                return;
            }
            if (role == "vca")
            {
                note ("The VCA " + name + " is not carried over");
                return;
            }
            Track t;
            t.name = name;
            t.colour = text (n, "color");
            const auto content = text (n, "contentType");
            t.notes = content.find ("notes") != std::string::npos;
            t.audio = content.find ("audio") != std::string::npos;
            const auto isReturn = role == "effect";
            auto& list = isReturn ? project.returns : project.tracks;
            const Where where { isReturn, (int) list.size() };
            list.push_back (std::move (t));
            if (channel != nullptr)
            {
                mixer (*channel, list.back(), where);
                byId[text (*channel, "id")] = where;
            }
            byId[text (n, "id")] = where;
            (void) group;
        }

        void structure (const Node& s)
        {
            for (const auto& c : s.children)
                if (c.name == "Track")
                    structureTrack (c, {});
                else if (c.name == "Channel" && text (c, "role") == "master")
                {
                    project.masterDb = volumeDb (c.child ("Volume"), 0.0);
                    masterChannel = text (c, "id");
                }
            for (const auto& p : pendingSends)
            {
                auto* from = track (p.from);
                const auto it = byId.find (p.destination);
                if (from == nullptr)
                    continue;
                auto& send = from->sends[(size_t) p.sendIndex];
                if (it != byId.end() && it->second.isReturn)
                    send.target = it->second.index;
                else
                {
                    send.target = -1;
                    note (from->name + ": a send to something other than an effect track is not carried over");
                }
            }
            for (auto& list : { &project.tracks, &project.returns })
                for (auto& t : *list)
                    t.sends.erase (std::remove_if (t.sends.begin(), t.sends.end(), [] (const Send& s) { return s.target < 0; }), t.sends.end());
        }

        //======================================================================
        // Notes, in beats on the content's own timeline.
        void contentNotes (const Node& container, const std::string& unit, std::vector<Note>& out)
        {
            for (const auto& c : container.children)
            {
                const auto u = c.attribute ("timeUnit") != nullptr ? text (c, "timeUnit") : unit;
                if (c.name == "Notes")
                    for (const auto& n : c.children)
                    {
                        if (n.name != "Note")
                            continue;
                        Note note;
                        note.time = beats (number (n, "time", 0.0), u);
                        note.duration = std::max (beats (number (n, "duration", 0.0), u), 1.0e-6);
                        note.key = std::clamp ((int) number (n, "key", 60.0), 0, 127);
                        note.channel = std::clamp ((int) number (n, "channel", 0.0), 0, 15);
                        note.velocity = std::clamp (number (n, "vel", 0.8), 0.0, 1.0);
                        note.release = std::clamp (number (n, "rel", note.velocity), 0.0, 1.0);
                        if (! n.children.empty())
                            this->note ("Per-note expressions are not carried over");
                        out.push_back (note);
                    }
                else if (c.name == "Clips")
                    for (const auto& clip : c.children)
                    {
                        if (clip.name != "Clip" || ! flag (clip, "enable", true))
                            continue;
                        double length = 0.0;
                        auto laid = layoutNotes (clip, u, length);
                        const auto at = beats (number (clip, "time", 0.0), u);
                        for (auto& n : laid)
                        {
                            n.time += at;
                            out.push_back (n);
                        }
                    }
                else if (c.name == "Lanes")
                    contentNotes (c, u, out);
                else if (c.name == "Points")
                    this->note ("Automation is not carried over");
            }
        }

        // A clip's notes from its start, over its length; `length` is set in beats.
        std::vector<Note> layoutNotes (const Node& clip, const std::string& unit, double& length)
        {
            const auto contentUnit = clip.attribute ("contentTimeUnit") != nullptr ? text (clip, "contentTimeUnit") : unit;
            std::vector<Note> content;
            contentNotes (clip, contentUnit, content);
            const auto playStart = beats (number (clip, "playStart", 0.0), contentUnit);
            const auto loopStart = beats (number (clip, "loopStart", 0.0), contentUnit);
            const auto loopEnd = beats (number (clip, "loopEnd", 0.0), contentUnit);
            const auto loops = clip.attribute ("loopEnd") != nullptr;
            length = beats (number (clip, "duration", -1.0), unit);
            if (length < 0.0)
            {
                length = 0.0;
                for (const auto& n : content)
                    length = std::max (length, n.time + n.duration - playStart);
                if (loops)
                    length = std::max (length, loopEnd - playStart);
            }
            if (clip.attribute ("playStop") != nullptr && ! loops)
                length = std::min (length, beats (number (clip, "playStop", 0.0), contentUnit) - playStart);
            return laidOut (content, playStart, length, loopStart, loopEnd, loops);
        }

        //======================================================================
        // A warp map: content time (seconds in the file) for a time on the clip's content timeline.
        struct WarpMap
        {
            std::vector<std::pair<double, double>> points;   // (time, content), in order
            double slope = 1.0;                              // when there is one point or none

            double operator() (double t) const
            {
                if (points.empty())
                    return t * slope;
                if (points.size() == 1)
                    return points[0].second + (t - points[0].first) * slope;
                size_t i = 1;
                while (i + 1 < points.size() && t > points[i].first)
                    ++i;
                const auto& [t0, c0] = points[i - 1];
                const auto& [t1, c1] = points[i];
                return t1 - t0 <= 1.0e-12 ? c0 : c0 + (t - t0) * (c1 - c0) / (t1 - t0);
            }
        };

        // Audio parts of a clip, from its start, in seconds.
        std::vector<AudioClip> layoutAudio (const Node& clip, const std::string& unit)
        {
            std::vector<AudioClip> out;
            const auto contentUnit = clip.attribute ("contentTimeUnit") != nullptr ? text (clip, "contentTimeUnit") : unit;
            const auto* content = firstOf (clip, { "Audio", "Warps", "Clips", "Lanes" });
            if (content == nullptr)
                return out;
            const auto playStart = number (clip, "playStart", 0.0);
            const auto loops = clip.attribute ("loopEnd") != nullptr;
            const auto loopStart = number (clip, "loopStart", 0.0), loopEnd = number (clip, "loopEnd", 0.0);
            auto length = seconds (number (clip, "duration", -1.0), unit);

            const Node* audio = content->name == "Audio" ? content : content->name == "Warps" ? content->child ("Audio") : nullptr;
            if (audio != nullptr)
            {
                const auto* file = audio->child ("File");
                if (file == nullptr)
                    return out;
                AudioClip a;
                a.file = audioFile ? audioFile (text (*file, "path"), flag (*file, "external", false)) : text (*file, "path");
                a.fileLength = number (*audio, "duration", 0.0);
                a.channels = (int) number (*audio, "channels", 2.0);
                a.sampleRate = (int) number (*audio, "sampleRate", 48000.0);
                a.name = text (clip, "name");
                a.muted = ! flag (clip, "enable", true);
                WarpMap map;
                const auto contentIsSeconds = content->name == "Audio" ? true : text (*content, "contentTimeUnit") != "beats";
                if (content->name == "Warps")
                {
                    for (const auto& w : content->children)
                        if (w.name == "Warp")
                            map.points.emplace_back (number (w, "time", 0.0), number (w, "contentTime", 0.0));
                    std::sort (map.points.begin(), map.points.end());
                    map.slope = contentUnit == "beats" ? 60.0 / project.bpm : 1.0;
                    if (! contentIsSeconds)
                        for (auto& p : map.points)
                            p.second *= 60.0 / project.bpm;
                }
                else
                    map.slope = contentUnit == "beats" ? 60.0 / project.bpm : 1.0;   // raw audio, its time as it is
                if (length < 0.0)
                    length = std::max (0.0, a.fileLength - map (playStart));
                const auto lengthInContent = contentUnit == "beats" ? beats (length, "seconds") : length;
                for (const auto& s : segments (playStart, lengthInContent, loopStart, loopEnd, loops))
                {
                    auto part = a;
                    part.start = seconds (s.at, contentUnit);
                    part.offset = std::max (0.0, map (s.from));
                    part.length = seconds (s.to - s.from, contentUnit);
                    const auto inFile = map (s.to) - map (s.from);
                    part.warped = std::abs (inFile - part.length) > 0.01 * std::max (part.length, 1.0e-3);
                    if (part.warped)
                        note ("Warped (time-stretched) audio plays at its own speed in PHI");
                    if (part.length > 1.0e-6)
                        out.push_back (part);
                }
                return out;
            }

            // Clips inside a clip: each placed on the content timeline, then cut to what plays.
            std::vector<AudioClip> inner;
            std::function<void (const Node&, const std::string&)> gather = [&] (const Node& n, const std::string& u)
            {
                for (const auto& c : n.children)
                {
                    const auto cu = c.attribute ("timeUnit") != nullptr ? text (c, "timeUnit") : u;
                    if (c.name == "Clips")
                    {
                        for (const auto& k : c.children)
                            if (k.name == "Clip")
                            {
                                auto parts = layoutAudio (k, cu);
                                const auto at = seconds (number (k, "time", 0.0), cu);
                                for (auto& p : parts)
                                {
                                    p.start += at;
                                    inner.push_back (p);
                                }
                            }
                    }
                    else if (c.name == "Lanes")
                        gather (c, cu);
                }
            };
            gather (clip, contentUnit);
            if (length < 0.0)
            {
                length = 0.0;
                for (const auto& p : inner)
                    length = std::max (length, p.start + p.length - seconds (playStart, contentUnit));
            }
            if (loops)
                note ("A looped clip of clips plays once in PHI");
            const auto from = seconds (playStart, contentUnit), to = from + length;
            for (auto p : inner)
            {
                const auto s = std::max (p.start, from), e = std::min (p.start + p.length, to);
                if (e - s <= 1.0e-6)
                    continue;
                p.offset += s - p.start;
                p.length = e - s;
                p.start = s - from;
                out.push_back (p);
            }
            return out;
        }

        //======================================================================
        void placeClip (const Node& clip, const std::string& unit, Track& t)
        {
            const auto start = seconds (number (clip, "time", 0.0), unit);
            if (holdsAudio (clip))
            {
                for (auto& p : layoutAudio (clip, unit))
                {
                    p.start += start;
                    t.audioClips.push_back (p);
                }
                t.audio = true;
                return;
            }
            NoteClip c;
            c.notes = layoutNotes (clip, unit, c.lengthBeats);
            c.start = start;
            c.length = seconds (c.lengthBeats, "beats");
            c.name = text (clip, "name");
            c.muted = ! flag (clip, "enable", true);
            if (c.lengthBeats > 0.0)
                t.noteClips.push_back (std::move (c));
            t.notes = true;
        }

        void lanes (const Node& n, const std::string& unit, const std::string& trackId)
        {
            for (const auto& c : n.children)
            {
                const auto u = c.attribute ("timeUnit") != nullptr ? text (c, "timeUnit") : unit;
                const auto id = c.attribute ("track") != nullptr ? text (c, "track") : trackId;
                const auto it = byId.find (id);
                auto* t = it == byId.end() ? nullptr : track (it->second);
                if (c.name == "Lanes")
                    lanes (c, u, id);
                else if (c.name == "Clips" && t != nullptr)
                {
                    for (const auto& clip : c.children)
                        if (clip.name == "Clip")
                            placeClip (clip, u, *t);
                }
                else if ((c.name == "Notes" || c.name == "Audio" || c.name == "Warps") && t != nullptr)
                {
                    // Content straight on the track: one clip of all of it.
                    Node clip;
                    clip.name = "Clip";
                    clip.attributes = { { "time", "0" }, { "contentTimeUnit", u } };
                    clip.children.push_back (c);
                    placeClip (clip, u, *t);
                }
                else if (c.name == "Points")
                    note ("Automation is not carried over");
                else if (c.name == "Video")
                    note ("Video is not carried over");
            }
        }

        void scenes (const Node& list)
        {
            int index = 0;
            for (const auto& scene : list.children)
            {
                if (scene.name != "Scene")
                    continue;
                project.scenes.push_back (text (scene, "name"));
                std::function<void (const Node&, const std::string&, const std::string&)> walk =
                    [&] (const Node& n, const std::string& unit, const std::string& trackId)
                {
                    for (const auto& c : n.children)
                    {
                        const auto u = c.attribute ("timeUnit") != nullptr ? text (c, "timeUnit") : unit;
                        const auto id = c.attribute ("track") != nullptr ? text (c, "track") : trackId;
                        if (c.name == "Lanes")
                            walk (c, u, id);
                        else if (c.name == "ClipSlot")
                        {
                            const auto it = byId.find (id);
                            auto* t = it == byId.end() ? nullptr : track (it->second);
                            const auto* clip = c.child ("Clip");
                            if (t == nullptr || clip == nullptr)
                                continue;
                            Slot slot;
                            slot.scene = index;
                            if (holdsAudio (*clip))
                            {
                                const auto parts = layoutAudio (*clip, u);
                                if (parts.empty())
                                    continue;
                                if (parts.size() > 1)
                                    note ("A launcher clip made of several pieces of audio keeps only its first");
                                slot.audio = true;
                                slot.audioClip = parts.front();
                                t->audio = true;
                            }
                            else
                            {
                                slot.noteClip.notes = layoutNotes (*clip, u, slot.noteClip.lengthBeats);
                                slot.noteClip.length = seconds (slot.noteClip.lengthBeats, "beats");
                                slot.noteClip.name = text (*clip, "name");
                                t->notes = true;
                            }
                            t->slots.push_back (std::move (slot));
                        }
                    }
                };
                walk (scene, "beats", {});
                ++index;
            }
        }

        bool run (const std::string& xml, std::string& error)
        {
            Node root;
            if (! minixml::parse (xml, root, error))
                return false;
            if (root.name != "Project")
            {
                error = "this is not a DAWproject (its project.xml has no Project)";
                return false;
            }
            if (const auto* app = root.child ("Application"))
            {
                project.application = text (*app, "name");
                project.applicationVersion = text (*app, "version");
            }
            if (const auto* transport = root.child ("Transport"))
            {
                if (const auto* tempo = transport->child ("Tempo"))
                    project.bpm = std::clamp (number (*tempo, "value", 120.0), 10.0, 999.0);
                if (const auto* sig = transport->child ("TimeSignature"))
                {
                    project.numerator = std::clamp ((int) number (*sig, "numerator", 4.0), 1, 32);
                    project.denominator = std::clamp ((int) number (*sig, "denominator", 4.0), 1, 32);
                }
            }
            if (const auto* s = root.child ("Structure"))
                structure (*s);
            if (const auto* arrangement = root.child ("Arrangement"))
            {
                if (const auto* tempo = arrangement->child ("TempoAutomation"))
                {
                    std::set<double> values;
                    for (const auto& p : tempo->children)
                        if (p.name == "RealPoint")
                            values.insert (number (p, "value", project.bpm));
                    if (values.size() > 1)
                        note ("Tempo changes are not carried over: PHI keeps one tempo, " + format (project.bpm) + " BPM");
                }
                if (const auto* sig = arrangement->child ("TimeSignatureAutomation"); sig != nullptr && sig->children.size() > 1)
                    note ("Time signature changes are not carried over");
                if (const auto* l = arrangement->child ("Lanes"))
                    lanes (*l, l->attribute ("timeUnit") != nullptr ? text (*l, "timeUnit") : "beats", text (*l, "track"));
                if (const auto* m = arrangement->child ("Markers"); m != nullptr && ! m->children.empty())
                    note ("Markers are not carried over");
            }
            if (const auto* s = root.child ("Scenes"))
                scenes (*s);
            for (auto& list : { &project.tracks, &project.returns })
                for (auto& t : *list)
                {
                    std::stable_sort (t.audioClips.begin(), t.audioClips.end(), [] (const AudioClip& a, const AudioClip& b) { return a.start < b.start; });
                    std::stable_sort (t.noteClips.begin(), t.noteClips.end(), [] (const NoteClip& a, const NoteClip& b) { return a.start < b.start; });
                }
            return true;
        }
    };

    void readMetadata (const std::string& xml, Project& p)
    {
        Node root;
        std::string error;
        if (! minixml::parse (xml, root, error))
            return;
        if (const auto* t = root.child ("Title"))
            p.title = t->text;
        if (const auto* a = root.child ("Artist"))
            p.artist = a->text;
        if (const auto* c = root.child ("Comment"))
            p.comment = c->text;
    }

    std::string safeName (std::string s)
    {
        for (auto& ch : s)
            if (ch == '/' || ch == '\\' || ch == ':' || (unsigned char) ch < 32)
                ch = '_';
        return s.empty() ? std::string ("file") : s;
    }
}

//==============================================================================
std::string Device::format() const
{
    if (element == "Vst3Plugin") return "VST3";
    if (element == "ClapPlugin") return "CLAP";
    if (element == "Vst2Plugin") return "VST";
    if (element == "AuPlugin") return "AudioUnit";
    return {};
}

const Device* Track::instrument() const
{
    for (const auto& d : devices)
        if (d.role == "instrument")
            return &d;
    return nullptr;
}

bool parse (const std::string& xml, Project& p, std::string& error)
{
    p = {};
    Reader r (p);
    return r.run (xml, error);
}

bool read (const std::string& path, const std::string& folder, Project& p, std::string& error)
{
    p = {};
    zip::Reader archive;
    if (! archive.open (path, error))
    {
        error = fs::path (path).filename().string() + ": " + error;
        return false;
    }
    std::vector<uint8_t> bytes;
    if (! archive.read ("project.xml", bytes, error))
    {
        error = fs::path (path).filename().string() + " is not a DAWproject: " + error;
        return false;
    }
    const std::string xml (bytes.begin(), bytes.end());
    const auto base = fs::path (path).parent_path();
    std::map<std::string, std::string> unpacked;   // archive path to where it went
    std::set<std::string> taken;
    Reader r (p);
    r.audioFile = [&] (const std::string& file, bool external) -> std::string
    {
        if (external)
        {
            const fs::path f (file);
            return (f.is_absolute() ? f : base / f).lexically_normal().string();
        }
        if (const auto it = unpacked.find (file); it != unpacked.end())
            return it->second;
        const auto* e = archive.find (file);
        if (e == nullptr)
        {
            r.note (file + " is named in the project but is not in it");
            return {};
        }
        // Flat in the folder, by its own name; a second file of the same name gets a number.
        auto name = safeName (fs::path (file).filename().string());
        const auto stem = fs::path (name).stem().string(), ext = fs::path (name).extension().string();
        for (int n = 2; taken.count (name) > 0; ++n)
            name = stem + " " + std::to_string (n) + ext;
        taken.insert (name);
        const auto to = (fs::path (folder) / name).string();
        std::string why;
        if (! archive.extract (*e, to, why))
        {
            r.note (why);
            return {};
        }
        unpacked[file] = to;
        return to;
    };
    r.stateFile = [&] (const std::string& file, bool external) -> std::vector<uint8_t>
    {
        std::vector<uint8_t> out;
        if (external)
        {
            const fs::path f (file);
            std::ifstream in (f.is_absolute() ? f : base / f, std::ios::binary);
            out.assign ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
            return out;
        }
        std::string why;
        if (! archive.read (file, out, why))
            r.note ("A plugin's saved state could not be read: " + why);
        return out;
    };
    if (! r.run (xml, error))
    {
        error = fs::path (path).filename().string() + ": " + error;
        return false;
    }
    if (archive.read ("metadata.xml", bytes, error))
        readMetadata (std::string (bytes.begin(), bytes.end()), p);
    error.clear();
    return true;
}

//==============================================================================
namespace
{
    struct Writer
    {
        explicit Writer (const Project& project) : p (project) {}

        const Project& p;
        int nextId = 0;
        std::string out;
        // Audio files embedded: their path to the name in the archive.
        std::map<std::string, std::string> audioNames;
        std::set<std::string> takenNames;
        // Plugin states: the name in the archive and the bytes.
        std::vector<std::pair<std::string, const std::vector<uint8_t>*>> states;
        std::vector<std::string> trackIds, trackChannels, returnIds, returnChannels;
        std::string masterChannel;

        std::string id() { return "id" + std::to_string (nextId++); }
        double beats (double secondsValue) const { return secondsValue * p.bpm / 60.0; }

        std::string attr (const char* name, const std::string& value)
        {
            return std::string (" ") + name + "=\"" + minixml::escape (value, true) + "\"";
        }
        std::string attr (const char* name, double v) { return attr (name, format (v)); }
        std::string attr (const char* name, bool v) { return attr (name, std::string (v ? "true" : "false")); }

        std::string audioName (const std::string& file)
        {
            if (const auto it = audioNames.find (file); it != audioNames.end())
                return it->second;
            auto name = safeName (fs::path (file).filename().string());
            const auto stem = fs::path (name).stem().string(), ext = fs::path (name).extension().string();
            for (int n = 2; takenNames.count (name) > 0; ++n)
                name = stem + "-" + std::to_string (n) + ext;
            takenNames.insert (name);
            return audioNames[file] = "audio/" + name;
        }

        void devices (const Track& t, const std::string& indent)
        {
            bool any = false;
            for (const auto& d : t.devices)
                if (d.isPlugin())
                    any = true;
            if (! any)
                return;
            out += indent + "<Devices>\n";
            for (const auto& d : t.devices)
            {
                if (! d.isPlugin())
                    continue;
                out += indent + "  <" + d.element + attr ("deviceID", d.deviceId) + attr ("deviceName", d.name) + attr ("deviceRole", d.role);
                if (! d.vendor.empty())
                    out += attr ("deviceVendor", d.vendor);
                out += attr ("loaded", true) + attr ("id", id()) + attr ("name", d.name) + ">\n";
                out += indent + "    <Parameters/>\n";
                out += indent + "    <Enabled" + attr ("value", d.enabled) + attr ("id", id()) + attr ("name", std::string ("On/Off")) + "/>\n";
                if (! d.state.empty())
                {
                    const auto ext = d.element == "Vst3Plugin" ? ".vstpreset" : d.element == "ClapPlugin" ? ".clap-preset" : ".bin";
                    const auto name = "plugins/" + std::to_string (states.size() + 1) + ext;
                    states.emplace_back (name, &d.state);
                    out += indent + "    <State" + attr ("path", name) + "/>\n";
                }
                out += indent + "  </" + d.element + ">\n";
            }
            out += indent + "</Devices>\n";
        }

        void channel (const Track& t, const std::string& role, const std::string& channelId, const std::string& indent)
        {
            out += indent + "<Channel" + attr ("audioChannels", 2.0) + (masterChannel.empty() || role == "master" ? std::string() : attr ("destination", masterChannel))
                 + attr ("role", role) + attr ("solo", t.solo) + attr ("id", channelId) + ">\n";
            devices (t, indent + "  ");
            out += indent + "  <Mute" + attr ("value", t.mute) + attr ("id", id()) + attr ("name", std::string ("Mute")) + "/>\n";
            out += indent + "  <Pan" + attr ("max", 1.0) + attr ("min", 0.0) + attr ("unit", std::string ("normalized"))
                 + attr ("value", (std::clamp (t.pan, -1.0, 1.0) + 1.0) / 2.0) + attr ("id", id()) + attr ("name", std::string ("Pan")) + "/>\n";
            if (! t.sends.empty())
            {
                out += indent + "  <Sends>\n";
                for (const auto& s : t.sends)
                {
                    if (s.target < 0 || s.target >= (int) returnChannels.size())
                        continue;
                    out += indent + "    <Send" + attr ("destination", returnChannels[(size_t) s.target]) + attr ("type", std::string (s.pre ? "pre" : "post"))
                         + attr ("id", id()) + attr ("name", std::string ("Send")) + ">\n";
                    out += indent + "      <Enable" + attr ("value", s.enabled) + attr ("id", id()) + attr ("name", std::string ("Enable")) + "/>\n";
                    out += indent + "      <Volume" + attr ("max", 2.0) + attr ("min", 0.0) + attr ("unit", std::string ("linear"))
                         + attr ("value", toGain (s.db)) + attr ("id", id()) + attr ("name", std::string ("Volume")) + "/>\n";
                    out += indent + "    </Send>\n";
                }
                out += indent + "  </Sends>\n";
            }
            out += indent + "  <Volume" + attr ("max", 2.0) + attr ("min", 0.0) + attr ("unit", std::string ("linear"))
                 + attr ("value", toGain (t.volumeDb)) + attr ("id", id()) + attr ("name", std::string ("Volume")) + "/>\n";
            out += indent + "</Channel>\n";
        }

        void track (const Track& t, const std::string& trackId, const std::string& channelId, const std::string& role)
        {
            std::string content;
            if (t.audio || ! t.audioClips.empty())
                content = "audio";
            if (t.notes || ! t.noteClips.empty() || t.instrument() != nullptr)
                content += content.empty() ? "notes" : " notes";
            if (content.empty())
                content = "audio";
            out += "    <Track" + attr ("contentType", content) + attr ("loaded", true) + attr ("id", trackId) + attr ("name", t.name);
            if (! t.colour.empty())
                out += attr ("color", t.colour);
            out += ">\n";
            channel (t, role, channelId, "      ");
            out += "    </Track>\n";
        }

        void notes (const std::vector<Note>& list, const std::string& indent)
        {
            out += indent + "<Notes" + attr ("id", id()) + ">\n";
            for (const auto& n : list)
                out += indent + "  <Note" + attr ("time", n.time) + attr ("duration", n.duration) + attr ("channel", (double) n.channel)
                     + attr ("key", (double) n.key) + attr ("vel", n.velocity) + attr ("rel", n.release) + "/>\n";
            out += indent + "</Notes>\n";
        }

        void audioClip (const AudioClip& a, double time, double duration, const std::string& indent, const std::string& extra = {})
        {
            out += indent + "<Clip" + attr ("time", time) + attr ("duration", duration) + attr ("contentTimeUnit", std::string ("seconds"))
                 + attr ("playStart", a.offset) + extra;
            if (a.muted)
                out += attr ("enable", false);
            if (! a.name.empty())
                out += attr ("name", a.name);
            out += ">\n";
            out += indent + "  <Audio" + attr ("channels", (double) std::max (1, a.channels)) + attr ("sampleRate", (double) std::max (1, a.sampleRate))
                 + attr ("duration", std::max (a.fileLength, a.offset + a.length)) + attr ("id", id()) + ">\n";
            out += indent + "    <File" + attr ("path", audioName (a.file)) + "/>\n";
            out += indent + "  </Audio>\n";
            out += indent + "</Clip>\n";
        }

        void noteClip (const NoteClip& c, double time, const std::string& indent, const std::string& extra = {})
        {
            out += indent + "<Clip" + attr ("time", time) + attr ("duration", c.lengthBeats) + attr ("playStart", 0.0) + extra;
            if (c.muted)
                out += attr ("enable", false);
            if (! c.name.empty())
                out += attr ("name", c.name);
            out += ">\n";
            notes (c.notes, indent + "  ");
            out += indent + "</Clip>\n";
        }

        std::string run()
        {
            out = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
            out += "<Project" + attr ("version", std::string ("1.0")) + ">\n";
            out += "  <Application" + attr ("name", p.application.empty() ? std::string ("PHI") : p.application)
                 + attr ("version", p.applicationVersion.empty() ? std::string ("1.0") : p.applicationVersion) + "/>\n";
            out += "  <Transport>\n";
            out += "    <Tempo" + attr ("max", 999.0) + attr ("min", 20.0) + attr ("unit", std::string ("bpm")) + attr ("value", p.bpm)
                 + attr ("id", id()) + attr ("name", std::string ("Tempo")) + "/>\n";
            out += "    <TimeSignature" + attr ("denominator", (double) p.denominator) + attr ("numerator", (double) p.numerator) + attr ("id", id()) + "/>\n";
            out += "  </Transport>\n";

            const auto masterTrack = id();
            masterChannel = id();
            for (size_t i = 0; i < p.tracks.size(); ++i)
            {
                trackIds.push_back (id());
                trackChannels.push_back (id());
            }
            for (size_t i = 0; i < p.returns.size(); ++i)
            {
                returnIds.push_back (id());
                returnChannels.push_back (id());
            }
            out += "  <Structure>\n";
            for (size_t i = 0; i < p.tracks.size(); ++i)
                track (p.tracks[i], trackIds[i], trackChannels[i], "regular");
            for (size_t i = 0; i < p.returns.size(); ++i)
                track (p.returns[i], returnIds[i], returnChannels[i], "effect");
            Track master;
            master.name = "Master";
            master.volumeDb = p.masterDb;
            master.audio = true;
            track (master, masterTrack, masterChannel, "master");
            out += "  </Structure>\n";

            out += "  <Arrangement" + attr ("id", id()) + ">\n";
            out += "    <Lanes" + attr ("timeUnit", std::string ("beats")) + attr ("id", id()) + ">\n";
            auto lane = [&] (const Track& t, const std::string& trackId)
            {
                if (t.audioClips.empty() && t.noteClips.empty())
                    return;
                out += "      <Lanes" + attr ("track", trackId) + attr ("id", id()) + ">\n";
                out += "        <Clips" + attr ("id", id()) + ">\n";
                for (const auto& a : t.audioClips)
                    audioClip (a, beats (a.start), beats (a.length), "          ");
                for (const auto& c : t.noteClips)
                    noteClip (c, beats (c.start), "          ");
                out += "        </Clips>\n";
                out += "      </Lanes>\n";
            };
            for (size_t i = 0; i < p.tracks.size(); ++i)
                lane (p.tracks[i], trackIds[i]);
            for (size_t i = 0; i < p.returns.size(); ++i)
                lane (p.returns[i], returnIds[i]);
            out += "    </Lanes>\n";
            out += "  </Arrangement>\n";

            // The launcher, a Scene for each scene that has a clip (and those before it).
            int scenes = (int) p.scenes.size();
            for (const auto& t : p.tracks)
                for (const auto& s : t.slots)
                    scenes = std::max (scenes, s.scene + 1);
            if (scenes > 0)
            {
                out += "  <Scenes>\n";
                for (int sc = 0; sc < scenes; ++sc)
                {
                    const auto name = sc < (int) p.scenes.size() && ! p.scenes[(size_t) sc].empty() ? p.scenes[(size_t) sc] : std::to_string (sc + 1);
                    out += "    <Scene" + attr ("id", id()) + attr ("name", name) + ">\n";
                    out += "      <Lanes" + attr ("timeUnit", std::string ("beats")) + attr ("id", id()) + ">\n";
                    for (size_t i = 0; i < p.tracks.size(); ++i)
                        for (const auto& s : p.tracks[i].slots)
                        {
                            if (s.scene != sc)
                                continue;
                            out += "        <ClipSlot" + attr ("track", trackIds[i]) + attr ("hasStop", true) + attr ("id", id()) + ">\n";
                            if (s.audio)
                            {
                                const auto length = beats (s.audioClip.length);
                                audioClip (s.audioClip, 0.0, length, "          ",
                                           attr ("loopStart", s.audioClip.offset) + attr ("loopEnd", s.audioClip.offset + s.audioClip.length));
                            }
                            else
                                noteClip (s.noteClip, 0.0, "          ", attr ("loopStart", 0.0) + attr ("loopEnd", s.noteClip.lengthBeats));
                            out += "        </ClipSlot>\n";
                        }
                    out += "      </Lanes>\n";
                    out += "    </Scene>\n";
                }
                out += "  </Scenes>\n";
            }
            out += "</Project>\n";
            return out;
        }
    };

    std::string metadataXml (const Project& p)
    {
        std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<MetaData>\n";
        if (! p.title.empty())
            out += "  <Title>" + minixml::escape (p.title) + "</Title>\n";
        if (! p.artist.empty())
            out += "  <Artist>" + minixml::escape (p.artist) + "</Artist>\n";
        if (! p.comment.empty())
            out += "  <Comment>" + minixml::escape (p.comment) + "</Comment>\n";
        out += "</MetaData>\n";
        return out;
    }
}

std::string toXml (const Project& p)
{
    Writer w (p);
    return w.run();
}

bool write (const Project& p, const std::string& path, std::string& error)
{
    Writer w (p);
    const auto xml = w.run();
    const auto meta = metadataXml (p);
    // Written beside it, then put in its place, so a failed write leaves an old file whole.
    const auto temporary = path + ".part";
    {
        zip::Writer archive;
        if (! archive.open (temporary, error))
            return false;
        if (! archive.add ("metadata.xml", meta.data(), meta.size(), true, error)
            || ! archive.add ("project.xml", xml.data(), xml.size(), true, error))
            return false;
        for (const auto& [file, name] : w.audioNames)
            if (! archive.addFile (name, file, false, error))
                return false;
        for (const auto& [name, bytes] : w.states)
            if (! archive.add (name, bytes->data(), bytes->size(), true, error))
                return false;
        if (! archive.close (error))
            return false;
    }
    std::error_code ec;
    fs::rename (temporary, path, ec);
    if (ec)
    {
        fs::remove (temporary, ec);
        error = path + " could not be written";
        return false;
    }
    return true;
}

//==============================================================================
midifile::SongPtr songOf (const std::vector<Note>& notes, double lengthBeats, double bpm)
{
    auto song = std::make_shared<midifile::Song>();
    song->format = 0;
    song->ticksPerQuarter = 480;
    song->tempos = { { 0, 60.0e6 / std::max (bpm, 1.0) } };
    const auto tick = [] (double beats) { return (int64_t) std::llround (std::max (0.0, beats) * 480.0); };
    struct Timed { int64_t tick; int order; midifile::Event e; };
    std::vector<Timed> events;
    for (const auto& n : notes)
    {
        const auto ch = (uint8_t) std::clamp (n.channel, 0, 15);
        const auto key = (uint8_t) std::clamp (n.key, 0, 127);
        const auto on = tick (n.time), off = std::max (on + 1, tick (n.time + n.duration));
        const auto vel = (uint8_t) std::clamp ((int) std::lround (n.velocity * 127.0), 1, 127);
        const auto rel = (uint8_t) std::clamp ((int) std::lround (n.release * 127.0), 0, 127);
        events.push_back ({ off, 0, { off, (uint8_t) (0x80 | ch), key, rel } });   // an off before an on at the same tick
        events.push_back ({ on, 1, { on, (uint8_t) (0x90 | ch), key, vel } });
        song->channelsUsed |= (uint16_t) (1u << ch);
    }
    std::stable_sort (events.begin(), events.end(), [] (const Timed& a, const Timed& b) { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });
    for (const auto& t : events)
        song->events.push_back (t.e);
    song->lastTick = std::max (tick (lengthBeats), events.empty() ? (int64_t) 0 : events.back().tick);
    return song;
}

std::vector<Note> notesOf (const midifile::Song& song, double offset, double length, double fixedBpm, double bpm)
{
    std::vector<Note> out;
    struct Open { double start; int velocity; };
    std::map<int, std::vector<Open>> open;   // channel * 128 + key: first on, first off
    const auto end = offset + length;
    auto close = [&] (int key, double at, int release)
    {
        auto& list = open[key];
        if (list.empty())
            return;
        const auto o = list.front();
        list.erase (list.begin());
        if (o.start < offset - 1.0e-9 || o.start >= end - 1.0e-9)
            return;
        Note n;
        n.time = (o.start - offset) * bpm / 60.0;
        n.duration = std::max (1.0e-3, (std::min (at, end) - o.start) * bpm / 60.0);
        n.key = key % 128;
        n.channel = key / 128;
        n.velocity = o.velocity / 127.0;
        n.release = release < 0 ? n.velocity : release / 127.0;
        out.push_back (n);
    };
    for (const auto& e : song.events)
    {
        const auto at = song.seconds (e.tick, fixedBpm);
        const auto key = e.channel() * 128 + (e.data1 & 0x7F);
        if (e.kind() == 0x90 && e.data2 > 0)
            open[key].push_back ({ at, e.data2 });
        else if (e.kind() == 0x80 || e.kind() == 0x90)
            close (key, at, e.kind() == 0x80 ? e.data2 : -1);
    }
    const auto last = song.seconds (song.lastTick, fixedBpm);
    for (auto& [key, list] : open)
        while (! list.empty())
            close (key, std::max (last, list.front().start + 1.0e-3), -1);
    std::stable_sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.time < b.time; });
    return out;
}

std::vector<Note> laidOut (const std::vector<Note>& content, double playStart, double length, double loopStart, double loopEnd, bool loops)
{
    std::vector<Note> out;
    for (const auto& s : segments (playStart, length, loopStart, loopEnd, loops))
        for (const auto& n : content)
            if (n.time >= s.from - 1.0e-9 && n.time < s.to - 1.0e-9)
            {
                auto placed = n;
                placed.time = s.at + (n.time - s.from);
                placed.duration = std::min ({ n.duration, s.to - n.time, length - placed.time });   // cut at the loop's end
                out.push_back (placed);
            }
    std::stable_sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.time < b.time; });
    return out;
}

std::string vstPresetClassId (const std::vector<uint8_t>& b)
{
    // 'VST3', a version (4 bytes), the class id as 32 ASCII hex digits, the chunk list's offset.
    if (b.size() < 48 || b[0] != 'V' || b[1] != 'S' || b[2] != 'T' || b[3] != '3')
        return {};
    std::string id (b.begin() + 8, b.begin() + 40);
    for (const auto ch : id)
        if (! std::isxdigit ((unsigned char) ch))
            return {};
    return id;
}
}
