// SPDX-License-Identifier: AGPL-3.0-only
#include "AbletonSet.h"

#include "../core/MiniXml.h"
#include "../core/Zip.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <set>

namespace ableton
{
namespace
{
    namespace fs = std::filesystem;
    using minixml::Node;
    using dawproject::Note;
    using dawproject::Project;
    using dawproject::Track;

    double number (const std::string* text, double otherwise)
    {
        if (text == nullptr || text->empty())
            return otherwise;
        double v = otherwise;
        const auto r = std::from_chars (text->data(), text->data() + text->size(), v);
        return r.ec == std::errc() && std::isfinite (v) ? v : otherwise;
    }

    // <Name Value="..."/>: the Value of a child.
    const std::string* valueOf (const Node* n) { return n == nullptr ? nullptr : n->attribute ("Value"); }

    const Node* path (const Node* n, std::initializer_list<const char*> names)
    {
        for (const auto* name : names)
        {
            if (n == nullptr)
                return nullptr;
            n = n->child (name);
        }
        return n;
    }

    double value (const Node* n, std::initializer_list<const char*> names, double otherwise) { return number (valueOf (path (n, names)), otherwise); }
    std::string text (const Node* n, std::initializer_list<const char*> names)
    {
        const auto* v = valueOf (path (n, names));
        return v == nullptr ? std::string() : *v;
    }
    bool flag (const Node* n, std::initializer_list<const char*> names, bool otherwise)
    {
        const auto* v = valueOf (path (n, names));
        return v == nullptr ? otherwise : *v == "true";
    }
    // A parameter's value: <Volume><Manual Value=.../></Volume>.
    double manual (const Node* n, const char* name, double otherwise) { return value (n, { name, "Manual" }, otherwise); }

    // The first element of that name below `n` that has a child `with`: a parameter, say,
    // <Volume><Manual .../></Volume>, rather than a plain <Volume Value=.../>.
    const Node* descendantWith (const Node& n, const std::string& name, const char* with)
    {
        for (const auto& c : n.children)
        {
            if (c.name == name && c.child (with) != nullptr)
                return &c;
            if (const auto* d = descendantWith (c, name, with))
                return d;
        }
        return nullptr;
    }

    const Node* descendant (const Node& n, const std::string& name)
    {
        for (const auto& c : n.children)
        {
            if (c.name == name)
                return &c;
            if (const auto* d = descendant (c, name))
                return d;
        }
        return nullptr;
    }

    // Whether an element's automation envelopes move anything: Live keeps one event before the
    // song's start (time -63072000) for each parameter's value, which is not automation.
    bool hasAutomation (const Node* n)
    {
        const auto* envelopes = path (n, { "AutomationEnvelopes", "Envelopes" });
        if (envelopes == nullptr)
            return false;
        bool found = false;
        std::function<void (const Node&)> walk = [&] (const Node& e)
        {
            for (const auto& c : e.children)
            {
                if (found)
                    return;
                if (const auto* t = c.attribute ("Time"); t != nullptr && number (t, -1.0) >= 0.0)
                    found = true;
                walk (c);
            }
        };
        walk (*envelopes);
        return found;
    }

    double toDb (double gain) { return gain > 1.0e-5 ? 20.0 * std::log10 (gain) : -100.0; }

    std::string trackName (const Node& t)
    {
        auto name = text (&t, { "Name", "EffectiveName" });
        if (name.empty())
            name = text (&t, { "Name", "UserName" });
        return name;
    }

    bool isMidiEffect (const std::string& device)
    {
        return device.rfind ("Midi", 0) == 0 || device == "MidiEffectGroupDevice";
    }

    std::vector<uint8_t> hexBytes (const std::string& s)
    {
        std::vector<uint8_t> out;
        int hi = -1;
        for (const auto ch : s)
        {
            if (! std::isxdigit ((unsigned char) ch))
                continue;
            const auto v = std::isdigit ((unsigned char) ch) ? ch - '0' : std::tolower ((unsigned char) ch) - 'a' + 10;
            if (hi < 0)
                hi = v;
            else
            {
                out.push_back ((uint8_t) (hi * 16 + v));
                hi = -1;
            }
        }
        return out;
    }

