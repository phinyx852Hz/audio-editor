// SPDX-License-Identifier: AGPL-3.0-only
#include "RecordDialog.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLockFile>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    constexpr float floorDb = -60.0f;
    constexpr int blockFrames = 4096;
    constexpr qint64 headerBytes = 44;
    constexpr qint64 maxWavBytes = 0xFFFFFFF0LL;   // a plain WAV cannot hold more

    float toDb (float linear) { return linear > 0.0f ? 20.0f * std::log10 (linear) : -100.0f; }

    QString formatTime (double seconds)
    {
        const auto tenths = (qint64) std::floor (seconds * 10.0);
        return QStringLiteral ("%1:%2:%3.%4")
            .arg (tenths / 36000, 2, 10, QLatin1Char ('0'))
            .arg ((tenths / 600) % 60, 2, 10, QLatin1Char ('0'))
            .arg ((tenths / 10) % 60, 2, 10, QLatin1Char ('0'))
            .arg (tenths % 10);
    }

    QString formatDuration (qint64 seconds)
    {
        if (seconds >= 3600)
            return QObject::tr ("%1 h %2 min").arg (seconds / 3600).arg ((seconds / 60) % 60);
        return QObject::tr ("%1 min").arg (std::max<qint64> (1, seconds / 60));
    }

    // Writes the 44-byte header of a 32-bit float WAV holding `dataBytes` of samples.
    QByteArray wavHeader (int sampleRate, int numChannels, qint64 dataBytes)
    {
        QByteArray h (headerBytes, '\0');
        auto* p = reinterpret_cast<uchar*> (h.data());
        const auto put32 = [p] (int at, quint32 v) { qToLittleEndian (v, p + at); };
        const auto put16 = [p] (int at, quint16 v) { qToLittleEndian (v, p + at); };

        std::memcpy (p, "RIFF", 4);
        put32 (4, (quint32) (36 + dataBytes));
        std::memcpy (p + 8, "WAVEfmt ", 8);
        put32 (16, 16);
        put16 (20, 3);                                     // IEEE float
        put16 (22, (quint16) numChannels);
        put32 (24, (quint32) sampleRate);
        put32 (28, (quint32) (sampleRate * numChannels * 4));
        put16 (32, (quint16) (numChannels * 4));
        put16 (34, 32);
        std::memcpy (p + 36, "data", 4);
        put32 (40, (quint32) dataBytes);
        return h;
    }
}

// ---------------------------------------------------------------------------

RecordMeter::RecordMeter (QWidget* parent) : QWidget (parent)
{
    setMinimumHeight (36);
    setToolTip (tr ("Input level. The red light on the right means a sample clipped; click to clear it."));
}

void RecordMeter::setFormat (int numChannels, int sampleRate)
{
    channels.assign ((size_t) std::max (1, numChannels), Channel());
    rate = std::max (1, sampleRate);
    update();
}

void RecordMeter::push (const float* interleaved, int frames)
{
    const auto n = (int) channels.size();
    if (frames <= 0 || n == 0)
        return;

    const auto fall = 24.0f * (float) frames / (float) rate;   // dB per second the bar falls back
    for (int ch = 0; ch < n; ++ch)
    {
        float peak = 0.0f;
        for (int i = 0; i < frames; ++i)
            peak = std::max (peak, std::abs (interleaved[i * n + ch]));

        auto& c = channels[(size_t) ch];
        const auto db = toDb (peak);
        c.levelDb = std::max (db, c.levelDb - fall);
        c.clip = c.clip || peak >= 0.9999f;

        c.holdFrames -= frames;
        if (db >= c.holdDb || c.holdFrames <= 0)
        {
            c.holdDb = std::max (db, c.holdFrames <= 0 ? c.levelDb : c.holdDb);
            if (db >= c.holdDb)
                c.holdFrames = rate * 3 / 2;
        }
    }
    update();
}

void RecordMeter::fall (int frames)
{
    const auto drop = 24.0f * (float) frames / (float) rate;
    for (auto& c : channels)
    {
        c.levelDb = std::max (-100.0f, c.levelDb - drop);
        c.holdFrames -= frames;
        if (c.holdFrames <= 0)
            c.holdDb = c.levelDb;
    }
    update();
}

void RecordMeter::resetClip()
{
    for (auto& c : channels)
        c.clip = false;
    update();
}

