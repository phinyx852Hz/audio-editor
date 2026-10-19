// SPDX-License-Identifier: AGPL-3.0-only
// The Album (album assembly) part of MainWindow: commands, dialogs, saving
// and exporting. The model and its mixdown are in core/Album.h.
#include "MainWindow.h"
#include "Settings.h"
#include "AlbumFile.h"
#include "AlbumView.h"

#include "../core/Dsp.h"
#include "../core/Album.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace
{
    QString tagText (const Tags& tags, const char* key)
    {
        const auto t = tags.find (key);
        return t != tags.end() ? QString::fromStdString (t->second) : QString();
    }

    void setTag (Tags& tags, const char* key, const QString& value)
    {
        if (value.trimmed().isEmpty())
            tags.erase (key);
        else
            tags[key] = value.trimmed().toStdString();
    }

    QString seconds (Frame frames, double rate) { return QString::number ((double) frames / rate, 'f', 1); }

    // A name that is safe as a file name on every system the files may travel to.
    QString fileNameFor (QString name)
    {
        name.replace (QRegularExpression (QStringLiteral ("[/\\\\:*?\"<>|]")), QStringLiteral ("_"));
        return name.trimmed().isEmpty() ? QObject::tr ("Untitled") : name.trimmed();
    }
}

//==============================================================================
void MainWindow::registerAlbumCommands()
{
    using KS = QKeySequence;
    const auto category = tr ("Album");
    auto add = [this, &category] (const char* id, const QString& text, QList<QKeySequence> keys, std::function<void()> fn)
    {
        addAction (commands.add (QString::fromLatin1 (id), category, text, keys, std::move (fn)));
    };

    add ("album.new", tr ("&New Album"), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_M) }, [this] { newAlbum(); });
    add ("album.addSongs", tr ("&Add Songs..."), { KS (Qt::ALT | Qt::Key_Insert) }, [this] { addSongs(); });
    add ("album.previousSong", tr ("&Previous Song"), { KS (Qt::ALT | Qt::Key_Left) }, [this] { jumpToSong (-1); });
    add ("album.nextSong", tr ("Ne&xt Song"), { KS (Qt::ALT | Qt::Key_Right) }, [this] { jumpToSong (1); });
    add ("album.moveEarlier", tr ("Move Song &Earlier"), { KS (Qt::ALT | Qt::SHIFT | Qt::Key_Left) }, [this] { moveSong (-1); });
    add ("album.moveLater", tr ("Move Song &Later"), { KS (Qt::ALT | Qt::SHIFT | Qt::Key_Right) }, [this] { moveSong (1); });
    add ("album.longerPause", tr ("Longer Pa&use Before Song"), { KS (Qt::ALT | Qt::Key_Up) }, [this] { changePause (0.5); });
    add ("album.shorterPause", tr ("Shorter Pause or &Crossfade"), { KS (Qt::ALT | Qt::Key_Down) }, [this] { changePause (-0.5); });
    add ("album.songProperties", tr ("&Song Properties..."), { KS (Qt::ALT | Qt::Key_Return) }, [this] { songProperties(); });
    add ("album.removeSong", tr ("&Remove Song"), { KS (Qt::ALT | Qt::Key_Delete) }, [this] { removeSong(); });
    add ("album.openSong", tr ("&Open Song in Editor"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_Return) }, [this] { openSong(); });
    add ("album.properties", tr ("Al&bum Properties..."), { KS (Qt::ALT | Qt::SHIFT | Qt::Key_Return) }, [this] { albumProperties(); });
    add ("album.export", tr ("E&xport Album..."), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_X) }, [this] { exportAlbum(); });
    add ("album.twoTracks", tr ("&Two Tracks (Songs Alternate)"), { KS (Qt::ALT | Qt::SHIFT | Qt::Key_T) }, [this]
    {
        const auto* current = album();
        if (current == nullptr)
            return;
        auto m = *current;
        m.twoTracks = ! m.twoTracks;
        const auto two = m.twoTracks;
        commitAlbum (two ? tr ("Two Tracks") : tr ("One Track"), std::move (m), cursor);
        statusBar()->showMessage (two ? tr ("Two tracks: songs alternate, and overlapping songs play over each other, shaped by their own fades "
                                           "(Song Properties, Alt+Enter). Alt+Down makes them overlap.")
                                      : tr ("One track: overlapping songs crossfade."), 10000);
    });
    commands.action (QStringLiteral ("album.twoTracks"))->setCheckable (true);
}

