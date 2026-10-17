// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"
#include "../engine/AudioCapture.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>

#include <memory>
#include <optional>
#include <vector>

class QComboBox;
class QLockFile;
class QLabel;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;

// Horizontal level bars, one per channel, with a peak hold and a clip light
// that stays lit until clicked.
class RecordMeter final : public QWidget
{
public:
    explicit RecordMeter (QWidget* parent);
    void setFormat (int numChannels, int sampleRate);
    void push (const float* interleaved, int frames);
    void fall (int frames);   // nothing arrived for this long: let the bars drop
    void resetClip();
    bool clipped() const;

protected:
    void paintEvent (QPaintEvent*) override;
    void mousePressEvent (QMouseEvent*) override;

private:
    struct Channel { float levelDb = -100.0f, holdDb = -100.0f; int holdFrames = 0; bool clip = false; };
    std::vector<Channel> channels;
    int rate = 48000;
};

// While recording, the take is also written to a WAV file in the cache folder
// with its header kept up to date, so a crash loses at most about a second.
class RecordingFile
{
public:
    bool open (int sampleRate, int numChannels);
    void write (const float* interleaved, int frames);
    void close (bool keep);
    QString path() const { return file.fileName(); }

    static QString folder();
    // Recordings left behind by a crash: repairs each header and moves the file to
    // the Music folder. Returns the new paths.
    static QStringList recoverAll();
    static int countLeftBehind();

private:
    void updateHeader();

    QFile file;
    std::shared_ptr<QLockFile> lock;   // tells other PHINYX windows this file is still being written
    int channels = 2;
    qint64 dataBytes = 0, lastHeaderBytes = 0, bytesPerSecond = 192000;
    bool full = false;
};

// Record from the desktop, an input or one application (Ctrl+R).
// Monitoring starts as soon as a source is picked, so the meters move and the
// pre-record buffer fills before Record is pressed.
class RecordDialog final : public QDialog
{
    Q_OBJECT

public:
    // `target` is the open file's audio (for inserting at the cursor), or null.
    RecordDialog (AudioCapture&, AudioDataPtr target, const QString& targetName, QWidget* parent);
    ~RecordDialog() override;

    AudioDataPtr take() const { return result; }
    bool insertAtCursor() const;

protected:
    void reject() override;

private:
    void refreshSources();
    std::optional<CaptureSource> chosenSource() const;
    int chosenRate() const;
    int chosenChannels() const;
    void restartMonitoring();
    void poll();
    void startRecording();
    void stopRecording();
    void updateStatus();
    void updateOptionState();

    AudioCapture& capture;
    AudioDataPtr target;
    QTreeWidget* sourceList;
    QComboBox* rateBox;
    QComboBox* channelBox;
    QSpinBox* preRecord;
    QRadioButton* toNewTab;
    QRadioButton* toCursor;
    RecordMeter* meter;
    QLabel* status;
    QLabel* timeLabel;
    QPushButton* recordButton;

    QTimer timer;
    QElapsedTimer sinceData;   // how long nothing has arrived, for the meter's fall
    uint64_t shownVersion = ~uint64_t (0);
    bool recording = false;
    std::vector<float> block;                 // interleaved, from AudioCapture::read
    std::vector<float> preRoll;               // ring of the last preRecord seconds, interleaved
    size_t preRollPos = 0, preRollFilled = 0;
    std::optional<AudioData::Builder> builder;
    std::vector<std::vector<float>> split;    // block split into channels for the builder
    Frame recordedFrames = 0;
    uint64_t droppedAtStart = 0;
    RecordingFile safety;
    QString captureError;
    QString recordingName;
    AudioDataPtr result;
};