bool RecordMeter::clipped() const
{
    return std::any_of (channels.begin(), channels.end(), [] (const Channel& c) { return c.clip; });
}

void RecordMeter::mousePressEvent (QMouseEvent*)
{
    resetClip();
}

void RecordMeter::paintEvent (QPaintEvent*)
{
    QPainter g (this);
    const auto n = (int) channels.size();
    if (n == 0)
        return;

    const int lightW = 14, gap = 2;
    const QRectF area (0, 0, width() - lightW - 4, height());
    const auto rowH = (area.height() - gap * (n - 1)) / n;
    const auto xFor = [&] (float db) { return area.width() * (std::clamp (db, floorDb, 0.0f) - floorDb) / -floorDb; };

    for (int ch = 0; ch < n; ++ch)
    {
        const auto& c = channels[(size_t) ch];
        const QRectF row (0, ch * (rowH + gap), area.width(), rowH);
        g.fillRect (row, palette().color (QPalette::Base).darker (115));

        // Green up to -12 dB, yellow to -3 dB, red above.
        const auto level = xFor (c.levelDb);
        const auto x12 = xFor (-12.0f), x3 = xFor (-3.0f);
        g.fillRect (QRectF (0, row.top(), std::min (level, x12), rowH), QColor (0x3c, 0xc8, 0x5a));
        if (level > x12)
            g.fillRect (QRectF (x12, row.top(), std::min (level, x3) - x12, rowH), QColor (0xe6, 0xc8, 0x32));
        if (level > x3)
            g.fillRect (QRectF (x3, row.top(), level - x3, rowH), QColor (0xe6, 0x3c, 0x32));

        if (c.holdDb > floorDb)
            g.fillRect (QRectF (xFor (c.holdDb) - 1, row.top(), 2, rowH), palette().color (QPalette::Text));

        const QRectF light (width() - lightW, row.top(), lightW, rowH);
        g.fillRect (light, c.clip ? QColor (0xff, 0x30, 0x30) : palette().color (QPalette::Base).darker (130));
    }

    // dB scale marks
    g.setPen (QColor (128, 128, 128, 160));
    for (float db : { -48.0f, -24.0f, -12.0f, -6.0f, -3.0f })
        g.drawLine (QPointF (xFor (db), 0), QPointF (xFor (db), height()));
}

// ---------------------------------------------------------------------------

QString RecordingFile::folder()
{
    return QStandardPaths::writableLocation (QStandardPaths::GenericCacheLocation) + QStringLiteral ("/" APP_ID "/recordings");
}

bool RecordingFile::open (int sampleRate, int numChannels)
{
    close (true);
    QDir().mkpath (folder());

    const auto name = folder() + QStringLiteral ("/recording-%1.wav").arg (QDateTime::currentDateTime().toString (QStringLiteral ("yyyyMMdd-HHmmss")));
    lock = std::make_shared<QLockFile> (name + QStringLiteral (".lock"));
    lock->setStaleLockTime (0);
    if (! lock->tryLock (0))
    {
        lock.reset();
        return false;
    }

    channels = numChannels;
    bytesPerSecond = (qint64) sampleRate * numChannels * (qint64) sizeof (float);
    dataBytes = lastHeaderBytes = 0;
    full = false;
    file.setFileName (name);
    if (! file.open (QIODevice::ReadWrite | QIODevice::Truncate) || file.write (wavHeader (sampleRate, numChannels, 0)) != headerBytes)
    {
        file.close();
        lock.reset();
        return false;
    }
    return true;
}

void RecordingFile::write (const float* interleaved, int frames)
{
    if (! file.isOpen() || full || frames <= 0)
        return;

    const auto bytes = (qint64) frames * channels * (qint64) sizeof (float);
    if (headerBytes + dataBytes + bytes > maxWavBytes)
    {
        full = true;   // the recording continues in memory
        return;
    }

    if (file.write (reinterpret_cast<const char*> (interleaved), bytes) == bytes)
        dataBytes += bytes;

    // About once a second, make the file a complete WAV as it stands.
    if (dataBytes - lastHeaderBytes >= bytesPerSecond)
        updateHeader();
}

