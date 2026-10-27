// SPDX-License-Identifier: AGPL-3.0-only
// Control surfaces and MIDI Learn (docs/control-surfaces.md): Launchpads, Launchkeys and Push 2
// and 3 found by their ports and driven as Ableton and Bitwig drive them, and any controller's
// knobs and buttons mapped to the mixer and transport by MIDI Learn.
#include "MainWindow.h"
#include "MixerWindow.h"
#include "Settings.h"
#include "../control/MidiLearn.h"
#include "../engine/ControlSurfaces.h"
#include "../engine/MidiInputs.h"
#include "../engine/PushDisplay.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
    QJsonObject toJson (const control::Mapping& m)
    {
        QJsonObject o;
        o["source"] = (int) m.source;
        o["channel"] = m.channel;
        o["number"] = m.number;
        o["device"] = QString::fromStdString (m.device);
        o["action"] = (int) m.target.action;
        o["track"] = m.target.track;
        o["scene"] = m.target.scene;
        o["index"] = m.target.index;
        o["value"] = m.target.value;
        o["mode"] = (int) m.mode;
        return o;
    }

    control::Mapping fromJson (const QJsonObject& o)
    {
        control::Mapping m;
        m.source = (control::Mapping::Source) std::clamp (o.value ("source").toInt(), 0, 2);
        m.channel = std::clamp (o.value ("channel").toInt(), 0, 16);
        m.number = std::clamp (o.value ("number").toInt(), 0, 127);
        m.device = o.value ("device").toString().toStdString();
        m.target.action = (control::Action) std::clamp (o.value ("action").toInt(), 0, (int) control::Action::none);
        m.target.track = std::clamp (o.value ("track").toInt (-1), -1, control::maxTracks - 1);
        m.target.scene = std::clamp (o.value ("scene").toInt (-1), -1, control::maxScenes - 1);
        m.target.index = std::clamp (o.value ("index").toInt(), 0, 1);
        m.target.value = (float) o.value ("value").toDouble (-1.0);
        m.mode = (control::Mapping::Mode) std::clamp (o.value ("mode").toInt(), 0, (int) control::Mapping::Mode::button);
        return m;
    }

    QStringList modeNames()
    {
        return { QObject::tr ("Absolute"), QObject::tr ("Relative (1 up, 127 down)"), QObject::tr ("Relative (65 up, 63 down)"),
                 QObject::tr ("Relative (1 up, 65 down)"), QObject::tr ("Button") };
    }
}

