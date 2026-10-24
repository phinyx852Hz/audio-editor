// SPDX-License-Identifier: AGPL-3.0-only
// The analysis part of MainWindow: Vamp plugins (engine/VampHost.h) turned into
// markers, regions and tags, and slicing a file at its markers.
#include "MainWindow.h"

#include "../core/Album.h"
#include "../engine/VampHost.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QStatusBar>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>

namespace
{
    namespace id
    {
        constexpr auto beats = "qm-vamp-plugins:qm-barbeattracker:beats";
        constexpr auto onsets = "qm-vamp-plugins:qm-onsetdetector:onsets";
        constexpr auto tempo = "qm-vamp-plugins:qm-tempotracker:tempo";
        constexpr auto key = "qm-vamp-plugins:qm-keydetector:key";
    }

    constexpr int manyResults = 2000;   // more than this many markers is asked about first

    QString missingPluginText (const std::string& outputId)
    {
        QStringList folders;
        for (const auto& f : vamphost::searchPath())
            folders << QString::fromStdString (f);
        return QObject::tr ("The analysis plugin %1 was not found. PHI ships the QM Vamp Plugins on x86-64; elsewhere install "
                            "them (Arch: qm-vamp-plugins) or put Vamp plugins in one of these folders:\n\n%2")
            .arg (QString::fromStdString (outputId), folders.join (QLatin1Char ('\n')));
    }

    // "Eb / D# minor" -> "Ebm", "F# / Gb major" -> "F#": the short form tag readers expect.
    QString shortKey (const QString& label)
    {
        const auto words = label.split (QLatin1Char (' '), Qt::SkipEmptyParts);
        if (words.isEmpty())
            return {};
        const bool minor = label.contains (QStringLiteral ("minor"), Qt::CaseInsensitive);
        return words.front() + (minor ? QStringLiteral ("m") : QString());
    }

    // The value held the longest: Vamp trackers report a value when it changes.
    template <typename Key>
    Key heldLongest (const std::vector<std::pair<Key, Frame>>& spans)
    {
        std::map<Key, Frame> total;
        for (const auto& [k, length] : spans)
            total[k] += length;
        return std::max_element (total.begin(), total.end(), [] (const auto& a, const auto& b) { return a.second < b.second; })->first;
    }

    // How long each feature lasts: until the next one, the last until `end`.
    std::vector<Frame> spans (const std::vector<vamphost::Feature>& features, Frame end)
    {
        std::vector<Frame> out;
        for (size_t i = 0; i < features.size(); ++i)
            out.push_back (std::max<Frame> (0, (i + 1 < features.size() ? features[i + 1].start : end) - features[i].start));
        return out;
    }
}

//==============================================================================
void MainWindow::registerAnalysisCommands()
{
    using KS = QKeySequence;
    const auto category = tr ("Analyze");
    auto add = [this, &category] (const char* id, const QString& text, QList<QKeySequence> keys, std::function<void()> fn)
    {
        addAction (commands.add (QString::fromLatin1 (id), category, text, keys, std::move (fn)));
    };

    add ("analyze.beats", tr ("Mark &Beats"), { KS (Qt::CTRL | Qt::ALT | Qt::Key_B) },
         [this] { markFeatures (QString::fromLatin1 (id::beats), tr ("Mark Beats")); });
    add ("analyze.onsets", tr ("Mark &Onsets"), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_O) },
         [this] { markFeatures (QString::fromLatin1 (id::onsets), tr ("Mark Onsets")); });
    add ("analyze.tempoKey", tr ("Detect &Tempo and Key..."), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_T) },
         [this] { detectTempoAndKey(); });
    add ("analyze.vamp", tr ("&Vamp Plugins..."), { KS (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_V) }, [this] { chooseVampPlugin(); });
    addAction (commands.add (QStringLiteral ("marker.exportSlices"), tr ("Markers"), tr ("Export &Slices at Markers..."), {},
                             [this] { exportSlices(); }));
    addAction (commands.add (QStringLiteral ("marker.exportLabels"), tr ("Markers"), tr ("Export Markers as &Labels..."), {},
                             [this] { exportLabels(); }));
}