    // A .vstpreset from a VST3's component and controller states and its class id.
    std::vector<uint8_t> vstPreset (const std::string& classId, const std::vector<uint8_t>& comp, const std::vector<uint8_t>& cont)
    {
        std::vector<uint8_t> b { 'V', 'S', 'T', '3', 1, 0, 0, 0 };
        b.insert (b.end(), classId.begin(), classId.end());
        auto put64 = [&] (uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back ((uint8_t) (v >> (8 * i))); };
        const auto listAt = (uint64_t) (48 + comp.size() + cont.size());
        put64 (listAt);
        b.insert (b.end(), comp.begin(), comp.end());
        b.insert (b.end(), cont.begin(), cont.end());
        b.insert (b.end(), { 'L', 'i', 's', 't' });
        const uint32_t count = cont.empty() ? 1 : 2;
        for (int i = 0; i < 4; ++i)
            b.push_back ((uint8_t) (count >> (8 * i)));
        b.insert (b.end(), { 'C', 'o', 'm', 'p' });
        put64 (48);
        put64 (comp.size());
        if (! cont.empty())
        {
            b.insert (b.end(), { 'C', 'o', 'n', 't' });
            put64 (48 + comp.size());
            put64 (cont.size());
        }
        return b;
    }

    struct Reader
    {
        Project& project;
        fs::path folder;
        std::set<std::string> said;
        std::vector<std::string> sceneNames;
        std::vector<std::string> missing;   // samples not found

        void note (const std::string& s)
        {
            if (said.insert (s).second)
                project.notes.push_back (s);
        }

        double seconds (double beats) const { return beats * 60.0 / project.bpm; }

        //======================================================================
        // Where a sample is: Live 11 on keeps its path; Live 10 and before keep it relative to
        // the project and as a hint of where it was. A set moved from another computer finds
        // its samples by name in its own folder.
        std::string sample (const Node* fileRef)
        {
            if (fileRef == nullptr)
                return {};
            std::vector<fs::path> tries;
            const auto name = text (fileRef, { "Name" });
            if (const auto p = text (fileRef, { "Path" }); ! p.empty())
                tries.emplace_back (p);
            if (const auto* rel = fileRef->child ("RelativePath"))
            {
                if (const auto* v = rel->attribute ("Value"); v != nullptr && ! v->empty())
                    tries.push_back (folder / *v);
                fs::path joined;
                for (const auto& e : rel->children)
                    if (const auto* d = e.attribute ("Dir"))
                        joined /= (*d == ".." ? std::string ("..") : *d);
                if (! name.empty())
                {
                    tries.push_back (folder / joined / name);
                    tries.push_back (folder.parent_path() / joined / name);
                }
            }
            if (const auto* hint = path (fileRef, { "SearchHint", "PathHint" }); hint != nullptr && ! name.empty())
            {
                fs::path joined ("/");
                for (const auto& e : hint->children)
                    if (const auto* d = e.attribute ("Dir"))
                        joined /= *d;
                tries.push_back (joined / name);
            }
            std::error_code ec;
            for (const auto& t : tries)
                if (fs::is_regular_file (t, ec))
                    return t.lexically_normal().string();
            const auto wanted = name.empty() && ! tries.empty() ? tries.front().filename().string() : name;
            if (! wanted.empty())
            {
                int looked = 0;
                for (auto it = fs::recursive_directory_iterator (folder, fs::directory_options::skip_permission_denied, ec);
                     ! ec && it != fs::recursive_directory_iterator() && looked < 20000; it.increment (ec), ++looked)
                    if (it->path().filename() == wanted && it->is_regular_file (ec))
                        return it->path().lexically_normal().string();
            }
            missing.push_back (wanted);
            return tries.empty() ? std::string() : tries.front().string();
        }

