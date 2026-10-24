// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QUrl>
#include <QWidget>

class QPushButton;
class QWebEngineView;

// A web page as the background of the whole PHI window, like the Visualizer behind
// PHI (Visualizer menu): seen through the window as far as its opacity allows, and
// recorded with it by Performance > Record Video. Built only with Qt WebEngine
// (PHI_WEB_BACKDROP). Its cookies and logins are kept in PHI's own browser profile.
class WebBackdrop final : public QWidget
{
    Q_OBJECT

public:
    explicit WebBackdrop (QWidget* window);

    void load (const QUrl&);
    QUrl url() const;

    // In front of PHI, taking the mouse and keys (to log in, start a video...); Esc or
    // the button at the top goes back.
    void setInteractive (bool);
    bool isInteractive() const { return interactive; }
    void setMuted (bool);

Q_SIGNALS:
    void interactiveChanged (bool);

protected:
    bool eventFilter (QObject*, QEvent*) override;   // follows the window's size; Esc while in front

private:
    void place();

    QWebEngineView* view = nullptr;
    QWidget* tint = nullptr;
    QPushButton* back = nullptr;
    bool interactive = false;
};
