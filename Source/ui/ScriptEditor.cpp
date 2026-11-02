// SPDX-License-Identifier: AGPL-3.0-only
// Tools > Scripts > Script Editor (see ScriptEditor.h, docs/scripting.md).
#include "ScriptEditor.h"
#include "../script/ReaperImport.h"

#include <QAction>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QStandardPaths>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextBrowser>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
    // Scripts PHI puts in a new scripts folder, to start from.
    struct Example { const char* file; const char* code; };
    const Example examples[] {
        { "Markers every few seconds.lua", R"LUA(-- Adds a marker every few seconds through the selection (or the whole file).
if not phi.is_open() then
    phi.message("Open a file first.")
    return
end

local every = phi.ask_number("Seconds between markers:", 10, 0.1, 3600, 2)
if not every then return end

local a, b = phi.selection()
if a == b then a, b = 0, phi.duration() end

local n = 0
for t = a, b, every do
    n = n + 1
    phi.add_marker(t, ("Marker %d"):format(n))
end
phi.set_undo_name(("Add %d markers"):format(n))
print(("Added %d markers"):format(n))
)LUA" },
        { "Fade out the last two seconds.lua", R"LUA(-- Fades out the last two seconds of every channel, sample by sample:
-- an example of reading and writing samples.
if not phi.is_open() then
    phi.message("Open a file first.")
    return
end

local total = phi.frames()
local length = math.min(phi.to_frames(2), total)
local start = total - length
for ch = 0, phi.channels() - 1 do
    local s = phi.get_samples(ch, start, length)
    for i = 1, #s do
        s[i] = s[i] * (1 - (i - 1) / length)
    end
    phi.set_samples(ch, start, s)
end
phi.set_undo_name("Fade out the last two seconds")
)LUA" },
        { "Reset the mixer.lua", R"LUA(-- Puts every mixer track back to 0 dB, centred, neither muted nor soloed.
for i = 0, phi.track_count() - 1 do
    phi.set_track(i, { volume = 0, pan = 0, mute = false, solo = false })
    print(phi.track(i).name .. ": reset")
end
)LUA" },
        { "Gain the selection.py", R"PY(# Changes the level of the selection (or the whole file) by some dB, a block
# at a time: an example of sample processing in Python.
if not phi.is_open():
    phi.message("Open a file first.")
    raise SystemExit

db = phi.ask_number("Gain in dB:", -3, -48, 24, 1)
if db is not None:
    factor = 10 ** (db / 20)
    a, b = phi.selection()
    if a == b:
        a, b = 0, phi.duration()
    start, end = phi.to_frames(a), phi.to_frames(b)
    block = 65536
    for ch in range(phi.channels()):
        for pos in range(start, end, block):
            count = min(block, end - pos)
            samples = phi.get_samples(ch, pos, count)
            phi.set_samples(ch, pos, [x * factor for x in samples])
    phi.set_undo_name(f"Gain {db:+.1f} dB")
)PY" },
        { "List commands.py", R"PY(# Lists PHI's commands with a word in their name or id: the ids are what
# phi.run() takes, so a script can do anything the menus do.
word = phi.ask("Find commands with this in them (empty for all):", "normal")
if word is not None:
    for c in phi.commands(word):
        print(f'{c["id"]:32} {c["category"]}: {c["name"]}  {c["shortcut"]}')
)PY" },
    };

    QString displayName (const QString& path)
    {
        return QFileInfo (path).completeBaseName();
    }
}

