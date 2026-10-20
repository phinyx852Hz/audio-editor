// SPDX-License-Identifier: AGPL-3.0-only
// Auditioning files from the Find panel: decoded in the background, then played
// as the engine's live source (after the Master Section, like the Tracker).
#include "MainWindow.h"
#include "FindPanel.h"
#include "../tracker/TrackerEngine.h"

#include <QtConcurrent>

struct MainWindow::AuditionVoice
{
    AudioDataPtr data;   // at the device's rate
    std::atomic<Frame> position { 0 };
    std::atomic<bool> finished { false };
};

void MainWindow::auditionFile (const QString& path)
{
    stopAudition();
    const auto request = ++auditionRequest;
    const auto deviceRate = engine.deviceSampleRate() > 0.0 ? engine.deviceSampleRate() : 48000.0;
    auto* watcher = new QFutureWatcher<AudioDataPtr> (this);
    connect (watcher, &QFutureWatcher<AudioDataPtr>::finished, this, [this, watcher, request]
    {
        watcher->deleteLater();
        auto data = watcher->result();
        if (request != auditionRequest || data == nullptr || data->length() == 0)
        {
            if (request == auditionRequest)
                findWidget->auditionEnded();
            return;
        }
        if (tracker != nullptr && tracker->isPlaying())
            trackerStop();
        auto voice = std::make_shared<AuditionVoice>();
        voice->data = std::move (data);
        auditionVoice = voice;
        engine.setLiveSource ([voice] (float* left, float* right, int frames)
        {
            const auto& d = *voice->data;
            const auto at = voice->position.load();
            if (d.numChannels() == 1)
            {
                float* dest[] { left };
                d.read (at, frames, dest);
                std::copy (left, left + frames, right);
            }
            else
            {
                // The first two channels (left and right of surround).
                thread_local std::vector<float*> dest;
                thread_local std::vector<float> spare;
                dest.assign ((size_t) d.numChannels(), nullptr);
                spare.resize ((size_t) frames * (size_t) std::max (0, d.numChannels() - 2));
                dest[0] = left;
                dest[1] = right;
                for (size_t c = 2; c < dest.size(); ++c)
                    dest[c] = spare.data() + (c - 2) * (size_t) frames;
                d.read (at, frames, dest.data());
            }
            voice->position = at + frames;
            if (at + frames >= d.length())
                voice->finished = true;
        });
        auditionWatch.start (100);
    });
    watcher->setFuture (QtConcurrent::run ([path, deviceRate]
    {
        std::vector<Marker> markers;
        Tags tags;
        std::string error;
        auto data = AudioEngine::loadFile (path.toStdString(), markers, tags, error);
        if (data != nullptr && std::abs (data->sampleRate() - deviceRate) > 0.5)
            data = dsp::resample (*data, deviceRate);
        return data;
    }));
}

void MainWindow::stopAudition()
{
    ++auditionRequest;
    auditionWatch.stop();
    if (auditionVoice != nullptr)
    {
        auditionVoice.reset();
        if (engine.hasLiveSource() && ! (tracker != nullptr && tracker->isPlaying()))
            engine.setLiveSource ({});
    }
}

void MainWindow::auditionTick()
{
    // Played to the end, or Space or the Tracker took the sound.
    if (auditionVoice != nullptr && ! auditionVoice->finished && engine.hasLiveSource())
        return;
    stopAudition();
    findWidget->auditionEnded();
}
