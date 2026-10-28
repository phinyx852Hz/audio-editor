// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/Ambisonics.h"

#include <QDialog>
#include <QStringList>
#include <vector>

class QLabel;
class QLineEdit;
class QListWidget;
class QTableWidget;
class StudioHud;

// Your own speaker arrays (up to 16 channels), kept in the settings by name. The ambisonic
// decoders (Spatial > Decode Ambisonics and the Ambisonic Studio's master bus) list them as
// "Custom: <name>".
namespace speakerarrays
{
    QStringList names();
    std::vector<ambi::Speaker> get (const QString& name);   // empty when there is none by that name
    void put (const QString& name, const std::vector<ambi::Speaker>&);
    void remove (const QString& name);
}

// The HRTF sets (.sofa) found: your own, then PHI's, then the system's (SpatialSection.cpp).
QStringList spatialHrtfFiles();

// Spatial > Speaker Arrays: make, change and delete your arrays, seen from above and the side.
class SpeakerArraysDialog final : public QDialog
{
    Q_OBJECT

public:
    SpeakerArraysDialog (const QString& initial, QWidget* parent);
    QString chosen() const { return current; }   // the array last saved or picked

private:
    void loadList();
    void showArray (const QString& name, const std::vector<ambi::Speaker>&);
    void addRow (const ambi::Speaker&);
    std::vector<ambi::Speaker> fromTable (QString& error) const;
    void refreshView();
    bool save();

    QListWidget* list;
    QLineEdit* nameEdit;
    QTableWidget* table;
    StudioHud* view;
    QLabel* status;
    QString current;
    bool updating = false;
};