void MainWindow::updateAlbumCommandStates()
{
    const auto* m = album();
    const bool any = m != nullptr && ! m->clips.empty() && ! loading;
    commands.action (QStringLiteral ("album.new"))->setEnabled (! loading);
    commands.action (QStringLiteral ("album.addSongs"))->setEnabled (m != nullptr && ! loading);
    for (const auto* id : { "album.previousSong", "album.nextSong", "album.moveEarlier", "album.moveLater",
                            "album.longerPause", "album.shorterPause", "album.songProperties", "album.removeSong",
                            "album.openSong", "album.export" })
        commands.action (QString::fromLatin1 (id))->setEnabled (any);
    commands.action (QStringLiteral ("album.properties"))->setEnabled (m != nullptr && ! loading);
    auto* twoTracks = commands.action (QStringLiteral ("album.twoTracks"));
    twoTracks->setEnabled (m != nullptr && ! loading);
    twoTracks->setChecked (m != nullptr && m->twoTracks);
}

const Album* MainWindow::album() const
{
    return doc.isAlbum() ? doc.current().album.get() : nullptr;
}

int MainWindow::currentSong() const
{
    const auto* m = album();
    if (m == nullptr || m->clips.empty())
        return -1;
    if (const auto i = m->clipAt (cursor); i >= 0)
        return i;
    for (int i = 0; i < (int) m->clips.size(); ++i)
        if (m->clipStart (i) >= cursor)
            return i;
    return (int) m->clips.size() - 1;
}

bool MainWindow::refuseAlbumEdit()
{
    if (! doc.isAlbum())
        return false;

    statusBar()->showMessage (tr ("An album is put together from its songs, so its audio is not edited here. "
                                  "Open a song in its own tab with Ctrl+Alt+Enter: your edits show up in the album."), 10000);
    return true;
}