namespace scripts
{
    QString folder()
    {
        const auto dir = QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation) + QStringLiteral ("/" APP_ID "/scripts");
        if (! QDir (dir).exists() && QDir().mkpath (dir))
            for (const auto& e : examples)
            {
                QFile f (QDir (dir).filePath (QString::fromUtf8 (e.file)));
                if (f.open (QIODevice::WriteOnly))
                    f.write (e.code);
            }
        return dir;
    }

    QStringList all()
    {
        QStringList paths;
        const QDir dir (folder());
        for (const auto& name : dir.entryList ({ QStringLiteral ("*.lua"), QStringLiteral ("*.py"), QStringLiteral ("*.ny") }, QDir::Files, QDir::Name | QDir::IgnoreCase))
            paths << dir.filePath (name);
        return paths;
    }

    QString templateFor (script::Language l)
    {
        switch (l)
        {
            case script::Language::python:
                return QStringLiteral ("# A PHI script in Python. F5 runs it on the open file.\n"
                                       "# phi.* works the file, its markers, PHI's commands and the mixer;\n"
                                       "# the API Reference lists every function.\n\n"
                                       "a, b = phi.selection()\n"
                                       "print(f\"Selection: {a:.3f} to {b:.3f} s\")\n");
            case script::Language::nyquist:
                return QStringLiteral (";; A Nyquist script, run on the selection as the Nyquist Prompt does.\n"
                                       ";; *track* is the selected audio.\n\n"
                                       "(mult *track* 0.5)\n");
            case script::Language::lua:
            case script::Language::unknown:
                break;
        }
        return QStringLiteral ("-- A PHI script in Lua. F5 runs it on the open file.\n"
                               "-- phi.* works the file, its markers, PHI's commands and the mixer;\n"
                               "-- the API Reference lists every function.\n\n"
                               "local a, b = phi.selection()\n"
                               "print((\"Selection: %.3f to %.3f s\"):format(a, b))\n");
    }
}

// Colours keywords, strings, comments, numbers and the phi functions, by language.
class ScriptHighlighter final : public QSyntaxHighlighter
{
public:
    explicit ScriptHighlighter (QTextDocument* d) : QSyntaxHighlighter (d) {}

    void setLanguage (script::Language l)
    {
        language = l;
        rehighlight();
    }

protected:
    void highlightBlock (const QString& text) override
    {
        const auto palette = QPalette();
        const bool dark = palette.color (QPalette::Base).lightness() < 128;
        QTextCharFormat keyword, string, comment, number, api;
        keyword.setForeground (dark ? QColor (0xc6, 0x92, 0xff) : QColor (0x80, 0x30, 0xc0));
        keyword.setFontWeight (QFont::DemiBold);
        string.setForeground (dark ? QColor (0x9c, 0xdc, 0x8c) : QColor (0x20, 0x80, 0x20));
        comment.setForeground (dark ? QColor (0x80, 0x88, 0x90) : QColor (0x70, 0x78, 0x80));
        comment.setFontItalic (true);
        number.setForeground (dark ? QColor (0xf0, 0xb0, 0x70) : QColor (0xb0, 0x60, 0x00));
        api.setForeground (dark ? QColor (0x70, 0xc0, 0xff) : QColor (0x00, 0x60, 0xb0));

        static const QRegularExpression luaWords (QStringLiteral (
            "\\b(and|break|do|else|elseif|end|false|for|function|goto|if|in|local|nil|not|or|repeat|return|then|true|until|while)\\b"));
        static const QRegularExpression pyWords (QStringLiteral (
            "\\b(False|None|True|and|as|assert|break|class|continue|def|del|elif|else|except|finally|for|from|global|if|import|in|is|"
            "lambda|nonlocal|not|or|pass|raise|return|try|while|with|yield)\\b"));
        static const QRegularExpression nyWords (QStringLiteral (
            "\\b(defun|let|let\\*|setf|setq|if|cond|when|unless|dotimes|dolist|do|loop|lambda|progn|return|t|nil)\\b"));
        static const QRegularExpression numbers (QStringLiteral ("\\b(0[xX][0-9a-fA-F]+|\\d+\\.?\\d*([eE][-+]?\\d+)?)\\b"));
        static const QRegularExpression apiCalls (QStringLiteral ("\\bphi\\.[A-Za-z_]+"));
        static const QRegularExpression strings (QStringLiteral ("\"(\\\\.|[^\"\\\\])*\"|'(\\\\.|[^'\\\\])*'"));

        const auto apply = [&] (const QRegularExpression& re, const QTextCharFormat& f)
        {
            for (auto it = re.globalMatch (text); it.hasNext();)
            {
                const auto m = it.next();
                setFormat ((int) m.capturedStart(), (int) m.capturedLength(), f);
            }
        };
        apply (language == script::Language::python ? pyWords : language == script::Language::nyquist ? nyWords : luaWords, keyword);
        apply (numbers, number);
        apply (apiCalls, api);
        if (language != script::Language::nyquist)
            apply (strings, string);
        else
        {
            static const QRegularExpression lispString (QStringLiteral ("\"(\\\\.|[^\"\\\\])*\""));
            apply (lispString, string);
        }

        // Comments to the end of the line, when the marker is not inside a string.
        const QString marker = language == script::Language::python ? QStringLiteral ("#")
                             : language == script::Language::nyquist ? QStringLiteral (";") : QStringLiteral ("--");
        QChar quote;
        for (int i = 0; i < text.size(); ++i)
        {
            const auto c = text[i];
            if (! quote.isNull())
            {
                if (c == QLatin1Char ('\\'))
                    ++i;
                else if (c == quote)
                    quote = QChar();
                continue;
            }
            if (c == QLatin1Char ('"') || (c == QLatin1Char ('\'') && language != script::Language::nyquist))
                quote = c;
            else if (text.mid (i, marker.size()) == marker)
            {
                setFormat (i, (int) text.size() - i, comment);
                break;
            }
        }
    }

private:
    script::Language language = script::Language::lua;
};

