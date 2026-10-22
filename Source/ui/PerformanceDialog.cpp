// SPDX-License-Identifier: AGPL-3.0-only
#include "PerformanceDialog.h"
#include "Settings.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
    const QString group = QStringLiteral ("performance/");
    QString key (const char* name) { return group + QLatin1String (name); }

    QString heightLabel (int height)
    {
        switch (height)
        {
            case 720:  return QObject::tr ("720p (1280 × 720)");
            case 1440: return QObject::tr ("1440p (2560 × 1440)");
            case 2160: return QObject::tr ("4K (3840 × 2160)");
            default:   return QObject::tr ("1080p (1920 × 1080)");
        }
    }

    // A check box centred in a table cell.
    QWidget* cellCheck (bool on, const QString& name, QCheckBox** box)
    {
        auto* holder = new QWidget;
        auto* row = new QHBoxLayout (holder);
        row->setContentsMargins (0, 0, 0, 0);
        row->setAlignment (Qt::AlignCenter);
        *box = new QCheckBox (holder);
        (*box)->setChecked (on);
        (*box)->setAccessibleName (name);
        row->addWidget (*box);
        return holder;
    }

    // Applications are known by name: their PipeWire serial changes each time they start playing.
    bool sameSource (const CaptureSource& a, const CaptureSource& b)
    {
        if (a.kind == CaptureSource::Kind::application || b.kind == CaptureSource::Kind::application)
            return a.kind == b.kind && a.name == b.name;
        return a == b;
    }

    // PHINYX's own sound is already the main track; recording it again would double it.
    bool isPhinyx (const CaptureSource& s)
    {
        return QString::fromStdString (s.name).contains (QLatin1String (APP_ID), Qt::CaseInsensitive);
    }

    QCheckBox* checkIn (QTableWidget* table, int row, int column)
    {
        auto* w = table->cellWidget (row, column);
        return w != nullptr ? w->findChild<QCheckBox*>() : nullptr;
    }
}

