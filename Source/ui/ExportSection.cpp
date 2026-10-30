// SPDX-License-Identifier: AGPL-3.0-only
// The Export section of MainWindow: File > Export writes every output of the
// Export dialog for the whole file, the selection, each region or each part
// between markers, optionally through the Master Section. Preview hears an
// output encoded in place of your audio (with the A/B reference engine), or
// only what the codec removes.
#include "ExportDialog.h"
#include "MainWindow.h"
#include "Theme.h"

#include "../core/Dsp.h"
#include "../engine/Codec.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QStandardPaths>
#include <QPushButton>
#include <QStatusBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace
{
    QString decibels (double db)
    {
        return db <= -199.0 ? QStringLiteral ("-inf") : QString::number (db, 'f', 1);
    }

    // How far `decoded` lags `original` (encoder delay left after decoding), by
    // correlating a few seconds of their mono sums.
    Frame findLag (const AudioData& original, const AudioData& decoded, Frame maxLag)
    {
        const auto window = (Frame) std::min<Frame> (16384, original.length() / 2);
        if (window < 1024 || decoded.length() < window + maxLag)
            return 0;
        const auto at = std::max<Frame> (0, std::min (original.length() / 2, original.length() - window));

        auto mono = [] (const AudioData& a, Frame start, Frame n)
        {
            std::vector<std::vector<float>> buf ((size_t) a.numChannels(), std::vector<float> ((size_t) n));
            std::vector<float*> ptr;
            for (auto& b : buf)
                ptr.push_back (b.data());
            a.read (start, (int) n, ptr.data());
            std::vector<float> sum ((size_t) n);
            for (const auto& b : buf)
                for (size_t i = 0; i < sum.size(); ++i)
                    sum[i] += b[i];
            return sum;
        };

        const auto a = mono (original, at, window);
        const auto from = std::max<Frame> (0, at - maxLag);
        const auto b = mono (decoded, from, std::min (decoded.length() - from, window + 2 * maxLag));
        double best = 0.0;
        Frame lag = 0;
        for (Frame l = -maxLag; l <= maxLag; ++l)
        {
            const auto offset = at + l - from;
            if (offset < 0 || offset + window > (Frame) b.size())
                continue;
            double c = 0.0;
            for (Frame i = 0; i < window; ++i)
                c += (double) a[(size_t) i] * b[(size_t) (offset + i)];
            if (c > best)
            {
                best = c;
                lag = l;
            }
        }
        return lag;
    }

    // `decoded` in the shape of `original` (rate, channels, length, aligned), or
    // the original minus it when `difference`.
    AudioDataPtr matchShape (const AudioData& original, AudioDataPtr decoded, bool difference)
    {
        if (std::abs (decoded->sampleRate() - original.sampleRate()) > 1.0e-6)
            decoded = dsp::resample (*decoded, original.sampleRate());
        const auto lag = std::max<Frame> (0, findLag (original, *decoded, 4096));

        const auto channels = original.numChannels();
        const auto length = original.length();
        AudioData::Builder out (channels, original.sampleRate());
        constexpr int block = 8192;
        std::vector<std::vector<float>> a ((size_t) channels, std::vector<float> (block)),
                                        b ((size_t) decoded->numChannels(), std::vector<float> (block));
        std::vector<float*> pa, pb;
        for (auto& v : a)
            pa.push_back (v.data());
        for (auto& v : b)
            pb.push_back (v.data());
        std::vector<std::vector<float>> mixed ((size_t) channels, std::vector<float> (block));
        std::vector<const float*> pm;
        for (auto& v : mixed)
            pm.push_back (v.data());

        for (Frame pos = 0; pos < length; pos += block)
        {
            const auto n = (int) std::min<Frame> (block, length - pos);
            original.read (pos, n, pa.data());
            for (auto& v : b)
                std::fill (v.begin(), v.end(), 0.0f);
            const auto have = std::clamp<Frame> (decoded->length() - (pos + lag), 0, n);
            if (have > 0)
                decoded->read (pos + lag, (int) have, pb.data());
            for (int c = 0; c < channels; ++c)
            {
                // A mono encoding of stereo audio is heard on every channel.
                const auto& src = b[(size_t) std::min (c, decoded->numChannels() - 1)];
                for (int i = 0; i < n; ++i)
                    mixed[(size_t) c][(size_t) i] = difference ? a[(size_t) c][(size_t) i] - src[(size_t) i] : src[(size_t) i];
            }
            out.append (pm.data(), n);
        }
        return out.finish();
    }
}