void MainWindow::updateAnalysisCommandStates()
{
    const bool open = doc.isOpen() && ! loading;
    for (const auto* a : { "analyze.beats", "analyze.onsets", "analyze.vamp", "marker.exportSlices" })
        commands.action (QString::fromLatin1 (a))->setEnabled (open && ! doc.isAlbum());
    commands.action (QStringLiteral ("analyze.tempoKey"))->setEnabled (open && (! doc.isAlbum() || currentSong() >= 0));
    commands.action (QStringLiteral ("marker.exportLabels"))->setEnabled (open && ! doc.current().markers.empty());
}

bool MainWindow::runAnalysis (const QString& title, const AudioData& audio, Frame start, Frame end,
                              const QString& outputId, vamphost::Result& result)
{
    if (vamphost::find (outputId.toStdString()) == nullptr)
    {
        QMessageBox::information (this, title, missingPluginText (outputId.toStdString()));
        return false;
    }
    const auto ran = runWithProgress (title, [&] (std::atomic<float>& progress, const std::atomic<bool>& cancel)
    {
        result = vamphost::run (audio, start, end, outputId.toStdString(), &progress, &cancel);
    });
    waveform->setFocus();
    if (! ran)
        return false;
    if (! result.error.empty())
    {
        QMessageBox::warning (this, title, QString::fromStdString (result.error));
        return false;
    }
    return true;
}

void MainWindow::markFeatures (const QString& outputId, const QString& title)
{
    const auto d = data();
    if (d == nullptr || refuseAlbumEdit())
        return;
    const auto* output = vamphost::find (outputId.toStdString());
    const auto [s, e] = processRange();
    vamphost::Result result;
    if (! runAnalysis (title, *d, s, e, outputId, result))
        return;
    if (result.features.empty())
    {
        statusBar()->showMessage (tr ("%1 found nothing in %2").arg (title, hasSelection() ? tr ("the selection") : tr ("the file")), 6000);
        return;
    }
    if (result.features.size() > (size_t) manyResults
        && QMessageBox::question (this, title, tr ("This found %1 results. Add a marker for every one?").arg (result.features.size()))
               != QMessageBox::Yes)
        return;

    // Beat trackers label each beat with its place in the bar: 1 starts a bar.
    const bool beats = outputId == QLatin1String (id::beats);
    const auto outputName = QString::fromStdString (output->outputName);
    auto markers = doc.current().markers;
    int bar = 0, count = 0;
    for (const auto& f : result.features)
    {
        Marker m;
        m.id = doc.nextMarkerId();
        m.start = std::clamp<Frame> (f.start, 0, d->length());
        m.end = std::clamp<Frame> (f.start + f.duration, m.start, d->length());
        m.type = m.end > m.start ? Marker::Type::region : Marker::Type::generic;
        ++count;

        const auto label = QString::fromStdString (f.label).trimmed();
        QString name;
        if (beats)
        {
            const auto beat = std::max (1, label.toInt());
            if (beat == 1 || bar == 0)
                ++bar;
            name = beat == 1 ? tr ("Bar %1").arg (bar) : tr ("Beat %1.%2").arg (bar).arg (beat);
        }
        else if (outputId == QLatin1String (id::onsets))
            name = tr ("Onset %1").arg (count);
        else if (! label.isEmpty())
            name = label;
        else if (output->hasValue)
            name = QStringLiteral ("%1 %2").arg ((double) f.value, 0, 'g', 4).arg (QString::fromStdString (output->unit)).trimmed();
        else
            name = QStringLiteral ("%1 %2").arg (outputName).arg (count);
        m.name = name.toStdString();
        markers.push_back (std::move (m));
    }
    std::stable_sort (markers.begin(), markers.end(), [] (const Marker& a, const Marker& b) { return a.start < b.start; });
    commitMarkers (title, std::move (markers));
    statusBar()->showMessage (tr ("%1: added %2 markers. Undo removes them all.").arg (title).arg (result.features.size()), 6000);
}