namespace perfui
{
QString defaultFolder()
{
    return QStandardPaths::writableLocation (QStandardPaths::MoviesLocation) + QStringLiteral ("/" APP_DISPLAY_NAME);
}

const QList<Service>& services()
{
    static const QList<Service> list {
        { QStringLiteral ("YouTube"), QStringLiteral ("rtmp://a.rtmp.youtube.com/live2") },
        { QStringLiteral ("Twitch"), QStringLiteral ("rtmp://live.twitch.tv/app") },
        { QStringLiteral ("Facebook Live"), QStringLiteral ("rtmps://live-api-s.facebook.com:443/rtmp") },
        { QObject::tr ("Custom (RTMP, RTMPS or SRT)"), QString() },
    };
    return list;
}

perf::Settings loadSettings()
{
    auto q = appSettings();
    perf::Settings s;
    s.height = q.value (key ("height"), 0).toInt();
    s.fps = q.value (key ("fps"), 30).toInt();
    s.encoder = q.value (key ("encoder")).toString().toStdString();
    s.quality = q.value (key ("quality"), 1.0).toDouble();
    s.audioKbps = q.value (key ("audioKbps"), 256).toInt();
    s.programTrack = q.value (key ("programTrack"), true).toBool();
    s.fragmented = q.value (key ("fragmented"), false).toBool();
    s.folder = q.value (key ("folder"), defaultFolder()).toString().toStdString();
    s.followWindow = q.value (key ("followWindow"), true).toBool();

    const auto inputs = q.beginReadArray (key ("inputs"));
    for (int i = 0; i < inputs; ++i)
    {
        q.setArrayIndex (i);
        perf::InputChoice c;
        c.source.kind = q.value (QStringLiteral ("application"), false).toBool() ? CaptureSource::Kind::application : CaptureSource::Kind::input;
        c.source.target = q.value (QStringLiteral ("target")).toString().toStdString();
        c.source.name = q.value (QStringLiteral ("name")).toString().toStdString();
        c.source.channels = q.value (QStringLiteral ("channels"), 2).toInt();
        c.gainDb = q.value (QStringLiteral ("gainDb"), 0.0).toDouble();
        c.ownTrack = q.value (QStringLiteral ("ownTrack"), true).toBool();
        s.inputs.push_back (c);
    }
    q.endArray();

    std::vector<std::string> buses;
    const auto cameras = q.beginReadArray (key ("cameras"));
    for (int i = 0; i < cameras; ++i)
    {
        q.setArrayIndex (i);
        buses.push_back (q.value (QStringLiteral ("bus")).toString().toStdString());
        perf::CameraChoice c;
        c.path = q.value (QStringLiteral ("path")).toString().toStdString();
        c.name = q.value (QStringLiteral ("name")).toString().toStdString();
        c.width = q.value (QStringLiteral ("width"), 1280).toInt();
        c.height = q.value (QStringLiteral ("height"), 720).toInt();
        c.corner = q.value (QStringLiteral ("corner"), (int) perf::CameraChoice::bottomRight).toInt();
        c.sizePercent = q.value (QStringLiteral ("size"), 25).toInt();
        c.shown = q.value (QStringLiteral ("shown"), true).toBool();
        c.ownTrack = q.value (QStringLiteral ("ownTrack"), false).toBool();
        s.cameras.push_back (c);
    }
    q.endArray();

    // A camera is remembered by its USB port, so it is found again when its /dev number changes.
    const auto plugged = perf::listCameras();
    for (size_t i = 0; i < s.cameras.size(); ++i)
        for (const auto& d : plugged)
            if (! buses[i].empty() && d.bus == buses[i])
                s.cameras[i].path = d.path;
    s.cameras.erase (std::remove_if (s.cameras.begin(), s.cameras.end(), [&] (const perf::CameraChoice& c)
    {
        return std::none_of (plugged.begin(), plugged.end(), [&] (const perf::CameraDevice& d) { return d.path == c.path; });
    }), s.cameras.end());

    s.streamServer = q.value (key ("streamServer"), services().front().server).toString().toStdString();
    s.streamKey = q.value (key ("streamKey")).toString().toStdString();
    s.streamHeight = q.value (key ("streamHeight"), 1080).toInt();
    s.streamKbps = q.value (key ("streamKbps"), 6000).toInt();
    s.streamCodec = q.value (key ("streamCodec")).toString() == QLatin1String ("hevc") ? perf::Codec::hevc : perf::Codec::h264;
    s.streamAudioKbps = q.value (key ("streamAudioKbps"), 160).toInt();
    return s;
}

void saveSettings (const perf::Settings& s)
{
    auto q = appSettings();
    q.setValue (key ("height"), s.height);
    q.setValue (key ("fps"), s.fps);
    q.setValue (key ("encoder"), QString::fromStdString (s.encoder));
    q.setValue (key ("quality"), s.quality);
    q.setValue (key ("audioKbps"), s.audioKbps);
    q.setValue (key ("programTrack"), s.programTrack);
    q.setValue (key ("fragmented"), s.fragmented);
    q.setValue (key ("folder"), QString::fromStdString (s.folder));
    q.setValue (key ("followWindow"), s.followWindow);

    q.beginWriteArray (key ("inputs"), (int) s.inputs.size());
    for (int i = 0; i < (int) s.inputs.size(); ++i)
    {
        const auto& c = s.inputs[(size_t) i];
        q.setArrayIndex (i);
        q.setValue (QStringLiteral ("target"), QString::fromStdString (c.source.target));
        q.setValue (QStringLiteral ("application"), c.source.kind == CaptureSource::Kind::application);
        q.setValue (QStringLiteral ("name"), QString::fromStdString (c.source.name));
        q.setValue (QStringLiteral ("channels"), c.source.channels);
        q.setValue (QStringLiteral ("gainDb"), c.gainDb);
        q.setValue (QStringLiteral ("ownTrack"), c.ownTrack);
    }
    q.endArray();

    const auto plugged = perf::listCameras();
    q.beginWriteArray (key ("cameras"), (int) s.cameras.size());
    for (int i = 0; i < (int) s.cameras.size(); ++i)
    {
        const auto& c = s.cameras[(size_t) i];
        q.setArrayIndex (i);
        q.setValue (QStringLiteral ("path"), QString::fromStdString (c.path));
        q.setValue (QStringLiteral ("name"), QString::fromStdString (c.name));
        for (const auto& d : plugged)
            if (d.path == c.path)
                q.setValue (QStringLiteral ("bus"), QString::fromStdString (d.bus));
        q.setValue (QStringLiteral ("width"), c.width);
        q.setValue (QStringLiteral ("height"), c.height);
        q.setValue (QStringLiteral ("corner"), c.corner);
        q.setValue (QStringLiteral ("size"), c.sizePercent);
        q.setValue (QStringLiteral ("shown"), c.shown);
        q.setValue (QStringLiteral ("ownTrack"), c.ownTrack);
    }
    q.endArray();

    q.setValue (key ("streamServer"), QString::fromStdString (s.streamServer));
    q.setValue (key ("streamKey"), QString::fromStdString (s.streamKey));
    q.setValue (key ("streamHeight"), s.streamHeight);
    q.setValue (key ("streamKbps"), s.streamKbps);
    q.setValue (key ("streamCodec"), s.streamCodec == perf::Codec::hevc ? QStringLiteral ("hevc") : QStringLiteral ("h264"));
    q.setValue (key ("streamAudioKbps"), s.streamAudioKbps);
}
}