void MainWindow::registerControlCommands()
{
    const auto category = tr ("Performance");
    auto add = [this, &category] (const char* id, const QString& text, std::function<void()> fn)
    {
        auto* action = commands.add (QString::fromLatin1 (id), category, text, {}, std::move (fn));
        addAction (action);
        return action;
    };
    add ("control.surfaces", tr ("Control &Surfaces..."), [this] { controlSurfacesDialog(); })
        ->setStatusTip (tr ("Launchpad, Launchkey and Push: found when plugged in, and driving the clip launcher, the mixer and the transport"));
    add ("control.learn", tr ("MIDI &Learn..."), [this] { midiLearnDialog(); })
        ->setStatusTip (tr ("Map any controller's knobs, faders and buttons (keytars, Intech Grid...) to the mixer and the transport"));

    surfaces = std::make_unique<ControlSurfaces>();
    midiLearn = std::make_unique<control::Learn>();

    auto settings = appSettings();
    std::set<std::string> disabled;
    for (const auto& id : settings.value (QStringLiteral ("control/disabled")).toStringList())
        disabled.insert (id.toStdString());
    surfaces->setDisabled (disabled);
    std::vector<control::Mapping> mappings;
    for (const auto& v : QJsonDocument::fromJson (settings.value (QStringLiteral ("control/mappings")).toByteArray()).array())
        mappings.push_back (fromJson (v.toObject()));
    midiLearn->setMappings (std::move (mappings));

    // A surface's pads in Note view, and its drum pads, play like a keyboard (straight from the MIDI thread).
    surfaces->setNotes ([this] (const uint8_t* d, int size)
    {
        if (size < 2)
            return;
        const int status = d[0] & 0xF0, channel = (d[0] & 0x0F) + 1;
        if (status == 0xE0)
            playMidi (status, channel, d[1] | ((size > 2 ? d[2] : 0) << 7), 0);
        else if (status == 0x90 || status == 0x80)
            playMidi (size > 2 && d[2] == 0 ? 0x80 : status, channel, d[1], size > 2 ? d[2] : 0);
    });

    // MIDI Learn sees every keyboard's and controller's messages first; what it maps does not play.
    midiInputs->setFilter ([this] (const std::string& device, int status, int channel, int a, int b)
    {
        std::vector<control::Command> out;
        std::optional<control::Mapping> learned;
        if (! midiLearn->handle (device, status, channel, a, b, out, &learned))
            return false;
        if (! out.empty() || learned)
            QMetaObject::invokeMethod (this, [this, out, learned]
            {
                for (const auto& c : out)
                    controlCommand (c);
                if (learned)
                {
                    saveMidiMappings();
                    const auto* t = control::targetOf (learned->target);
                    statusBar()->showMessage (tr ("Learned: %1 does %2").arg (QString::fromStdString (control::describeSource (*learned)),
                                                                               t != nullptr ? tr (t->name) : QString()),
                                              5000);
                    if (learnDialogRefresh)
                        learnDialogRefresh();
                }
            }, Qt::QueuedConnection);
        return true;
    });

    controlTimer.setInterval (33);
    connect (&controlTimer, &QTimer::timeout, this, [this] { controlTick(); });
    QTimer::singleShot (0, this, [this]
    {
        if (surfaces->scan())
            openMidiInputs();
        controlTimer.start();
    });
}

void MainWindow::controlCommand (const control::Command& c)
{
    if (c.action == control::Action::showLauncher)
    {
        showMixer();
        mixerWin->showLauncher (true);
        return;
    }
    mixerWindow()->surfaceCommand (c);
}

void MainWindow::controlTick()
{
    for (const auto& c : surfaces->takeCommands())
        controlCommand (c);
    // Plugged in and taken out: looked for every two seconds.
    if (++controlTicks % 60 == 0)
    {
        if (surfaces->scan())
            openMidiInputs();
        else
            lookForMidiInputs();
    }
    const auto state = mixerWin != nullptr ? mixerWin->surfaceState() : control::State {};
    surfaces->refresh (state);

    // Push's display: opened while a Push is in use (tried again every two seconds until it
    // opens), drawn 15 times a second.
    const bool push = surfaces->push().open;
    if (pushDisplay != nullptr && (! push || pushDisplay->failed()))
        pushDisplay = nullptr;
    if (push && pushDisplay == nullptr && PushDisplay::available() && (controlTicks % 60 == 1 || controlTicks < 3))
    {
        auto display = std::make_unique<PushDisplay>();
        if (display->open())
            pushDisplay = std::move (display);
    }
    if (pushDisplay != nullptr && controlTicks % 2 == 0)
        drawPushDisplay (state);
}

