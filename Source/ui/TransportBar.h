// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"

#include <QToolButton>
#include <QWidget>

class CommandRegistry;

// The bar across the top of the window, after Audacity's transport toolbar:
// Go to Start, Play/Pause, Stop, Go to End, Record and Loop, then a large time
// display. Each button runs the same command as its menu item and keyboard
// shortcut, and hovering it names that shortcut. Drawn in the theme's colours.
class TransportBar final : public QWidget
{
    Q_OBJECT

public:
    TransportBar (CommandRegistry&, QWidget* parent);

    // Called on every UI tick.
    void setState (Frame position, double sampleRate, bool playing, bool looping);

private:
    CommandRegistry& commands;
    class TransportButton* playButton = nullptr;
    class TransportButton* loopButton = nullptr;
    class TimeDisplay* time = nullptr;
};

// One round button drawing its own symbol.
class TransportButton final : public QToolButton
{
    Q_OBJECT

public:
    enum class Symbol { start, play, pause, stop, end, record, loop };

    TransportButton (Symbol, const QString& commandId, CommandRegistry&, QWidget* parent);
    void setSymbol (Symbol);

protected:
    bool event (QEvent*) override;
    void paintEvent (QPaintEvent*) override;

private:
    Symbol symbol;
    QString commandId;
    CommandRegistry& commands;
};

// Large read-out of the playback position. Click it to choose the format.
class TimeDisplay final : public QWidget
{
    Q_OBJECT

public:
    enum class Format { hms, seconds, samples, cdFrames };

    explicit TimeDisplay (QWidget* parent);
    void setPosition (Frame, double sampleRate);
    QString text() const;

    QSize sizeHint() const override;

protected:
    void paintEvent (QPaintEvent*) override;
    void mousePressEvent (QMouseEvent*) override;
    void keyPressEvent (QKeyEvent*) override;

private:
    void chooseFormat();

    Format format = Format::hms;
    Frame position = 0;
    double rate = 48000.0;
};