//==============================================================================
PerformanceDialog::PerformanceDialog (const perf::Settings& s, bool isLocked, QWidget* parent)
    : QDialog (parent), initial (s), locked (isLocked)
{
    setWindowTitle (tr ("Video and Stream Settings"));

    // Trying the encoders opens each one once, which can take a moment the first time.
    QApplication::setOverrideCursor (Qt::WaitCursor);
    perf::workingEncoders (perf::Codec::hevc);
    perf::workingEncoders (perf::Codec::h264);
    QApplication::restoreOverrideCursor();

    auto* tabs = new QTabWidget (this);
    tabs->addTab (buildRecording(), tr ("&Recording"));
    tabs->addTab (buildSound(), tr ("&Sound"));
    tabs->addTab (buildCameras(), tr ("&Cameras"));
    tabs->addTab (buildStreaming(), tr ("S&treaming"));

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout (this);
    if (locked)
    {
        auto* note = new QLabel (tr ("Recording or streaming now: the picture, sound and cameras can change once both have stopped."), this);
        note->setWordWrap (true);
        layout->addWidget (note);
    }
    layout->addWidget (tabs);
    layout->addWidget (buttons);
    resize (720, 520);
}

QWidget* PerformanceDialog::buildRecording()
{
    auto* page = new QWidget;
    auto* form = new QFormLayout (page);

    const auto recommended = perf::recommendedHeight();
    resolution = new QComboBox (page);
    resolution->addItem (tr ("Automatic: %1 on this computer").arg (heightLabel (recommended)), 0);
    for (int h : { 1080, 1440, 2160 })
        resolution->addItem (heightLabel (h), h);
    resolution->setCurrentIndex (std::max (0, resolution->findData (initial.height)));
    resolution->setToolTip (tr ("4K needs a graphics card that encodes H.265 (VAAPI or NVENC); on the processor alone, 1080p or 1440p keeps up"));
    form->addRow (tr ("&Resolution:"), resolution);

    frameRate = new QComboBox (page);
    for (int f : { 24, 25, 30, 50, 60 })
        frameRate->addItem (tr ("%1 frames a second").arg (f), f);
    frameRate->setCurrentIndex (std::max (0, frameRate->findData (initial.fps)));
    form->addRow (tr ("&Frame rate:"), frameRate);

    encoder = new QComboBox (page);
    const auto& hevc = perf::workingEncoders (perf::Codec::hevc);
    encoder->addItem (hevc.empty() ? tr ("None found: install FFmpeg with x265, or a VAAPI or NVENC driver")
                                   : tr ("Best: %1").arg (QString::fromStdString (hevc.front().label)), QString());
    for (const auto& e : hevc)
        encoder->addItem (QString::fromStdString (e.label), QString::fromStdString (e.name));
    encoder->setCurrentIndex (std::max (0, encoder->findData (QString::fromStdString (initial.encoder))));
    form->addRow (tr ("H.265 &encoder:"), encoder);

    quality = new QComboBox (page);
    quality->addItem (tr ("Standard (smaller files)"), 0.7);
    quality->addItem (tr ("High"), 1.0);
    quality->addItem (tr ("Very high"), 1.6);
    quality->addItem (tr ("Near lossless (large files)"), 3.0);
    int closest = 1;
    for (int i = 0; i < quality->count(); ++i)
        if (std::abs (quality->itemData (i).toDouble() - initial.quality) < std::abs (quality->itemData (closest).toDouble() - initial.quality))
            closest = i;
    quality->setCurrentIndex (closest);
    bitrateNote = new QLabel (page);
    auto* qualityRow = new QHBoxLayout;
    qualityRow->addWidget (quality);
    qualityRow->addWidget (bitrateNote, 1);
    auto* qualityLabel = new QLabel (tr ("&Quality:"), page);
    qualityLabel->setBuddy (quality);
    form->addRow (qualityLabel, qualityRow);

    folder = new QLineEdit (QString::fromStdString (initial.folder), page);
    auto* browse = new QPushButton (tr ("&Browse..."), page);
    connect (browse, &QPushButton::clicked, this, [this]
    {
        const auto chosen = QFileDialog::getExistingDirectory (this, tr ("Save Recordings In"), folder->text());
        if (! chosen.isEmpty())
            folder->setText (chosen);
    });
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget (folder, 1);
    folderRow->addWidget (browse);
    auto* folderLabel = new QLabel (tr ("Save &in:"), page);
    folderLabel->setBuddy (folder);
    form->addRow (folderLabel, folderRow);

    followWindow = new QCheckBox (tr ("Follow the PHINYX &window in front (Mixer, Tracker, Analysis...)"), page);
    followWindow->setChecked (initial.followWindow);
    form->addRow (QString(), followWindow);
    crashSafe = new QCheckBox (tr ("Crash-safe MP4 (fragmented: stays playable if PHINYX stops suddenly; a few older editors cannot open it)"), page);
    crashSafe->setChecked (initial.fragmented);
    form->addRow (QString(), crashSafe);

    auto* about = new QLabel (tr ("<p>Records only PHINYX: its main window, or the PHINYX window in front when that is "
                                  "ticked above. A full-screen Visualizer (F11) is always followed. No other window or "
                                  "screen is ever recorded, and Wayland asks nothing, since PHINYX draws the picture itself.</p>"
                                  "<p>Video is H.265 (HEVC) in an MP4, with AAC sound. FFmpeg %1.</p>")
                              .arg (QString::fromStdString (perf::ffmpegVersion())), page);
    about->setWordWrap (true);
    form->addRow (about);

    for (auto* box : { resolution, frameRate, quality })
        connect (box, &QComboBox::currentIndexChanged, this, &PerformanceDialog::updateBitrateNote);
    updateBitrateNote();

    for (auto* w : std::initializer_list<QWidget*> { resolution, frameRate, encoder, quality, folder, browse, crashSafe })
        w->setEnabled (! locked);
    return page;
}

