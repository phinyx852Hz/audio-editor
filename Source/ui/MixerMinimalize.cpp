// SPDX-License-Identifier: AGPL-3.0-only
// Minimalize (owner, 2026-10-26): a scene of the clip launcher, or the timeline from a point on
// (cubify's arrangement, a Φ deal), arranged by Reich's and Glass's processes (mixer/Minimalize.h)
// and placed after everything on the tracks, all of them starting together. Step 4 of PHI's
// generative chain: the Selector's set, the MIDI generators, cubify, then minimalize.
#include "MixerWindow.h"
#include "Settings.h"
#include "../mixer/Minimalize.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    using minimalize::Process;

    // The notes a clip plays between timeline frames [from, to), in ticks from `from`.
    minimalize::Notes notesOf (const mixer::Clip& clip, Frame from, Frame to, double ticksPerFrame)
    {
        minimalize::Notes out;
        if (clip.midi == nullptr || clip.muted)
            return out;
        struct Open { Frame at = -1; int velocity = 0; };
        std::array<std::array<Open, 128>, 16> open {};
        const auto end = std::min (to, clip.end());
        auto close = [&] (int channel, int pitch, Frame at)
        {
            auto& o = open[(size_t) channel][(size_t) pitch];
            if (o.at >= 0 && o.at >= from && o.at < to)
                out.push_back ({ (int64_t) std::llround ((double) (o.at - from) * ticksPerFrame),
                                 (int64_t) std::llround ((double) (std::min (at, end) - from) * ticksPerFrame),
                                 pitch, o.velocity, channel });
            o.at = -1;
        };
        for (const auto& e : clip.midi->events)
        {
            if (e.frame < clip.offset || e.frame >= clip.offset + clip.length)
                continue;
            const auto at = clip.start + e.frame - clip.offset;
            const auto kind = e.status & 0xF0;
            const int channel = e.status & 0x0F, pitch = e.data1 & 0x7F;
            if (kind == 0x90 && e.data2 > 0)
            {
                close (channel, pitch, at);
                open[(size_t) channel][(size_t) pitch] = { at, e.data2 };
            }
            else if (kind == 0x80 || kind == 0x90)
                close (channel, pitch, at);
        }
        for (int c = 0; c < 16; ++c)
            for (int p = 0; p < 128; ++p)
                close (c, p, end);
        return out;
    }

    bool allDrums (const minimalize::Notes& notes)
    {
        return ! notes.empty() && std::all_of (notes.begin(), notes.end(), [] (const minimalize::Note& n) { return n.channel == 9; });
    }

    int64_t toSixteenths (int64_t ticks)
    {
        const int64_t s = minimalize::ticksPerQuarter / 4;
        return std::max (s, (ticks + s / 2) / s * s);
    }

    const Process processes[] { Process::automatic, Process::hold, Process::phase, Process::canon, Process::buildUp,
                                Process::pulse, Process::additive, Process::subtractive, Process::cycles, Process::arpeggio };
}

// The tempo the notes are counted in: the mixer's when MIDI follows it, else the first clip's own.
static double countingBpm (bool follow, double mixerBpm, const std::vector<mixer::MidiClipPtr>& clips)
{
    if (! follow)
        for (const auto& c : clips)
            if (c != nullptr && c->song != nullptr && c->song->fileBpm() > 0.0)
                return c->bpm > 0.0 ? c->bpm : c->song->fileBpm();
    return mixerBpm;
}

