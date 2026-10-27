// SPDX-License-Identifier: AGPL-3.0-only
// cubify (owner, 2026-10-26): a clip launcher scene, or the MIDI clip shown in the Device
// Chain, arranged by Xenakis's cubes (mixer/Cubify.h, docs/cubify.md): 24 sections, V1 V1 V1
// V2 six times, onto the tracks' timelines after everything already on them, with a
// commentary of the sections and the modifications beside the MIDI files. Every name in it is
// lower case (owner).
#include "MixerWindow.h"
#include "Settings.h"
#include "../mixer/Cubify.h"
#include "../mixer/Tunings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int ticksPerQuarter = 480;

    const char* const messiaenModes[7] { "mode 1 (whole tone)", "mode 2 (octatonic)", "mode 3", "mode 4", "mode 5", "mode 6", "mode 7" };
    const char* const firstNotes[12] { "c", "c♯", "d", "e♭", "e", "f", "f♯", "g", "a♭", "a", "b♭", "b" };

    // A track of what is cubified: its clip, and what cubify reads of it.
    struct Source
    {
        int track = -1;
        mixer::Clip clip;
        cubify::Part part;
        int channel = 0;
    };
}

void MixerWindow::cubify()
{
    if (showsMidiClip())
        cubifyShownMidi();
    else
        cubifyScene (cursorScene);
}

void MixerWindow::cubifyShownMidi()
{
    int track = -1, scene = -1;
    mixer::Clip clip;
    mixer::SlotPtr slot;
    mixer::TuningChoice tuning;
    if (! shownMidiClip (track, scene, clip, slot, tuning))
        return;
    cubifyTracks ({ { track, clip } }, QFileInfo (QString::fromStdString (clip.midi->path)).completeBaseName());
}

void MixerWindow::cubifyScene (int scene)
{
    if (scene < 0 || scene >= numScenes)
        return;
    std::vector<std::pair<int, mixer::Clip>> clips;
    for (int t = 0; t < engine.numTracks(); ++t)
        if (const auto& slot = slots[(size_t) t][(size_t) scene]; slot != nullptr)
            clips.push_back ({ t, slot->clip });
    if (clips.empty())
    {
        say (tr ("cubify: %1 has no clips to arrange").arg (sceneName (scene)));
        return;
    }
    cubifyTracks (clips, sceneName (scene));
}