//==============================================================================
void MainWindow::detectTempoAndKey()
{
    // In an album this is about the song under the cursor, and writes to that song.
    const auto* m = album();
    const int song = m != nullptr ? currentSong() : -1;
    AudioDataPtr audio;
    Frame s = 0, e = 0;
    if (m != nullptr)
    {
        if (song < 0 || m->clips[(size_t) song].source == nullptr)
            return;
        const auto& c = m->clips[(size_t) song];
        audio = c.source;
        s = c.sourceStart;
        e = c.sourceEnd;
    }
    else
    {
        audio = data();
        if (audio == nullptr)
            return;
        std::tie (s, e) = processRange();
    }

    const auto title = tr ("Detect Tempo and Key");
    for (const auto* needed : { id::tempo, id::key })
        if (vamphost::find (needed) == nullptr)
        {
            QMessageBox::information (this, title, missingPluginText (needed));
            return;
        }

    vamphost::Result tempo, key;
    const auto ran = runWithProgress (title, [&] (std::atomic<float>& progress, const std::atomic<bool>& cancel)
    {
        std::atomic<float> part { 0.0f };
        tempo = vamphost::run (*audio, s, e, id::tempo, &part, &cancel);
        progress = 0.5f;
        if (! cancel.load() && tempo.error.empty())
            key = vamphost::run (*audio, s, e, id::key, &part, &cancel);
        progress = 1.0f;
    });
    waveform->setFocus();
    if (! ran)
        return;
    if (! tempo.error.empty() || ! key.error.empty())
    {
        QMessageBox::warning (this, title, QString::fromStdString (tempo.error.empty() ? key.error : tempo.error));
        return;
    }

    double bpm = 0.0;
    if (! tempo.features.empty())
    {
        // The tempo held for most of the time; tempos are grouped to a tenth of a BPM.
        std::vector<std::pair<long, Frame>> held;
        const auto lengths = spans (tempo.features, e);
        for (size_t i = 0; i < tempo.features.size(); ++i)
            if (tempo.features[i].value > 0.0f)
                held.emplace_back (std::lround (tempo.features[i].value * 10.0), lengths[i]);
        if (! held.empty())
            bpm = (double) heldLongest (held) / 10.0;
    }
    QString keyName;
    if (! key.features.empty())
    {
        std::vector<std::pair<std::string, Frame>> held;
        const auto lengths = spans (key.features, e);
        for (size_t i = 0; i < key.features.size(); ++i)
            if (! key.features[i].label.empty())
                held.emplace_back (key.features[i].label, lengths[i]);
        if (! held.empty())
            keyName = QString::fromStdString (heldLongest (held));
    }
    if (bpm <= 0.0 && keyName.isEmpty())
    {
        QMessageBox::information (this, title, tr ("No steady tempo or key was found."));
        return;
    }

    const auto bpmTag = bpm > 0.0 ? QString::number (std::lround (bpm)) : QString();
    const auto keyTag = shortKey (keyName);
    const auto where = m != nullptr ? tr ("song %1, %2").arg (song + 1).arg (QString::fromStdString (m->clipName (song)))
                     : hasSelection() ? tr ("the selection") : tr ("the whole file");

    QDialog dialog (this);
    dialog.setWindowTitle (title);
    auto* layout = new QVBoxLayout (&dialog);
    auto* text = new QLabel (tr ("<p>In %1:</p><p><b>Tempo:</b> %2<br><b>Key:</b> %3</p>"
                                 "<p>Write to tags saves BPM %4 and key %5 with the file (FLAC BPM and INITIALKEY, the WAV acid tempo). "
                                 "Detection can be off by double or half the tempo, or pick the relative major or minor; "
                                 "you can correct both in Metadata (Ctrl+Alt+M).</p>")
                                 .arg (where.toHtmlEscaped(),
                                       bpm > 0.0 ? tr ("%1 BPM").arg (bpm, 0, 'f', 1) : tr ("not found"),
                                       keyName.isEmpty() ? tr ("not found") : keyName.toHtmlEscaped(),
                                       bpmTag.isEmpty() ? QStringLiteral ("-") : bpmTag,
                                       keyTag.isEmpty() ? QStringLiteral ("-") : keyTag.toHtmlEscaped()),
                             &dialog);
    text->setWordWrap (true);
    text->setTextInteractionFlags (Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    text->setFocusPolicy (Qt::StrongFocus);
    layout->addWidget (text);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Close, &dialog);
    auto* write = buttons->addButton (tr ("&Write to Tags"), QDialogButtonBox::AcceptRole);
    write->setDefault (true);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget (buttons);
    dialog.resize (460, dialog.sizeHint().height());
    write->setFocus();
    const auto accepted = dialog.exec() == QDialog::Accepted;
    waveform->setFocus();
    if (! accepted)
        return;

    auto apply = [&] (Tags& tags)
    {
        if (! bpmTag.isEmpty())
            tags[tag::bpm] = bpmTag.toStdString();
        if (! keyTag.isEmpty())
            tags[tag::key] = keyTag.toStdString();
    };
    if (m != nullptr)
    {
        auto changed = *m;
        apply (changed.clips[(size_t) song].tags);
        commitAlbum (tr ("Write Tempo and Key"), std::move (changed));
        statusBar()->showMessage (tr ("Tempo and key are written to song %1 when you export the album.").arg (song + 1), 6000);
    }
    else
    {
        auto state = doc.current();
        apply (state.tags);
        doc.apply (tr ("Write Tempo and Key").toStdString(), std::move (state));
        refreshFromDocument (true);
        statusBar()->showMessage (tr ("Tempo and key are in the tags; they are written when you save."), 6000);
    }
}

