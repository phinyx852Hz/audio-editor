// SPDX-License-Identifier: AGPL-3.0-only
#include "WebBackdrop.h"
#include "Theme.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPushButton>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

namespace
{
    // The window's own colour over the page: the opacity is how much of PHINYX lies over it,
    // as with the Visualizer behind PHINYX.
    class Tint final : public QWidget
    {
    public:
        explicit Tint (QWidget* parent) : QWidget (parent)
        {
            setAttribute (Qt::WA_TransparentForMouseEvents);
            setAttribute (Qt::WA_TranslucentBackground);
        }

    protected:
        void paintEvent (QPaintEvent*) override
        {
            auto tint = palette().color (QPalette::Window);
            tint.setAlphaF ((float) Theme::opacity());
            QPainter (this).fillRect (rect(), tint);
        }
    };

    QWebEngineProfile* profile()
    {
        // A named profile keeps cookies and logins between runs (the default one forgets them).
        static auto* p = new QWebEngineProfile (QStringLiteral (APP_ID));
        return p;
    }
}

WebBackdrop::WebBackdrop (QWidget* window) : QWidget (window)
{
    view = new QWebEngineView (this);
    view->setPage (new QWebEnginePage (profile(), view));
    view->page()->setAudioMuted (true);
    tint = new Tint (this);

    // A button rather than a hint: the page covers the menus while it is in front.
    back = new QPushButton (tr ("Back to PHINYX (Esc)"), this);
    back->setCursor (Qt::PointingHandCursor);
    back->setFocusPolicy (Qt::NoFocus);
    back->setStyleSheet (QStringLiteral ("QPushButton { background: rgba(0,0,0,170); color: white; padding: 6px 14px; border: none; border-radius: 6px; }"
                                         "QPushButton:hover { background: rgba(0,0,0,220); }"));
    back->adjustSize();
    back->hide();
    connect (back, &QPushButton::clicked, this, [this] { setInteractive (false); });

    window->installEventFilter (this);
    place();
    setInteractive (false);
}

void WebBackdrop::load (const QUrl& url)
{
    view->load (url);
}

QUrl WebBackdrop::url() const
{
    return view->url();
}

void WebBackdrop::setMuted (bool muted)
{
    view->page()->setAudioMuted (muted);
}

void WebBackdrop::setInteractive (bool on)
{
    interactive = on;
    setAttribute (Qt::WA_TransparentForMouseEvents, ! on);
    view->setAttribute (Qt::WA_TransparentForMouseEvents, ! on);
    view->setFocusPolicy (on ? Qt::StrongFocus : Qt::NoFocus);
    tint->setVisible (! on);
    back->setVisible (on);
    // The page takes the keys before any shortcut would see them, so Esc is caught on its way in.
    if (on)
    {
        qApp->installEventFilter (this);
        raise();
        back->move ((width() - back->width()) / 2, 12);
        back->raise();
        view->setFocus();
    }
    else
    {
        qApp->removeEventFilter (this);
        lower();
    }
    Q_EMIT interactiveChanged (on);
}

void WebBackdrop::place()
{
    if (parentWidget() == nullptr)
        return;
    setGeometry (parentWidget()->rect());
    view->setGeometry (rect());
    tint->setGeometry (rect());
    back->move ((width() - back->width()) / 2, 12);
    if (interactive)
        raise();
    else
        lower();
}

bool WebBackdrop::eventFilter (QObject* watched, QEvent* e)
{
    if (interactive && (e->type() == QEvent::ShortcutOverride || e->type() == QEvent::KeyPress)
        && static_cast<QKeyEvent*> (e)->key() == Qt::Key_Escape && isAncestorOf (qobject_cast<QWidget*> (watched)))
    {
        if (e->type() == QEvent::KeyPress)
            setInteractive (false);
        e->accept();
        return true;
    }
    if (watched == parentWidget() && (e->type() == QEvent::Resize || e->type() == QEvent::ChildAdded))
        place();
    else if (watched == parentWidget() && e->type() == QEvent::PaletteChange)
        tint->update();
    return false;
}