void MixerWindow::cubifyTracks (const std::vector<std::pair<int, mixer::Clip>>& clips, const QString& name)
{
    const auto title = tr ("cubify");
    std::vector<Source> sources;
    int midi = 0, audio = 0;
    for (const auto& [track, clip] : clips)
    {
        Source s;
        s.track = track;
        s.clip = clip;
        const auto trackName = info[(size_t) track].name.toStdString();
        if (clip.midi != nullptr && clip.midi->song != nullptr)
        {
            auto song = clip.midi->song;
            if (song->ticksPerQuarter <= 0)
                continue;   // SMPTE time
            if (clip.reversed)
                song = midifile::reversed (*song);   // read as it is heard
            s.part = cubify::partOf (*song, trackName, ticksPerQuarter);
            if (s.part.notes.empty())
                continue;
            s.channel = s.part.drums ? 9 : 0;
            for (int c = 0; c < 16 && ! s.part.drums; ++c)
                if ((song->channelsUsed & (1u << c)) && c != 9)
                {
                    s.channel = c;
                    break;
                }
            ++midi;
        }
        else if (clip.audio != nullptr)
        {
            s.part = { trackName, {}, false, true };
            ++audio;
        }
        else
            continue;
        sources.push_back (std::move (s));
    }
    if (midi == 0)
    {
        say (tr ("cubify needs a MIDI clip with notes to take its cubes from"));
        return;
    }

    // The Mode menu's mode, when it is one of Messiaen's, starts the dialog on it.
    auto settings = appSettings();
    const auto modeSystem = mixer::findTuning (settings.value (QStringLiteral ("mode/system")).toString().toStdString());
    int menuMessiaen = 0;
    if (modeSystem >= 0 && mixer::tuningSystems()[(size_t) modeSystem].family == "messiaen mode")
        menuMessiaen = mixer::tuningSystems()[(size_t) modeSystem].plain.back() - '0';

    QDialog dialog (this);
    dialog.setWindowTitle (title);
    auto* form = new QFormLayout (&dialog);
    auto* pitchesBox = new QComboBox (&dialog);
    pitchesBox->addItem (tr ("a sieve"));
    pitchesBox->addItem (tr ("the clips' own, then the sieve"));
    pitchesBox->addItem (tr ("a messiaen mode of limited transposition"));
    pitchesBox->setToolTip (tr ("Where each pitch cube's eight fixed pitches come from: never moved by an octave, as Xenakis's are"));
    auto* sieveEdit = new QLineEdit (&dialog);
    sieveEdit->setToolTip (tr ("Xenakis's sieve, counted from c (MIDI key 0) in quarter tones, or in semitones when quarter tones are "
                               "off: m@r is every step r more than a multiple of m; | joins, & keeps what both have, - leaves out. "
                               "At first the nomos alpha sieve, period 143: no octaves in it"));
    auto* messiaenBox = new QComboBox (&dialog);
    for (const auto* m : messiaenModes)
        messiaenBox->addItem (tr (m));
    auto* rootBox = new QComboBox (&dialog);
    rootBox->addItem (tr ("each track's lowest note"), -1);
    for (int i = 0; i < 12; ++i)
        rootBox->addItem (QString::fromUtf8 (firstNotes[i]), i);
    rootBox->setToolTip (tr ("The mode's first note"));
    auto* quarterBox = new QCheckBox (tr ("quarter tones, by pitch bend (± 2 semitones)"), &dialog);
    quarterBox->setToolTip (tr ("Each track plays one note at a time, so each can bend on its own; any instrument that bends ± 2 "
                                "semitones plays them, as the synth does. Off: semitones only"));
    auto* pathBox = new QComboBox (&dialog);
    pathBox->addItem (tr ("nomos alpha: each cube turns its own way"));
    pathBox->addItem (tr ("mode de valeurs: the cubes locked together (messiaen)"));
    pathBox->setToolTip (tr ("Nomos alpha: the pitch cube on Xenakis's path (D, Q12, Q4...), the durations on another, the dynamics "
                             "against the pitches, the articulations on a third.\nMode de valeurs: each pitch keeps its own duration, "
                             "dynamic and attack, as in Messiaen's Mode de valeurs et d'intensités; only their order turns"));
    auto* readingsBox = new QSpinBox (&dialog);
    readingsBox->setRange (1, 4);
    readingsBox->setToolTip (tr ("Times each V1 section reads the eight corners, the cube turned further each time"));
    auto* durationsBox = new QCheckBox (tr ("durations from the clips"), &dialog);
    durationsBox->setToolTip (tr ("The duration cube from the clips' own, onset to onset; off: 1, 2, 3, 4, 6, 8, 12 and 16 sixteenths"));
    auto* palindromeBox = new QCheckBox (tr ("non-retrogradable rhythms (messiaen)"), &dialog);
    palindromeBox->setToolTip (tr ("Each reading's eight durations a palindrome: the same backwards"));
    auto* rangeBox = new QComboBox (&dialog);
    rangeBox->addItem (tr ("the track's own pitch at that corner"));
    rangeBox->addItem (tr ("a rest"));
    rangeBox->setToolTip (tr ("What a track plays for a pitch out of its range, when it has turned onto another track's cube: never "
                              "the pitch an octave away, which would undo the fixed pitches"));
    auto* balanceBox = new QCheckBox (tr ("balance: bring up a part too soft to be heard"), &dialog);
    auto* hairpinBox = new QCheckBox (tr ("hairpins between jumps in dynamics, swells in V2"), &dialog);
    auto* breathBox = new QCheckBox (tr ("room to breathe, every two bars (winds)"), &dialog);
    auto* glissBox = new QCheckBox (tr ("glissandi in V2"), &dialog);
    auto* variation = new QSpinBox (&dialog);
    variation->setRange (1, 9999);
    variation->setToolTip (tr ("1: Xenakis's own paths through the group; another number, other paths"));

    pitchesBox->setCurrentIndex (menuMessiaen > 0 ? 2 : std::clamp (settings.value (QStringLiteral ("cubify/pitches"), 0).toInt(), 0, 2));
    sieveEdit->setText (settings.value (QStringLiteral ("cubify/sieve"), QString::fromStdString (cubify::Settings {}.sieve)).toString());
    messiaenBox->setCurrentIndex (std::clamp ((menuMessiaen > 0 ? menuMessiaen : settings.value (QStringLiteral ("cubify/messiaen"), 2).toInt()) - 1, 0, 6));
    rootBox->setCurrentIndex (menuMessiaen > 0 ? std::clamp (settings.value (QStringLiteral ("mode/final"), 0).toInt(), 0, 11) + 1
                                               : std::clamp (settings.value (QStringLiteral ("cubify/root"), -1).toInt() + 1, 0, 12));
    quarterBox->setChecked (settings.value (QStringLiteral ("cubify/quarterTones"), true).toBool());
    pathBox->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("cubify/path"), 0).toInt(), 0, 1));
    readingsBox->setValue (settings.value (QStringLiteral ("cubify/readings"), 2).toInt());
    durationsBox->setChecked (settings.value (QStringLiteral ("cubify/clipDurations"), true).toBool());
    palindromeBox->setChecked (settings.value (QStringLiteral ("cubify/palindromes"), false).toBool());
    rangeBox->setCurrentIndex (std::clamp (settings.value (QStringLiteral ("cubify/outOfRange"), 0).toInt(), 0, 1));
    balanceBox->setChecked (settings.value (QStringLiteral ("cubify/balance"), true).toBool());
    hairpinBox->setChecked (settings.value (QStringLiteral ("cubify/hairpins"), true).toBool());
    breathBox->setChecked (settings.value (QStringLiteral ("cubify/breaths"), false).toBool());
    glissBox->setChecked (settings.value (QStringLiteral ("cubify/glissandi"), true).toBool());
    variation->setValue (settings.value (QStringLiteral ("cubify/variation"), 1).toInt());
    auto shown = [&]
    {
        const auto messiaen = pitchesBox->currentIndex() == 2;
        sieveEdit->setEnabled (! messiaen);
        messiaenBox->setEnabled (messiaen);
        rootBox->setEnabled (messiaen);
    };
    connect (pitchesBox, &QComboBox::currentIndexChanged, &dialog, shown);
    shown();

    form->addRow (tr ("pitches"), pitchesBox);
    form->addRow (tr ("sieve"), sieveEdit);
    form->addRow (tr ("messiaen mode"), messiaenBox);
    form->addRow (tr ("on"), rootBox);
    form->addRow (QString(), quarterBox);
    form->addRow (tr ("path"), pathBox);
    form->addRow (tr ("readings"), readingsBox);
    form->addRow (QString(), durationsBox);
    form->addRow (QString(), palindromeBox);
    form->addRow (tr ("out of range"), rangeBox);
    form->addRow (QString(), balanceBox);
    form->addRow (QString(), hairpinBox);
    form->addRow (QString(), breathBox);
    form->addRow (QString(), glissBox);
    form->addRow (tr ("variation"), variation);
    auto* note = new QLabel (tr ("%1: %2 MIDI and %3 audio track(s), each on a corner of the grand cube. 24 sections (V1 V1 V1 V2, six "
                                 "times) go onto their tracks after everything already on them, with a commentary of the "
                                 "modifications beside the MIDI files.").arg (name).arg (midi).arg (audio), &dialog);
    note->setWordWrap (true);
    note->setForegroundRole (QPalette::PlaceholderText);
    form->addRow (note);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button (QDialogButtonBox::Ok)->setText (tr ("cubify"));
    form->addRow (buttons);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    cubify::Settings options;
    options.ticksPerQuarter = ticksPerQuarter;
    options.pitches = (cubify::Pitches) pitchesBox->currentIndex();
    options.sieve = sieveEdit->text().trimmed().toStdString();
    options.messiaenMode = messiaenBox->currentIndex() + 1;
    options.root = rootBox->currentData().toInt();
    options.quarterTones = quarterBox->isChecked();
    options.path = (cubify::Path) pathBox->currentIndex();
    options.readings = readingsBox->value();
    options.clipDurations = durationsBox->isChecked();
    options.palindromes = palindromeBox->isChecked();
    options.outOfRange = (cubify::OutOfRange) rangeBox->currentIndex();
    options.balance = balanceBox->isChecked();
    options.hairpins = hairpinBox->isChecked();
    options.breaths = breathBox->isChecked();
    options.glissandi = glissBox->isChecked();
    options.seed = (uint32_t) variation->value();
    settings.setValue (QStringLiteral ("cubify/pitches"), pitchesBox->currentIndex());
    settings.setValue (QStringLiteral ("cubify/sieve"), sieveEdit->text().trimmed());
    settings.setValue (QStringLiteral ("cubify/messiaen"), options.messiaenMode);
    settings.setValue (QStringLiteral ("cubify/root"), options.root);
    settings.setValue (QStringLiteral ("cubify/quarterTones"), options.quarterTones);
    settings.setValue (QStringLiteral ("cubify/path"), pathBox->currentIndex());
    settings.setValue (QStringLiteral ("cubify/readings"), options.readings);
    settings.setValue (QStringLiteral ("cubify/clipDurations"), options.clipDurations);
    settings.setValue (QStringLiteral ("cubify/palindromes"), options.palindromes);
    settings.setValue (QStringLiteral ("cubify/outOfRange"), rangeBox->currentIndex());
    settings.setValue (QStringLiteral ("cubify/balance"), options.balance);
    settings.setValue (QStringLiteral ("cubify/hairpins"), options.hairpins);
    settings.setValue (QStringLiteral ("cubify/breaths"), options.breaths);
    settings.setValue (QStringLiteral ("cubify/glissandi"), options.glissandi);
    settings.setValue (QStringLiteral ("cubify/variation"), variation->value());

    std::vector<cubify::Part> parts;
    for (const auto& s : sources)
        parts.push_back (s.part);
    // At once: 24 sections of eight tracks are a few thousand notes.
    const auto result = cubify::arrange (parts, options);
    if (! result.error.empty())
    {
        say (tr ("cubify: %1").arg (QString::fromStdString (result.error)));
        return;
    }

    // Onto the tracks, all at one time, after everything already on them.
    const auto bpm = tempoBox->value();
    const auto framesPerTick = rate() * 60.0 / (bpm * ticksPerQuarter);
    auto frameOf = [&] (int64_t tick) { return (Frame) std::llround ((double) tick * framesPerTick); };
    Frame at = 0;
    for (const auto& s : sources)
        for (const auto& c : engine.clips (s.track))
            at = std::max (at, c.end());
    const auto base = editsFolder();
    QDir().mkpath (base);
    auto stem = name.isEmpty() ? tr ("scene") : name;
    stem.replace (QRegularExpression (QStringLiteral ("[/\\\\:*?\"<>|]")), QStringLiteral ("_"));
    remember();
    int written = 0;
    for (size_t i = 0; i < sources.size(); ++i)
    {
        const auto& s = sources[i];
        const auto& line = result.lines[i];
        auto list = engine.clips (s.track);
        if (s.part.audio)
        {
            // The loop, repeated through each span it plays in.
            for (const auto& [from, length] : line.plays)
            {
                auto pos = at + frameOf (from);
                const auto end = at + frameOf (from + length);
                while (s.clip.length > 0 && pos < end)
                {
                    auto piece = s.clip;
                    piece.start = pos;
                    piece.length = std::min (s.clip.length, end - pos);
                    piece.id = 0;
                    list.push_back (piece);
                    pos += piece.length;
                }
            }
        }
        else
        {
            const auto song = cubify::toSong (line, ticksPerQuarter, bpm, s.channel);
            QString file;
            for (int n = 1; file.isEmpty() || QFileInfo::exists (file); ++n)
                file = QDir (base).filePath (n == 1 ? QStringLiteral ("%1 cubify %2.mid").arg (stem, info[(size_t) s.track].name)
                                                    : QStringLiteral ("%1 cubify %2 %3.mid").arg (stem, info[(size_t) s.track].name).arg (n));
            std::string why;
            if (! midifile::write (*song, file.toStdString(), why))
            {
                say (tr ("cubify could not save %1: %2").arg (file, QString::fromStdString (why)));
                continue;
            }
            auto placed = mixer::makeClip (mixer::placeMidi (song, file.toStdString(), rate(), followTempoBox->isChecked() ? bpm : 0.0), at);
            list.push_back (placed);
            giveSynth (s.track);
        }
        std::sort (list.begin(), list.end(), [] (const mixer::Clip& a, const mixer::Clip& b) { return a.start < b.start; });
        editClips (s.track, std::move (list));
        ++written;
    }
    // The commentary, as a composer writes one for a cube piece.
    QFile commentary (QDir (base).filePath (QStringLiteral ("%1 cubify commentary.txt").arg (stem)));
    if (commentary.open (QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        QTextStream (&commentary) << QString::fromStdString (result.commentary);
    generatedPlaced();
    const auto bars = (int) (result.length / (4 * ticksPerQuarter));
    say (tr ("cubified %1: %n track(s), 24 sections, %2 bars from bar %3, %4 modifications (the commentary is beside the mix)", "", written)
             .arg (name).arg (bars).arg ((int) std::floor ((double) at / (rate() * 240.0 / bpm)) + 1).arg (result.modified.total()));
}