void MainWindow::drawPushDisplay (const control::State& state)
{
    using Knobs = control::PadSurface::Knobs;
    using View = control::PadSurface::View;
    const auto shown = surfaces->push();
    static QImage image (PushDisplay::width, PushDisplay::height, QImage::Format_ARGB32);
    image.fill (QColor (0, 0, 0));
    QPainter p (&image);
    p.setRenderHint (QPainter::Antialiasing);
    auto font = [&p] (int px, bool bold = false)
    {
        QFont f = p.font();
        f.setPixelSize (px);
        f.setBold (bold);
        p.setFont (f);
    };
    constexpr int column = PushDisplay::width / 8;

    // Along the top: the view, what the encoders turn, the scenes shown, and the transport.
    const auto knobName = shown.knobs == Knobs::volume ? tr ("Volume") : shown.knobs == Knobs::pan ? tr ("Pan")
                        : shown.knobs == Knobs::sendA ? tr ("Send A") : tr ("Send B");
    const auto viewName = shown.view == View::session ? tr ("Session: scenes %1-%2").arg (shown.sceneOffset + 1).arg (shown.sceneOffset + 8)
                        : shown.view == View::note ? tr ("Note") : tr ("Mixer");
    p.fillRect (0, 0, PushDisplay::width, 20, QColor (28, 28, 28));
    font (13, true);
    p.setPen (QColor (230, 230, 230));
    p.drawText (QRect (8, 0, 600, 20), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral ("PHI   %1   %2").arg (viewName, knobName));
    QString transport = QStringLiteral ("%1 BPM").arg (state.tempo, 0, 'f', 2);
    if (state.metronome)
        transport.prepend (tr ("Click   "));
    p.drawText (QRect (PushDisplay::width - 400, 0, 392, 20), Qt::AlignVCenter | Qt::AlignRight, transport);
    if (state.playing)
    {
        p.setBrush (QColor (60, 200, 90));
        p.setPen (Qt::NoPen);
        const QPointF play[] = { { 760.0, 5.0 }, { 760.0, 15.0 }, { 769.0, 10.0 } };
        p.drawPolygon (play, 3);
    }
    if (state.recording)
    {
        p.setBrush (QColor (230, 40, 40));
        p.setPen (Qt::NoPen);
        p.drawEllipse (QPointF (745.0, 10.0), 5.0, 5.0);
    }

    for (int t = 0; t < 8; ++t)
    {
        const int x = t * column;
        if (t >= state.tracks)
            continue;
        const auto& track = state.track[(size_t) t];
        const QColor colour = QColor::fromRgb (track.rgb);
        const bool selected = state.selected == t;
        if (selected)
            p.fillRect (x + 1, 20, column - 2, 112, QColor (36, 36, 36));

        // Arm, mute, solo and play, over the encoders.
        font (11, true);
        int bx = x + 6;
        auto badge = [&] (const QString& text, QColor bg)
        {
            const int w = QFontMetrics (p.font()).horizontalAdvance (text) + 8;
            p.setPen (Qt::NoPen);
            p.setBrush (bg);
            p.drawRoundedRect (QRectF (bx, 25, w, 15), 3, 3);
            p.setPen (QColor (0, 0, 0));
            p.drawText (QRect (bx, 25, w, 15), Qt::AlignCenter, text);
            bx += w + 4;
        };
        if (track.armed)
            badge (tr ("ARM"), QColor (230, 50, 50));
        if (track.mute)
            badge (tr ("M"), QColor (240, 190, 40));
        if (track.solo)
            badge (tr ("S"), QColor (70, 150, 240));
        if (track.playing)
            badge (QStringLiteral ("\u25B6"), QColor (60, 200, 90));

        // What the encoder above turns: its value in words, then as a bar.
        const float unit = shown.knobs == Knobs::volume ? track.volume : shown.knobs == Knobs::pan ? track.pan
                         : track.send[shown.knobs == Knobs::sendA ? 0 : 1];
        QString value;
        if (shown.knobs == Knobs::pan)
        {
            const int amount = (int) std::lround (std::abs (track.pan) * 100.0f);
            value = amount == 0 ? tr ("C") : QStringLiteral ("%1 %2").arg (track.pan < 0.0f ? tr ("L") : tr ("R")).arg (amount);
        }
        else
        {
            const float db = control::dbFromUnit (unit);
            value = db <= -99.0f ? tr ("-inf dB") : QStringLiteral ("%1 dB").arg (db, 0, 'f', 1);
        }
        font (14);
        p.setPen (QColor (220, 220, 220));
        p.drawText (QRect (x + 6, 44, column - 12, 20), Qt::AlignVCenter | Qt::AlignLeft, value);
        const QRectF bar (x + 8, 70, column - 16, 10);
        p.setPen (Qt::NoPen);
        p.setBrush (QColor (60, 60, 60));
        p.drawRoundedRect (bar, 3, 3);
        p.setBrush (colour);
        if (shown.knobs == Knobs::pan)
        {
            const double mid = bar.center().x(), to = mid + track.pan * bar.width() / 2.0;
            p.drawRect (QRectF (std::min (mid, to), bar.top(), std::max (2.0, std::abs (to - mid)), bar.height()));
        }
        else
            p.drawRoundedRect (QRectF (bar.left(), bar.top(), bar.width() * std::clamp (unit, 0.0f, 1.0f), bar.height()), 3, 3);

        // The clip playing.
        font (13);
        p.setPen (track.clip.empty() ? QColor (110, 110, 110) : QColor (235, 235, 235));
        const auto clip = track.clip.empty() ? tr ("no clip") : QString::fromStdString (track.clip);
        p.drawText (QRect (x + 6, 92, column - 12, 30), Qt::AlignVCenter | Qt::AlignLeft,
                    QFontMetrics (p.font()).elidedText (clip, Qt::ElideRight, column - 12));

        // The track's name in its colour, over the button that selects it.
        p.fillRect (x + 2, 134, column - 4, 26, selected ? colour : colour.darker (300));
        font (14, true);
        p.setPen (selected ? (colour.lightness() > 140 ? QColor (0, 0, 0) : QColor (255, 255, 255)) : QColor (235, 235, 235));
        const auto name = track.name.empty() ? tr ("Track %1").arg (t + 1) : QString::fromStdString (track.name);
        p.drawText (QRect (x + 6, 134, column - 12, 26), Qt::AlignVCenter | Qt::AlignLeft,
                    QFontMetrics (p.font()).elidedText (name, Qt::ElideRight, column - 12));
    }
    p.end();
    pushDisplay->setFrame (reinterpret_cast<const uint32_t*> (image.constBits()), (int) (image.bytesPerLine() / 4));
}

