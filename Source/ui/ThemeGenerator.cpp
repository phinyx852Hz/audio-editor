// SPDX-License-Identifier: AGPL-3.0-only
// Tools > Theme Generator (see ThemeGenerator.h and docs/themes.md).
#include "ThemeGenerator.h"
#include "Settings.h"
#include "ThemeAi.h"
#include "../core/Zip.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSlider>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

namespace
{
    QColor toQColor (const themegen::Rgb& c) { return QColor::fromRgbF ((float) c.r, (float) c.g, (float) c.b, (float) c.a); }
    themegen::Rgb fromQColor (const QColor& c) { return { c.redF(), c.greenF(), c.blueF(), c.alphaF() }; }

    QIcon swatchIcon (const QColor& c, int size = 16)
    {
        QPixmap p (size, size);
        p.fill (Qt::transparent);
        QPainter g (&p);
        // A checkerboard under colours with alpha, so see-through ones look it.
        g.fillRect (0, 0, size, size, Qt::white);
        g.fillRect (0, 0, size / 2, size / 2, Qt::lightGray);
        g.fillRect (size / 2, size / 2, size / 2, size / 2, Qt::lightGray);
        g.fillRect (0, 0, size, size, c);
        g.setPen (Qt::gray);
        g.drawRect (0, 0, size - 1, size - 1);
        return QIcon (p);
    }

    // A file name for a theme name: letters, digits, '-' and '_'.
    QString fileNameFor (const QString& themeName)
    {
        QString s;
        for (const auto ch : themeName.trimmed())
            s += ch.isLetterOrNumber() ? ch.toLower() : QChar ('-');
        while (s.contains (QStringLiteral ("--")))
            s.replace (QStringLiteral ("--"), QStringLiteral ("-"));
        s.remove (QRegularExpression (QStringLiteral ("^-+|-+$")));
        return (s.isEmpty() ? QStringLiteral ("theme") : s) + QStringLiteral (".json");
    }
}

// A small picture of the main things a theme colours, drawn with the generated colours.
class ThemePreview final : public QWidget
{
public:
    explicit ThemePreview (QWidget* parent) : QWidget (parent)
    {
        setMinimumSize (320, 150);
        setAccessibleName (tr ("Preview"));
    }