bool MainWindow::runWithProgress (const QString& title, const std::function<void (std::atomic<float>&, const std::atomic<bool>&)>& work)
{
    auto progress = std::make_shared<std::atomic<float>> (0.0f);
    auto cancel = std::make_shared<std::atomic<bool>> (false);
    engine.stop();

    QProgressDialog dialog (title + QStringLiteral ("..."), tr ("Cancel"), 0, 1000, this);
    dialog.setWindowTitle (title);
    dialog.setWindowModality (Qt::WindowModal);
    dialog.setMinimumDuration (400);
    connect (&dialog, &QProgressDialog::canceled, this, [cancel] { cancel->store (true); });

    QFutureWatcher<void> watcher;
    QEventLoop loop;
    connect (&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
    QTimer poll;
    connect (&poll, &QTimer::timeout, this, [&] { dialog.setValue ((int) (progress->load() * 1000.0f)); });
    poll.start (50);

    watcher.setFuture (QtConcurrent::run ([work, progress, cancel] { work (*progress, *cancel); }));
    loop.exec();
    poll.stop();
    dialog.reset();
    return ! cancel->load();
}

//==============================================================================
void MainWindow::newAlbum()
{
    if (loading)
        return;
    if (doc.isOpen())
        newTab();

    auto m = std::make_shared<Album>();
    doc.reset (m->mixdown(), {}, std::string(), {}, m);
    cursor = selStart = selEnd = selAnchor = 0;
    optionsConfirmedFor.clear();
    refreshFromDocument (false);
    statusBar()->showMessage (tr ("New album. Add songs with Alt+Insert, or open files from the Find panel (Ctrl+F) to add them."), 10000);
}

void MainWindow::openAlbum (const QString& path)
{
    if (loading)
        return;

    std::shared_ptr<Album> m;
    QStringList problems;
    QString error;
    const auto finished = runWithProgress (tr ("Opening the album"), [&] (std::atomic<float>& progress, const std::atomic<bool>& cancel)
    {
        m = albumfile::load (path, problems, error, &progress, &cancel);
    });
    if (! finished)
        return;
    if (m == nullptr)
    {
        QMessageBox::warning (this, tr ("Open failed"), tr ("Could not open %1:\n%2").arg (path, error));
        return;
    }

    if (doc.isOpen())
        newTab();
    std::shared_ptr<const Album> shared = m;
    doc.reset (shared->mixdown(), shared->trackMarkers(), path.toStdString(), {}, shared);
    cursor = selStart = selEnd = selAnchor = 0;
    refreshFromDocument (false);
    refreshAlbumSources();
    if (! problems.isEmpty())
        QMessageBox::warning (this, tr ("Missing songs"),
                              tr ("These songs could not be read and stay silent until they are found:\n\n%1").arg (problems.join (QLatin1Char ('\n'))));
    waveform->setFocus();
}

bool MainWindow::saveAlbum (const QString& path)
{
    const auto* m = album();
    if (m == nullptr)
        return false;

    QString error;
    if (! albumfile::save (*m, path, error))
    {
        QMessageBox::warning (this, tr ("Save failed"), tr ("Could not save %1:\n%2").arg (path, error));
        return false;
    }
    doc.setPath (path.toStdString());
    doc.markSaved();
    updateTitle();
    statusBar()->showMessage (tr ("Saved the album %1. It refers to the song files, so keep them.").arg (QFileInfo (path).fileName()), 6000);
    return true;
}

bool MainWindow::saveAlbumAs()
{
    const auto* m = album();
    if (m == nullptr)
        return false;

    auto suggested = QString::fromStdString (doc.path());
    if (suggested.isEmpty())
    {
        const auto title = tagText (m->tags, tag::album);
        const auto folder = m->clips.empty() ? QStandardPaths::writableLocation (QStandardPaths::MusicLocation)
                                             : QFileInfo (QString::fromStdString (m->clips.front().path)).absolutePath();
        suggested = folder + QLatin1Char ('/') + fileNameFor (title.isEmpty() ? tr ("Album") : title) + QStringLiteral (".") + albumfile::extension;
    }
    // An album opened from an old .montage file is saved as .phalbum beside it.
    auto withExtension = [] (QString p)
    {
        const QFileInfo info (p);
        if (info.suffix().toLower() == albumfile::oldExtension)
            p = info.path() + QLatin1Char ('/') + info.completeBaseName();
        if (QFileInfo (p).suffix().toLower() != albumfile::extension)
            p += QStringLiteral (".") + albumfile::extension;
        return p;
    };
    auto path = QFileDialog::getSaveFileName (this, tr ("Save Album As"), withExtension (suggested), tr ("PHINYX album (*.phalbum)"));
    if (path.isEmpty())
        return false;
    path = withExtension (path);
    return saveAlbum (path);
}

//==============================================================================
void MainWindow::commitAlbum (const QString& description, Album m, Frame newCursor)
{
    m.clampPauses();
    auto shared = std::make_shared<const Album> (std::move (m));
    auto state = doc.current();
    const bool wasEmpty = state.data == nullptr || state.data->length() == 0;
    state.data = shared->mixdown();
    state.markers = shared->trackMarkers();
    state.album = shared;
    doc.apply (description.toStdString(), std::move (state));
    if (newCursor >= 0)
        cursor = newCursor;
    selStart = selEnd = selAnchor = cursor;
    refreshFromDocument (! wasEmpty);   // the first songs are shown whole
    if (newCursor >= 0)
        setCursor (newCursor);
}

void MainWindow::addSongs()
{
    if (album() == nullptr)
        return;

    QStringList patterns;
    for (const auto& ext : AudioEngine::readableExtensions())
        patterns << QStringLiteral ("*.") + QString::fromStdString (ext);
    const auto* m = album();
    const auto start = m->clips.empty() ? QStandardPaths::writableLocation (QStandardPaths::MusicLocation)
                                        : QFileInfo (QString::fromStdString (m->clips.back().path)).absolutePath();
    const auto paths = QFileDialog::getOpenFileNames (this, tr ("Add Songs"), start,
                                                      tr ("Audio files (%1);;All files (*)").arg (patterns.join (QLatin1Char (' '))));
    if (! paths.isEmpty())
        addSongFiles (paths);
}

void MainWindow::addSongFiles (const QStringList& paths)
{
    if (album() == nullptr || paths.isEmpty())
        return;

    auto m = *album();
    const bool wasEmpty = m.clips.empty();
    struct Song { QString path; AudioDataPtr data, original; Tags tags; QString error; };
    std::vector<Song> songs ((size_t) paths.size());

    const auto finished = runWithProgress (tr ("Adding songs"), [&] (std::atomic<float>& progress, const std::atomic<bool>& cancel)
    {
        auto rate = wasEmpty ? 0.0 : m.sampleRate;
        for (int i = 0; i < paths.size() && ! cancel.load(); ++i)
        {
            auto& s = songs[(size_t) i];
            s.path = paths[i];
            std::vector<Marker> markers;
            std::string error;
            s.original = AudioEngine::loadFile (s.path.toStdString(), markers, s.tags, error);
            s.error = QString::fromStdString (error);
            if (s.original != nullptr)
            {
                // The first song of a new album sets its sample rate; the others follow it.
                if (rate <= 0.0)
                    rate = s.original->sampleRate();
                s.data = std::abs (s.original->sampleRate() - rate) > 1.0e-6 ? dsp::resample (*s.original, rate) : s.original;
            }
            progress.store ((float) (i + 1) / (float) paths.size());
        }
    });
    if (! finished)
        return;

    QStringList failed;
    auto insertAt = wasEmpty ? 0 : currentSong() + 1;
    const auto firstInserted = insertAt;
    for (auto& s : songs)
    {
        if (s.data == nullptr)
        {
            failed << tr ("%1: %2").arg (QFileInfo (s.path).fileName(), s.error);
            continue;
        }
        if (m.clips.empty())
        {
            m.sampleRate = s.data->sampleRate();
            m.channels = 0;
        }
        m.channels = std::clamp (std::max (m.channels, s.data->numChannels()), 1, MeterBlock::maxChannels);

        AlbumClip c;
        c.path = s.path.toStdString();
        c.source = s.data;
        c.original = s.original;
        c.sourceEnd = s.data->length();
        c.pauseBefore = insertAt == 0 ? 0 : (Frame) std::llround (Album::defaultPauseSeconds * m.sampleRate);
        for (const auto* key : { tag::title, tag::artist, tag::isrc })
            if (const auto t = s.tags.find (key); t != s.tags.end() && ! t->second.empty())
                c.tags[key] = t->second;
        if (insertAt == 0 && ! m.clips.empty())
            m.clips.front().pauseBefore = (Frame) std::llround (Album::defaultPauseSeconds * m.sampleRate);
        m.clips.insert (m.clips.begin() + insertAt, std::move (c));
        ++insertAt;
    }

    if (insertAt > firstInserted)
    {
        const auto count = insertAt - firstInserted;
        Album probe = m;
        probe.clampPauses();
        commitAlbum (count == 1 ? tr ("Add Song") : tr ("Add %1 Songs").arg (count), std::move (m), probe.clipStart (firstInserted));
        statusBar()->showMessage (tr ("Added %n song(s). Alt+Left and Alt+Right move between songs, Alt+Up and Alt+Down change the pause.",
                                      nullptr, count), 8000);
    }
    if (! failed.isEmpty())
        QMessageBox::warning (this, tr ("Add Songs"), tr ("These files could not be read:\n\n%1").arg (failed.join (QLatin1Char ('\n'))));
    waveform->setFocus();
}

void MainWindow::jumpToSong (int direction)
{
    const auto* m = album();
    if (m == nullptr || m->clips.empty())
        return;

    const auto n = (int) m->clips.size();
    Frame target = -1;
    if (direction < 0)
    {
        // Back to the start of this song first, then to the one before.
        for (int i = n - 1; i >= 0 && target < 0; --i)
            if (m->clipStart (i) < cursor)
                target = m->clipStart (i);
        if (target < 0)
            target = m->clipStart (0);
    }
    else
    {
        for (int i = 0; i < n && target < 0; ++i)
            if (m->clipStart (i) > cursor)
                target = m->clipStart (i);
        if (target < 0)
            return;
    }

    setCursor (target);
    const auto i = currentSong();
    statusBar()->showMessage (tr ("Song %1 of %2: %3").arg (i + 1).arg (n).arg (QString::fromStdString (m->clipName (i))), 4000);
}

void MainWindow::moveSong (int direction)
{
    const auto i = currentSong();
    const auto* current = album();
    const auto j = i + direction;
    if (current == nullptr || i < 0 || j < 0 || j >= (int) current->clips.size())
        return;

    // The pauses stay where they are; the songs trade places.
    auto m = *current;
    auto& a = m.clips[(size_t) i];
    auto& b = m.clips[(size_t) j];
    std::swap (a.pauseBefore, b.pauseBefore);
    std::swap (a, b);
    Album probe = m;
    probe.clampPauses();
    commitAlbum (tr ("Move Song"), std::move (m), probe.clipStart (j));
}

void MainWindow::changePause (double secondsChange)
{
    const auto i = currentSong();
    const auto* current = album();
    if (current == nullptr || i < 0)
        return;

    auto m = *current;
    auto& c = m.clips[(size_t) i];
    c.pauseBefore += (Frame) std::llround (secondsChange * m.sampleRate);
    // Round to the step, so a song's own odd pause becomes a tidy one.
    const auto step = (Frame) std::llround (0.5 * m.sampleRate);
    c.pauseBefore = (Frame) std::llround ((double) c.pauseBefore / (double) step) * step;
    m.clampPauses();
    const auto pause = m.clips[(size_t) i].pauseBefore;
    const auto start = m.clipStart (i);
    commitAlbum (pause < 0 ? tr ("Crossfade") : tr ("Change Pause"), std::move (m), start);

    if (pause < 0)
        statusBar()->showMessage (tr ("Song %1 crossfades with song %2 over %3 s").arg (i + 1).arg (i).arg (seconds (-pause, album()->sampleRate)), 5000);
    else
        statusBar()->showMessage (i == 0 ? tr ("%1 s of silence before the first song").arg (seconds (pause, album()->sampleRate))
                                         : tr ("Pause before song %1: %2 s").arg (i + 1).arg (seconds (pause, album()->sampleRate)), 5000);
}

void MainWindow::songProperties()
{
    const auto i = currentSong();
    const auto* current = album();
    if (current == nullptr || i < 0)
        return;

    auto m = *current;
    auto& c = m.clips[(size_t) i];
    const auto rate = m.sampleRate;
    const auto fileLength = c.source != nullptr ? (double) c.source->length() / rate : (double) c.sourceEnd / rate;

    QDialog dialog (this);
    dialog.setWindowTitle (tr ("Song %1 Properties").arg (i + 1));
    auto* form = new QFormLayout (&dialog);
    auto* file = new QLabel (QString::fromStdString (c.path), &dialog);
    file->setTextInteractionFlags (Qt::TextSelectableByMouse);
    file->setWordWrap (true);
    form->addRow (tr ("File:"), file);

    auto* title = new QLineEdit (tagText (c.tags, tag::title), &dialog);
    title->setPlaceholderText (QString::fromStdString (m.clipName (i)));
    auto* artist = new QLineEdit (tagText (c.tags, tag::artist), &dialog);
    artist->setPlaceholderText (tagText (m.tags, tag::artist));
    auto* isrc = new QLineEdit (tagText (c.tags, tag::isrc), &dialog);
    isrc->setPlaceholderText (tr ("12 characters, like USRC17607839"));
    isrc->setValidator (new QRegularExpressionValidator (QRegularExpression (QStringLiteral ("[A-Za-z]{2}[A-Za-z0-9]{3}[0-9]{7}")), isrc));
    form->addRow (tr ("&Title:"), title);
    form->addRow (tr ("&Artist:"), artist);
    form->addRow (tr ("&ISRC:"), isrc);

    auto spin = [&dialog] (double value, double low, double high, const QString& suffix)
    {
        auto* box = new QDoubleSpinBox (&dialog);
        box->setRange (low, high);
        box->setDecimals (2);
        box->setSingleStep (0.5);
        box->setSuffix (suffix);
        box->setValue (value);
        return box;
    };
    const auto s = tr (" s");
    auto* pause = spin ((double) c.pauseBefore / rate, -fileLength, 3600.0, s);
    pause->setToolTip (tr ("Silence before the song. Below 0 the song starts before the one before it ends, and they crossfade."));
    auto* gain = spin (c.gainDb, -48.0, 24.0, tr (" dB"));
    auto* fadeIn = spin ((double) c.fadeIn / rate, 0.0, fileLength, s);
    auto* fadeOut = spin ((double) c.fadeOut / rate, 0.0, fileLength, s);
    auto* from = spin ((double) c.sourceStart / rate, 0.0, fileLength, s);
    auto* to = spin ((double) c.sourceEnd / rate, 0.0, fileLength, s);
    for (auto* box : { from, to })
        box->setDecimals (3);
    form->addRow (i == 0 ? tr ("&Silence before:") : tr ("&Pause before:"), pause);
    form->addRow (tr ("&Gain:"), gain);
    form->addRow (tr ("Fade &in:"), fadeIn);
    form->addRow (tr ("Fade &out:"), fadeOut);
    form->addRow (tr ("&Starts at:"), from);
    form->addRow (tr ("&Ends at:"), to);
    auto* note = new QLabel (tr ("Starts at and Ends at choose the part of the file that is used (%1 s long)."
                                 " A crossfade with the next song is set in that song's pause.").arg (fileLength, 0, 'f', 1), &dialog);
    note->setWordWrap (true);
    form->addRow (note);

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow (buttons);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    setTag (c.tags, tag::title, title->text());
    setTag (c.tags, tag::artist, artist->text());
    setTag (c.tags, tag::isrc, isrc->text().toUpper());
    auto frames = [rate] (QDoubleSpinBox* box) { return (Frame) std::llround (box->value() * rate); };
    c.pauseBefore = frames (pause);
    c.gainDb = gain->value();
    c.fadeIn = frames (fadeIn);
    c.fadeOut = frames (fadeOut);
    c.sourceStart = std::min (frames (from), frames (to));
    c.sourceEnd = std::max (frames (from), frames (to));
    if (c.source != nullptr)
        c.sourceEnd = std::min (c.sourceEnd, c.source->length());
    Album probe = m;
    probe.clampPauses();
    commitAlbum (tr ("Song Properties"), std::move (m), probe.clipStart (i));
}

void MainWindow::albumProperties()
{
    const auto* current = album();
    if (current == nullptr)
        return;

    auto m = *current;
    QDialog dialog (this);
    dialog.setWindowTitle (tr ("Album Properties"));
    auto* form = new QFormLayout (&dialog);
    auto* album = new QLineEdit (tagText (m.tags, tag::album), &dialog);
    auto* artist = new QLineEdit (tagText (m.tags, tag::artist), &dialog);
    auto* catalog = new QLineEdit (tagText (m.tags, tag::catalog), &dialog);
    catalog->setPlaceholderText (tr ("UPC or EAN barcode, 12 or 13 digits"));
    catalog->setValidator (new QRegularExpressionValidator (QRegularExpression (QStringLiteral ("[0-9]{0,13}")), catalog));
    form->addRow (tr ("&Album title:"), album);
    form->addRow (tr ("A&rtist:"), artist);
    form->addRow (tr ("&Catalogue number:"), catalog);
    auto* tracks = new QComboBox (&dialog);
    tracks->addItems ({ tr ("One track: overlapping songs crossfade"), tr ("Two tracks: songs alternate and overlap freely") });
    tracks->setCurrentIndex (m.twoTracks ? 1 : 0);
    form->addRow (tr ("&Tracks:"), tracks);
    auto* note = new QLabel (tr ("Used in the CUE sheet and in the tags of exported files. Songs without an artist of their own get this one."), &dialog);
    note->setWordWrap (true);
    form->addRow (note);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow (buttons);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    setTag (m.tags, tag::album, album->text());
    setTag (m.tags, tag::artist, artist->text());
    setTag (m.tags, tag::catalog, catalog->text());
    m.twoTracks = tracks->currentIndex() == 1;
    commitAlbum (tr ("Album Properties"), std::move (m));
}

void MainWindow::removeSong()
{
    const auto i = currentSong();
    const auto* current = album();
    if (current == nullptr || i < 0)
        return;

    auto m = *current;
    const auto name = QString::fromStdString (m.clipName (i));
    const auto removedPause = m.clips[(size_t) i].pauseBefore;
    m.clips.erase (m.clips.begin() + i);
    if (i == 0 && ! m.clips.empty())
        m.clips.front().pauseBefore = std::max<Frame> (0, removedPause);
    Album probe = m;
    probe.clampPauses();
    const auto next = std::min (i, (int) probe.clips.size() - 1);
    commitAlbum (tr ("Remove Song"), std::move (m), next >= 0 ? probe.clipStart (next) : 0);
    statusBar()->showMessage (tr ("Removed %1. Ctrl+Z brings it back.").arg (name), 5000);
}

void MainWindow::openSong()
{
    const auto i = currentSong();
    const auto* m = album();
    if (m == nullptr || i < 0)
        return;

    const auto path = QString::fromStdString (m->clips[(size_t) i].path);
    for (int t = 0; t < (int) tabs.size(); ++t)
        if (t != activeTab && QString::fromStdString (tabs[(size_t) t].doc.path()) == path && ! tabs[(size_t) t].doc.isAlbum())
        {
            tabBar->setCurrentIndex (t);
            return;
        }

    openFile (path);
    statusBar()->showMessage (tr ("Edits you make here show up in the album when you go back to its tab, even before you save."), 8000);
}

void MainWindow::refreshAlbumSources()
{
    const auto* current = album();
    if (current == nullptr)
        return;

    // The songs open in other tabs, as they are now.
    std::vector<std::pair<int, AudioDataPtr>> changed;
    for (int i = 0; i < (int) current->clips.size(); ++i)
    {
        const auto& c = current->clips[(size_t) i];
        for (int t = 0; t < (int) tabs.size(); ++t)
        {
            const auto& other = tabs[(size_t) t].doc;
            // A tab with no edits since it was opened holds what the album read from the file too.
            if (t == activeTab || ! other.isOpen() || other.isAlbum() || other.path() != c.path || ! other.canUndo())
                continue;
            if (const auto d = other.current().data; d != c.original)
                changed.emplace_back (i, d);
        }
    }
    if (changed.empty())
        return;

    auto m = *current;
    std::vector<AudioDataPtr> sources (changed.size());
    runWithProgress (tr ("Updating the album"), [&] (std::atomic<float>& progress, const std::atomic<bool>&)
    {
        for (size_t k = 0; k < changed.size(); ++k)
        {
            const auto& d = changed[k].second;
            sources[k] = std::abs (d->sampleRate() - m.sampleRate) > 1.0e-6 ? dsp::resample (*d, m.sampleRate) : d;
            progress.store ((float) (k + 1) / (float) changed.size());
        }
    });

    for (size_t k = 0; k < changed.size(); ++k)
    {
        if (sources[k] == nullptr)
            continue;
        auto& c = m.clips[(size_t) changed[k].first];
        // A song used whole stays whole when the edit changed its length.
        const bool whole = c.source != nullptr && c.sourceStart == 0 && c.sourceEnd == c.source->length();
        c.source = sources[k];
        c.original = changed[k].second;
        c.sourceEnd = whole ? c.source->length() : std::min (c.sourceEnd, c.source->length());
        c.sourceStart = std::min (c.sourceStart, c.sourceEnd);
    }
    commitAlbum (tr ("Use Edited Songs"), std::move (m), cursor);
    statusBar()->showMessage (tr ("The album now uses your edits to %n song(s).", nullptr, (int) changed.size()), 6000);
}

//==============================================================================
void MainWindow::exportAlbum()
{
    const auto* m = album();
    const auto album = data();
    if (m == nullptr || album == nullptr || m->clips.empty())
        return;

    auto settings = appSettings();
    QDialog dialog (this);
    dialog.setWindowTitle (tr ("Export Album"));
    auto* form = new QFormLayout (&dialog);

    const auto albumTitle = tagText (m->tags, tag::album);
    auto defaultFolder = doc.path().empty() ? QFileInfo (QString::fromStdString (m->clips.front().path)).absolutePath()
                                            : QFileInfo (QString::fromStdString (doc.path())).absolutePath();
    auto* folder = new QLineEdit (settings.value (QStringLiteral ("album/exportFolder"), defaultFolder).toString(), &dialog);
    auto* browse = new QPushButton (tr ("&Browse..."), &dialog);
    auto* folderRow = new QHBoxLayout();
    folderRow->addWidget (folder, 1);
    folderRow->addWidget (browse);
    connect (browse, &QPushButton::clicked, &dialog, [&]
    {
        const auto chosen = QFileDialog::getExistingDirectory (&dialog, tr ("Export To"), folder->text());
        if (! chosen.isEmpty())
            folder->setText (chosen);
    });
    auto* folderLabel = new QLabel (tr ("&Folder:"), &dialog);
    folderLabel->setBuddy (folder);
    form->addRow (folderLabel, folderRow);

    auto* name = new QLineEdit (fileNameFor (! albumTitle.isEmpty() ? albumTitle
                                             : doc.path().empty() ? tr ("Album") : QFileInfo (QString::fromStdString (doc.path())).completeBaseName()),
                                &dialog);
    form->addRow (tr ("&Album file name:"), name);

    auto* format = new QComboBox (&dialog);
    for (const auto& ext : AudioEngine::writableExtensions())
        format->addItem (QString::fromStdString (ext).toUpper(), QString::fromStdString (ext));
    format->setCurrentIndex (std::max (0, format->findData (settings.value (QStringLiteral ("album/exportFormat"), QStringLiteral ("wav")))));
    form->addRow (tr ("F&ormat:"), format);

    auto* depth = new QComboBox (&dialog);
    depth->addItem (tr ("16-bit (CD)"), 16);
    depth->addItem (tr ("24-bit"), 24);
    depth->addItem (tr ("32-bit float"), 32);
    depth->setCurrentIndex (std::max (0, depth->findData (settings.value (QStringLiteral ("album/exportBits"), 16))));
    form->addRow (tr ("&Bit depth:"), depth);
    auto* dither = new QCheckBox (tr ("&Dither when reducing the bit depth"), &dialog);
    dither->setChecked (settings.value (QStringLiteral ("album/exportDither"), true).toBool());
    form->addRow (dither);

    auto* whole = new QCheckBox (tr ("One file for the &whole album, with a CUE sheet"), &dialog);
    whole->setChecked (settings.value (QStringLiteral ("album/exportWhole"), true).toBool());
    auto* tracks = new QCheckBox (tr ("A file for each &song (gapless: each pause goes with the song before it)"), &dialog);
    tracks->setChecked (settings.value (QStringLiteral ("album/exportTracks"), true).toBool());
    auto* master = new QCheckBox (tr ("Through the &Master Section"), &dialog);
    master->setChecked (engine.masterIsActive());
    master->setEnabled (engine.masterIsActive());
    if (! engine.masterIsActive())
        master->setToolTip (tr ("The Master Section has no active plugins and no gain."));
    form->addRow (whole);
    form->addRow (tracks);
    form->addRow (master);

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button (QDialogButtonBox::Ok)->setText (tr ("E&xport"));
    form->addRow (buttons);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || (! whole->isChecked() && ! tracks->isChecked()))
        return;

    settings.setValue (QStringLiteral ("album/exportFolder"), folder->text());
    settings.setValue (QStringLiteral ("album/exportFormat"), format->currentData());
    settings.setValue (QStringLiteral ("album/exportBits"), depth->currentData());
    settings.setValue (QStringLiteral ("album/exportDither"), dither->isChecked());
    settings.setValue (QStringLiteral ("album/exportWhole"), whole->isChecked());
    settings.setValue (QStringLiteral ("album/exportTracks"), tracks->isChecked());

    const QDir dir (folder->text());
    if (! dir.exists() && ! QDir().mkpath (dir.absolutePath()))
    {
        QMessageBox::warning (this, tr ("Export Album"), tr ("Could not make the folder %1.").arg (dir.absolutePath()));
        return;
    }

    const auto ext = format->currentData().toString();
    const auto n = (int) m->clips.size();
    QStringList targets;
    const auto wholePath = dir.filePath (fileNameFor (name->text()) + QLatin1Char ('.') + ext);
    const auto cuePath = dir.filePath (fileNameFor (name->text()) + QStringLiteral (".cue"));
    if (whole->isChecked())
        targets << wholePath << cuePath;
    QStringList trackPaths;
    for (int i = 0; i < n; ++i)
        trackPaths << dir.filePath (QStringLiteral ("%1 %2.%3").arg (i + 1, 2, 10, QLatin1Char ('0'))
                                        .arg (fileNameFor (QString::fromStdString (m->clipName (i))), ext));
    if (tracks->isChecked())
        targets << trackPaths;

    QStringList existing;
    for (const auto& t : targets)
        if (QFileInfo::exists (t))
            existing << QFileInfo (t).fileName();
    if (! existing.isEmpty()
        && QMessageBox::question (this, tr ("Export Album"), tr ("Replace these files?\n\n%1").arg (existing.join (QLatin1Char ('\n'))))
               != QMessageBox::Yes)
        return;

    // The album as heard: through the Master Section when chosen, rendered once
    // so the song files join up without a click.
    auto audio = album;
    if (master->isChecked())
    {
        std::string error;
        audio = renderWithProgress (0, album->length(), error);
        if (audio == nullptr)
        {
            if (! error.empty())
                QMessageBox::warning (this, tr ("Export Album"), QString::fromStdString (error));
            return;
        }
    }

    SaveOptions options;
    options.bitDepth = depth->currentData().toInt();
    options.dither = dither->isChecked();

    QStringList written, failed;
    const auto albumTags = m->tags;
    QApplication::setOverrideCursor (Qt::WaitCursor);
    if (whole->isChecked())
    {
        std::string error;
        auto tags = albumTags;
        if (const auto title = tagText (albumTags, tag::album); ! title.isEmpty())
            tags[tag::title] = title.toStdString();
        if (AudioEngine::saveFile (wholePath.toStdString(), *audio, m->trackMarkers(), tags, options, error))
            written << QFileInfo (wholePath).fileName();
        else
            failed << QStringLiteral ("%1: %2").arg (QFileInfo (wholePath).fileName(), QString::fromStdString (error));

        QFile cue (cuePath);
        if (cue.open (QIODevice::WriteOnly | QIODevice::Truncate))
        {
            cue.write (QByteArray::fromStdString (m->cueSheet (QFileInfo (wholePath).fileName().toStdString())));
            written << QFileInfo (cuePath).fileName();
        }
        else
            failed << QStringLiteral ("%1: %2").arg (QFileInfo (cuePath).fileName(), cue.errorString());
    }

    if (tracks->isChecked())
    {
        for (int i = 0; i < n; ++i)
        {
            const auto start = m->clipStart (i);
            const auto end = i + 1 < n ? m->clipStart (i + 1) : audio->length();
            const auto part = audio->slice (start, std::max (start, end));
            auto tags = albumTags;
            tags.erase (tag::catalog);
            tags[tag::title] = m->clipName (i);
            tags[tag::track] = std::to_string (i + 1) + "/" + std::to_string (n);
            for (const auto& [key, value] : m->clips[(size_t) i].tags)
                tags[key] = value;
            std::string error;
            if (AudioEngine::saveFile (trackPaths[i].toStdString(), *part, {}, tags, options, error))
                written << QFileInfo (trackPaths[i]).fileName();
            else
                failed << QStringLiteral ("%1: %2").arg (QFileInfo (trackPaths[i]).fileName(), QString::fromStdString (error));
        }
    }
    QApplication::restoreOverrideCursor();

    if (! failed.isEmpty())
        QMessageBox::warning (this, tr ("Export Album"), tr ("Some files were not written:\n\n%1").arg (failed.join (QLatin1Char ('\n'))));
    statusBar()->showMessage (tr ("Exported %n file(s) to %1", nullptr, (int) written.size()).arg (dir.absolutePath()), 8000);
}