void PerformanceDialog::updateBitrateNote()
{
    auto height = resolution->currentData().toInt();
    if (height == 0)
        height = perf::recommendedHeight();
    const auto size = perf::frameSize (height);
    const auto bits = perf::defaultBitrate (size.width(), size.height(), frameRate->currentData().toInt(), perf::Codec::hevc)
                      * quality->currentData().toDouble();
    bitrateNote->setText (tr ("about %1 Mbit/s, %2 GB an hour").arg (bits / 1.0e6, 0, 'f', 1).arg (bits * 3600.0 / 8.0e9, 0, 'f', 1));
}

QWidget* PerformanceDialog::buildSound()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout (page);
    auto* form = new QFormLayout;
    layout->addLayout (form);

    audioRate = new QComboBox (page);
    for (int k : { 128, 192, 256, 320 })
        audioRate->addItem (tr ("%1 kbit/s per stereo pair").arg (k), k);
    audioRate->setCurrentIndex (std::max (0, audioRate->findData (initial.audioKbps)));
    form->addRow (tr ("&AAC quality:"), audioRate);

    programTrack = new QCheckBox (tr ("Also put PHINYX's sound on a &track of its own, in all its channels (5.1, 7.1, Ambisonics)"), page);
    programTrack->setChecked (initial.programTrack);
    form->addRow (QString(), programTrack);

    auto* tracksNote = new QLabel (tr ("The first track is the mix (PHINYX folded to stereo, plus the inputs below): players and "
                                       "streams use it. The other tracks are for editing afterwards, in Resolve, Premiere or PHINYX."), page);
    tracksNote->setWordWrap (true);
    layout->addWidget (tracksNote);

    auto* inputsLabel = new QLabel (tr ("&Inputs in the mix: microphones, and applications such as a web browser (heard whenever they play):"), page);
    layout->addWidget (inputsLabel);
    inputTable = new QTableWidget (0, 4, page);
    inputsLabel->setBuddy (inputTable);
    inputTable->setHorizontalHeaderLabels ({ tr ("Use"), tr ("Input"), tr ("Gain (dB)"), tr ("Own track") });
    inputTable->horizontalHeader()->setSectionResizeMode (1, QHeaderView::Stretch);
    inputTable->verticalHeader()->hide();
    inputTable->setSelectionMode (QAbstractItemView::NoSelection);
    layout->addWidget (inputTable, 1);
    inputNote = new QLabel (page);
    inputNote->setWordWrap (true);
    layout->addWidget (inputNote);

    if (auto e = capture.connect(); ! e.empty())
        inputNote->setText (tr ("Inputs need PipeWire: %1").arg (QString::fromStdString (e)));
    connect (&inputPoll, &QTimer::timeout, this, &PerformanceDialog::refreshInputs);
    inputPoll.start (300);
    refreshInputs();

    for (auto* w : std::initializer_list<QWidget*> { audioRate, programTrack, inputTable })
        w->setEnabled (! locked);
    return page;
}