    void setTheme (const Theme& t)
    {
        theme = t;
        update();
    }

protected:
    void paintEvent (QPaintEvent*) override
    {
        QPainter g (this);
        g.setRenderHint (QPainter::Antialiasing);
        const auto c = [this] (const char* key) { return theme.color (QString::fromLatin1 (key)); };
        const auto r = rect();
        g.fillRect (r, c ("window"));

        // A list with a selected row and a button.
        const QRect list (8, 8, 110, 62);
        g.fillRect (list, c ("base"));
        g.fillRect (QRect (list.left(), list.top() + 20, list.width(), 20), c ("highlight"));
        g.setPen (c ("text"));
        g.drawText (list.adjusted (6, 2, 0, 0), Qt::AlignLeft | Qt::AlignTop, tr ("Track 1"));
        g.setPen (c ("highlightedText"));
        g.drawText (list.adjusted (6, 22, 0, 0), Qt::AlignLeft | Qt::AlignTop, tr ("Selected"));
        g.setPen (c ("placeholderText"));
        g.drawText (list.adjusted (6, 42, 0, 0), Qt::AlignLeft | Qt::AlignTop, tr ("Hint text"));
        const QRect button (8, 78, 110, 24);
        g.setPen (c ("mid"));
        g.setBrush (c ("button"));
        g.drawRoundedRect (button, 3, 3);
        g.setPen (c ("buttonText"));
        g.drawText (button, Qt::AlignCenter, tr ("Button"));
        g.setPen (c ("accent"));
        g.drawText (QRect (8, 106, 110, 18), Qt::AlignLeft | Qt::AlignVCenter, tr ("Link"));
        g.setPen (c ("disabledText"));
        g.drawText (QRect (50, 106, 70, 18), Qt::AlignLeft | Qt::AlignVCenter, tr ("Disabled"));

        // A waveform with a selection, cursor, marker and region.
        const QRect wave (126, 8, std::max (60, r.width() - 160), 94);
        g.fillRect (wave, c ("waveformBackground"));
        g.setPen (c ("centerLine"));
        g.drawLine (wave.left(), wave.center().y(), wave.right(), wave.center().y());
        QPainterPath path;
        for (int x = 0; x <= wave.width(); ++x)
        {
            const double t = x / (double) wave.width();
            const double env = 0.25 + 0.7 * std::abs (std::sin (t * 7.0)) * (0.6 + 0.4 * std::sin (t * 31.0));
            const double y = wave.center().y() - env * (wave.height() / 2 - 4) * std::sin (t * 260.0);
            if (x == 0)
                path.moveTo (wave.left() + x, y);
            else
                path.lineTo (wave.left() + x, y);
        }
        g.setPen (QPen (c ("waveform"), 1.0));
        g.setBrush (Qt::NoBrush);
        g.drawPath (path);
        g.fillRect (QRect (wave.left() + wave.width() / 3, wave.top(), wave.width() / 4, wave.height()), c ("selection"));
        g.setPen (QPen (c ("region"), 2));
        g.drawLine (wave.left() + wave.width() * 3 / 4, wave.top(), wave.left() + wave.width() * 3 / 4, wave.bottom());
        g.setPen (QPen (c ("marker"), 2));
        g.drawLine (wave.left() + 14, wave.top(), wave.left() + 14, wave.bottom());
        g.setPen (QPen (c ("cursor"), 1.5));
        const int cx = wave.left() + wave.width() * 3 / 5;
        g.drawLine (cx, wave.top(), cx, wave.bottom());

        // A meter, with its three ranges and peak hold.
        const QRect meter (r.width() - 26, 8, 18, 94);
        g.fillRect (meter, c ("meterBackground"));
        const int h = meter.height();
        g.fillRect (QRect (meter.left() + 3, meter.bottom() - h * 55 / 100, 12, h * 55 / 100), c ("meterLow"));
        g.fillRect (QRect (meter.left() + 3, meter.bottom() - h * 75 / 100, 12, h * 20 / 100), c ("meterMid"));
        g.fillRect (QRect (meter.left() + 3, meter.bottom() - h * 85 / 100, 12, h * 10 / 100), c ("meterHigh"));
        g.fillRect (QRect (meter.left() + 3, meter.bottom() - h * 92 / 100, 12, 2), c ("meterPeakHold"));

        // The spectrogram colours and the level colours.
        const QRect spec (126, 108, wave.width(), 14);
        QLinearGradient gradient (spec.topLeft(), spec.topRight());
        for (int i = 0; i < theme.spectrogram.size(); ++i)
            gradient.setColorAt (theme.spectrogram.size() < 2 ? 0.0 : i / (double) (theme.spectrogram.size() - 1), theme.spectrogram[i]);
        g.fillRect (spec, gradient);
        const char* levels[] = { "levelQuiet", "levelSoft", "levelMedium", "levelLoud", "levelHot", "levelPeak" };
        const int w = spec.width() / 6;
        for (int i = 0; i < 6; ++i)
            g.fillRect (QRect (spec.left() + i * w, spec.bottom() + 4, w, 8), c (levels[i]));
    }

private:
    Theme theme;
};