void RecordingFile::updateHeader()
{
    const auto end = file.pos();
    file.seek (4);
    QByteArray riff (4, '\0'), data (4, '\0');
    qToLittleEndian ((quint32) (36 + dataBytes), reinterpret_cast<uchar*> (riff.data()));
    qToLittleEndian ((quint32) dataBytes, reinterpret_cast<uchar*> (data.data()));
    file.write (riff);
    file.seek (40);
    file.write (data);
    file.seek (end);
    file.flush();
    lastHeaderBytes = dataBytes;
}

void RecordingFile::close (bool keep)
{
    if (file.isOpen())
    {
        updateHeader();
        file.close();
        if (! keep)
            file.remove();
    }
    lock.reset();
}

int RecordingFile::countLeftBehind()
{
    int count = 0;
    for (const auto& info : QDir (folder()).entryInfoList ({ QStringLiteral ("*.wav") }, QDir::Files))
    {
        QLockFile lk (info.filePath() + QStringLiteral (".lock"));
        lk.setStaleLockTime (0);
        if (lk.tryLock (0))
            ++count;
    }
    return count;
}

QStringList RecordingFile::recoverAll()
{
    auto music = QStandardPaths::writableLocation (QStandardPaths::MusicLocation);
    if (music.isEmpty())
        music = QDir::homePath();
    const auto destination = music + QStringLiteral ("/" APP_DISPLAY_NAME " Recovered");
    QDir().mkpath (destination);

    QStringList recovered;
    for (const auto& info : QDir (folder()).entryInfoList ({ QStringLiteral ("*.wav") }, QDir::Files, QDir::Name))
    {
        QLockFile lk (info.filePath() + QStringLiteral (".lock"));
        lk.setStaleLockTime (0);
        if (! lk.tryLock (0))
            continue;   // another PHINYX window is still recording into it

        QFile f (info.filePath());
        if (! f.open (QIODevice::ReadWrite) || f.size() < headerBytes)
        {
            f.close();
            f.remove();
            continue;
        }

        // Fix the sizes in the header to match what reached the disk.
        f.seek (22);
        const auto ch = std::max<quint16> (1, qFromLittleEndian<quint16> (reinterpret_cast<const uchar*> (f.read (2).constData())));
        const auto bytes = std::min (f.size() - headerBytes, maxWavBytes - headerBytes) / (ch * 4) * (ch * 4);
        QByteArray riff (4, '\0'), data (4, '\0');
        qToLittleEndian ((quint32) (36 + bytes), reinterpret_cast<uchar*> (riff.data()));
        qToLittleEndian ((quint32) bytes, reinterpret_cast<uchar*> (data.data()));
        f.seek (4);
        f.write (riff);
        f.seek (40);
        f.write (data);
        f.resize (headerBytes + bytes);
        f.close();

        const auto target = destination + QStringLiteral ("/recovered-") + info.fileName().section (QLatin1Char ('-'), 1);
        if (QFile::rename (info.filePath(), target) || (QFile::copy (info.filePath(), target) && QFile::remove (info.filePath())))
            recovered << target;
    }
    return recovered;
}

// ---------------------------------------------------------------------------

namespace
{
    enum Roles { kindRole = Qt::UserRole, targetRole, nameRole };
}