//==============================================================================
void MainWindow::exportAudio()
{
    const auto d = data();
    if (d == nullptr)
        return;

    const auto& markers = doc.current().markers;
    ExportDialog::Context context;
    context.hasSelection = hasSelection();
    context.masterActive = engine.masterIsActive();
    context.regions = (int) std::count_if (markers.begin(), markers.end(), [] (const Marker& m) { return m.isRegion(); });
    context.markers = (int) std::count_if (markers.begin(), markers.end(), [] (const Marker& m) { return ! m.isRegion(); });
    context.sampleRate = d->sampleRate();
    context.channels = d->numChannels();
    context.sourcePath = QString::fromStdString (doc.path());
    context.tags = doc.current().tags;

    engine.stop();
    ExportDialog dialog (context, this);
    connect (&dialog, &ExportDialog::previewRequested, this, [this, &dialog] (const exporter::Output& o, bool difference)
    {
        previewExport (o, difference, dialog.throughMaster());
    });
    connect (&dialog, &ExportDialog::previewStopped, this, [this] { stopExportPreview(); });
    connect (&dialog, &ExportDialog::abRequested, this, [this]
    {
        if (previewName.isEmpty())
            return;
        engine.setHearingReference (! engine.isHearingReference());
        statusBar()->showMessage (engine.isHearingReference() ? tr ("Hearing the encoded preview") : tr ("Hearing your audio"), 3000);
        placeReferenceBadge();
    });
    const auto accepted = dialog.exec() == QDialog::Accepted;
    stopExportPreview();
    waveform->setFocus();
    if (! accepted)
        return;

    const auto outputs = dialog.outputs();
    ExportDialog::remember (outputs);
    if (outputs.empty())
        return;

    // The parts to write: each with its range and name.
    struct Part { Frame start, end; std::string name; };
    std::vector<Part> parts;
    switch (dialog.range())
    {
        case ExportDialog::Range::selection:
            if (hasSelection())
                parts.push_back ({ selStart, selEnd, {} });
            break;
        case ExportDialog::Range::regions:
            for (const auto& m : markers)
                if (m.isRegion() && m.end > m.start)
                    parts.push_back ({ m.start, m.end, m.name.empty() ? tr ("Region %1").arg (parts.size() + 1).toStdString() : m.name });
            break;
        case ExportDialog::Range::markers:
        {
            // From each point marker to the next (and from the start to the first).
            std::vector<const Marker*> points;
            for (const auto& m : markers)
                if (! m.isRegion() && m.start > 0 && m.start < d->length())
                    points.push_back (&m);
            std::sort (points.begin(), points.end(), [] (auto* a, auto* b) { return a->start < b->start; });
            Frame from = 0;
            std::string name;
            for (size_t i = 0; i <= points.size(); ++i)
            {
                const auto to = i < points.size() ? points[i]->start : d->length();
                if (to - from > (Frame) (d->sampleRate() * 0.01))
                    parts.push_back ({ from, to, name.empty() ? tr ("Part %1").arg (parts.size() + 1).toStdString() : name });
                if (i < points.size())
                {
                    from = to;
                    name = points[i]->name;
                }
            }
            break;
        }
        case ExportDialog::Range::whole:
            break;
    }
    if (parts.empty())
        parts.push_back ({ 0, d->length(), {} });
    const bool numbered = parts.size() > 1;

    // Through the Master Section: render the span once and export from that.
    AudioDataPtr source = d;
    Frame offset = 0;
    if (dialog.throughMaster() && engine.masterIsActive())
    {
        Frame lo = d->length(), hi = 0;
        for (const auto& p : parts)
        {
            lo = std::min (lo, p.start);
            hi = std::max (hi, p.end);
        }
        std::string error;
        source = renderWithProgress (lo, hi, error, false);   // each output dithers to its own depth last
        if (source == nullptr)
        {
            if (error != "Cancelled")
                QMessageBox::warning (this, tr ("Export"), QString::fromStdString (error));
            return;
        }
        offset = lo;
    }

    std::vector<exporter::Source> jobs;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        exporter::Source s;
        s.data = source;
        s.start = parts[i].start - offset;
        s.end = std::min (parts[i].end - offset, source->length());
        s.markers = markers;
        for (auto& m : s.markers)
        {
            m.start -= offset;
            m.end -= offset;
        }
        s.tags = doc.current().tags;
        s.layout = doc.current().layout;
        s.sourcePath = doc.path();
        s.part = parts[i].name;
        s.index = numbered ? (int) i + 1 : 0;
        jobs.push_back (std::move (s));
    }

    std::vector<exporter::Result> results;
    const auto total = (float) (jobs.size() * outputs.size());
    runWithProgress (tr ("Export"), [&] (std::atomic<float>& progress, const std::atomic<bool>& cancel)
    {
        std::atomic<float> one { 0.0f };
        for (const auto& job : jobs)
            for (const auto& o : outputs)
            {
                if (cancel.load())
                    return;
                const auto done = (float) results.size();
                results.push_back (exporter::run (job, o, total == 1.0f ? &progress : &one, &cancel));
                progress = (done + 1.0f) / total;
            }
    });
    waveform->setFocus();

    // The summary: every file with its loudness and true peak, and any problem.
    int written = 0, skipped = 0, problems = 0;
    QDialog summary (this);
    summary.setWindowTitle (tr ("Export"));
    summary.resize (820, 360);
    auto* table = new QTreeWidget (&summary);
    table->setRootIsDecorated (false);
    table->setHeaderLabels ({ tr ("File"), tr ("Loudness"), tr ("True peak"), tr ("After encoding"), tr ("Note") });
    QString lastFolder;
    for (const auto& r : results)
    {
        const QFileInfo file (QString::fromStdString (r.path));
        auto* item = new QTreeWidgetItem (table);
        item->setText (0, file.fileName().isEmpty() ? tr ("(no file)") : file.fileName());
        item->setToolTip (0, file.absoluteFilePath());
        if (r.skipped)
        {
            ++skipped;
            item->setText (4, tr ("Skipped: it already exists"));
            continue;
        }
        if (! r.ok)
        {
            ++problems;
            item->setText (4, QString::fromStdString (r.error));
            item->setForeground (4, Theme::current().color (QStringLiteral ("meterHigh")));
            continue;
        }
        ++written;
        lastFolder = file.absolutePath();
        item->setText (1, tr ("%1 LUFS").arg (decibels (r.lufs)));
        item->setText (2, tr ("%1 dBTP").arg (decibels (r.truePeakDb)));
        if (r.encodedTruePeakDb > -199.0)
            item->setText (3, tr ("%1 dBTP").arg (decibels (r.encodedTruePeakDb)));
        if (! r.warning.empty())
        {
            ++problems;
            item->setText (4, QString::fromStdString (r.warning));
            item->setForeground (4, Theme::current().color (QStringLiteral ("meterMid")));
        }
    }
    for (int c = 0; c < 4; ++c)
        table->resizeColumnToContents (c);

    const bool cancelled = results.size() < jobs.size() * outputs.size();
    auto text = cancelled ? tr ("Cancelled after %n file(s).", nullptr, written) : tr ("Wrote %n file(s).", nullptr, written);
    if (skipped > 0)
        text += QLatin1Char (' ') + tr ("Skipped %n that already existed.", nullptr, skipped);
    if (problems > 0)
        text += QLatin1Char (' ') + tr ("See the notes for %n of them.", nullptr, problems);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok, &summary);
    if (! lastFolder.isEmpty())
    {
        auto* open = buttons->addButton (tr ("Open &Folder"), QDialogButtonBox::ActionRole);
        connect (open, &QPushButton::clicked, &summary, [lastFolder] { QDesktopServices::openUrl (QUrl::fromLocalFile (lastFolder)); });
    }
    connect (buttons, &QDialogButtonBox::accepted, &summary, &QDialog::accept);
    auto* layout = new QVBoxLayout (&summary);
    layout->addWidget (new QLabel (text, &summary));
    layout->addWidget (table, 1);
    layout->addWidget (buttons);
    buttons->button (QDialogButtonBox::Ok)->setFocus();
    summary.exec();
    statusBar()->showMessage (tr ("Exported %n file(s)", nullptr, written), 6000);
    waveform->setFocus();
}