ScriptEditor::ScriptEditor (QWidget* parent) : QWidget (parent, Qt::Window)
{
    setProperty ("macroSkip", true);   // its own buttons and keys are not macro steps
    setAttribute (Qt::WA_DeleteOnClose, false);
    resize (1000, 680);

    list = new QListWidget;
    list->setAccessibleName (tr ("Scripts"));
    list->setToolTip (tr ("The scripts folder. Double-click or press Enter to open one."));
    code = new QPlainTextEdit;
    code->setAccessibleName (tr ("Script"));
    code->setLineWrapMode (QPlainTextEdit::NoWrap);
    code->setFont (QFontDatabase::systemFont (QFontDatabase::FixedFont));
    code->setTabStopDistance (QFontMetricsF (code->font()).horizontalAdvance (QLatin1Char (' ')) * 4);
    code->installEventFilter (this);
    highlighter = new ScriptHighlighter (code->document());
    console = new QPlainTextEdit;
    console->setAccessibleName (tr ("Output"));
    console->setReadOnly (true);
    console->setFont (code->font());
    console->setMaximumBlockCount (5000);
    position = new QLabel;

    auto* bar = new QToolBar;
    bar->setToolButtonStyle (Qt::ToolButtonTextOnly);
    auto* newMenu = new QMenu (this);
    for (const auto l : { script::Language::lua, script::Language::python, script::Language::nyquist })
        newMenu->addAction (tr ("%1 Script").arg (script::languageName (l)), this, [this, l] { if (askToSave()) newScript (l); });
    newMenu->addSeparator();
    newMenu->addAction (tr ("From a &Reaper Script..."), this, [this] { importReaperScript(); });
    auto* newButton = new QToolButton;
    newButton->setText (tr ("&New"));
    newButton->setMenu (newMenu);
    newButton->setPopupMode (QToolButton::InstantPopup);
    bar->addWidget (newButton);

    const auto add = [this, bar] (const QString& text, const QKeySequence& key, std::function<void()> fn)
    {
        auto* a = bar->addAction (text);
        a->setShortcut (key);
        a->setShortcutContext (Qt::WindowShortcut);
        if (! key.isEmpty())
            a->setToolTip (QStringLiteral ("%1 (%2)").arg (QString (text).remove (QLatin1Char ('&')), key.toString (QKeySequence::NativeText)));
        connect (a, &QAction::triggered, this, std::move (fn));
        return a;
    };
    add (tr ("&Open..."), QKeySequence::Open, [this]
    {
        if (! askToSave())
            return;
        const auto file = QFileDialog::getOpenFileName (this, tr ("Open Script"), scripts::folder(), tr ("Scripts (*.lua *.py *.ny);;All files (*)"));
        if (! file.isEmpty())
            openScript (file);
    });
    add (tr ("&Save"), QKeySequence::Save, [this] { save (false); });
    add (tr ("Save &As..."), QKeySequence::SaveAs, [this] { save (true); });
    bar->addSeparator();
    runAction = add (tr ("&Run"), QKeySequence (Qt::Key_F5), [this] { run(); });
    auto* runToo = new QAction (this);
    runToo->setShortcut (QKeySequence (Qt::CTRL | Qt::Key_Return));
    connect (runToo, &QAction::triggered, this, [this] { run(); });
    addAction (runToo);
    stopAction = add (tr ("S&top"), QKeySequence (Qt::SHIFT | Qt::Key_F5), [this] { if (stopRequested) stopRequested(); });
    stopAction->setEnabled (false);
    bar->addSeparator();
    add (tr ("API &Reference"), QKeySequence (Qt::Key_F1), [this] { showReference(); });
    add (tr ("Open &Folder"), {}, [] { QDesktopServices::openUrl (QUrl::fromLocalFile (scripts::folder())); });

    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout (left);
    leftLayout->setContentsMargins (0, 0, 0, 0);
    auto* listLabel = new QLabel (tr ("S&cripts:"));
    listLabel->setBuddy (list);
    leftLayout->addWidget (listLabel);
    leftLayout->addWidget (list, 1);
    auto* deleteButton = new QPushButton (tr ("&Delete..."));
    deleteButton->setAutoDefault (false);
    connect (deleteButton, &QPushButton::clicked, this, [this] { deleteSelected(); });
    leftLayout->addWidget (deleteButton);

    auto* outputBox = new QWidget;
    auto* outputLayout = new QVBoxLayout (outputBox);
    outputLayout->setContentsMargins (0, 0, 0, 0);
    auto* outputRow = new QHBoxLayout;
    auto* outputLabel = new QLabel (tr ("O&utput:"));
    outputLabel->setBuddy (console);
    outputRow->addWidget (outputLabel);
    outputRow->addStretch (1);
    outputRow->addWidget (position);
    auto* clear = new QPushButton (tr ("C&lear"));
    clear->setAutoDefault (false);
    connect (clear, &QPushButton::clicked, console, &QPlainTextEdit::clear);
    outputRow->addWidget (clear);
    outputLayout->addLayout (outputRow);
    outputLayout->addWidget (console, 1);

    auto* vertical = new QSplitter (Qt::Vertical);
    vertical->addWidget (code);
    vertical->addWidget (outputBox);
    vertical->setStretchFactor (0, 3);
    vertical->setStretchFactor (1, 1);
    auto* horizontal = new QSplitter (Qt::Horizontal);
    horizontal->addWidget (left);
    horizontal->addWidget (vertical);
    horizontal->setStretchFactor (0, 1);
    horizontal->setStretchFactor (1, 4);

    auto* layout = new QVBoxLayout (this);
    layout->addWidget (bar);
    layout->addWidget (horizontal, 1);

    connect (list, &QListWidget::itemActivated, this, [this] (QListWidgetItem* item)
    {
        const auto file = item->data (Qt::UserRole).toString();
        if (file != path && askToSave())
            openScript (file);
    });
    connect (code->document(), &QTextDocument::modificationChanged, this, [this] { updateTitle(); });
    connect (code, &QPlainTextEdit::cursorPositionChanged, this, [this]
    {
        const auto c = code->textCursor();
        position->setText (tr ("Line %1, column %2").arg (c.blockNumber() + 1).arg (c.positionInBlock() + 1));
    });

    refreshList();
    newScript (script::Language::lua);
}

