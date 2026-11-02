// SPDX-License-Identifier: AGPL-3.0-only
// DAWproject (docs/dawproject.md): a project of Bitwig Studio, Studio One, Cubase or another
// program that writes the open format, brought in as a PHI project, and a PHI project
// written as one; and Ableton Live Sets (docs/ableton-sets.md), brought in the same way. The
// formats are Source/mixer/DawProject.* and AbletonSet.*; this is what they mean for the mixer.
#include "MixerWindow.h"
#include "../mixer/EffectPrism.h"
#include "../engine/AudioEngine.h"
#include "../mixer/AbletonSet.h"
#include "../mixer/DawProject.h"
#include "Settings.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QSaveFile>
#include <QSpinBox>

#include <algorithm>
#include <cmath>
#include <optional>

namespace
{
    const QString dawSuffix = QStringLiteral ("dawproject");

    QString fromStd (const std::string& s) { return QString::fromStdString (s); }

    // A plugin PHI knows that is the device: the same format and name, else the same name in
    // another format (whose state cannot be carried, so it starts as it is).
    const PluginInfo* matching (const std::vector<PluginInfo>& known, const dawproject::Device& d, bool& sameFormat)
    {
        const auto name = fromStd (d.name).trimmed();
        const PluginInfo* other = nullptr;
        for (const auto& p : known)
        {
            if (fromStd (p.name).trimmed().compare (name, Qt::CaseInsensitive) != 0)
                continue;
            if (fromStd (p.format).compare (fromStd (d.format()), Qt::CaseInsensitive) == 0)
            {
                sameFormat = true;
                return &p;
            }
            if (other == nullptr)
                other = &p;
        }
        sameFormat = false;
        return other;
    }

    QString freeFolder (const QDir& in, const QString& name)
    {
        auto candidate = name;
        for (int n = 2; in.exists (candidate); ++n)
            candidate = name + QStringLiteral (" %1").arg (n);
        return in.filePath (candidate);
    }

    QString fileSafe (QString s)
    {
        static const QString bad = QStringLiteral ("/\\:*?\"<>|");
        for (auto& ch : s)
            if (bad.contains (ch) || ch.unicode() < 32)
                ch = QLatin1Char ('_');
        s = s.trimmed();
        return s.isEmpty() ? QStringLiteral ("Track") : s;
    }

    void showNotes (QWidget* parent, const QString& title, const QString& lead, const std::vector<std::string>& notes)
    {
        if (notes.empty())
            return;
        QStringList lines;
        for (const auto& n : notes)
            lines << QStringLiteral ("• ") + fromStd (n);
        QMessageBox box (QMessageBox::Information, title, lead, QMessageBox::Ok, parent);
        box.setInformativeText (MixerWindow::tr ("Not carried over:"));
        box.setDetailedText (lines.join (QLatin1Char ('\n')));
        box.exec();
    }
}

bool MixerWindow::isDawProject (const QString& file)
{
    return QFileInfo (file).suffix().compare (dawSuffix, Qt::CaseInsensitive) == 0;
}

bool MixerWindow::isAbletonSet (const QString& file)
{
    return QFileInfo (file).suffix().compare (QLatin1String ("als"), Qt::CaseInsensitive) == 0;
}

bool MixerWindow::isOtherProject (const QString& file) { return isDawProject (file) || isAbletonSet (file); }