        //======================================================================
        dawproject::NoteClip midiClip (const Node& clip)
        {
            std::vector<Note> content;
            if (const auto* keys = path (&clip, { "Notes", "KeyTracks" }))
                for (const auto& kt : keys->children)
                {
                    const auto key = (int) value (&kt, { "MidiKey" }, 60.0);
                    if (const auto* list = kt.child ("Notes"))
                        for (const auto& e : list->children)
                        {
                            if (e.name != "MidiNoteEvent" || number (e.attribute ("IsEnabled"), 1.0) == 0.0 || (e.attribute ("IsEnabled") != nullptr && *e.attribute ("IsEnabled") == "false"))
                                continue;
                            Note n;
                            n.time = number (e.attribute ("Time"), 0.0);
                            n.duration = std::max (number (e.attribute ("Duration"), 0.25), 1.0e-6);
                            n.key = std::clamp (key, 0, 127);
                            n.velocity = std::clamp (number (e.attribute ("Velocity"), 100.0) / 127.0, 0.0, 1.0);
                            n.release = std::clamp (number (e.attribute ("OffVelocity"), 64.0) / 127.0, 0.0, 1.0);
                            content.push_back (n);
                        }
                }
            std::sort (content.begin(), content.end(), [] (const Note& a, const Note& b) { return a.time < b.time; });
            const auto* loop = clip.child ("Loop");
            const auto loopStart = value (loop, { "LoopStart" }, 0.0), loopEnd = value (loop, { "LoopEnd" }, 0.0);
            const auto loops = flag (loop, { "LoopOn" }, false);
            const auto playStart = loopStart + value (loop, { "StartRelative" }, 0.0);
            dawproject::NoteClip c;
            c.lengthBeats = std::max (0.0, value (&clip, { "CurrentEnd" }, 0.0) - value (&clip, { "CurrentStart" }, 0.0));
            c.length = seconds (c.lengthBeats);
            c.start = seconds (number (clip.attribute ("Time"), 0.0));
            c.notes = dawproject::laidOut (content, loops ? playStart : loopStart, c.lengthBeats, loopStart, loopEnd, loops);
            c.name = text (&clip, { "Name" });
            c.muted = flag (&clip, { "Disabled" }, false);
            return c;
        }

        // An audio clip, one part a pass of its loop.
        std::vector<dawproject::AudioClip> audioClip (const Node& clip)
        {
            std::vector<dawproject::AudioClip> out;
            dawproject::AudioClip a;
            const auto* ref = path (&clip, { "SampleRef" });
            a.file = sample (path (ref, { "FileRef" }));
            if (a.file.empty())
                return out;
            a.sampleRate = (int) value (ref, { "DefaultSampleRate" }, 44100.0);
            a.fileLength = value (ref, { "DefaultDuration" }, 0.0) / std::max (1, a.sampleRate);
            a.name = text (&clip, { "Name" });
            a.muted = flag (&clip, { "Disabled" }, false);
            const auto start = seconds (number (clip.attribute ("Time"), 0.0));
            const auto lengthBeats = std::max (0.0, value (&clip, { "CurrentEnd" }, 0.0) - value (&clip, { "CurrentStart" }, 0.0));
            const auto* loop = clip.child ("Loop");
            const auto loopStart = value (loop, { "LoopStart" }, 0.0), loopEnd = value (loop, { "LoopEnd" }, 0.0);
            const auto loops = flag (loop, { "LoopOn" }, false);
            const auto playStart = loopStart + value (loop, { "StartRelative" }, 0.0);
            const auto warpedClip = flag (&clip, { "IsWarped" }, false);

            // Content time to seconds in the file: through the warp markers (beats), or as it is.
            std::vector<std::pair<double, double>> markers;
            if (const auto* list = clip.child ("WarpMarkers"))
                for (const auto& m : list->children)
                    markers.emplace_back (number (m.attribute ("BeatTime"), 0.0), number (m.attribute ("SecTime"), 0.0));
            std::sort (markers.begin(), markers.end());
            const auto tempoSlope = 60.0 / project.bpm;
            auto inFile = [&] (double t)
            {
                if (! warpedClip)
                    return t;
                if (markers.size() < 2)
                    return (markers.empty() ? 0.0 : markers[0].second) + (t - (markers.empty() ? 0.0 : markers[0].first)) * tempoSlope;
                size_t i = 1;
                while (i + 1 < markers.size() && t > markers[i].first)
                    ++i;
                const auto& [b0, s0] = markers[i - 1];
                const auto& [b1, s1] = markers[i];
                return b1 - b0 <= 1.0e-12 ? s0 : s0 + (t - b0) * (s1 - s0) / (b1 - b0);
            };
            // Timeline beats to content units: beats when warped, seconds when not.
            const auto contentLength = warpedClip ? lengthBeats : seconds (lengthBeats);
            std::vector<std::tuple<double, double, double>> parts;   // from, to, at (content units)
            {
                double at = 0.0, from = loops ? playStart : loopStart;
                if (! loops || loopEnd - loopStart <= 1.0e-9)
                    parts.emplace_back (from, from + contentLength, 0.0);
                else
                    while (at < contentLength - 1.0e-9 && parts.size() < 10000)
                    {
                        if (from >= loopEnd)
                            from = loopStart;
                        const auto to = std::min (loopEnd, from + (contentLength - at));
                        parts.emplace_back (from, to, at);
                        at += to - from;
                        from = loopStart;
                    }
            }
            for (const auto& [from, to, at] : parts)
            {
                auto p = a;
                p.start = start + (warpedClip ? seconds (at) : at);
                p.offset = std::max (0.0, inFile (from));
                p.length = warpedClip ? seconds (to - from) : to - from;
                p.warped = warpedClip && std::abs ((inFile (to) - inFile (from)) - p.length) > 0.01 * std::max (p.length, 1.0e-3);
                if (p.warped)
                    note ("Warped audio plays at its own speed in PHI");
                if (p.length > 1.0e-6)
                    out.push_back (p);
            }
            return out;
        }