void PerformanceDialog::refreshInputs()
{
    if (capture.sourcesVersion() == inputsVersion)
        return;
    inputsVersion = capture.sourcesVersion();

    // Keep what was ticked while the list changes under it.
    const auto chosen = inputTable->rowCount() == 0 ? initial.inputs : chosenInputs();

    // Inputs, then applications (each once, by name: a browser can play several streams).
    inputSources.clear();
    for (const auto& s : capture.sources())
        if (s.kind == CaptureSource::Kind::input
            || (s.kind == CaptureSource::Kind::application && ! isPhinyx (s)
                && std::none_of (inputSources.begin(), inputSources.end(), [&] (const CaptureSource& o) { return sameSource (o, s); })))
            inputSources.push_back (s);
    // A remembered input that is not plugged in, or an application not playing now, stays listed so it is not forgotten.
    for (const auto& c : chosen)
        if (std::none_of (inputSources.begin(), inputSources.end(), [&] (const CaptureSource& s) { return sameSource (s, c.source); }))
            inputSources.push_back (c.source);
    std::stable_partition (inputSources.begin(), inputSources.end(), [] (const CaptureSource& s) { return s.kind == CaptureSource::Kind::input; });

    inputTable->setRowCount ((int) inputSources.size());
    for (int row = 0; row < (int) inputSources.size(); ++row)
    {
        const auto& s = inputSources[(size_t) row];
        const auto it = std::find_if (chosen.begin(), chosen.end(), [&] (const perf::InputChoice& c) { return sameSource (c.source, s); });
        QCheckBox* box = nullptr;
        inputTable->setCellWidget (row, 0, cellCheck (it != chosen.end(), tr ("Use %1").arg (QString::fromStdString (s.name)), &box));
        auto* name = new QTableWidgetItem (s.kind == CaptureSource::Kind::application ? tr ("%1 (application)").arg (QString::fromStdString (s.name))
                                                                                     : QString::fromStdString (s.name));
        name->setFlags (Qt::ItemIsEnabled);
        inputTable->setItem (row, 1, name);
        auto* gain = new QDoubleSpinBox;
        gain->setRange (-60.0, 24.0);
        gain->setDecimals (1);
        gain->setSuffix (tr (" dB"));
        gain->setValue (it != chosen.end() ? it->gainDb : 0.0);
        gain->setAccessibleName (tr ("Gain of %1").arg (QString::fromStdString (s.name)));
        inputTable->setCellWidget (row, 2, gain);
        inputTable->setCellWidget (row, 3, cellCheck (it != chosen.end() ? it->ownTrack : true, tr ("Own track for %1").arg (QString::fromStdString (s.name)), &box));
    }
    inputTable->resizeColumnsToContents();
    inputTable->horizontalHeader()->setSectionResizeMode (1, QHeaderView::Stretch);
    if (inputSources.empty() && inputNote->text().isEmpty())
        inputNote->setText (tr ("No inputs found."));
}