//==============================================================================
// In: a folder beside the file ("<name> PHI") gets a .phi, the note clips as MIDI files and,
// from a .dawproject, its audio unpacked (a Live Set's samples stay where they are); then
// the .phi is opened.
bool MixerWindow::importDawProject (const QString& file, QString& error)
{
    const QFileInfo source (file);
    const auto folderPath = freeFolder (source.absoluteDir(), source.completeBaseName() + tr (" PHI"));
    QDir folder (folderPath);
    const auto live = isAbletonSet (file);   // its audio stays where it is; a DAWproject's is unpacked
    if ((! live && ! QDir().mkpath (folder.filePath (QStringLiteral ("Audio")))) || ! QDir().mkpath (folder.filePath (QStringLiteral ("MIDI"))))
    {
        error = tr ("%1 could not be made").arg (folderPath);
        return false;
    }
    dawproject::Project p;
    std::string why;
    if (live ? ! ableton::read (file.toStdString(), p, why)
             : ! dawproject::read (file.toStdString(), folder.filePath (QStringLiteral ("Audio")).toStdString(), p, why))
    {
        error = fromStd (why);
        folder.removeRecursively();
        return false;
    }
    auto notes = p.notes;
    auto say = [&notes] (const QString& s) { notes.push_back (s.toStdString()); };

    const auto instruments = audio.knownInstruments();
    const auto effects = audio.knownPlugins();
    // The plugins' states in their own formats, to give them once they run: by track, the
    // instrument's, then each insert's in order; the returns' effects after.
    struct Native { int track = -1, insert = -1, ret = -1; std::string pluginId; std::vector<uint8_t> state; };
    std::vector<Native> natives;

    auto insertsFor = [&] (const dawproject::Track& t, int track, int ret, int max) -> QJsonArray
    {
        QJsonArray list;
        for (const auto& d : t.devices)
        {
            if (d.role == "instrument" || ! d.isPlugin())
                continue;
            if (d.role == "noteFX")
            {
                say (tr ("%1: %2 works on notes, which PHI's inserts do not take").arg (fromStd (t.name), fromStd (d.name)));
                continue;
            }
            if (list.size() >= max)
            {
                say (tr ("%1: more effects than PHI holds there; %2 and those after it are left out").arg (fromStd (t.name), fromStd (d.name)));
                break;
            }
            bool same = false;
            const auto* found = matching (effects, d, same);
            if (found == nullptr)
            {
                say (tr ("%1: the effect %2 (%3) is not among PHI's plug-ins").arg (fromStd (t.name), fromStd (d.name), fromStd (d.format())));
                continue;
            }
            if (same && ! d.state.empty())
                natives.push_back ({ track, (int) list.size(), ret, found->id, d.state });
            else if (! d.state.empty())
                say (tr ("%1: %2 is there as %3, without its settings").arg (fromStd (t.name), fromStd (d.name), fromStd (found->format)));
            list.append (QJsonObject { { "id", fromStd (found->id) }, { "name", fromStd (found->name) }, { "state", QString() },
                                       { "bypassed", ! d.enabled }, { "mix", 1.0 } });
        }
        return list;
    };

    // A note clip as a MIDI file in MIDI/.
    int midiCount = 0;
    auto midiFile = [&] (const dawproject::Track& t, const dawproject::NoteClip& c) -> QString
    {
        const auto name = folder.filePath (QStringLiteral ("MIDI/%1 %2.mid").arg (fileSafe (fromStd (t.name))).arg (++midiCount));
        std::string problem;
        if (! midifile::write (*dawproject::songOf (c.notes, c.lengthBeats, p.bpm), name.toStdString(), problem))
        {
            say (fromStd (problem));
            return {};
        }
        return name;
    };

    if ((int) p.tracks.size() > mixer::maxTracks)
        say (tr ("PHI has %1 tracks; the %2 after them are left out").arg (mixer::maxTracks).arg ((int) p.tracks.size() - mixer::maxTracks));
    if ((int) p.returns.size() > mixer::numReturns)
        say (tr ("PHI has %1 returns; the effect tracks after the first %1 are left out, and the sends to them").arg (mixer::numReturns));
    const auto returnCount = std::min ((int) p.returns.size(), mixer::numReturns);

    QJsonArray tracks;
    int scenes = (int) p.scenes.size();
    QJsonArray slotList;
    for (int i = 0; i < std::min ((int) p.tracks.size(), mixer::maxTracks); ++i)
    {
        const auto& t = p.tracks[(size_t) i];
        QJsonArray sends;
        for (int r = 0; r < returnCount; ++r)
        {
            double db = -100.0;
            bool pre = false;
            for (const auto& s : t.sends)
                if (s.target == r)
                {
                    db = s.db;
                    pre = s.pre;
                }
            sends.append (QJsonObject { { "db", std::clamp (db, -100.0, 0.0) }, { "pre", pre } });
        }
        QJsonObject o {
            { "name", fromStd (t.name) }, { "fader", std::clamp (t.volumeDb, -100.0, 6.0) }, { "pan", t.pan },
            { "mute", t.mute }, { "solo", t.solo }, { "sends", sends },
            { "mode", t.audio && t.notes ? 0 : t.audio ? 1 : t.notes ? 2 : 0 },
            { "inserts", insertsFor (t, i, -1, mixer::maxInserts) },
        };
        if (const QColor c (fromStd (t.colour)); c.isValid())
            o.insert ("colour", c.name());
        QJsonArray clips;
        for (const auto& a : t.audioClips)
        {
            if (a.file.empty())
                continue;
            clips.append (QJsonObject { { "kind", "audio" }, { "file", fromStd (a.file) }, { "start", a.start }, { "offset", a.offset },
                                        { "length", a.length }, { "muted", a.muted }, { "reversed", false } });
        }
        for (const auto& c : t.noteClips)
            if (const auto mid = midiFile (t, c); ! mid.isEmpty())
                clips.append (QJsonObject { { "kind", "midi" }, { "file", mid }, { "start", c.start }, { "offset", 0.0 },
                                            { "length", c.length }, { "muted", c.muted }, { "reversed", false } });
        o.insert ("clips", clips);
        if (const auto* inst = t.instrument())
        {
            bool same = false;
            const auto* found = inst->isPlugin() ? matching (instruments, *inst, same) : nullptr;
            if (found != nullptr)
            {
                o.insert ("instrument", QJsonObject { { "kind", "plugin" }, { "id", fromStd (found->id) }, { "name", fromStd (found->name) }, { "state", QString() } });
                if (same && ! inst->state.empty())
                    natives.push_back ({ i, -1, -1, found->id, inst->state });
                else if (! inst->state.empty())
                    say (tr ("%1: %2 is there as %3, without its settings").arg (fromStd (t.name), fromStd (inst->name), fromStd (found->format)));
            }
            else if (inst->isPlugin())
                say (tr ("%1: the instrument %2 (%3) is not among PHI's plug-ins; the Synth plays it").arg (fromStd (t.name), fromStd (inst->name), fromStd (inst->format())));
        }
        // A drum machine (Live's Drum Rack) on PHI's Slicer: a pad each, in order from bank A,
        // each answering the note it did, so the clips play the same sounds and Push has them.
        if (! o.contains (QStringLiteral ("instrument")) && ! t.kit.empty())
        {
            QJsonArray pads;
            const auto r = rate();
            for (int k = 0; k < std::min ((int) t.kit.size(), slicer::numPads); ++k)
            {
                const auto& pad = t.kit[(size_t) k];
                const auto start = std::max (0.0, pad.start) * r;
                const auto end = pad.end > pad.start ? pad.end * r : 1.0e12;   // to the sound's end
                QJsonObject layer { { "layer", 0 }, { "file", fromStd (pad.file) }, { "start", std::floor (start) }, { "end", std::ceil (end) },
                                    { "gain", 0.0 }, { "pitch", 0.0 }, { "fadeIn", 0.0 }, { "fadeOut", 0.0 }, { "reverse", false } };
                pads.append (QJsonObject { { "pad", k }, { "name", fromStd (pad.name) }, { "layers", QJsonArray { layer } },
                                           { "gain", std::clamp (pad.gainDb, -60.0, 24.0) }, { "pan", pad.pan }, { "pitch", std::clamp (pad.pitch, -48.0, 48.0) },
                                           { "gate", pad.gate }, { "choke", pad.choke }, { "mute", pad.mute }, { "receive", pad.note } });
            }
            if ((int) t.kit.size() > slicer::numPads)
                say (tr ("%1: the pads after pad %2 are left out").arg (fromStd (t.name)).arg (slicer::numPads));
            o.insert ("instrument", QJsonObject { { "kind", "slicer" },
                                                  { "kit", QJsonObject { { "version", 2 }, { "pads", pads }, { "layout", "push" }, { "pushPage", 0 } } } });
        }
        // A one-sample instrument (Live's Simpler) on PHI's Sampler.
        if (! o.contains (QStringLiteral ("instrument")) && ! t.samplerFile.empty())
            o.insert ("instrument", QJsonObject { { "kind", "sampler" }, { "sound", fromStd (t.samplerFile) } });
        // Notes need something to play them: the Synth, as for a MIDI file put on a track.
        if (! o.contains (QStringLiteral ("instrument")) && (! t.noteClips.empty() || std::any_of (t.slots.begin(), t.slots.end(), [] (const dawproject::Slot& x) { return ! x.audio; })))
            o.insert ("instrument", QJsonObject { { "kind", "synth" }, { "state", QString() } });
        for (const auto& s : t.slots)
        {
            if (s.scene >= mixer::maxScenes)
            {
                say (tr ("Scenes after scene %1 are left out").arg (mixer::maxScenes));
                continue;
            }
            scenes = std::max (scenes, s.scene + 1);
            QJsonObject slot { { "track", i }, { "scene", s.scene }, { "loop", true }, { "quantise", -1.0 }, { "reversed", false } };
            if (s.audio)
            {
                if (s.audioClip.file.empty())
                    continue;
                slot.insert ("kind", "audio");
                slot.insert ("file", fromStd (s.audioClip.file));
                slot.insert ("offset", s.audioClip.offset);
                slot.insert ("length", s.audioClip.length);
            }
            else
            {
                const auto mid = midiFile (t, s.noteClip);
                if (mid.isEmpty())
                    continue;
                slot.insert ("kind", "midi");
                slot.insert ("file", mid);
                slot.insert ("offset", 0.0);
                slot.insert ("length", s.noteClip.length);
            }
            slotList.append (slot);
        }
        tracks.append (o);
    }

    QJsonArray returns;
    // Every effect track a return (A, B, then C...), with its name. One effect goes in the
    // return's slot; several go in series into a Prism of effects there, all in its one ray.
    for (int r = 0; r < returnCount; ++r)
    {
        const auto& t = p.returns[(size_t) r];
        QJsonObject o { { "fader", std::clamp (t.volumeDb, -100.0, 6.0) }, { "pan", t.pan }, { "mute", t.mute }, { "name", fromStd (t.name) } };
        const auto effectsHere = insertsFor (t, -1, r, mixer::EffectPrism::maxEffects);
        if (effectsHere.size() == 1)
            o.insert ("effect", effectsHere.first());
        else if (effectsHere.size() > 1)
            o.insert ("effect", QJsonObject { { "id", QString::fromUtf8 (mixer::EffectPrism::identifier) }, { "name", fromStd (t.name) },
                                              { "state", QString() }, { "rays", QJsonArray { QJsonObject { { "effects", effectsHere } } } } });
        if (! t.audioClips.empty() || ! t.noteClips.empty())
            say (tr ("The clips on the effect track %1 are left out").arg (fromStd (t.name)));
        returns.append (o);
    }

    // The launcher in front when the project is only scenes, as it was made in Bitwig's clip launcher.
    const auto onlyScenes = ! slotList.isEmpty() && std::none_of (p.tracks.begin(), p.tracks.end(), [] (const dawproject::Track& t)
                                                                  { return ! t.audioClips.empty() || ! t.noteClips.empty(); });
    QJsonArray sceneNames;
    for (const auto& name : p.scenes)
        sceneNames.append (fromStd (name));
    const auto numTracks = std::clamp ((int) p.tracks.size(), 1, mixer::maxTracks);
    QJsonObject root {
        { "phinyxProject", 1 }, { "phinyxMix", 3 }, { "tempo", p.bpm }, { "beatsPerBar", std::clamp (p.numerator, 1, 12) },
        { "midiFollowsTempo", false }, { "master", std::clamp (p.masterDb, -100.0, 6.0) }, { "armed", 0 },
        { "tracks", tracks }, { "trackLimit", std::max (mixer::defaultTrackLimit, numTracks) }, { "returns", returns },
        { "launcher", QJsonObject { { "scenes", std::clamp (std::max (scenes, 1), 1, mixer::maxScenes) }, { "quantise", 4.0 },
                                    { "sceneNames", sceneNames }, { "slots", slotList }, { "shown", onlyScenes } } },
    };
    if (p.denominator != 4)
        say (tr ("The time signature's %1/%2: PHI counts %1 beats to the bar").arg (p.numerator).arg (p.denominator));

    const auto phiFile = folder.filePath (source.completeBaseName() + QLatin1Char ('.') + projectSuffix);
    {
        QSaveFile out (phiFile);
        if (! out.open (QIODevice::WriteOnly) || out.write (QJsonDocument (root).toJson()) < 0 || ! out.commit())
        {
            error = tr ("%1 could not be written: %2").arg (phiFile, out.errorString());
            return false;
        }
    }
    if (! loadMix (phiFile, error))
        return false;

    // The plugins' own states, now they run.
    int given = 0;
    for (const auto& n : natives)
    {
        bool ok = false;
        if (n.ret >= 0)
        {
            // The return's effect, or the n.insert'th in its Prism's ray.
            if (auto fx = engine.returnEffect (n.ret); fx != nullptr && mixer::EffectPrism::is (*fx))
            {
                const auto rays = static_cast<const mixer::EffectPrism&> (*fx).rays();
                if (! rays.empty() && n.insert < (int) rays.front().effects.size() && rays.front().effects[(size_t) n.insert].effect->pluginId() == n.pluginId)
                    ok = rays.front().effects[(size_t) n.insert].effect->setNativeState (n.state);
            }
            else if (fx != nullptr && n.insert == 0 && fx->pluginId() == n.pluginId)
                ok = fx->setNativeState (n.state);
        }
        else if (n.insert < 0)
        {
            if (auto inst = engine.instrument (n.track))
                ok = inst->setNativeState (n.state);
        }
        else
        {
            const auto list = engine.inserts (n.track);
            if (n.insert < (int) list.size() && list[(size_t) n.insert].effect->pluginId() == n.pluginId)
                ok = list[(size_t) n.insert].effect->setNativeState (n.state);
        }
        if (ok)
            ++given;
        else
            say (tr ("A plug-in's settings could not be given back (%1)").arg (fromStd (n.pluginId)));
    }
    if (given > 0)
    {
        QString why2;
        writeMix (phiFile, why2);   // the plugins' states in PHI's own form, in the .phi
    }
    showStatus (tr ("Opened %1 from %2 (%3), saved as %4").arg (source.fileName(), fromStd (p.application).isEmpty() ? tr ("another program") : fromStd (p.application),
                                                                      live ? tr ("a Live Set") : QStringLiteral ("DAWproject"), phiFile));
    showNotes (this, live ? tr ("Open Live Set") : tr ("Open DAWproject"), tr ("%1 is open as a PHI project in %2.").arg (source.fileName(), folderPath), notes);
    return true;
}