//==============================================================================
namespace
{
    // The plugin list: typing filters it, Up/Down move through it from the search field.
    class VampChooser final : public QDialog
    {
    public:
        explicit VampChooser (QWidget* parent) : QDialog (parent)
        {
            setWindowTitle (tr ("Vamp Plugins"));
            auto* layout = new QVBoxLayout (this);
            auto* label = new QLabel (tr ("&Search analysis plugins:"), this);
            search = new QLineEdit (this);
            label->setBuddy (search);
            list = new QListWidget (this);
            about = new QLabel (this);
            about->setWordWrap (true);
            about->setMinimumHeight (about->fontMetrics().lineSpacing() * 3);
            layout->addWidget (label);
            layout->addWidget (search);
            layout->addWidget (list, 1);
            layout->addWidget (about);
            auto* buttons = new QDialogButtonBox (QDialogButtonBox::Cancel, this);
            buttons->addButton (tr ("&Run"), QDialogButtonBox::AcceptRole)->setDefault (true);
            connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
            connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
            layout->addWidget (buttons);

            for (const auto& o : vamphost::outputs())
            {
                auto* item = new QListWidgetItem (QStringLiteral ("%1: %2").arg (QString::fromStdString (o.pluginName),
                                                                                 QString::fromStdString (o.outputName)), list);
                item->setData (Qt::UserRole, QString::fromStdString (o.id()));
                item->setToolTip (QString::fromStdString (o.maker));
            }
            connect (search, &QLineEdit::textChanged, this, [this] { filter(); });
            connect (list, &QListWidget::currentItemChanged, this, [this] { describe(); });
            connect (list, &QListWidget::itemActivated, this, &QDialog::accept);
            search->installEventFilter (this);
            filter();
            resize (560, 480);
        }

        QString chosen() const { return list->currentItem() != nullptr ? list->currentItem()->data (Qt::UserRole).toString() : QString(); }

    protected:
        bool eventFilter (QObject* o, QEvent* e) override
        {
            if (o == search && e->type() == QEvent::KeyPress)
            {
                const auto k = static_cast<QKeyEvent*> (e)->key();
                if (k == Qt::Key_Up || k == Qt::Key_Down || k == Qt::Key_PageUp || k == Qt::Key_PageDown)
                {
                    QApplication::sendEvent (list, e);
                    return true;
                }
            }
            return QDialog::eventFilter (o, e);
        }

