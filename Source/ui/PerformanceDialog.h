// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../engine/AudioCapture.h"
#include "../performance/Cameras.h"
#include "../performance/Recorder.h"

#include <QDialog>
#include <QTimer>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;

// The Performance menu's settings: the recording (resolution, frame rate, encoder, quality,
// folder), the sound (AAC rate, PHINYX's own channels, microphones), the cameras and the
// live stream. Kept in settings.ini under [performance].
namespace perfui
{
    perf::Settings loadSettings();
    void saveSettings (const perf::Settings&);
    QString defaultFolder();

    struct Service { QString name, server; };
    const QList<Service>& services();   // YouTube, Twitch... and Custom (empty server) last
}

class PerformanceDialog final : public QDialog
{
    Q_OBJECT

public:
    // `locked`: recording or streaming now, so only the stream's own settings can change.
    PerformanceDialog (const perf::Settings&, bool locked, QWidget* parent);
    perf::Settings settings() const;

private:
    QWidget* buildRecording();
    QWidget* buildSound();
    QWidget* buildCameras();
    QWidget* buildStreaming();
    std::vector<perf::InputChoice> chosenInputs() const;
    std::vector<perf::CameraChoice> chosenCameras() const;
    void refreshInputs();
    void refreshCameras();
    void updateBitrateNote();
    void serviceChosen();

    perf::Settings initial;
    bool locked = false;

    QComboBox *resolution = nullptr, *frameRate = nullptr, *encoder = nullptr, *quality = nullptr;
    QLabel* bitrateNote = nullptr;
    QLineEdit* folder = nullptr;
    QCheckBox *crashSafe = nullptr, *followWindow = nullptr;

    QComboBox* audioRate = nullptr;
    QCheckBox* programTrack = nullptr;
    QTableWidget* inputTable = nullptr;
    QLabel* inputNote = nullptr;
    AudioCapture capture;
    uint64_t inputsVersion = ~(uint64_t) 0;
    std::vector<CaptureSource> inputSources;
    QTimer inputPoll;

    QTableWidget* cameraTable = nullptr;
    QLabel* cameraNote = nullptr;
    std::vector<perf::CameraDevice> cameraDevices;

    QComboBox *service = nullptr, *streamResolution = nullptr, *streamCodec = nullptr, *streamAudio = nullptr;
    QLineEdit *server = nullptr, *key = nullptr;
    QSpinBox* streamBitrate = nullptr;
};