void MixerWindow::importDawProjectFromMenu()
{
    const auto title = tr ("Import a Project");
    if (modified && QMessageBox::question (this, title, tr ("Open another project? Changes to %1 that were not saved are lost.").arg (mixName())) != QMessageBox::Yes)
        return;
    const auto file = QFileDialog::getOpenFileName (this, title, folder(),
                                                    tr ("Projects of other programs (*.dawproject *.als);;DAWproject: Bitwig, Studio One, Cubase... (*.dawproject);;Ableton Live Sets (*.als);;All files (*)"));
    if (file.isEmpty())
        return;
    QString error;
    if (! importDawProject (file, error))
        QMessageBox::warning (this, title, error);
}

//==============================================================================
// Out: the tracks, their clips and plugins as DAWproject keeps them, the audio in it.
void MixerWindow::exportDawProject()
{
    const auto suggested = QDir (folder()).filePath (mixName() + QLatin1Char ('.') + dawSuffix);
    auto file = QFileDialog::getSaveFileName (this, tr ("Export DAWproject"), suggested, tr ("DAWproject (*.dawproject)"));
    if (file.isEmpty())
        return;
    if (! isDawProject (file))
        file += QLatin1Char ('.') + dawSuffix;

    dawproject::Project p;
    p.application = "PHI";
    p.applicationVersion = APP_VERSION;
    p.bpm = tempoBox->value();
    p.numerator = beatsBox->value();
    p.masterDb = engine.masterDb.load();
    p.title = mixName().toStdString();
    auto say = [&p] (const QString& s) { p.notes.push_back (s.toStdString()); };
    const auto r = rate();

    auto audioClipOf = [&] (const mixer::Clip& c, const QString& track) -> std::optional<dawproject::AudioClip>
    {
        if (c.audio->path.empty())
        {
            say (tr ("%1: a clip that has no file (record or bounce it to one first)").arg (track));
            return std::nullopt;
        }
        if (c.reversed)
            say (tr ("%1: reversed clips go forwards").arg (track));
        dawproject::AudioClip a;
        a.file = c.audio->path;
        a.start = (double) c.start / r;
        a.offset = (double) c.offset / r;
        a.length = (double) c.length / r;
        a.muted = c.muted;
        a.channels = c.audio->fileChannels > 0 ? c.audio->fileChannels : c.audio->mono ? 1 : 2;
        a.sampleRate = (int) std::lround (r);
        a.fileLength = (double) c.audio->length() / r;
        a.name = QFileInfo (QString::fromStdString (c.audio->path)).completeBaseName().toStdString();
        return a;
    };
    auto noteClipOf = [&] (const mixer::Clip& c, const QString& track, int transpose) -> std::optional<dawproject::NoteClip>
    {
        if (c.midi == nullptr || c.midi->song == nullptr)
            return std::nullopt;
        if (c.reversed)
            say (tr ("%1: reversed clips go forwards").arg (track));
        dawproject::NoteClip n;
        n.start = (double) c.start / r;
        n.length = (double) c.length / r;
        n.lengthBeats = n.length * p.bpm / 60.0;
        n.muted = c.muted;
        n.notes = dawproject::notesOf (*c.midi->song, (double) c.offset / r, n.length, c.midi->bpm, p.bpm);
        for (auto& note : n.notes)
            if (note.channel != 9)   // as the mixer plays them: the drums' channel is left alone
                note.key = std::clamp (note.key + transpose, 0, 127);
        n.name = QFileInfo (QString::fromStdString (c.midi->path)).completeBaseName().toStdString();
        return n;
    };
    auto pluginOf = [&] (const auto& thing, const char* role, bool enabled, const QString& where, std::vector<dawproject::Device>& into)
    {
        std::string format, id;
        dawproject::Device d;
        if (! thing.nativeState (format, id, d.state))
        {
            say (tr ("%1: %2, which is not a VST3 or CLAP plug-in").arg (where, fromStd (thing.name())));
            return;
        }
        d.element = format == "VST3" ? "Vst3Plugin" : "ClapPlugin";
        d.role = role;
        d.deviceId = id;
        d.name = thing.name();
        d.enabled = enabled;
        into.push_back (std::move (d));
    };

    for (int i = 0; i < engine.numTracks(); ++i)
    {
        const auto& s = engine.strip (i);
        const auto name = info[(size_t) i].name;
        dawproject::Track t;
        t.name = name.toStdString();
        t.colour = info[(size_t) i].colour.name().toStdString();
        t.volumeDb = s.faderDb.load();
        t.pan = s.pan.load();
        t.mute = s.mute.load();
        t.solo = s.solo.load();
        t.audio = s.mode.load() != 2;
        t.notes = s.mode.load() != 1;
        for (int ret = 0; ret < engine.returnsInUse(); ++ret)
            if (s.sendDb[(size_t) ret].load() > -100.0f)
                t.sends.push_back ({ ret, s.sendDb[(size_t) ret].load(), s.sendPre[(size_t) ret].load(), true });
        for (const auto& c : engine.clips (i))
            if (c.audio != nullptr)
            {
                if (auto a = audioClipOf (c, name))
                    t.audioClips.push_back (*a);
            }
            else if (auto n = noteClipOf (c, name, s.transpose.load()))
                t.noteClips.push_back (*n);
        for (int sc = 0; sc < mixer::maxScenes; ++sc)
            if (const auto& slot = slots[(size_t) i][(size_t) sc]; slot != nullptr)
            {
                dawproject::Slot out;
                out.scene = sc;
                if (slot->clip.audio != nullptr)
                {
                    const auto a = audioClipOf (slot->clip, name);
                    if (! a)
                        continue;
                    out.audio = true;
                    out.audioClip = *a;
                }
                else if (const auto n = noteClipOf (slot->clip, name, s.transpose.load()))
                    out.noteClip = *n;
                else
                    continue;
                t.slots.push_back (std::move (out));
            }
        if (const auto inst = engine.instrument (i))
        {
            if (inst->kind() == mixer::Instrument::Kind::plugin)
                pluginOf (*inst, "instrument", true, name, t.devices);
            else
                say (tr ("%1: PHI's own %2 (Bounce the track to keep its sound)").arg (name, fromStd (inst->name())));
        }
        for (const auto& x : engine.inserts (i))
            pluginOf (*x.effect, "audioFX", ! x.bypassed, name, t.devices);
        if (s.width.load() != 1.0f || s.lowCut.load() || s.highCut.load() || s.delayMs.load() != 0.0f || s.trimDb.load() != 0.0f)
            say (tr ("%1: the strip's trim, width, cuts and delay").arg (name));
        p.tracks.push_back (std::move (t));
    }
    for (int ret = 0; ret < engine.returnsInUse(); ++ret)
    {
        auto& c = engine.returnStrip (ret);
        dawproject::Track t;
        t.name = returnTitle (ret).toStdString();
        t.audio = true;
        t.volumeDb = c.faderDb.load();
        t.pan = c.pan.load();
        t.mute = c.mute.load();
        if (const auto fx = engine.returnEffect (ret); fx != nullptr && mixer::EffectPrism::is (*fx))
        {
            // A Prism's first ray, in series, as the effect track's devices.
            const auto& prism = static_cast<const mixer::EffectPrism&> (*fx);
            const auto rays = prism.rays();
            if (! rays.empty())
                for (const auto& x : rays.front().effects)
                    pluginOf (*x.effect, "audioFX", ! x.bypassed, fromStd (t.name), t.devices);
            if (rays.size() > 1 || prism.split() != prism::Split::parallel)
                say (tr ("%1: its Prism's first ray, in series (the other rays and the split are PHI's own)").arg (returnTitle (ret)));
        }
        else if (fx != nullptr)
            pluginOf (*fx, "audioFX", true, fromStd (t.name), t.devices);
        p.returns.push_back (std::move (t));
    }
    for (int sc = 0; sc < numScenes; ++sc)
        p.scenes.push_back (sceneNames[(size_t) sc].toStdString());
    if (! engine.inserts (mixer::busTrack).empty())
        say (tr ("The Pre-Master bus and its inserts"));

    std::string why;
    if (! dawproject::write (p, file.toStdString(), why))
    {
        QMessageBox::warning (this, tr ("Export DAWproject"), fromStd (why));
        return;
    }
    appSettings().setValue (QStringLiteral ("mixer/folder"), QFileInfo (file).absolutePath());
    showStatus (tr ("Exported the project to %1").arg (file));
    showNotes (this, tr ("Export DAWproject"), tr ("Wrote %1.").arg (QFileInfo (file).fileName()), p.notes);
}