RecordDialog::RecordDialog (AudioCapture& c, AudioDataPtr into, const QString& targetName, QWidget* parent)
    : QDialog (parent), capture (c), target (std::move (into))
{
    setWindowTitle (tr ("Record"));
    resize (620, 560);

    sourceList = new QTreeWidget (this);
    sourceList->setHeaderLabels ({ tr ("Record from"), tr ("Details") });
    sourceList->setRootIsDecorated (false);
    sourceList->setAllColumnsShowFocus (true);
    sourceList->header()->setSectionResizeMode (0, QHeaderView::Stretch);

    rateBox = new QComboBox (this);
    for (int r : { 44100, 48000, 88200, 96000, 192000 })
        rateBox->addItem (tr ("%1 Hz").arg (r), r);
    rateBox->setCurrentIndex (1);

    channelBox = new QComboBox (this);
    channelBox->addItem (tr ("Mono"), 1);
    channelBox->addItem (tr ("Stereo"), 2);
    channelBox->setCurrentIndex (1);

    preRecord = new QSpinBox (this);
    preRecord->setRange (0, 30);
    preRecord->setValue (5);
    preRecord->setSuffix (tr (" s"));
    preRecord->setToolTip (tr ("Keeps this much of what was heard just before you pressed Record"));

    toNewTab = new QRadioButton (tr ("A new tab"), this);
    toCursor = new QRadioButton (targetName.isEmpty() ? tr ("At the cursor") : tr ("At the cursor in %1").arg (targetName), this);
    toCursor->setEnabled (target != nullptr);
    toNewTab->setChecked (true);
    auto* destination = new QWidget (this);
    auto* destLayout = new QHBoxLayout (destination);
    destLayout->setContentsMargins (0, 0, 0, 0);
    destLayout->addWidget (toNewTab);
    destLayout->addWidget (toCursor);
    destLayout->addStretch();

    auto* form = new QFormLayout();
    form->addRow (tr ("Sample &rate:"), rateBox);
    form->addRow (tr ("C&hannels:"), channelBox);
    form->addRow (tr ("&Pre-record:"), preRecord);
    form->addRow (tr ("Put the take in:"), destination);

    meter = new RecordMeter (this);
    timeLabel = new QLabel (formatTime (0), this);
    auto big = timeLabel->font();
    big.setPointSizeF (big.pointSizeF() * 1.8);
    big.setStyleHint (QFont::Monospace);
    big.setFamily (QStringLiteral ("monospace"));
    timeLabel->setFont (big);
    status = new QLabel (this);
    status->setWordWrap (true);

    recordButton = new QPushButton (tr ("● &Record"), this);
    recordButton->setDefault (true);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Cancel, this);
    buttons->addButton (recordButton, QDialogButtonBox::ActionRole);
    connect (buttons, &QDialogButtonBox::rejected, this, &RecordDialog::reject);
    connect (recordButton, &QPushButton::clicked, this, [this] { recording ? stopRecording() : startRecording(); });

    auto* timeRow = new QHBoxLayout();
    timeRow->addWidget (timeLabel);
    timeRow->addWidget (status, 1);

    auto* layout = new QVBoxLayout (this);
    layout->addWidget (sourceList, 1);
    layout->addLayout (form);
    layout->addWidget (meter);
    layout->addLayout (timeRow);
    layout->addWidget (buttons);

    connect (sourceList, &QTreeWidget::currentItemChanged, this, [this] { if (! recording) restartMonitoring(); });
    connect (rateBox, &QComboBox::currentIndexChanged, this, [this] { restartMonitoring(); });
    connect (channelBox, &QComboBox::currentIndexChanged, this, [this] { restartMonitoring(); });
    connect (preRecord, &QSpinBox::valueChanged, this, [this] { restartMonitoring(); });
    connect (toCursor, &QRadioButton::toggled, this, [this] { updateOptionState(); restartMonitoring(); });

    if (auto e = capture.connect(); ! e.empty())
        captureError = QString::fromStdString (e);

    refreshSources();
    updateOptionState();
    restartMonitoring();
    sourceList->setFocus();

    connect (&timer, &QTimer::timeout, this, &RecordDialog::poll);
    timer.start (30);
}

RecordDialog::~RecordDialog()
{
    capture.stop();
    safety.close (false);
}

bool RecordDialog::insertAtCursor() const
{
    return target != nullptr && toCursor->isChecked();
}

void RecordDialog::updateOptionState()
{
    // Inserting into a file means recording in its format.
    const bool locked = insertAtCursor();
    if (locked)
    {
        const QSignalBlocker b1 (rateBox), b2 (channelBox);
        const auto r = (int) std::lround (target->sampleRate());
        if (rateBox->findData (r) < 0)
            rateBox->addItem (tr ("%1 Hz").arg (r), r);
        rateBox->setCurrentIndex (rateBox->findData (r));

        const auto ch = target->numChannels();
        if (channelBox->findData (ch) < 0)
            channelBox->addItem (tr ("%1 channels").arg (ch), ch);
        channelBox->setCurrentIndex (channelBox->findData (ch));
    }

    rateBox->setEnabled (! locked && ! recording);
    channelBox->setEnabled (! locked && ! recording);
    preRecord->setEnabled (! recording);
    toNewTab->setEnabled (! recording);
    toCursor->setEnabled (target != nullptr && ! recording);
}