//==============================================================================
ThemeGeneratorDialog::ThemeGeneratorDialog (QWidget* parent)
    : QDialog (parent), original (Theme::base()), ai (new themeai::Client (this)), applyTimer (new QTimer (this))
{
    setWindowTitle (tr ("Theme Generator"));
    resize (980, 680);

    applyTimer->setSingleShot (true);
    applyTimer->setInterval (60);
    connect (applyTimer, &QTimer::timeout, this, [this] { Theme::apply (theme()); });

    // Start from the theme in use: its highlight is the main colour.
    settings.seed = fromQColor (original.color (QStringLiteral ("highlight")));
    settings.seed.a = 1;
    settings.dark = original.dark;

    //--- Start from
    name = new QLineEdit (tr ("My Theme"));
    name->setAccessibleName (tr ("Theme name"));

    seedButton = new QPushButton (tr ("Main &Colour..."));
    seedHex = new QLineEdit;
    seedHex->setAccessibleName (tr ("Main colour as #rrggbb"));
    seedHex->setMaximumWidth (90);
    auto* picture = new QPushButton (tr ("From &Picture..."));
    fromWallpaper = new QPushButton (tr ("From &Wallpaper"));
    fromTheme = new QComboBox;
    fromTheme->setAccessibleName (tr ("Start from a theme"));
    fromTheme->addItem (tr ("From a theme..."));
    for (const auto& t : Theme::available())
        fromTheme->addItem (t.name, t.source);
    auto* reaper = new QPushButton (tr ("From &REAPER Theme..."));
    reaper->setToolTip (tr ("Takes the colours of a .ReaperTheme or .ReaperThemeZip. Its images and layouts are for REAPER's own screens and are left out."));

    auto* seedRow = new QHBoxLayout;
    seedRow->addWidget (seedButton);
    seedRow->addWidget (seedHex);
    seedRow->addWidget (picture);
    seedRow->addWidget (fromWallpaper);
    seedRow->addWidget (fromTheme);
    seedRow->addWidget (reaper);
    seedRow->addStretch();

    paletteLabel = new QLabel (tr ("Colours in the picture, pick one:"));
    paletteLabel->hide();
    paletteRow = new QHBoxLayout;
    paletteRow->addWidget (paletteLabel);
    paletteRow->addStretch();

    //--- Shape
    mode = new QComboBox;
    mode->addItems ({ tr ("Dark"), tr ("Light") });
    harmony = new QComboBox;
    harmony->addItems ({ tr ("One colour"), tr ("Neighbours (analogous)"), tr ("Opposite (complementary)"),
                         tr ("Split opposite"), tr ("Three-way (triadic)") });
    harmony->setToolTip (tr ("How the accent, marker and region colours relate to the main colour"));
    meters = new QComboBox;
    meters->addItems ({ tr ("Green, yellow, red"), tr ("The theme's colours") });
    const auto slider = [] (const QString& accessible)
    {
        auto* s = new QSlider (Qt::Horizontal);
        s->setRange (0, 100);
        s->setAccessibleName (accessible);
        return s;
    };
    saturation = slider (tr ("Saturation"));
    saturation->setRange (0, 200);
    contrast = slider (tr ("Contrast"));
    tint = slider (tr ("Background tint"));
    autoFix = new QCheckBox (tr ("&Keep text readable (fix contrast automatically)"));
    autoFix->setChecked (appSettings().value (QStringLiteral ("themeGenerator/autoFix"), true).toBool());
    auto* shuffle = new QPushButton (tr ("S&huffle"));
    shuffle->setToolTip (tr ("Other accent, marker and region colours that go with the main colour"));

    auto* shape = new QFormLayout;
    shape->addRow (tr ("&Name:"), name);
    shape->addRow (tr ("&Mode:"), mode);
    shape->addRow (tr ("&Harmony:"), harmony);
    shape->addRow (tr ("&Saturation:"), saturation);
    shape->addRow (tr ("C&ontrast:"), contrast);
    shape->addRow (tr ("Background &tint:"), tint);
    shape->addRow (tr ("M&eters:"), meters);
    shape->addRow (autoFix);
    shape->addRow (shuffle);

    //--- Describe it (AI)
    description = new QLineEdit;
    description->setPlaceholderText (tr ("Describe a look, for example: a 1970s tape deck, warm and brown"));
    description->setAccessibleName (tr ("Describe the theme"));
    describeButton = new QPushButton (tr ("&Make It"));
    auto* aiSetup = new QPushButton (tr ("AI &Settings..."));
    aiStatus = new QLabel;
    aiStatus->setWordWrap (true);
    auto* aiBox = new QGroupBox (tr ("Describe it (optional AI)"));
    auto* aiLayout = new QVBoxLayout (aiBox);
    auto* aiRow = new QHBoxLayout;
    aiRow->addWidget (description, 1);
    aiRow->addWidget (describeButton);
    aiRow->addWidget (aiSetup);
    aiLayout->addLayout (aiRow);
    aiLayout->addWidget (aiStatus);

    //--- Colours, checks and preview
    preview = new ThemePreview (this);
    colors = new QTableWidget (0, 3);
    colors->setAccessibleName (tr ("Colours"));
    colors->setHorizontalHeaderLabels ({ tr ("Colour"), tr ("Value"), tr ("Keep") });
    colors->horizontalHeader()->setSectionResizeMode (0, QHeaderView::Stretch);
    colors->horizontalHeader()->setSectionResizeMode (1, QHeaderView::ResizeToContents);
    colors->horizontalHeader()->setSectionResizeMode (2, QHeaderView::ResizeToContents);
    colors->verticalHeader()->hide();
    colors->setSelectionBehavior (QAbstractItemView::SelectRows);
    colors->setEditTriggers (QAbstractItemView::NoEditTriggers);
    colors->setToolTip (tr ("Double-click a colour to choose it yourself. Kept colours stay as they are when you change anything else."));
    checks = new QListWidget;
    checks->setAccessibleName (tr ("Contrast checks"));

    auto* colorsLabel = new QLabel (tr ("Colo&urs (double-click to change, tick Keep to hold one):"));
    colorsLabel->setBuddy (colors);
    auto* checksLabel = new QLabel (tr ("Contrast c&hecks (WCAG: 4.5 for text, 3 for lines and meters):"));
    checksLabel->setBuddy (checks);

    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout (left);
    leftLayout->setContentsMargins (0, 0, 0, 0);
    leftLayout->addLayout (shape);
    leftLayout->addWidget (preview, 1);
    leftLayout->addWidget (checksLabel);
    leftLayout->addWidget (checks, 1);
    auto* right = new QWidget;
    auto* rightLayout = new QVBoxLayout (right);
    rightLayout->setContentsMargins (0, 0, 0, 0);
    rightLayout->addWidget (colorsLabel);
    rightLayout->addWidget (colors, 1);
    auto* split = new QSplitter (Qt::Horizontal);
    split->addWidget (left);
    split->addWidget (right);
    split->setStretchFactor (0, 3);
    split->setStretchFactor (1, 2);

    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Cancel);
    auto* saveButton = buttons->addButton (tr ("Sa&ve Theme"), QDialogButtonBox::AcceptRole);
    auto* exportButton = buttons->addButton (tr ("E&xport..."), QDialogButtonBox::ActionRole);
    saveButton->setToolTip (tr ("Saves it in your themes folder and uses it; Options > Theme lists it"));
    for (auto* b : buttons->buttons())
        if (auto* push = qobject_cast<QPushButton*> (b))
            push->setAutoDefault (false);

    auto* layout = new QVBoxLayout (this);
    layout->addLayout (seedRow);
    layout->addLayout (paletteRow);
    layout->addWidget (aiBox);
    layout->addWidget (split, 1);
    layout->addWidget (buttons);

    //--- Connections
    connect (seedButton, &QPushButton::clicked, this, [this]
    {
        const auto c = QColorDialog::getColor (toQColor (settings.seed), this, tr ("Main Colour"));
        if (c.isValid())
            setSeed (fromQColor (c));
    });
    connect (seedHex, &QLineEdit::editingFinished, this, [this]
    {
        themegen::Rgb c;
        auto text = seedHex->text().trimmed();
        if (! text.startsWith (QLatin1Char ('#')))
            text.prepend (QLatin1Char ('#'));
        if (themegen::fromHex (text.toStdString(), c))
            setSeed (c);
        else
            showSettings();
    });
    connect (picture, &QPushButton::clicked, this, [this]
    {
        const auto path = QFileDialog::getOpenFileName (this, tr ("Theme From a Picture"), QDir::homePath(),
                                                        tr ("Pictures (*.png *.jpg *.jpeg *.webp *.bmp *.gif)"));
        if (! path.isEmpty())
            startFromPicture (path);
    });
    const auto wallpaper = appSettings().value (QStringLiteral ("appearance/wallpaper")).toString();
    fromWallpaper->setEnabled (! wallpaper.isEmpty() && QFileInfo::exists (wallpaper));
    fromWallpaper->setToolTip (fromWallpaper->isEnabled() ? wallpaper : tr ("Choose a wallpaper with Options > Choose Wallpaper Picture first"));
    connect (fromWallpaper, &QPushButton::clicked, this, [this, wallpaper] { startFromPicture (wallpaper); });
    connect (fromTheme, &QComboBox::activated, this, &ThemeGeneratorDialog::startFromTheme);
    connect (reaper, &QPushButton::clicked, this, &ThemeGeneratorDialog::startFromReaper);

    connect (mode, &QComboBox::currentIndexChanged, this, [this] (int i) { if (! updating) { settings.dark = i == 0; regenerate(); } });
    connect (harmony, &QComboBox::currentIndexChanged, this, [this] (int i)
    {
        if (! updating)
        {
            settings.harmony = (themegen::Harmony) i;
            regenerate();
        }
    });
    connect (meters, &QComboBox::currentIndexChanged, this, [this] (int i)
    {
        if (! updating)
        {
            settings.meters = i == 0 ? themegen::Meters::classic : themegen::Meters::palette;
            regenerate();
        }
    });
    connect (saturation, &QSlider::valueChanged, this, [this] (int v) { if (! updating) { settings.saturation = v / 100.0; regenerate(); } });
    connect (contrast, &QSlider::valueChanged, this, [this] (int v) { if (! updating) { settings.contrast = v / 100.0; regenerate(); } });
    connect (tint, &QSlider::valueChanged, this, [this] (int v) { if (! updating) { settings.tint = v / 100.0; regenerate(); } });
    connect (autoFix, &QCheckBox::toggled, this, [this] (bool on)
    {
        appSettings().setValue (QStringLiteral ("themeGenerator/autoFix"), on);
        regenerate();
    });
    connect (shuffle, &QPushButton::clicked, this, [this]
    {
        ++settings.variation;
        regenerate();
    });

    connect (colors, &QTableWidget::cellDoubleClicked, this, [this] (int row, int) { editColor (row); });
    connect (colors, &QTableWidget::itemChanged, this, [this] (QTableWidgetItem* item)
    {
        if (updating || item->column() != 2)
            return;
        const auto key = colors->item (item->row(), 0)->data (Qt::UserRole).toString().toStdString();
        if (item->checkState() == Qt::Checked)
            locked[key] = result.colors[key];
        else
            locked.erase (key);
        regenerate();
    });

    connect (describeButton, &QPushButton::clicked, this, &ThemeGeneratorDialog::describe);
    connect (description, &QLineEdit::returnPressed, this, &ThemeGeneratorDialog::describe);
    connect (aiSetup, &QPushButton::clicked, this, &ThemeGeneratorDialog::aiSettings);
    connect (ai, &themeai::Client::suggested, this, &ThemeGeneratorDialog::suggested);
    connect (ai, &themeai::Client::failed, this, [this] (const QString& message)
    {
        aiStatus->setText (message);
        updateAiControls();
    });

    connect (buttons, &QDialogButtonBox::accepted, this, [this] { if (save()) accept(); });
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect (exportButton, &QPushButton::clicked, this, &ThemeGeneratorDialog::exportFile);

    updateAiControls();
    regenerate();
}