        //======================================================================
        dawproject::Device plugin (const Node& d)
        {
            dawproject::Device device;
            device.element = "BuiltinDevice";
            device.name = d.name;
            if (d.name == "PluginDevice")
            {
                if (const auto* vst3 = descendant (d, "Vst3PluginInfo"))
                {
                    device.element = "Vst3Plugin";
                    device.name = text (vst3, { "Name" });
                    char id[40] = {};
                    if (const auto* uid = vst3->child ("Uid"))
                    {
                        uint32_t f[4] {};
                        for (int i = 0; i < 4; ++i)
                            f[i] = (uint32_t) (int64_t) value (uid, { ("Fields." + std::to_string (i)).c_str() }, 0.0);
                        std::snprintf (id, sizeof (id), "%08X%08X%08X%08X", f[0], f[1], f[2], f[3]);
                        device.deviceId = id;
                    }
                    const auto* comp = descendant (*vst3, "ProcessorState");
                    const auto* cont = descendant (*vst3, "ControllerState");
                    if (comp != nullptr && ! device.deviceId.empty())
                    {
                        const auto c = hexBytes (comp->text);
                        if (! c.empty())
                            device.state = vstPreset (device.deviceId, c, cont != nullptr ? hexBytes (cont->text) : std::vector<uint8_t>());
                    }
                }
                else if (const auto* vst2 = descendant (d, "VstPluginInfo"))
                {
                    device.element = "Vst2Plugin";
                    device.name = text (vst2, { "PlugName" });
                    note ("VST2 plugins load without their settings: " + device.name);
                }
            }
            else if (d.name == "AuPluginDevice")
            {
                device.element = "AuPlugin";
                if (const auto* au = descendant (d, "AuPluginInfo"))
                    device.name = text (au, { "Name" });
            }
            if (const auto* user = d.child ("UserName"); user != nullptr && valueOf (user) != nullptr && ! valueOf (user)->empty() && ! device.isPlugin())
                device.name = *valueOf (user);
            device.enabled = flag (&d, { "On", "Manual" }, true);
            return device;
        }