void ScriptEditor::refreshList()
{
    const QSignalBlocker block (list);
    list->clear();
    for (const auto& file : scripts::all())
    {
        auto* item = new QListWidgetItem (QStringLiteral ("%1  (%2)").arg (displayName (file), script::languageName (script::languageFor (file))));
        item->setData (Qt::UserRole, file);
        list->addItem (item);
        if (file == path)
            list->setCurrentItem (item);
    }
}

void ScriptEditor::setLanguage (script::Language l)
{
    language = l;
    highlighter->setLanguage (l);
    updateTitle();
}

void ScriptEditor::importReaperScript()
{
    if (! askToSave())
        return;
    const auto file = QFileDialog::getOpenFileName (this, tr ("Import Reaper Script"), QDir::homePath(), tr ("Reaper scripts (*.lua *.py *.eel);;All files (*)"));
    if (file.isEmpty())
        return;
    QFile f (file);
    if (! f.open (QIODevice::ReadOnly))
    {
        QMessageBox::warning (this, tr ("Import Reaper Script"), tr ("Could not read %1.").arg (file));
        return;
    }
    const auto result = reaperimport::convert (QString::fromUtf8 (f.readAll()), file);
    if (! result.ok)
    {
        QMessageBox box (QMessageBox::Warning, tr ("Import Reaper Script"),
                         tr ("\"%1\" cannot be converted for PHI.").arg (QFileInfo (file).fileName()), QMessageBox::Ok, this);
        box.setInformativeText (result.reasons.join (QStringLiteral ("\n\n")));
        box.exec();
        return;
    }
    path.clear();
    suggestedName = QFileInfo (file).completeBaseName() + tr (" (from Reaper)");
    code->setPlainText (result.code);
    code->document()->setModified (true);
    setLanguage (script::languageFor (file));
    list->clearSelection();
    console->clear();
    showOutput (result.report(), false);
    if (! result.unsupported.isEmpty())
        showOutput (tr ("Lines marked in the header need changing by hand for the script to run to its end.\n"), true);
    showOutput (tr ("Look it over, then Save to keep it.\n"), false);
    code->setFocus();
}