void RecordDialog::refreshSources()
{
    const auto v = capture.sourcesVersion();
    if (v == shownVersion)
        return;
    shownVersion = v;

    const auto previous = chosenSource();
    const QSignalBlocker quiet (sourceList);
    sourceList->clear();

    if (! AudioCapture::isAvailable() || ! capture.isConnected())
        return;

    struct Group { CaptureSource::Kind kind; QString title, empty; QTreeWidgetItem* item = nullptr; };
    Group groups[] {
        { CaptureSource::Kind::desktop, tr ("Desktop audio"), {} },
        { CaptureSource::Kind::input, tr ("Inputs"), tr ("No microphones or inputs found") },
        { CaptureSource::Kind::application, tr ("Applications"), tr ("Nothing is playing right now") },
    };

    QTreeWidgetItem* reselect = nullptr;
    QTreeWidgetItem* first = nullptr;
    for (auto& g : groups)
    {
        g.item = new QTreeWidgetItem (sourceList, { g.title });
        g.item->setFlags (Qt::ItemIsEnabled);
        auto f = g.item->font (0);
        f.setBold (true);
        g.item->setFont (0, f);
    }

    for (const auto& s : capture.sources())
    {
        auto& g = *std::find_if (std::begin (groups), std::end (groups), [&] (const Group& x) { return x.kind == s.kind; });
        auto* item = new QTreeWidgetItem (g.item, { QString::fromStdString (s.name), QString::fromStdString (s.detail) });
        item->setData (0, kindRole, (int) s.kind);
        item->setData (0, targetRole, QString::fromStdString (s.target));
        item->setData (0, nameRole, QString::fromStdString (s.name));
        first = first != nullptr ? first : item;
        if (previous && s == *previous)
            reselect = item;
    }

    for (auto& g : groups)
        if (g.item->childCount() == 0)
        {
            auto* none = new QTreeWidgetItem (g.item, { g.empty });
            none->setFlags (Qt::NoItemFlags);
        }

    sourceList->expandAll();
    sourceList->resizeColumnToContents (1);

    if (reselect != nullptr)
        sourceList->setCurrentItem (reselect);
    else if (recording)
        sourceList->setCurrentItem (nullptr);   // the source went away; the take keeps what arrived
    else
    {
        sourceList->setCurrentItem (first);
        if (previous && ! recording)
            restartMonitoring();   // what we were listening to went away
    }
}

std::optional<CaptureSource> RecordDialog::chosenSource() const
{
    const auto* item = sourceList->currentItem();
    if (item == nullptr || ! item->data (0, kindRole).isValid())
        return std::nullopt;

    CaptureSource s;
    s.kind = (CaptureSource::Kind) item->data (0, kindRole).toInt();
    s.target = item->data (0, targetRole).toString().toStdString();
    s.name = item->data (0, nameRole).toString().toStdString();
    return s;
}

int RecordDialog::chosenRate() const     { return rateBox->currentData().toInt(); }
int RecordDialog::chosenChannels() const { return channelBox->currentData().toInt(); }

void RecordDialog::restartMonitoring()
{
    if (recording)
        return;

    capture.stop();
    const auto source = chosenSource();
    if (source && captureError.isEmpty())
    {
        if (auto e = capture.start (*source, chosenRate(), chosenChannels()); ! e.empty())
            status->setText (QString::fromStdString (e));
    }

    const auto ch = chosenChannels();
    meter->setFormat (ch, chosenRate());
    block.assign ((size_t) blockFrames * (size_t) ch, 0.0f);
    preRoll.assign ((size_t) preRecord->value() * (size_t) chosenRate() * (size_t) ch, 0.0f);
    preRollPos = preRollFilled = 0;
    updateStatus();
}

void RecordDialog::poll()
{
    refreshSources();

    const auto ch = (size_t) capture.numChannels();
    bool any = false;
    for (;;)
    {
        const auto n = capture.read (block.data(), blockFrames);
        if (n <= 0)
            break;

        any = true;
        meter->push (block.data(), n);
        const auto samples = (size_t) n * ch;

        if (recording)
        {
            safety.write (block.data(), n);
            split.resize (ch);
            std::vector<const float*> ptrs (ch);
            for (size_t c = 0; c < ch; ++c)
            {
                split[c].resize ((size_t) n);
                for (int i = 0; i < n; ++i)
                    split[c][(size_t) i] = block[(size_t) i * ch + c];
                ptrs[c] = split[c].data();
            }
            builder->append (ptrs.data(), n);
            recordedFrames += n;
        }
        else if (! preRoll.empty())
        {
            for (size_t i = 0; i < samples; ++i)
            {
                preRoll[preRollPos] = block[i];
                preRollPos = (preRollPos + 1) % preRoll.size();
            }
            preRollFilled = std::min (preRoll.size(), preRollFilled + samples);
        }
    }

    if (any || ! sinceData.isValid())
        sinceData.start();
    else if (const auto ms = sinceData.restart(); ms > 0)
        meter->fall ((int) (capture.sampleRate() * ms / 1000));

    updateStatus();
}