        // A Simpler or Sampler's first sample and its part of it, as a pad.
        bool simplerPad (const Node& device, Track::Pad& pad)
        {
            const auto* part = descendant (device, "MultiSamplePart");
            if (part == nullptr)
                return false;
            const auto* ref = part->child ("SampleRef");
            pad.file = sample (path (ref, { "FileRef" }));
            if (pad.file.empty())
                return false;
            const auto rate = std::max (1.0, value (ref, { "DefaultSampleRate" }, 44100.0));
            pad.start = value (part, { "SampleStart" }, 0.0) / rate;
            const auto end = value (part, { "SampleEnd" }, -1.0);
            pad.end = end > 0.0 ? (end + 1.0) / rate : -1.0;
            // The Simpler's own volume (dB, -12 when new) and the sample's (a gain).
            pad.gainDb = number (valueOf (path (descendantWith (device, "Volume", "Manual"), { "Manual" })), 0.0) + toDb (value (part, { "Volume" }, 1.0));
            pad.pan = std::clamp (value (part, { "Panorama" }, 0.0), -1.0, 1.0);
            if (const auto* key = descendantWith (device, "TransposeKey", "Manual"))
                pad.pitch += number (valueOf (key->child ("Manual")), 0.0);
            if (const auto* fine = descendantWith (device, "TransposeFine", "Manual"))
                pad.pitch += number (valueOf (fine->child ("Manual")), 0.0) / 100.0;
            if (const auto* mode = descendant (device, "PlaybackMode"))
                pad.gate = number (valueOf (mode), 1.0) == 0.0;   // Classic follows the key; One-Shot plays out
            if (descendant (device, "MultiSamplePart") != nullptr && part != nullptr)
            {
                int parts = 0;
                std::function<void (const Node&)> count = [&] (const Node& n)
                {
                    for (const auto& c : n.children)
                    {
                        if (c.name == "MultiSamplePart")
                            ++parts;
                        count (c);
                    }
                };
                count (device);
                if (parts > 1)
                    note ("A Sampler with several samples keeps only its first in PHI");
            }
            return true;
        }

        void drumRack (const Node& rack, Track& t)
        {
            const auto* branches = rack.child ("Branches");
            if (branches == nullptr)
                return;
            for (const auto& b : branches->children)
            {
                if (b.name != "DrumBranch")
                    continue;
                Track::Pad pad;
                pad.name = text (&b, { "Name", "EffectiveName" });
                // Live keeps the note it receives as 128 less the MIDI note.
                const auto receiving = value (&b, { "BranchInfo", "ReceivingNote" }, -1.0);
                const auto* zone = descendant (b, "ReceivingNote");
                const auto stored = receiving >= 0.0 ? receiving : number (valueOf (zone), -1.0);
                if (stored < 0.0)
                    continue;
                pad.note = std::clamp (128 - (int) stored, 0, 127);
                if (const auto* choke = descendant (b, "ChokeGroup"))
                    pad.choke = std::clamp ((int) number (valueOf (choke), 0.0), 0, 16);
                // The pad's chain: its first Simpler or Sampler, and its own mixer.
                const Node* simpler = nullptr;
                std::function<void (const Node&)> find = [&] (const Node& n)
                {
                    for (const auto& c : n.children)
                    {
                        if (simpler != nullptr)
                            return;
                        if (c.name == "OriginalSimpler" || c.name == "MultiSampler")
                            simpler = &c;
                        else if (c.name != "Branches" || &n == &b)
                            find (c);
                    }
                };
                find (b);
                if (simpler == nullptr || ! simplerPad (*simpler, pad))
                {
                    note ("Drum Rack pads that are not a Simpler or Sampler are left empty: " + pad.name);
                    continue;
                }
                // The branch's own mixer (the last Mixer in it, after the devices).
                const Node* mixer = nullptr;
                for (const auto& c : b.children)
                    if (c.name == "DeviceChain")
                        mixer = descendant (c, "Mixer");
                if (mixer != nullptr)
                {
                    pad.gainDb += toDb (manual (mixer, "Volume", 1.0));
                    pad.pan = std::clamp (pad.pan + manual (mixer, "Pan", 0.0), -1.0, 1.0);
                    pad.mute = ! flag (mixer, { "Speaker", "Manual" }, true);
                }
                t.kit.push_back (pad);
            }
            std::stable_sort (t.kit.begin(), t.kit.end(), [] (const Track::Pad& a, const Track::Pad& b) { return a.note < b.note; });
        }

