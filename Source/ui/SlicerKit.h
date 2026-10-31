// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// The Slicer's kits on disk: as JSON inside a .phi project, and as kit files of their own
// (.phikit, with the kit's sample beside it). Pads' own sounds are kept as the files they
// came from, by a path relative to the project or kit where it can be.
#include "../sampler/SlicerEngine.h"
#include "../core/AudioData.h"

#include <QDir>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

namespace slicerkit
{
    // A sound file at `rate`, its first two channels; null (and `error` set) when it cannot be read.
    sampler::SamplePtr loadSound (const QString& path, double rate, QString& error, AudioDataPtr* audio = nullptr);
    AudioDataPtr asAudio (const sampler::Sample&, double rate);
    bool saveSound (const sampler::Sample&, double rate, const QString& path, QString& error);   // a 32-bit WAV

    QString storedPath (const QString& file, const QDir& base);   // relative to base, unless far from it
    QString resolvedPath (const QString& stored, const QDir& base);

    // The kit as JSON. A pad's own sound with no file is given one by `store` (which writes it
    // and gives where, or empty to leave it out).
    using Store = std::function<QString (const sampler::Sample&, int pad, int layer)>;
    QJsonObject toJson (const slicer::Kit&, const QDir& base, const Store& store = {});
    // And back, with the pads' own sounds read at `rate`; what could not be read is added to `problems`.
    // Kits from before 128 pads open as they were (Chromatic ones on the same notes).
    slicer::Kit fromJson (const QJsonObject&, const QDir& base, double rate, QStringList* problems = nullptr);

    // The step sequencer's patterns (sampler/StepSequencer.h): only the lanes and steps that
    // differ from new ones are written.
    QJsonObject songToJson (const slicer::Song&);
    std::unique_ptr<slicer::Song> songFromJson (const QJsonObject&);

    // A kit file: the kit, its patterns (if given) and its sample as a WAV in "<name> Sounds" beside it.
    bool saveKitFile (const QString& path, const slicer::Kit&, const sampler::SamplePtr& sample, double rate, QString& error,
                      const slicer::Song* song = nullptr);
    struct Loaded
    {
        slicer::Kit kit;
        sampler::SamplePtr sample;
        AudioDataPtr audio;
        std::unique_ptr<slicer::Song> song;   // null: a kit saved without patterns
        QStringList problems;
    };
    bool loadKitFile (const QString& path, double rate, Loaded& out, QString& error);

    // Where the owner's own PHI tabla default is kept (Make This the PHI tabla Default), in PHI's data folder.
    QString phiTablaDefaultPath();
}