void MixerWindow::minimalizeScene (int scene)
{
    if (scene < 0 || scene >= numScenes)
        return;
    std::vector<mixer::MidiClipPtr> sources;
    for (int t = 0; t < engine.numTracks(); ++t)
        if (const auto& slot = slots[(size_t) t][(size_t) scene]; slot != nullptr && slot->clip.midi != nullptr)
            sources.push_back (slot->clip.midi);
    const auto bpm = countingBpm (followTempoBox->isChecked(), tempoBox->value(), sources);
    const auto ticksPerFrame = bpm / 60.0 * minimalize::ticksPerQuarter / rate();

    std::vector<int> tracks;
    std::vector<minimalize::Part> parts;
    for (int t = 0; t < engine.numTracks(); ++t)
    {
        const auto& slot = slots[(size_t) t][(size_t) scene];
        if (slot == nullptr || slot->clip.midi == nullptr)
            continue;
        auto clip = slot->clip;
        clip.start = 0;
        auto notes = notesOf (clip, 0, clip.length, ticksPerFrame);
        if (notes.empty())
            continue;
        minimalize::Part part;
        part.name = trackName (t).toStdString();
        part.drums = allDrums (notes);
        part.cells.push_back ({ std::move (notes), toSixteenths ((int64_t) std::llround ((double) clip.length * ticksPerFrame)) });
        tracks.push_back (t);
        parts.push_back (std::move (part));
    }
    if (parts.empty())
    {
        say (tr ("%1 has no MIDI clips with notes to minimalize.").arg (sceneName (scene)));
        return;
    }
    arrangeMinimal (tracks, std::move (parts), bpm, false, sceneName (scene));
}

void MixerWindow::minimalizeTimeline (Frame from)
{
    // Every MIDI clip that sounds from here on, to where the last of them ends.
    Frame to = from;
    std::vector<mixer::MidiClipPtr> sources;
    for (int t = 0; t < engine.numTracks(); ++t)
        for (const auto& c : engine.clips (t))
            if (c.midi != nullptr && ! c.muted && c.end() > from)
            {
                to = std::max (to, c.end());
                sources.push_back (c.midi);
            }
    const auto bpm = countingBpm (followTempoBox->isChecked(), tempoBox->value(), sources);
    const auto ticksPerFrame = bpm / 60.0 * minimalize::ticksPerQuarter / rate();
    const auto length = toSixteenths ((int64_t) std::llround ((double) (to - from) * ticksPerFrame));

    std::vector<int> tracks;
    std::vector<minimalize::Part> parts;
    for (int t = 0; t < engine.numTracks(); ++t)
    {
        minimalize::Notes notes;
        for (const auto& c : engine.clips (t))
        {
            auto some = notesOf (c, from, to, ticksPerFrame);
            notes.insert (notes.end(), some.begin(), some.end());
        }
        if (notes.empty())
            continue;
        minimalize::Part part;
        part.name = trackName (t).toStdString();
        part.drums = allDrums (notes);
        part.cells.push_back ({ std::move (notes), length });
        tracks.push_back (t);
        parts.push_back (std::move (part));
    }
    if (parts.empty())
    {
        say (tr ("No MIDI notes on the tracks from here on to minimalize."));
        return;
    }
    arrangeMinimal (tracks, std::move (parts), bpm, true, tr ("timeline"));
}