void ScriptEditor::newScript (script::Language l)
{
    suggestedName.clear();
    path.clear();
    code->setPlainText (scripts::templateFor (l));
    code->document()->setModified (false);
    setLanguage (l);
    list->clearSelection();
    code->setFocus();
    code->moveCursor (QTextCursor::End);
}

void ScriptEditor::openScript (const QString& file)
{
    QFile f (file);
    if (! f.open (QIODevice::ReadOnly))
    {
        QMessageBox::warning (this, tr ("Open Script"), tr ("Could not read %1.").arg (file));
        return;
    }
    path = file;
    code->setPlainText (QString::fromUtf8 (f.readAll()));
    code->document()->setModified (false);
    const auto l = script::languageFor (file);
    setLanguage (l == script::Language::unknown ? script::Language::lua : l);
    refreshList();
    code->setFocus();
}

bool ScriptEditor::save (bool as)
{
    auto target = path;
    if (as || target.isEmpty())
    {
        const auto ext = script::extensionFor (language);
        const auto suggested = QDir (scripts::folder()).filePath ((! path.isEmpty() ? displayName (path) : ! suggestedName.isEmpty() ? suggestedName : tr ("New script")) + QLatin1Char ('.') + ext);
        target = QFileDialog::getSaveFileName (this, tr ("Save Script"), suggested, tr ("%1 scripts (*.%2)").arg (script::languageName (language), ext));
        if (target.isEmpty())
            return false;
        if (QFileInfo (target).suffix().isEmpty())
            target += QLatin1Char ('.') + ext;
    }
    QFile f (target);
    if (! f.open (QIODevice::WriteOnly | QIODevice::Truncate) || f.write (code->toPlainText().toUtf8()) < 0)
    {
        QMessageBox::warning (this, tr ("Save Script"), tr ("Could not write %1.").arg (target));
        return false;
    }
    f.close();
    path = target;
    code->document()->setModified (false);
    const auto l = script::languageFor (path);
    if (l != script::Language::unknown && l != language)
        setLanguage (l);
    updateTitle();
    refreshList();
    if (scriptsChanged)
        scriptsChanged();
    return true;
}

