// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/AudioData.h"

#include <QToolButton>
#include <QWidget>

class CommandRegistry;

// The bar across the top of the editor: Go to Start, Play/Pause, Stop, Go to
// End, Record and Loop, then the playback time. Each button runs the same
// command as its menu item and keyboard shortcut, and hovering it names that
// shortcut. Flat symbols in the theme's text colour, with no background.
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

// One flat button drawing its own symbol.
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

// The playback position as 00:00:00.000 (hours, minutes, seconds,
// milliseconds), in the interface font with steady digits.
class TimeDisplay final : public QWidget
{
    Q_OBJECT

public:
    explicit TimeDisplay (QWidget* parent);
    void setPosition (Frame, double sampleRate);
    QString text() const;

    QSize sizeHint() const override;

protected:
    void paintEvent (QPaintEvent*) override;

private:
    QFont displayFont() const;

    Frame position = 0;
    double rate = 48000.0;
};