        void devices (const Node& track, Track& t, bool midi)
        {
            const auto* list = path (&track, { "DeviceChain", "DeviceChain", "Devices" });
            if (list == nullptr)
                return;
            bool instrument = ! midi;   // an audio track has none to find
            for (const auto& d : list->children)
            {
                if (! instrument && isMidiEffect (d.name))
                {
                    note (t.name + ": the MIDI effect " + d.name + " is not carried over");
                    continue;
                }
                if (! instrument)
                {
                    instrument = true;
                    if (d.name == "DrumGroupDevice")
                    {
                        drumRack (d, t);
                        continue;
                    }
                    if (d.name == "OriginalSimpler" || d.name == "MultiSampler")
                    {
                        Track::Pad pad;
                        if (simplerPad (d, pad))
                            t.samplerFile = pad.file;
                        note (t.name + ": the Simpler's sample is on PHI's Sampler, without its settings");
                        continue;
                    }
                    auto device = plugin (d);
                    device.role = "instrument";
                    if (! device.isPlugin())
                        note (t.name + ": Live's " + device.name + " is not carried over");
                    t.devices.push_back (std::move (device));
                    continue;
                }
                auto device = plugin (d);
                device.role = "audioFX";
                if (! device.isPlugin())
                    note (t.name + ": Live's " + device.name + " is not carried over");
                t.devices.push_back (std::move (device));
            }
        }

        //======================================================================
        void track (const Node& n, bool isReturn)
        {
            Track t;
            t.name = trackName (n);
            const auto midi = n.name == "MidiTrack";
            t.notes = midi;
            t.audio = ! midi;
            const auto* mixer = path (&n, { "DeviceChain", "Mixer" });
            t.volumeDb = toDb (manual (mixer, "Volume", 1.0));
            t.pan = std::clamp (manual (mixer, "Pan", 0.0), -1.0, 1.0);
            t.mute = ! flag (mixer, { "Speaker", "Manual" }, true);
            t.solo = flag (mixer, { "SoloSink" }, false);
            if (const auto* sends = mixer != nullptr ? mixer->child ("Sends") : nullptr)
            {
                int r = 0;
                for (const auto& h : sends->children)
                {
                    const auto db = toDb (manual (&h, "Send", 0.0));
                    if (db > -69.0)
                        t.sends.push_back ({ r, db, false, true });
                    ++r;
                }
            }
            devices (n, t, midi);

            const auto* sequencer = path (&n, { "DeviceChain", "MainSequencer" });
            if (sequencer != nullptr)
            {
                // The arrangement: under ClipTimeable (MIDI) or Sample (audio).
                for (const auto* holder : { sequencer->child ("ClipTimeable"), sequencer->child ("Sample") })
                {
                    const auto* events = path (holder, { "ArrangerAutomation", "Events" });
                    if (events == nullptr)
                        continue;
                    for (const auto& c : events->children)
                    {
                        if (c.name == "MidiClip")
                        {
                            auto clip = midiClip (c);
                            if (clip.lengthBeats > 0.0)
                                t.noteClips.push_back (std::move (clip));
                        }
                        else if (c.name == "AudioClip")
                            for (auto& p : audioClip (c))
                                t.audioClips.push_back (p);
                    }
                }
                // The Session view's clips, a scene a slot.
                if (const auto* list = sequencer->child ("ClipSlotList"))
                {
                    int scene = 0;
                    for (const auto& slot : list->children)
                    {
                        if (const auto* v = path (&slot, { "ClipSlot", "Value" }))
                            for (const auto& c : v->children)
                            {
                                dawproject::Slot s;
                                s.scene = scene;
                                if (c.name == "MidiClip")
                                    s.noteClip = midiClip (c);
                                else if (c.name == "AudioClip")
                                {
                                    const auto parts = audioClip (c);
                                    if (parts.empty())
                                        continue;
                                    s.audio = true;
                                    s.audioClip = parts.front();
                                }
                                else
                                    continue;
                                t.slots.push_back (std::move (s));
                            }
                        ++scene;
                    }
                }
            }
            if (hasAutomation (&n))
                note ("Automation is not carried over");
            (isReturn ? project.returns : project.tracks).push_back (std::move (t));
        }