QWidget* PerformanceDialog::buildCameras()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout (page);
    auto* note = new QLabel (tr ("Cameras go on top of PHINYX in the corner you choose. Tick Own track to also keep each one "
                                 "whole on a video track of its own in the MP4, for multicam editing (the stream has only the picture)."), page);
    note->setWordWrap (true);
    layout->addWidget (note);

    cameraTable = new QTableWidget (0, 7, page);
    cameraTable->setHorizontalHeaderLabels ({ tr ("Use"), tr ("Camera"), tr ("Size"), tr ("Corner"), tr ("Width %"), tr ("On picture"), tr ("Own track") });
    cameraTable->horizontalHeader()->setSectionResizeMode (1, QHeaderView::Stretch);
    cameraTable->verticalHeader()->hide();
    cameraTable->setSelectionMode (QAbstractItemView::NoSelection);
    layout->addWidget (cameraTable, 1);
    cameraNote = new QLabel (page);
    layout->addWidget (cameraNote);

    auto* refresh = new QPushButton (tr ("&Look for Cameras Again"), page);
    connect (refresh, &QPushButton::clicked, this, &PerformanceDialog::refreshCameras);
    auto* row = new QHBoxLayout;
    row->addWidget (refresh);
    row->addStretch (1);
    layout->addLayout (row);
    refreshCameras();

    cameraTable->setEnabled (! locked);
    refresh->setEnabled (! locked);
    return page;
}

void PerformanceDialog::refreshCameras()
{
    const auto chosen = cameraTable->rowCount() == 0 ? initial.cameras : chosenCameras();
    cameraDevices = perf::listCameras();
    cameraTable->setRowCount ((int) cameraDevices.size());
    for (int row = 0; row < (int) cameraDevices.size(); ++row)
    {
        const auto& d = cameraDevices[(size_t) row];
        const auto it = std::find_if (chosen.begin(), chosen.end(), [&] (const perf::CameraChoice& c) { return c.path == d.path; });
        const auto c = it != chosen.end() ? *it : perf::CameraChoice {};
        const auto name = QString::fromStdString (d.name);
        QCheckBox* box = nullptr;
        cameraTable->setCellWidget (row, 0, cellCheck (it != chosen.end(), tr ("Use %1").arg (name), &box));
        auto* label = new QTableWidgetItem (QStringLiteral ("%1 (%2)").arg (name, QString::fromStdString (d.path)));
        label->setFlags (Qt::ItemIsEnabled);
        cameraTable->setItem (row, 1, label);

        auto* size = new QComboBox;
        for (const auto& [w, h] : { std::pair { 640, 480 }, { 1280, 720 }, { 1920, 1080 }, { 3840, 2160 } })
            size->addItem (QStringLiteral ("%1 × %2").arg (w).arg (h), QSize (w, h));
        size->setCurrentIndex (std::max (0, size->findData (QSize (c.width, c.height))));
        size->setAccessibleName (tr ("Size of %1").arg (name));
        cameraTable->setCellWidget (row, 2, size);

        auto* corner = new QComboBox;
        corner->addItem (tr ("Top left"), (int) perf::CameraChoice::topLeft);
        corner->addItem (tr ("Top right"), (int) perf::CameraChoice::topRight);
        corner->addItem (tr ("Bottom left"), (int) perf::CameraChoice::bottomLeft);
        corner->addItem (tr ("Bottom right"), (int) perf::CameraChoice::bottomRight);
        corner->setCurrentIndex (std::max (0, corner->findData (c.corner)));
        corner->setAccessibleName (tr ("Corner of %1").arg (name));
        cameraTable->setCellWidget (row, 3, corner);

        auto* width = new QSpinBox;
        width->setRange (5, 100);
        width->setSuffix (QStringLiteral (" %"));
        width->setValue (c.sizePercent);
        width->setAccessibleName (tr ("Width of %1, in percent of the picture").arg (name));
        cameraTable->setCellWidget (row, 4, width);

        cameraTable->setCellWidget (row, 5, cellCheck (c.shown, tr ("Show %1 on the picture").arg (name), &box));
        cameraTable->setCellWidget (row, 6, cellCheck (c.ownTrack, tr ("Own track for %1").arg (name), &box));
    }
    cameraTable->resizeColumnsToContents();
    cameraTable->horizontalHeader()->setSectionResizeMode (1, QHeaderView::Stretch);
    cameraNote->setText (cameraDevices.empty() ? tr ("No cameras found. Plug one in (USB webcams and capture cards work) and look again.") : QString());
}