    private:
        void filter()
        {
            const auto words = search->text().split (QLatin1Char (' '), Qt::SkipEmptyParts);
            QListWidgetItem* first = nullptr;
            for (int i = 0; i < list->count(); ++i)
            {
                auto* item = list->item (i);
                const auto* o = vamphost::find (item->data (Qt::UserRole).toString().toStdString());
                const auto haystack = item->text() + QLatin1Char (' ') + QString::fromStdString (o->maker + " " + o->category + " " + o->description);
                const bool shown = std::all_of (words.begin(), words.end(), [&] (const QString& w) { return haystack.contains (w, Qt::CaseInsensitive); });
                item->setHidden (! shown);
                if (shown && first == nullptr)
                    first = item;
            }
            if (list->currentItem() == nullptr || list->currentItem()->isHidden())
                list->setCurrentItem (first);
            describe();
        }

        void describe()
        {
            const auto* item = list->currentItem();
            const auto* o = item != nullptr && ! item->isHidden() ? vamphost::find (item->data (Qt::UserRole).toString().toStdString()) : nullptr;
            if (o == nullptr)
            {
                about->setText (list->count() == 0 ? tr ("No Vamp plugins were found.") : tr ("Nothing matches."));
                return;
            }
            auto description = QString::fromStdString (o->description).trimmed();
            if (! description.isEmpty() && ! description.endsWith (QLatin1Char ('.')))
                description += QLatin1Char ('.');
            about->setText (tr ("%1 Results become %2. By %3.")
                                .arg (description,
                                      o->hasDuration ? tr ("regions") : tr ("markers"),
                                      QString::fromStdString (o->maker)));
        }

        QLineEdit* search;
        QListWidget* list;
        QLabel* about;
    };
}

void MainWindow::chooseVampPlugin()
{
    if (data() == nullptr || refuseAlbumEdit())
        return;
    QApplication::setOverrideCursor (Qt::WaitCursor);
    const auto none = vamphost::outputs().empty();
    QApplication::restoreOverrideCursor();
    if (none)
    {
        QMessageBox::information (this, tr ("Vamp Plugins"), missingPluginText ("any"));
        return;
    }

    VampChooser chooser (this);
    const auto accepted = chooser.exec() == QDialog::Accepted;
    waveform->setFocus();
    if (accepted && ! chooser.chosen().isEmpty())
        runVampOutput (chooser.chosen());
}

void MainWindow::runVampOutput (const QString& outputId)
{
    const auto* o = vamphost::find (outputId.toStdString());
    if (o == nullptr)
        return;
    markFeatures (outputId, QStringLiteral ("%1: %2").arg (QString::fromStdString (o->pluginName), QString::fromStdString (o->outputName)));
}