ThemeGeneratorDialog::~ThemeGeneratorDialog() = default;

void ThemeGeneratorDialog::done (int r)
{
    ai->cancel();
    applyTimer->stop();
    if (r != QDialog::Accepted)
        Theme::apply (original);   // back to the theme from before
    QDialog::done (r);
}

Theme ThemeGeneratorDialog::theme() const
{
    Theme t;
    for (const auto& b : Theme::builtIns())
        if (b.name == (result.dark ? QStringLiteral ("PHI Dark") : QStringLiteral ("PHI Light")))
            t = b;
    t.name = name->text().trimmed().isEmpty() ? tr ("My Theme") : name->text().trimmed();
    t.source = QStringLiteral ("generator");
    t.dark = result.dark;
    for (const auto& [key, value] : result.colors)
        t.colors[QString::fromStdString (key)] = toQColor (value);
    t.spectrogram.clear();
    for (const auto& c : result.spectrogram)
        t.spectrogram.push_back (toQColor (c));
    return t;
}

QString ThemeGeneratorDialog::json() const
{
    const auto n = name->text().trimmed().isEmpty() ? tr ("My Theme") : name->text().trimmed();
    return QString::fromStdString (themegen::toJson (n.toStdString(), result, settings));
}

void ThemeGeneratorDialog::regenerate()
{
    result = themegen::generate (settings, locked);
    if (autoFix->isChecked())
        themegen::fixContrast (result.colors, locked);
    showSettings();
    showColors();
    showContrast();
    preview->setTheme (theme());
    previewSoon();
}