QWidget* PerformanceDialog::buildStreaming()
{
    auto* page = new QWidget;
    auto* form = new QFormLayout (page);

    service = new QComboBox (page);
    for (const auto& s : perfui::services())
        service->addItem (s.name, s.server);
    form->addRow (tr ("Ser&vice:"), service);

    server = new QLineEdit (QString::fromStdString (initial.streamServer), page);
    server->setPlaceholderText (QStringLiteral ("rtmp://server/app  or  srt://server:port?streamid=..."));
    form->addRow (tr ("Ser&ver:"), server);
    int at = service->count() - 1;
    for (int i = 0; i < service->count() - 1; ++i)
        if (service->itemData (i).toString() == server->text())
            at = i;
    service->setCurrentIndex (at);
    connect (service, &QComboBox::currentIndexChanged, this, &PerformanceDialog::serviceChosen);

    key = new QLineEdit (QString::fromStdString (initial.streamKey), page);
    key->setEchoMode (QLineEdit::Password);
    auto* show = new QCheckBox (tr ("S&how"), page);
    connect (show, &QCheckBox::toggled, this, [this] (bool on) { key->setEchoMode (on ? QLineEdit::Normal : QLineEdit::Password); });
    auto* keyRow = new QHBoxLayout;
    keyRow->addWidget (key, 1);
    keyRow->addWidget (show);
    form->addRow (tr ("Stream &key:"), keyRow);

    streamResolution = new QComboBox (page);
    for (int h : { 720, 1080, 1440, 2160 })
        streamResolution->addItem (heightLabel (h), h);
    streamResolution->setCurrentIndex (std::max (0, streamResolution->findData (initial.streamHeight)));
    form->addRow (tr ("&Resolution:"), streamResolution);

    streamBitrate = new QSpinBox (page);
    streamBitrate->setRange (1000, 60000);
    streamBitrate->setSingleStep (500);
    streamBitrate->setSuffix (tr (" kbit/s"));
    streamBitrate->setValue (initial.streamKbps);
    streamBitrate->setToolTip (tr ("Twitch takes up to 6000; YouTube suggests 4500 to 9000 for 1080p and 20000 to 51000 for 4K"));
    form->addRow (tr ("Video &bit rate:"), streamBitrate);

    streamCodec = new QComboBox (page);
    streamCodec->addItem (tr ("H.264 (every service)"), (int) perf::Codec::h264);
    streamCodec->addItem (perf::canStream (perf::Codec::hevc) ? tr ("H.265 (YouTube; half the bit rate for the same picture)")
                                                              : tr ("H.265 (needs FFmpeg 6.1 or later)"),
                          (int) perf::Codec::hevc);
    streamCodec->setCurrentIndex (initial.streamCodec == perf::Codec::hevc ? 1 : 0);
    form->addRow (tr ("&Codec:"), streamCodec);

    streamAudio = new QComboBox (page);
    for (int k : { 128, 160, 192, 320 })
        streamAudio->addItem (tr ("AAC %1 kbit/s").arg (k), k);
    streamAudio->setCurrentIndex (std::max (0, streamAudio->findData (initial.streamAudioKbps)));
    form->addRow (tr ("&Sound:"), streamAudio);

    auto* note = new QLabel (tr ("<p>The stream shows the same picture as the recording, with the mix track as its sound. "
                                 "Get the stream key from the service's dashboard (YouTube Studio, Twitch Creator Dashboard). "
                                 "It is kept in PHINYX's settings file, %1.</p>")
                             .arg (appSettings().fileName().toHtmlEscaped()), page);
    note->setWordWrap (true);
    form->addRow (note);
    return page;
}