void RecordDialog::startRecording()
{
    if (! capture.isRunning())
        return;

    poll();
    const auto ch = capture.numChannels();
    builder.emplace (ch, (double) capture.sampleRate());
    recordedFrames = 0;
    droppedAtStart = capture.droppedFrames();
    const bool safe = safety.open (capture.sampleRate(), ch);

    // Start the take with the pre-record buffer, oldest first.
    if (preRollFilled > 0)
    {
        std::vector<float> ordered;
        ordered.reserve (preRollFilled);
        const auto begin = preRollFilled < preRoll.size() ? 0 : preRollPos;
        for (size_t i = 0; i < preRollFilled; ++i)
            ordered.push_back (preRoll[(begin + i) % preRoll.size()]);

        const auto frames = (int) (ordered.size() / (size_t) ch);
        safety.write (ordered.data(), frames);
        std::vector<std::vector<float>> planes ((size_t) ch, std::vector<float> ((size_t) frames));
        std::vector<const float*> ptrs ((size_t) ch);
        for (int c = 0; c < ch; ++c)
        {
            for (int i = 0; i < frames; ++i)
                planes[(size_t) c][(size_t) i] = ordered[(size_t) (i * ch + c)];
            ptrs[(size_t) c] = planes[(size_t) c].data();
        }
        builder->append (ptrs.data(), frames);
        recordedFrames = frames;
    }

    const auto source = chosenSource();
    recordingName = source ? QString::fromStdString (source->name) : QString();
    recording = true;
    recordButton->setText (tr ("■ &Stop"));
    sourceList->setEnabled (false);
    updateOptionState();
    if (! safe)
        captureError = tr ("Could not write the safety copy in %1; the take is only in memory.").arg (RecordingFile::folder());
    updateStatus();
}

void RecordDialog::stopRecording()
{
    poll();
    recording = false;
    capture.stop();
    result = builder->finish();
    builder.reset();
    safety.close (false);
    accept();
}

void RecordDialog::reject()
{
    if (recording)
    {
        const auto answer = QMessageBox::question (this, tr ("Record"), tr ("Throw away this recording?"),
                                                   QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Discard)
            return;
        recording = false;
    }

    capture.stop();
    safety.close (false);
    QDialog::reject();
}

void RecordDialog::updateStatus()
{
    const auto rate = std::max (1, capture.sampleRate());
    timeLabel->setText (formatTime ((double) recordedFrames / rate));
    recordButton->setEnabled (recording || capture.isRunning());

    QStringList lines;
    if (! captureError.isEmpty())
        lines << captureError;
    if (const auto e = capture.streamError(); ! e.empty())
        lines << tr ("PipeWire: %1").arg (QString::fromStdString (e));

    const auto source = chosenSource();
    if (recording)
    {
        const QStorageInfo disk (RecordingFile::folder());
        const auto bytesPerSecond = (qint64) rate * capture.numChannels() * 4;
        lines << tr ("Recording %1. Press Enter to stop.").arg (recordingName);
        if (disk.isValid() && bytesPerSecond > 0)
            lines << tr ("Disk space for about %1 more.").arg (formatDuration (disk.bytesAvailable() / bytesPerSecond));
        if (const auto lost = capture.droppedFrames() - droppedAtStart; lost > 0)
            lines << tr ("%1 s of audio was lost because the computer was too busy.").arg ((double) lost / rate, 0, 'f', 2);
    }
    else if (capture.isRunning() && source)
    {
        lines << tr ("Listening to %1. Press Enter to record.").arg (QString::fromStdString (source->name));
        if (preRecord->value() > 0)
            lines << tr ("The last %1 s before you press Record are kept too.").arg (preRecord->value());
    }
    else if (captureError.isEmpty() && ! source)
        lines << tr ("Pick something to record from.");

    if (meter->clipped())
        lines << tr ("The level hit 0 dBFS. Turn the source down, then click the meter to clear the red light.");

    const auto text = lines.join (QLatin1Char ('\n'));
    if (status->text() != text)
        status->setText (text);
}