        bool run (const std::string& xml, std::string& error)
        {
            Node root;
            if (! minixml::parse (xml, root, error))
                return false;
            const auto* set = root.child ("LiveSet");
            if (root.name != "Ableton" || set == nullptr)
            {
                error = "this is not an Ableton Live Set";
                return false;
            }
            project.application = root.attribute ("Creator") != nullptr ? *root.attribute ("Creator") : "Ableton Live";
            const auto* master = set->child ("MasterTrack");
            if (master == nullptr)
                master = set->child ("MainTrack");   // Live 12
            const auto* masterMixer = path (master, { "DeviceChain", "Mixer" });
            project.bpm = std::clamp (manual (masterMixer, "Tempo", 120.0), 10.0, 999.0);
            project.masterDb = toDb (manual (masterMixer, "Volume", 1.0));
            if (const auto* sig = descendant (*set, "RemoteableTimeSignature"))
            {
                project.numerator = std::clamp ((int) value (sig, { "Numerator" }, 4.0), 1, 32);
                project.denominator = std::clamp ((int) value (sig, { "Denominator" }, 4.0), 1, 32);
            }
            if (hasAutomation (master))
                note ("Tempo changes and the master's automation are not carried over");
            if (const auto* d = path (master, { "DeviceChain", "DeviceChain", "Devices" }); d != nullptr && ! d->children.empty())
                note ("The master's devices are not carried over; PHI's Master Section is its own");
            if (const auto* tracks = set->child ("Tracks"))
                for (const auto& t : tracks->children)
                {
                    if (t.name == "GroupTrack")
                        note ("The group " + trackName (t) + " is not kept as a group (PHI has none); its tracks are");
                    else if (t.name == "MidiTrack" || t.name == "AudioTrack")
                        track (t, false);
                    else if (t.name == "ReturnTrack")
                        track (t, true);
                }
            if (const auto* scenes = set->child ("SceneNames"))
                for (const auto& s : scenes->children)
                    project.scenes.push_back (s.attribute ("Value") != nullptr ? *s.attribute ("Value") : std::string());
            else if (const auto* scenes12 = set->child ("Scenes"))
                for (const auto& s : scenes12->children)
                    project.scenes.push_back (text (&s, { "Name" }));
            // Trailing empty scenes are of no use in the launcher.
            int used = 0;
            for (const auto& t : project.tracks)
                for (const auto& s : t.slots)
                    used = std::max (used, s.scene + 1);
            if ((int) project.scenes.size() > used)
                project.scenes.resize ((size_t) used);
            if (const auto* locators = path (set, { "Locators", "Locators" }); locators != nullptr && ! locators->children.empty())
                note ("Locators are not carried over");
            if (! missing.empty())
            {
                std::sort (missing.begin(), missing.end());
                missing.erase (std::unique (missing.begin(), missing.end()), missing.end());
                std::string list;
                for (const auto& m : missing)
                    list += (list.empty() ? "" : ", ") + m;
                note (std::to_string (missing.size()) + " samples were not found, beside the set or where they were (collect them into the set's folder with Live's Collect All and Save): " + list);
            }
            return true;
        }
    };
}

bool parse (const std::string& xml, const std::string& setFolder, dawproject::Project& p, std::string& error)
{
    p = {};
    Reader r { p, fs::path (setFolder), {}, {}, {} };
    return r.run (xml, error);
}

bool read (const std::string& file, dawproject::Project& p, std::string& error)
{
    std::string xml;
    if (! zip::gunzip (file, xml, error))
        return false;
    if (! parse (xml, fs::path (file).parent_path().string(), p, error))
    {
        error = fs::path (file).filename().string() + ": " + error;
        return false;
    }
    return true;
}
}