void MainWindow::saveMidiMappings()
{
    QJsonArray list;
    for (const auto& m : midiLearn->mappings())
        list.append (toJson (m));
    appSettings().setValue (QStringLiteral ("control/mappings"), QJsonDocument (list).toJson (QJsonDocument::Compact));
}

void MainWindow::controlSurfacesDialog()
{
    QDialog dialog (this);
    dialog.setWindowTitle (tr ("Control Surfaces"));
    auto* layout = new QVBoxLayout (&dialog);
    QStringList supported;
    for (const auto& k : control::kinds())
        supported << QString::fromStdString (k.name);
    auto* about = new QLabel (tr ("Plugged-in surfaces are found by their MIDI ports and driven at once: their pads play the clip launcher "
                                  "(Session), the armed track (Note) or the strips (Mixer), lit in the tracks' colours, with their knobs, "
                                  "faders and transport buttons. No drivers are needed. Untick one to use it as a plain MIDI controller.\n\n"
                                  "Supported: %1.")
                                  .arg (supported.join (QStringLiteral (", "))),
                              &dialog);
    about->setWordWrap (true);
    layout->addWidget (about);
    auto* list = new QListWidget (&dialog);
    layout->addWidget (list);
    auto fill = [this, list]
    {
        const QSignalBlocker block (list);
        list->clear();
        for (const auto& f : surfaces->found())
        {
            const auto port = QString::fromStdString (f.inputName.empty() ? f.outputName : f.inputName);
            auto state = ! f.enabled ? tr ("off: a plain MIDI controller") : f.open ? tr ("connected through %1").arg (port) : tr ("could not be opened");
            if (f.open && (f.kind == "push2" || f.kind == "push3"))
                state += pushDisplay != nullptr ? tr ("; display on")
                       : ! PushDisplay::available() ? tr ("; display off (PHI was built without libusb)")
                       : tr ("; display off: no USB access to it (see the udev rule in docs/control-surfaces.md)");
            auto* item = new QListWidgetItem (QStringLiteral ("%1: %2").arg (QString::fromStdString (f.name), state), list);
            item->setData (Qt::UserRole, QString::fromStdString (f.kind));
            item->setFlags (item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState (f.enabled ? Qt::Checked : Qt::Unchecked);
        }
        if (list->count() == 0)
            new QListWidgetItem (tr ("No control surface found. Plug one in and press Look Again."), list);
    };
    auto apply = [this, list, fill]
    {
        QStringList off;
        std::set<std::string> disabled;
        for (int i = 0; i < list->count(); ++i)
            if (const auto* item = list->item (i); (item->flags() & Qt::ItemIsUserCheckable) && item->checkState() == Qt::Unchecked)
            {
                off << item->data (Qt::UserRole).toString();
                disabled.insert (off.back().toStdString());
            }
        // Kinds not plugged in now keep the choice they had.
        for (const auto& id : appSettings().value (QStringLiteral ("control/disabled")).toStringList())
        {
            bool present = false;
            for (const auto& f : surfaces->found())
                present |= QString::fromStdString (f.kind) == id;
            if (! present)
            {
                off << id;
                disabled.insert (id.toStdString());
            }
        }
        appSettings().setValue (QStringLiteral ("control/disabled"), off);
        surfaces->setDisabled (disabled);
        if (surfaces->scan())
            openMidiInputs();
        fill();
    };
    fill();
    connect (list, &QListWidget::itemChanged, &dialog, apply);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Close, &dialog);
    auto* again = buttons->addButton (tr ("&Look Again"), QDialogButtonBox::ActionRole);
    auto* learn = buttons->addButton (tr ("MIDI &Learn..."), QDialogButtonBox::ActionRole);
    connect (again, &QPushButton::clicked, &dialog, [this, fill]
    {
        if (surfaces->scan())
            openMidiInputs();
        if (pushDisplay == nullptr && surfaces->push().open)
        {
            auto display = std::make_unique<PushDisplay>();
            if (display->open())
                pushDisplay = std::move (display);
        }
        fill();
    });
    connect (learn, &QPushButton::clicked, &dialog, [this, &dialog]
    {
        dialog.accept();
        midiLearnDialog();
    });
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget (buttons);
    dialog.resize (520, 340);
    dialog.exec();
}