//==============================================================================
void MainWindow::exportSlices()
{
    const auto d = data();
    if (d == nullptr || refuseAlbumEdit())
        return;

    // Every marker start and region edge in the range cuts it.
    const auto [s, e] = processRange();
    std::vector<Frame> cuts { s, e };
    for (const auto& m : doc.current().markers)
        for (const auto f : { m.start, m.end })
            if (f > s && f < e)
                cuts.push_back (f);
    std::sort (cuts.begin(), cuts.end());
    cuts.erase (std::unique (cuts.begin(), cuts.end()), cuts.end());
    const auto shortest = (Frame) (d->sampleRate() * 0.01);   // slivers under 10 ms are joined to the next slice
    std::vector<std::pair<Frame, Frame>> slices;
    for (size_t i = 0; i + 1 < cuts.size(); ++i)
    {
        if (! slices.empty() && slices.back().second - slices.back().first < shortest)
            slices.back().second = cuts[i + 1];
        else
            slices.emplace_back (cuts[i], cuts[i + 1]);
    }
    if (slices.size() < 2)
    {
        statusBar()->showMessage (tr ("Add markers first (Mark Beats is Ctrl+Alt+B, Mark Onsets Ctrl+Alt+Shift+O): slices are cut at markers."), 8000);
        return;
    }

    const auto source = QFileInfo (QString::fromStdString (doc.path()));
    const auto base = source.completeBaseName().isEmpty() ? tr ("Slice") : source.completeBaseName();
    auto extension = source.suffix().toLower();
    if (extension.isEmpty() || extension == QLatin1String ("mp3") || extension == QLatin1String ("ogg"))
        extension = QStringLiteral ("wav");
    const auto folder = QFileDialog::getExistingDirectory (this, tr ("Export %1 Slices To").arg (slices.size()),
                                                           source.exists() ? source.absolutePath() : QDir::homePath());
    waveform->setFocus();
    if (folder.isEmpty())
        return;

    const auto digits = std::max (2, (int) std::to_string (slices.size()).size());
    QStringList paths;
    for (size_t i = 0; i < slices.size(); ++i)
        paths << QDir (folder).filePath (QStringLiteral ("%1 %2.%3").arg (base).arg ((int) i + 1, digits, 10, QLatin1Char ('0')).arg (extension));
    const auto existing = (int) std::count_if (paths.begin(), paths.end(), [] (const QString& p) { return QFileInfo::exists (p); });
    if (existing > 0
        && QMessageBox::question (this, tr ("Export Slices"), tr ("%1 of these files already exist in %2. Replace them?").arg (existing).arg (folder))
               != QMessageBox::Yes)
        return;

    QString failure;
    const auto tags = doc.current().tags;
    const auto options = saveOptions;
    runWithProgress (tr ("Export Slices"), [&] (std::atomic<float>& progress, const std::atomic<bool>& cancel)
    {
        for (size_t i = 0; i < slices.size() && ! cancel.load(); ++i)
        {
            std::string error;
            if (! AudioEngine::saveFile (paths[(int) i].toStdString(), *d->slice (slices[i].first, slices[i].second), {}, tags, options, error))
            {
                failure = QStringLiteral ("%1: %2").arg (paths[(int) i], QString::fromStdString (error));
                return;
            }
            progress = (float) (i + 1) / (float) slices.size();
        }
    });
    waveform->setFocus();
    if (! failure.isEmpty())
        QMessageBox::warning (this, tr ("Export Slices"), failure);
    else
        statusBar()->showMessage (tr ("Exported %1 slices to %2").arg (slices.size()).arg (folder), 8000);
}

void MainWindow::exportLabels()
{
    const auto d = data();
    if (d == nullptr || doc.current().markers.empty())
        return;

    const auto source = QFileInfo (QString::fromStdString (doc.path()));
    const auto suggested = (source.exists() ? source.absolutePath() : QDir::homePath()) + QLatin1Char ('/')
                         + (source.completeBaseName().isEmpty() ? tr ("Markers") : source.completeBaseName()) + QStringLiteral (" labels.txt");
    const auto path = QFileDialog::getSaveFileName (this, tr ("Export Markers as Labels"), suggested,
                                                    tr ("Audacity and Sonic Visualiser labels (*.txt);;CSV (*.csv)"));
    waveform->setFocus();
    if (path.isEmpty())
        return;

    // Labels: start, end and name in seconds, tab separated. CSV: the same with a header.
    const bool csv = path.endsWith (QStringLiteral (".csv"), Qt::CaseInsensitive);
    QSaveFile file (path);
    if (! file.open (QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning (this, tr ("Export Markers as Labels"), file.errorString());
        return;
    }
    QTextStream out (&file);
    if (csv)
        out << "start,end,name\n";
    for (const auto& m : doc.current().markers)
    {
        auto name = QString::fromStdString (m.name);
        const auto start = QString::number ((double) m.start / d->sampleRate(), 'f', 6);
        const auto end = QString::number ((double) m.end / d->sampleRate(), 'f', 6);
        if (csv)
            out << start << ',' << end << ",\"" << name.replace (QLatin1Char ('"'), QStringLiteral ("\"\"")) << "\"\n";
        else
            out << start << '\t' << end << '\t' << name.replace (QLatin1Char ('\t'), QLatin1Char (' ')) << '\n';
    }
    out.flush();
    if (! file.commit())
        QMessageBox::warning (this, tr ("Export Markers as Labels"), file.errorString());
    else
        statusBar()->showMessage (tr ("Wrote %1 markers to %2").arg (doc.current().markers.size()).arg (path), 6000);
}