void ThemeGeneratorDialog::previewSoon() { applyTimer->start(); }

void ThemeGeneratorDialog::showSettings()
{
    updating = true;
    seedButton->setIcon (swatchIcon (toQColor (settings.seed)));
    seedHex->setText (QString::fromStdString (themegen::toHex (settings.seed)));
    mode->setCurrentIndex (settings.dark ? 0 : 1);
    harmony->setCurrentIndex ((int) settings.harmony);
    meters->setCurrentIndex (settings.meters == themegen::Meters::classic ? 0 : 1);
    saturation->setValue ((int) std::lround (settings.saturation * 100));
    contrast->setValue ((int) std::lround (settings.contrast * 100));
    tint->setValue ((int) std::lround (settings.tint * 100));
    updating = false;
}

void ThemeGeneratorDialog::showColors()
{
    updating = true;
    const auto& keys = themegen::colorKeys();
    colors->setRowCount ((int) keys.size());
    for (int row = 0; row < (int) keys.size(); ++row)
    {
        const auto& key = keys[(size_t) row];
        const auto c = result.colors[key];
        auto* label = new QTableWidgetItem (swatchIcon (toQColor (c)), QString::fromStdString (key));
        label->setData (Qt::UserRole, QString::fromStdString (key));
        colors->setItem (row, 0, label);
        colors->setItem (row, 1, new QTableWidgetItem (QString::fromStdString (themegen::toHex (c))));
        auto* keep = new QTableWidgetItem;
        keep->setFlags (Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        keep->setCheckState (locked.count (key) ? Qt::Checked : Qt::Unchecked);
        keep->setData (Qt::AccessibleTextRole, tr ("Keep %1").arg (QString::fromStdString (key)));
        colors->setItem (row, 2, keep);
    }
    updating = false;
}

void ThemeGeneratorDialog::showContrast()
{
    checks->clear();
    int failing = 0;
    for (const auto& c : themegen::checkContrast (result.colors))
    {
        const auto text = QStringLiteral ("%1  %2: %3 (%4 on %5)")
                              .arg (c.ok() ? QStringLiteral ("✓") : QStringLiteral ("✗"), tr (c.what.c_str()))
                              .arg (c.ratio, 0, 'f', 1)
                              .arg (QString::fromStdString (c.foreground), QString::fromStdString (c.background));
        auto* item = new QListWidgetItem (text, checks);
        if (! c.ok())
        {
            ++failing;
            item->setToolTip (tr ("Needs %1. A kept colour stops the automatic fix: untick Keep, or pick it again.").arg (c.needed, 0, 'f', 1));
        }
    }
    checks->setAccessibleDescription (failing == 0 ? tr ("All pass") : tr ("%n do not pass", nullptr, failing));
}

void ThemeGeneratorDialog::setSeed (const themegen::Rgb& c)
{
    settings.seed = c;
    settings.seed.a = 1;
    regenerate();
}

void ThemeGeneratorDialog::startFromPicture (const QString& path)
{
    QImage image (path);
    if (image.isNull())
    {
        QMessageBox::warning (this, tr ("Theme Generator"), tr ("%1 is not a picture PHI can read.").arg (path));
        return;
    }
    image = image.scaled (64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat (QImage::Format_ARGB32);
    std::vector<themegen::Rgb> pixels;
    pixels.reserve ((size_t) (image.width() * image.height()));
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            pixels.push_back (fromQColor (image.pixelColor (x, y)));

    const auto palette = themegen::paletteFromPixels (pixels, 6);
    settings = themegen::settingsFromPalette (palette, settings);
    settings.variation = 0;
    locked.clear();
    if (name->text() == tr ("My Theme"))
        name->setText (QFileInfo (path).completeBaseName());
    showPalette (palette);
    regenerate();
}

void ThemeGeneratorDialog::showPalette (const std::vector<themegen::Swatch>& palette)
{
    // Drop the old swatches (everything between the label and the stretch).
    while (paletteRow->count() > 2)
    {
        auto* item = paletteRow->takeAt (1);
        delete item->widget();
        delete item;
    }
    paletteLabel->setVisible (! palette.empty());
    for (const auto& s : palette)
    {
        const auto color = toQColor (s.color);
        auto* b = new QPushButton (swatchIcon (color, 22), QString());
        b->setIconSize (QSize (22, 22));
        b->setToolTip (tr ("%1, %2% of the picture").arg (color.name()).arg (std::lround (s.share * 100)));
        b->setAccessibleName (tr ("Use %1").arg (color.name()));
        connect (b, &QPushButton::clicked, this, [this, c = s.color] { setSeed (c); });
        paletteRow->insertWidget (paletteRow->count() - 1, b);
    }
}

void ThemeGeneratorDialog::startFromTheme (int index)
{
    if (index <= 0)
        return;
    const auto themes = Theme::available();
    const auto wanted = fromTheme->itemText (index);
    fromTheme->setCurrentIndex (0);
    for (const auto& t : themes)
    {
        if (t.name != wanted)
            continue;
        settings.seed = fromQColor (t.color (QStringLiteral ("highlight")));
        settings.seed.a = 1;
        settings.dark = t.dark;
        settings.variation = 0;
        locked.clear();

        // A theme made here keeps its settings; open it as it was made.
        QFile file (t.source);
        if (t.source != QLatin1String ("built-in") && file.open (QIODevice::ReadOnly))
        {
            const auto root = QJsonDocument::fromJson (file.readAll()).object();
            const auto g = root.value (QStringLiteral ("generator")).toObject();
            if (! g.isEmpty())
            {
                themeai::Suggestion s;
                QString error;
                if (themeai::parseSuggestion (QString::fromUtf8 (QJsonDocument (g).toJson()), settings, s, error))
                    settings = s.settings;
                settings.variation = g.value (QStringLiteral ("variation")).toInt();
                // Colours that differ from what the settings give were chosen by hand: keep them.
                const auto generated = themegen::generate (settings);
                for (auto it = t.colors.begin(); it != t.colors.end(); ++it)
                {
                    const auto key = it.key().toStdString();
                    const auto made = generated.colors.find (key);
                    if (made != generated.colors.end() && QColor (toQColor (made->second)).name (QColor::HexArgb) != it.value().name (QColor::HexArgb))
                        locked[key] = fromQColor (it.value());
                }
            }
            name->setText (t.name);
        }
        else
            name->setText (tr ("%1 Variation").arg (t.name));
        regenerate();
        return;
    }
}

void ThemeGeneratorDialog::startFromReaper()
{
    const auto path = QFileDialog::getOpenFileName (this, tr ("Colours From a REAPER Theme"), QDir::homePath(),
                                                    tr ("REAPER themes (*.ReaperTheme *.ReaperThemeZip)"));
    if (path.isEmpty())
        return;

    std::string text;
    if (path.endsWith (QStringLiteral (".ReaperThemeZip"), Qt::CaseInsensitive))
    {
        zip::Reader reader;
        std::string error;
        std::vector<uint8_t> bytes;
        bool found = false;
        if (reader.open (path.toStdString(), error))
            for (const auto& entry : reader.entries())
                if (QString::fromStdString (entry.name).endsWith (QStringLiteral (".ReaperTheme"), Qt::CaseInsensitive)
                    && reader.read (entry, bytes, error))
                {
                    text.assign (bytes.begin(), bytes.end());
                    found = true;
                    break;
                }
        if (! found)
        {
            QMessageBox::warning (this, tr ("Theme Generator"),
                                  tr ("No .ReaperTheme file was found in %1.%2").arg (path, error.empty() ? QString() : QStringLiteral (" ") + QString::fromStdString (error)));
            return;
        }
    }
    else
    {
        QFile file (path);
        if (! file.open (QIODevice::ReadOnly))
        {
            QMessageBox::warning (this, tr ("Theme Generator"), tr ("%1 cannot be read.").arg (path));
            return;
        }
        text = file.readAll().toStdString();
    }

    themegen::Colors imported;
    if (! themegen::fromReaperTheme (text, imported, settings))
    {
        QMessageBox::warning (this, tr ("Theme Generator"), tr ("%1 has no colours PHI can use.").arg (path));
        return;
    }
    settings.variation = 0;
    locked = imported;
    name->setText (QFileInfo (path).completeBaseName());
    showPalette ({});
    regenerate();
    aiStatus->setText (tr ("Took %n colours from the REAPER theme and kept them (ticked); the rest are generated to match.", nullptr, (int) imported.size()));
}

void ThemeGeneratorDialog::editColor (int row)
{
    const auto key = colors->item (row, 0)->data (Qt::UserRole).toString().toStdString();
    const auto c = QColorDialog::getColor (toQColor (result.colors[key]), this, QString::fromStdString (key), QColorDialog::ShowAlphaChannel);
    if (! c.isValid())
        return;
    locked[key] = fromQColor (c);
    regenerate();
}

void ThemeGeneratorDialog::updateAiControls()
{
    const auto config = themeai::Config::load();
    description->setEnabled (config.enabled && ! ai->busy());
    describeButton->setEnabled (config.enabled && ! ai->busy());
    describeButton->setText (ai->busy() ? tr ("Working...") : tr ("&Make It"));
    if (! config.enabled && aiStatus->text().isEmpty())
        aiStatus->setText (tr ("Off. AI Settings can turn it on with a model on this computer (such as Ollama), or Claude in the cloud."));
}

void ThemeGeneratorDialog::describe()
{
    const auto config = themeai::Config::load();
    const auto text = description->text().trimmed();
    if (! config.enabled || text.isEmpty() || ai->busy())
        return;
    aiStatus->setText (config.provider == themeai::Provider::claude
                           ? tr ("Asking Claude...")
                           : tr ("Asking %1 at %2...").arg (config.model, config.endpoint));
    ai->describe (config, text, settings);
    updateAiControls();
}

void ThemeGeneratorDialog::suggested (const themeai::Suggestion& s)
{
    settings = s.settings;
    locked = s.colors;
    if (! s.name.isEmpty())
        name->setText (s.name);
    showPalette ({});
    regenerate();
    aiStatus->setText (tr ("Made from your description. Change anything you like; Shuffle tries other accents."));
    updateAiControls();
}

void ThemeGeneratorDialog::aiSettings()
{
    auto config = themeai::Config::load();

    QDialog d (this);
    d.setWindowTitle (tr ("Theme AI Settings"));
    auto* enabled = new QCheckBox (tr ("&Use AI to make themes from a description"));
    enabled->setChecked (config.enabled);
    auto* local = new QRadioButton (tr ("A model on &this computer (nothing leaves it)"));
    auto* claude = new QRadioButton (tr ("&Claude, in the cloud (sends your description to Anthropic; needs an API key)"));
    (config.provider == themeai::Provider::claude ? claude : local)->setChecked (true);

    auto* server = new QComboBox;
    for (const auto& p : themeai::localPresets())
        server->addItem (p.name, p.endpoint);
    server->addItem (tr ("Other"));
    auto* endpoint = new QLineEdit (config.endpoint);
    endpoint->setAccessibleName (tr ("Server address"));
    auto* model = new QLineEdit (config.model);
    model->setAccessibleName (tr ("Model"));
    model->setPlaceholderText (tr ("as your server names it, for example llama3.2 or qwen2.5"));
    auto* key = new QLineEdit (config.apiKey);
    key->setEchoMode (QLineEdit::Password);
    key->setAccessibleName (tr ("API key"));
    key->setPlaceholderText (tr ("Claude: from console.anthropic.com, or leave empty to use ANTHROPIC_API_KEY"));
    auto* note = new QLabel (tr ("A local model needs a server running, such as Ollama (ollama serve, then ollama pull llama3.2). "
                                 "The key is kept in PHI's settings file."));
    note->setWordWrap (true);

    const auto sync = [=]
    {
        const bool isLocal = local->isChecked();
        server->setEnabled (isLocal);
        endpoint->setEnabled (isLocal);
    };
    connect (local, &QRadioButton::toggled, &d, [=] (bool on)
    {
        sync();
        if (on && (model->text() == QLatin1String ("claude-opus-5-5") || model->text().isEmpty()))
            model->setText (themeai::localPresets().first().model);
        else if (! on && ! model->text().startsWith (QLatin1String ("claude")))
            model->setText (QStringLiteral ("claude-opus-5-5"));
    });
    const auto presetIndex = server->findData (config.endpoint);
    server->setCurrentIndex (presetIndex >= 0 ? presetIndex : server->count() - 1);
    connect (server, &QComboBox::activated, &d, [=] (int i)
    {
        const auto presets = themeai::localPresets();
        if (i < presets.size())
        {
            endpoint->setText (presets[i].endpoint);
            if (! presets[i].model.isEmpty())
                model->setText (presets[i].model);
        }
    });
    sync();

    auto* form = new QFormLayout;
    form->addRow (tr ("&Server:"), server);
    form->addRow (tr ("&Address:"), endpoint);
    form->addRow (tr ("&Model:"), model);
    form->addRow (tr ("API &key:"), key);
    auto* buttons = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect (buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    auto* layout = new QVBoxLayout (&d);
    layout->addWidget (enabled);
    layout->addWidget (local);
    layout->addWidget (claude);
    layout->addLayout (form);
    layout->addWidget (note);
    layout->addWidget (buttons);
    d.resize (560, d.sizeHint().height());

    if (d.exec() != QDialog::Accepted)
        return;
    config.enabled = enabled->isChecked();
    config.provider = claude->isChecked() ? themeai::Provider::claude : themeai::Provider::local;
    config.endpoint = endpoint->text().trimmed();
    config.model = model->text().trimmed();
    config.apiKey = key->text().trimmed();
    config.save();
    aiStatus->clear();
    updateAiControls();
}

bool ThemeGeneratorDialog::save()
{
    const auto themeName = name->text().trimmed();
    if (themeName.isEmpty())
    {
        name->setFocus();
        QMessageBox::information (this, tr ("Theme Generator"), tr ("Give the theme a name first."));
        return false;
    }
    for (const auto& b : Theme::builtIns())
        if (b.name.compare (themeName, Qt::CaseInsensitive) == 0)
        {
            QMessageBox::information (this, tr ("Theme Generator"), tr ("%1 is a built-in theme. Choose another name.").arg (themeName));
            return false;
        }

    QDir().mkpath (Theme::userThemeFolder());
    const auto path = Theme::userThemeFolder() + QLatin1Char ('/') + fileNameFor (themeName);
    if (QFileInfo::exists (path)
        && QMessageBox::question (this, tr ("Theme Generator"), tr ("Replace the theme in %1?").arg (path)) != QMessageBox::Yes)
        return false;

    QFile file (path);
    if (! file.open (QIODevice::WriteOnly | QIODevice::Truncate) || file.write (json().toUtf8()) < 0)
    {
        QMessageBox::warning (this, tr ("Theme Generator"), tr ("%1 could not be written.").arg (path));
        return false;
    }
    file.close();

    // Use the theme as Options > Theme will read it from now on.
    Theme saved;
    QString error;
    if (! Theme::loadFile (path, saved, error))
    {
        QMessageBox::warning (this, tr ("Theme Generator"), error);
        return false;
    }
    applyTimer->stop();
    Theme::apply (saved);
    Theme::saveName (saved.name);
    original = saved;
    return true;
}

void ThemeGeneratorDialog::exportFile()
{
    const auto path = QFileDialog::getSaveFileName (this, tr ("Export Theme"), QDir::homePath() + QLatin1Char ('/') + fileNameFor (name->text()),
                                                    tr ("PHI themes (*.json)"));
    if (path.isEmpty())
        return;
    QFile file (path);
    if (! file.open (QIODevice::WriteOnly | QIODevice::Truncate) || file.write (json().toUtf8()) < 0)
        QMessageBox::warning (this, tr ("Theme Generator"), tr ("%1 could not be written.").arg (path));
}