void MainWindow::midiLearnDialog()
{
    if (learnDialog != nullptr)
    {
        learnDialog->raise();
        learnDialog->activateWindow();
        return;
    }
    auto* dialog = new QDialog (this);
    dialog->setAttribute (Qt::WA_DeleteOnClose);
    dialog->setWindowTitle (tr ("MIDI Learn"));
    learnDialog = dialog;
    auto* layout = new QVBoxLayout (dialog);
    auto* about = new QLabel (tr ("Choose what a control should do, press Learn, then move the knob or fader, or press the key, pad or "
                                  "button, on any keyboard or controller (a keytar's buttons and strips, an Intech Grid's encoders). "
                                  "A mapped control no longer plays notes. Endless encoders: set their Mode to the relative kind they send."),
                              dialog);
    about->setWordWrap (true);
    layout->addWidget (about);

    auto* table = new QTableWidget (0, 3, dialog);
    table->setHorizontalHeaderLabels ({ tr ("Control"), tr ("Does"), tr ("Mode") });
    table->horizontalHeader()->setSectionResizeMode (0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode (1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode (2, QHeaderView::ResizeToContents);
    table->setSelectionBehavior (QAbstractItemView::SelectRows);
    table->setEditTriggers (QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    layout->addWidget (table, 1);

    auto describe = [] (const control::Command& c)
    {
        const auto* t = control::targetOf (c);
        auto text = t != nullptr ? tr (t->name) : tr ("Nothing");
        if (t != nullptr && t->perTrack)
            text += c.track >= 0 ? tr (", track %1").arg (c.track + 1) : tr (", selected track");
        if (t != nullptr && t->perScene)
            text += tr (", scene %1").arg (c.scene + 1);
        return text;
    };
    auto fill = [this, table, describe]
    {
        const auto list = midiLearn->mappings();
        table->setRowCount ((int) list.size());
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto& m = list[(size_t) i];
            table->setItem (i, 0, new QTableWidgetItem (QString::fromStdString (control::describeSource (m))));
            table->setItem (i, 1, new QTableWidgetItem (describe (m.target)));
            auto* mode = new QComboBox (table);
            mode->addItems (modeNames());
            mode->setCurrentIndex ((int) m.mode);
            connect (mode, &QComboBox::currentIndexChanged, table, [this, i] (int k)
            {
                auto all = midiLearn->mappings();
                if (i < (int) all.size())
                {
                    all[(size_t) i].mode = (control::Mapping::Mode) k;
                    midiLearn->setMappings (all);
                    saveMidiMappings();
                }
            });
            table->setCellWidget (i, 2, mode);
        }
    };
    fill();

    // What the next control learned will do.
    auto* row = new QHBoxLayout;
    auto* target = new QComboBox (dialog);
    for (const auto& t : control::targets())
        target->addItem (tr (t.name));
    auto* trackBox = new QComboBox (dialog);
    trackBox->addItem (tr ("Selected track"));
    for (int t = 0; t < control::maxTracks; ++t)
        trackBox->addItem (tr ("Track %1").arg (t + 1));
    auto* sceneBox = new QSpinBox (dialog);
    sceneBox->setRange (1, control::maxScenes);
    sceneBox->setPrefix (tr ("Scene "));
    auto* learn = new QPushButton (tr ("&Learn"), dialog);
    learn->setDefault (true);
    row->addWidget (target, 1);
    row->addWidget (trackBox);
    row->addWidget (sceneBox);
    row->addWidget (learn);
    layout->addLayout (row);
    auto* waiting = new QLabel (dialog);
    layout->addWidget (waiting);
    auto showFields = [target, trackBox, sceneBox]
    {
        const auto& t = control::targets()[(size_t) std::max (0, target->currentIndex())];
        trackBox->setEnabled (t.perTrack);
        sceneBox->setEnabled (t.perScene);
    };
    connect (target, &QComboBox::currentIndexChanged, dialog, showFields);
    showFields();
    connect (learn, &QPushButton::clicked, dialog, [this, target, trackBox, sceneBox, waiting]
    {
        const auto& t = control::targets()[(size_t) std::max (0, target->currentIndex())];
        control::Command c;
        c.action = t.action;
        c.index = t.index;
        c.value = t.action == control::Action::selectBy ? (float) t.value : -1.0f;
        c.track = t.perTrack ? trackBox->currentIndex() - 1 : -1;
        c.scene = t.perScene ? sceneBox->value() - 1 : -1;
        midiLearn->learn (c, t.continuous);
        waiting->setText (tr ("Move a knob or fader, or press a key or button, for \"%1\"...").arg (tr (t.name)));
    });

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Close, dialog);
    auto* remove = buttons->addButton (tr ("&Remove"), QDialogButtonBox::ActionRole);
    auto* clear = buttons->addButton (tr ("Remove &All"), QDialogButtonBox::ActionRole);
    connect (remove, &QPushButton::clicked, dialog, [this, table, fill]
    {
        auto all = midiLearn->mappings();
        const auto r = table->currentRow();
        if (r < 0 || r >= (int) all.size())
            return;
        all.erase (all.begin() + r);
        midiLearn->setMappings (all);
        saveMidiMappings();
        fill();
    });
    connect (clear, &QPushButton::clicked, dialog, [this, fill]
    {
        midiLearn->setMappings ({});
        saveMidiMappings();
        fill();
    });
    connect (buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget (buttons);
    connect (dialog, &QObject::destroyed, this, [this]
    {
        midiLearn->cancel();
        learnDialogRefresh = {};
    });
    // Learned: the list again, and nothing waiting.
    learnDialogRefresh = [fill, waiting]
    {
        fill();
        waiting->clear();
    };
    dialog->resize (640, 420);
    dialog->show();
}
