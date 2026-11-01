// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../core/ThemeGen.h"

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// The Theme Generator's optional "Describe it" mode: a language model turns a few words
// ("a 1970s tape deck", "deep sea at night") into generator settings. Off until set up.
//
// It talks to a model running on this computer by default (Ollama, LM Studio, the
// llama.cpp server or anything else with an OpenAI-style /chat/completions endpoint),
// so nothing leaves the machine. Claude, in the cloud, is the optional other choice and
// needs an API key. Either way the model only picks settings and colours: the generator
// builds the theme and its contrast checks still apply.
namespace themeai
{
    enum class Provider { local, claude };

    struct Config
    {
        Provider provider = Provider::local;
        QString endpoint = QStringLiteral ("http://localhost:11434/v1");   // Ollama
        QString model = QStringLiteral ("llama3.2");
        QString apiKey;   // Claude only; ANTHROPIC_API_KEY is used when this is empty

        bool enabled = false;   // set in the AI settings, off on a new install

        static Config load();   // from settings.ini
        void save() const;
    };

    // Known local servers, for the settings: name and endpoint.
    struct Preset { QString name, endpoint, model; };
    QVector<Preset> localPresets();

    struct Suggestion
    {
        QString name;
        themegen::Settings settings;
        themegen::Colors colors;   // colours the model chose itself; they are locked
    };

    // The model's answer to a theme, from its text (which may have words around the JSON).
    bool parseSuggestion (const QString& text, const themegen::Settings& start, Suggestion& out, QString& error);

    // One request at a time; a new one cancels the last.
    class Client final : public QObject
    {
        Q_OBJECT

    public:
        explicit Client (QObject* parent = nullptr);
        ~Client() override;

        void describe (const Config&, const QString& description, const themegen::Settings& start);
        void cancel();
        bool busy() const { return reply != nullptr; }

    signals:
        void suggested (const themeai::Suggestion&);
        void failed (const QString& message);

    private:
        void finished();

        QNetworkAccessManager* network;
        QNetworkReply* reply = nullptr;
        Provider replyProvider = Provider::local;
        themegen::Settings replyStart;
    };
}