void MixerWindow::arrangeMinimal (const std::vector<int>& tracks, std::vector<minimalize::Part> parts, double bpm, bool timeline, const QString& what)
{
    const auto title = tr ("minimalize");
    const int free = mixer::maxTracks - engine.numTracks();

    QDialog dialog (this);
    dialog.setWindowTitle (title);
    auto* form = new QFormLayout (&dialog);
    auto* styleBox = new QComboBox (&dialog);
    styleBox->addItem (tr ("reich and glass"));
    styleBox->addItem (tr ("reich: phasing, canon, build-up, pulse"));
    styleBox->addItem (tr ("glass: additive, subtractive, cycles, arpeggios"));
    styleBox->setToolTip (tr ("Which processes the tracks set to automatic take"));
    form->addRow (tr ("Style"), styleBox);

    std::vector<QComboBox*> processBoxes;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        auto* box = new QComboBox (&dialog);
        for (const auto p : processes)
            box->addItem (QString::fromLatin1 (minimalize::name (p)));
        box->setToolTip (tr ("phase: a copy a step ahead each stage until back in unison. canon: copies a beat apart. build-up: notes in place of rests, "
                             "one a stage. pulse: the harmony in breathing eighths. additive: the figure a note longer each stage. subtractive: a "
                             "note shorter. cycles: a little shorter than the others, so they part and meet. arpeggio: the chords broken in eighths."));
        form->addRow (QString::fromStdString (parts[i].name), box);
        processBoxes.push_back (box);
    }

    auto* repeatsBox = new QSpinBox (&dialog);
    repeatsBox->setRange (1, 32);
    repeatsBox->setSuffix (tr (" times"));
    repeatsBox->setToolTip (tr ("How often each stage plays before the next, as Reich and Glass mark a bar to repeat"));
    auto* stepBox = new QComboBox (&dialog);
    stepBox->addItem (tr ("a sixteenth"), minimalize::ticksPerQuarter / 4);
    stepBox->addItem (tr ("an eighth"), minimalize::ticksPerQuarter / 2);
    stepBox->setToolTip (tr ("How far the phasing copy moves ahead each stage"));
    auto* shiftsBox = new QSpinBox (&dialog);
    shiftsBox->setRange (0, 64);
    shiftsBox->setSpecialValueText (tr ("back to unison"));
    shiftsBox->setToolTip (tr ("Steps the phasing copy takes before it holds; all the way round brings it back to unison"));
    auto* voicesBox = new QCheckBox (tr ("New voices on the free tracks (%n free)", "", std::max (0, free)), &dialog);
    voicesBox->setToolTip (tr ("Phasing and canon copies on tracks of their own against the pattern held still, and a pulse"));
    voicesBox->setEnabled (free > 0);
    QComboBox* cellBox = nullptr;
    if (timeline)
    {
        cellBox = new QComboBox (&dialog);
        cellBox->addItem (tr ("each bar"), 1);
        cellBox->addItem (tr ("every two bars"), 2);
        cellBox->addItem (tr ("every four bars"), 4);
        cellBox->addItem (tr ("every eight bars"), 8);
        cellBox->addItem (tr ("all of it as one pattern"), 0);
        cellBox->setToolTip (tr ("The timeline is cut into patterns, a section each, one after another"));
    }
    auto* variation = new QSpinBox (&dialog);
    variation->setRange (1, 9999);
    variation->setToolTip (tr ("Another number gives another order of build-up and another arpeggio figure"));

    auto settings = appSettings();
    styleBox->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("minimalize/style"), 0).toInt(), 0, 2));
    repeatsBox->setValue (settings.value (QStringLiteral ("minimalize/repeats"), 4).toInt());
    stepBox->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("minimalize/step"), 0).toInt(), 0, 1));
    shiftsBox->setValue (settings.value (QStringLiteral ("minimalize/shifts"), timeline ? 4 : 0).toInt());
    voicesBox->setChecked (free > 0 && settings.value (QStringLiteral ("minimalize/voices"), true).toBool());
    if (cellBox != nullptr)
        cellBox->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("minimalize/cell"), 1).toInt(), 0, 4));
    variation->setValue (settings.value (QStringLiteral ("minimalize/variation"), 1).toInt());

    form->addRow (tr ("Each stage"), repeatsBox);
    form->addRow (tr ("Phase step"), stepBox);
    form->addRow (tr ("Phase steps"), shiftsBox);
    if (cellBox != nullptr)
        form->addRow (tr ("A section"), cellBox);
    form->addRow (QString(), voicesBox);
    form->addRow (tr ("Variation"), variation);
    auto* note = new QLabel (tr ("From %1: the arrangement goes on the tracks after everything on them, all starting together.").arg (what), &dialog);
    note->setWordWrap (true);
    note->setForegroundRole (QPalette::PlaceholderText);
    form->addRow (note);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button (QDialogButtonBox::Ok)->setText (tr ("minimalize"));
    form->addRow (buttons);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    settings.setValue (QStringLiteral ("minimalize/style"), styleBox->currentIndex());
    settings.setValue (QStringLiteral ("minimalize/repeats"), repeatsBox->value());
    settings.setValue (QStringLiteral ("minimalize/step"), stepBox->currentIndex());
    settings.setValue (QStringLiteral ("minimalize/shifts"), shiftsBox->value());
    settings.setValue (QStringLiteral ("minimalize/voices"), voicesBox->isChecked());
    if (cellBox != nullptr)
        settings.setValue (QStringLiteral ("minimalize/cell"), cellBox->currentIndex());
    settings.setValue (QStringLiteral ("minimalize/variation"), variation->value());

    minimalize::Settings options;
    options.style = styleBox->currentIndex() == 1 ? minimalize::Style::reich : styleBox->currentIndex() == 2 ? minimalize::Style::glass : minimalize::Style::both;
    for (auto* box : processBoxes)
        options.processes.push_back (processes[box->currentIndex()]);
    options.repeats = repeatsBox->value();
    options.stepTicks = stepBox->currentData().toInt();
    options.shifts = shiftsBox->value();
    options.newVoices = voicesBox->isChecked();
    options.freeTracks = std::max (0, free);
    options.seed = (uint32_t) variation->value();

    // The timeline's stretch cut into a pattern a section.
    if (cellBox != nullptr && cellBox->currentData().toInt() > 0)
    {
        const int64_t cell = (int64_t) minimalize::ticksPerQuarter * 4 * cellBox->currentData().toInt();
        for (auto& part : parts)
        {
            const auto whole = part.cells.front();
            part.cells.clear();
            for (int64_t at = 0; at < whole.length; at += cell)
                part.cells.push_back (minimalize::cut (whole.notes, at, at + cell));
        }
    }

    const auto result = minimalize::arrange (parts, options);
    if (result.length <= 0)
    {
        say (tr ("Nothing to minimalize."));
        return;
    }

    // After everything on the tracks, all at one time.
    Frame at = 0;
    for (int t = 0; t < engine.numTracks(); ++t)
        for (const auto& c : engine.clips (t))
            at = std::max (at, c.end());

    const auto base = editsFolder();
    QDir().mkpath (base);
    auto stem = what;
    stem.replace (QRegularExpression (QStringLiteral ("[/\\\\:*?\"<>|]")), QStringLiteral ("_"));
    const auto placeBpm = followTempoBox->isChecked() ? tempoBox->value() : 0.0;
    remember();
    int made = 0, added = 0;
    for (const auto& track : result.tracks)
    {
        if (track.notes.empty())
            continue;
        int target = -1;
        if (! track.added)
            target = tracks[(size_t) track.source];
        else if (engine.numTracks() < mixer::maxTracks)
        {
            const auto from = tracks[(size_t) track.source];
            target = engine.numTracks();
            engine.setNumTracks (target + 1);
            auto& to = engine.strip (target);
            to.trimDb = 0.0f;
            to.invertLeft = to.invertRight = false;
            to.lowCut = to.highCut = false;
            to.width = 1.0f;
            to.delayMs = 0.0f;
            to.faderDb = engine.strip (from).faderDb.load();
            to.pan = 0.0f;
            to.mute = to.solo = false;
            to.transpose = 0;
            to.channels = 0xFFFF;
            engine.setInserts (target, {});
            engine.setInstrument (target, nullptr);
            engine.setClips (target, {});
            info[(size_t) target].name = QString::fromStdString (track.name);
            info[(size_t) target].colour = info[(size_t) from].colour;
            info[(size_t) target].tuning = info[(size_t) from].tuning;
            ++added;
        }
        if (target < 0)
            continue;
        const auto song = minimalize::toSong (track.notes, bpm, result.length);
        auto name = QString::fromStdString (track.name);
        name.replace (QRegularExpression (QStringLiteral ("[/\\\\:*?\"<>|]")), QStringLiteral ("_"));
        QString file;
        for (int n = 1; file.isEmpty() || QFileInfo::exists (file); ++n)
            file = QDir (base).filePath (n == 1 ? QStringLiteral ("%1 minimalize %2.mid").arg (stem, name)
                                                : QStringLiteral ("%1 minimalize %2 %3.mid").arg (stem, name).arg (n));
        std::string why;
        if (! midifile::write (*song, file.toStdString(), why))
        {
            say (tr ("The arrangement could not be saved to %1: %2").arg (file, QString::fromStdString (why)));
            break;
        }
        auto clips = engine.clips (target);
        clips.push_back (mixer::makeClip (mixer::placeMidi (song, file.toStdString(), rate(), placeBpm), at));
        editClips (target, std::move (clips));
        giveSynth (target);
        ++made;
    }
    rebuildStrips();
    changed();
    const auto bars = (double) result.length / (minimalize::ticksPerQuarter * 4.0);
    auto text = tr ("minimalized %1: %n track(s), %2 bars", "", made).arg (what).arg (QString::number (bars, 'f', bars == std::floor (bars) ? 0 : 1));
    if (result.sections.size() > 1)
        text += tr (" in %n section(s)", "", (int) result.sections.size());
    if (added > 0)
        text += tr (", %n new voice(s)", "", added);
    say (text);
}