void PerformanceDialog::serviceChosen()
{
    const auto address = service->currentData().toString();
    if (! address.isEmpty())
        server->setText (address);
    else
    {
        server->clear();
        server->setFocus();
    }
}

perf::Settings PerformanceDialog::settings() const
{
    auto s = initial;
    s.height = resolution->currentData().toInt();
    s.fps = frameRate->currentData().toInt();
    s.encoder = encoder->currentData().toString().toStdString();
    s.quality = quality->currentData().toDouble();
    s.folder = QDir::cleanPath (folder->text().trimmed()).toStdString();
    if (s.folder.empty() || s.folder == ".")
        s.folder = perfui::defaultFolder().toStdString();
    s.followWindow = followWindow->isChecked();
    s.fragmented = crashSafe->isChecked();

    s.audioKbps = audioRate->currentData().toInt();
    s.programTrack = programTrack->isChecked();
    s.inputs = chosenInputs();
    s.cameras = chosenCameras();

    s.streamServer = server->text().trimmed().toStdString();
    s.streamKey = key->text().trimmed().toStdString();
    s.streamHeight = streamResolution->currentData().toInt();
    s.streamKbps = streamBitrate->value();
    s.streamCodec = (perf::Codec) streamCodec->currentData().toInt();
    s.streamAudioKbps = streamAudio->currentData().toInt();
    return s;
}

std::vector<perf::InputChoice> PerformanceDialog::chosenInputs() const
{
    std::vector<perf::InputChoice> inputs;
    for (int row = 0; row < inputTable->rowCount() && row < (int) inputSources.size(); ++row)
    {
        auto* use = checkIn (inputTable, row, 0);
        if (use == nullptr || ! use->isChecked())
            continue;
        perf::InputChoice c;
        c.source = inputSources[(size_t) row];
        if (auto* gain = qobject_cast<QDoubleSpinBox*> (inputTable->cellWidget (row, 2)))
            c.gainDb = gain->value();
        if (auto* own = checkIn (inputTable, row, 3))
            c.ownTrack = own->isChecked();
        inputs.push_back (c);
    }
    return inputs;
}

std::vector<perf::CameraChoice> PerformanceDialog::chosenCameras() const
{
    std::vector<perf::CameraChoice> cameras;
    for (int row = 0; row < cameraTable->rowCount() && row < (int) cameraDevices.size(); ++row)
    {
        auto* use = checkIn (cameraTable, row, 0);
        if (use == nullptr || ! use->isChecked())
            continue;
        perf::CameraChoice c;
        c.path = cameraDevices[(size_t) row].path;
        c.name = cameraDevices[(size_t) row].name;
        if (auto* size = qobject_cast<QComboBox*> (cameraTable->cellWidget (row, 2)))
        {
            const auto wh = size->currentData().toSize();
            c.width = wh.width();
            c.height = wh.height();
        }
        if (auto* corner = qobject_cast<QComboBox*> (cameraTable->cellWidget (row, 3)))
            c.corner = corner->currentData().toInt();
        if (auto* width = qobject_cast<QSpinBox*> (cameraTable->cellWidget (row, 4)))
            c.sizePercent = width->value();
        if (auto* shown = checkIn (cameraTable, row, 5))
            c.shown = shown->isChecked();
        if (auto* own = checkIn (cameraTable, row, 6))
            c.ownTrack = own->isChecked();
        cameras.push_back (c);
    }
    return cameras;
}