bool ScriptEditor::askToSave()
{
    if (! code->document()->isModified())
        return true;
    const auto answer = QMessageBox::question (this, tr ("Script Editor"),
                                               tr ("Save the changes to \"%1\"?").arg (path.isEmpty() ? tr ("the new script") : displayName (path)),
                                               QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    return answer == QMessageBox::Discard || save (false);
}

void ScriptEditor::run()
{
    if (! runAction->isEnabled() || ! runRequested)
        return;
    const auto name = path.isEmpty() ? tr ("untitled") + QLatin1Char ('.') + script::extensionFor (language) : QFileInfo (path).fileName();
    console->appendPlainText (tr ("▶ %1").arg (name));
    runRequested (language, code->toPlainText(), name, path.isEmpty() ? scripts::folder() : QFileInfo (path).absolutePath());
}

void ScriptEditor::showOutput (const QString& text, bool error)
{
    auto cursor = console->textCursor();
    cursor.movePosition (QTextCursor::End);
    QTextCharFormat format;
    if (error)
        format.setForeground (QColor (0xe0, 0x50, 0x50));
    cursor.insertText (text, format);
    console->setTextCursor (cursor);
    console->ensureCursorVisible();
}

void ScriptEditor::setRunning (bool on)
{
    runAction->setEnabled (! on);
    stopAction->setEnabled (on);
    if (! on)
        showOutput (QStringLiteral ("\n"), false);
}

void ScriptEditor::updateTitle()
{
    const auto name = path.isEmpty() ? tr ("Untitled") : displayName (path);
    setWindowTitle (tr ("Script Editor: %1 (%2)%3").arg (name, script::languageName (language),
                                                          code->document()->isModified() ? QStringLiteral (" *") : QString()));
}

void ScriptEditor::deleteSelected()
{
    auto* item = list->currentItem();
    if (item == nullptr)
        return;
    const auto file = item->data (Qt::UserRole).toString();
    if (QMessageBox::question (this, tr ("Delete Script"), tr ("Delete the script \"%1\"?").arg (displayName (file))) != QMessageBox::Yes)
        return;
    QFile::remove (file);
    if (file == path)
    {
        path.clear();
        code->document()->setModified (true);
        updateTitle();
    }
    refreshList();
    if (scriptsChanged)
        scriptsChanged();
}

void ScriptEditor::showReference()
{
    QString html = tr ("<h2>The phi module</h2>"
                       "<p>Every script has <b>phi</b> ready, in Lua and in Python alike. Times are in seconds; sample positions "
                       "are in frames. Channels, mixer tracks and tabs count from 0. A call that cannot be done raises an "
                       "error that stops the script, unless it is caught (Lua: <tt>pcall</tt>; Python: <tt>except phi.PhiError</tt>). "
                       "All the changes a script makes to a file are one step in Edit &gt; Undo.</p>"
                       "<p>Python scripts run on the computer's own Python 3, so its modules (numpy and the rest) are there "
                       "to use. Nyquist scripts (.ny) run on the selection as the Nyquist Prompt does.</p>");
    QString group;
    for (const auto& f : script::api())
    {
        if (group != QLatin1String (f.group))
        {
            if (! group.isEmpty())
                html += QStringLiteral ("</table>");
            group = QString::fromUtf8 (f.group);
            html += QStringLiteral ("<h3>%1</h3><table cellspacing='0' cellpadding='4'>").arg (group.toHtmlEscaped());
        }
        html += QStringLiteral ("<tr><td valign='top'><tt><b>phi.%1</b>(%2)</tt></td><td>%3</td></tr>")
                    .arg (QString::fromLatin1 (f.name), QString::fromLatin1 (f.signature).toHtmlEscaped(), QString::fromUtf8 (f.help).toHtmlEscaped());
    }
    html += QStringLiteral ("</table>");

    QDialog dialog (this);
    dialog.setWindowTitle (tr ("Script API Reference"));
    dialog.resize (760, 640);
    auto* view = new QTextBrowser;
    view->setHtml (html);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Close);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* layout = new QVBoxLayout (&dialog);
    layout->addWidget (view, 1);
    layout->addWidget (buttons);
    dialog.exec();
}

void ScriptEditor::closeEvent (QCloseEvent* e)
{
    if (! askToSave())
    {
        e->ignore();
        return;
    }
    QWidget::closeEvent (e);
}

bool ScriptEditor::eventFilter (QObject* watched, QEvent* e)
{
    // In the code: Tab indents with four spaces, Enter keeps the line's indent.
    if (watched == code && e->type() == QEvent::KeyPress)
    {
        const auto* key = static_cast<QKeyEvent*> (e);
        if (key->key() == Qt::Key_Tab && key->modifiers() == Qt::NoModifier && ! code->textCursor().hasSelection())
        {
            code->insertPlainText (QStringLiteral ("    "));
            return true;
        }
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && key->modifiers() == Qt::NoModifier)
        {
            const auto line = code->textCursor().block().text();
            qsizetype indent = 0;
            while (indent < line.size() && (line[indent] == QLatin1Char (' ') || line[indent] == QLatin1Char ('\t')))
                ++indent;
            code->insertPlainText (QLatin1Char ('\n') + line.left (indent));
            return true;
        }
    }
    return QWidget::eventFilter (watched, e);
}