//==============================================================================
void MainWindow::previewExport (const exporter::Output& output, bool difference, bool throughMaster)
{
    const auto d = data();
    if (d == nullptr)
        return;

    // The selection, or 20 seconds from the cursor.
    Frame s = selStart, e = selEnd;
    if (! hasSelection())
    {
        const auto span = std::min<Frame> (d->length(), (Frame) (d->sampleRate() * 20.0));
        s = std::clamp<Frame> (cursor, 0, d->length() - span);
        e = s + span;
    }
    if (e - s < (Frame) (d->sampleRate() * 0.1))
        return;

    engine.stop();
    const bool rendered = throughMaster && engine.masterIsActive();
    std::string error;
    AudioDataPtr original = rendered ? renderWithProgress (s, e, error, false) : d->slice (s, e);
    if (original == nullptr)
    {
        if (error != "Cancelled")
            QMessageBox::warning (this, tr ("Preview"), QString::fromStdString (error));
        return;
    }

    // Heard at the original's level, so the comparison is about the codec only.
    auto o = output;
    o.normalize = false;
    o.folder = (QStandardPaths::writableLocation (QStandardPaths::CacheLocation) + QStringLiteral ("/preview")).toStdString();
    o.pattern = "preview";
    o.overwrite = exporter::Output::Overwrite::replace;
    o.schemas = tagio::Options {};
    o.schemas.riff = o.schemas.bext = o.schemas.cart = o.schemas.ixml = o.schemas.id3 = o.schemas.id3v1 = false;
    o.schemas.vorbis = o.schemas.mp4 = o.schemas.ape = o.schemas.xmp = o.schemas.chapters = false;
    o.templateText.clear();
    o.fillLoudness = o.checkPeaks = false;
    QDir().mkpath (QString::fromStdString (o.folder));

    exporter::Source source;
    source.data = original;
    source.start = 0;
    source.end = original->length();
    source.sourcePath = doc.path();

    exporter::Result result;
    AudioDataPtr heard;
    runWithProgress (tr ("Preview"), [&] (std::atomic<float>& progress, const std::atomic<bool>& cancel)
    {
        result = exporter::run (source, o, &progress, &cancel);
        if (! result.ok || cancel.load())
            return;
        std::string decodeError;
        if (auto decoded = codec::decode (result.path, decodeError, nullptr, &cancel))
            heard = matchShape (*original, decoded, difference);
        else
            result.error = decodeError;
        QFile::remove (QString::fromStdString (result.path));
    });
    if (heard == nullptr)
    {
        if (! result.error.empty() && result.error != "Cancelled")
            QMessageBox::warning (this, tr ("Preview"), QString::fromStdString (result.error));
        return;
    }

    // In place of your audio, at the same position; the original plays as A.
    const auto* format = codec::find (output.formatId);
    previewName = (difference ? tr ("Removed by %1") : tr ("B  %1"))
                      .arg (QString::fromStdString (output.name.empty() && format != nullptr ? format->name : output.name));
    previewRange = { s, e };
    engine.setReference (d->replaced (s, e, heard.get()), 1.0f);
    // Rendered audio is already mastered; a difference is heard as it is.
    engine.setReferenceBypassesMaster (rendered || difference);
    engine.setHearingReference (true);
    engine.setLoop (s, e);
    engine.setPositionFrames (s);
    engine.play();
    placeReferenceBadge();
    statusBar()->showMessage (difference ? tr ("Hearing only what the codec removes. A switches to your audio.")
                                         : tr ("Hearing the encoded preview. A switches to your audio."), 5000);
}

void MainWindow::stopExportPreview()
{
    if (previewName.isEmpty())
        return;
    engine.stop();
    previewName.clear();
    previewRange = { 0, 0 };
    engine.setHearingReference (false);
    engine.setReferenceBypassesMaster (referenceClean);
    if (currentReference >= 0 && references[(size_t) currentReference].atRate != nullptr)
        engine.setReference (references[(size_t) currentReference].atRate, referenceGain (currentReference));
    else
        engine.setReference (nullptr, 1.0f);
    placeReferenceBadge();
}
