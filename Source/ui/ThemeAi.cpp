// SPDX-License-Identifier: AGPL-3.0-only
#include "ThemeAi.h"
#include "Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <algorithm>

namespace themeai
{
namespace
{
    constexpr auto claudeEndpoint = "https://api.anthropic.com/v1/messages";
    constexpr auto claudeModel = "claude-opus-5-5";

    QString instructions()
    {
        QStringList keys;
        for (const auto& k : themegen::colorKeys())
            keys << QString::fromStdString (k);

        return QStringLiteral (
            "You design colour themes for PHI, an audio editor. The user describes a look in a few words. "
            "Reply with one JSON object and nothing else, with these fields:\n"
            "- name: a short name for the theme, one to four words\n"
            "- seed: the main colour as #rrggbb; it colours the waveform, selections and highlights\n"
            "- dark: true for a dark theme, false for a light one\n"
            "- harmony: how the other colours relate to the seed, one of mono, analogous, complementary, split-complementary, triadic\n"
            "- saturation: 0 (grey) to 2 (vivid), 1 is the seed as it is\n"
            "- contrast: 0 (soft) to 1 (strong)\n"
            "- tint: 0 to 1, how much the seed tints the backgrounds\n"
            "- meters: \"classic\" for green, yellow and red level meters, or \"palette\" for meters in the theme's own colours\n"
            "- colors: optional, an object setting particular colours as #rrggbb, only where the description calls for one. "
            "The keys are: %1\n"
            "Leave out colours you have no reason to set: the app works out the rest and checks that text stays readable.")
            .arg (keys.join (QStringLiteral (", ")));
    }

    QString settingsKey (const char* name) { return QStringLiteral ("themeAi/") + QLatin1String (name); }
}

Config Config::load()
{
    auto s = appSettings();
    Config c;
    c.enabled = s.value (settingsKey ("enabled"), false).toBool();
    c.provider = s.value (settingsKey ("provider")).toString() == QLatin1String ("claude") ? Provider::claude : Provider::local;
    c.endpoint = s.value (settingsKey ("endpoint"), c.endpoint).toString();
    c.model = s.value (settingsKey ("model"), c.provider == Provider::claude ? QString::fromLatin1 (claudeModel) : c.model).toString();
    c.apiKey = s.value (settingsKey ("apiKey")).toString();
    return c;
}

void Config::save() const
{
    auto s = appSettings();
    s.setValue (settingsKey ("enabled"), enabled);
    s.setValue (settingsKey ("provider"), provider == Provider::claude ? QStringLiteral ("claude") : QStringLiteral ("local"));
    s.setValue (settingsKey ("endpoint"), endpoint);
    s.setValue (settingsKey ("model"), model);
    if (apiKey.isEmpty())
        s.remove (settingsKey ("apiKey"));
    else
        s.setValue (settingsKey ("apiKey"), apiKey);
}

QVector<Preset> localPresets()
{
    return {
        { QStringLiteral ("Ollama"), QStringLiteral ("http://localhost:11434/v1"), QStringLiteral ("llama3.2") },
        { QStringLiteral ("LM Studio"), QStringLiteral ("http://localhost:1234/v1"), QString() },
        { QStringLiteral ("llama.cpp server"), QStringLiteral ("http://localhost:8080/v1"), QString() },
    };
}

bool parseSuggestion (const QString& text, const themegen::Settings& start, Suggestion& out, QString& error)
{
    // Models often wrap the object in words or a ``` block: take the outermost braces.
    const auto first = text.indexOf (QLatin1Char ('{'));
    const auto last = text.lastIndexOf (QLatin1Char ('}'));
    if (first < 0 || last <= first)
    {
        error = QObject::tr ("The model did not answer with theme settings.");
        return false;
    }

    QJsonParseError parseError;
    const auto json = QJsonDocument::fromJson (text.mid (first, last - first + 1).toUtf8(), &parseError);
    if (! json.isObject())
    {
        error = QObject::tr ("The model's answer could not be read: %1").arg (parseError.errorString());
        return false;
    }

    const auto o = json.object();
    out = {};
    out.settings = start;
    out.name = o.value (QStringLiteral ("name")).toString().trimmed().left (60);

    themegen::Rgb seed;
    if (! themegen::fromHex (o.value (QStringLiteral ("seed")).toString().toStdString(), seed))
    {
        error = QObject::tr ("The model's answer has no main colour.");
        return false;
    }
    seed.a = 1;
    out.settings.seed = seed;

    if (o.contains (QStringLiteral ("dark")))
        out.settings.dark = o.value (QStringLiteral ("dark")).toBool (start.dark);
    themegen::Harmony h;
    if (themegen::harmonyFromName (o.value (QStringLiteral ("harmony")).toString().toLower().toStdString(), h))
        out.settings.harmony = h;
    const auto number = [&o] (const char* key, double fallback, double low, double high)
    {
        const auto v = o.value (QLatin1String (key));
        return v.isDouble() ? std::clamp (v.toDouble(), low, high) : fallback;
    };
    out.settings.saturation = number ("saturation", start.saturation, 0.0, 2.0);
    out.settings.contrast = number ("contrast", start.contrast, 0.0, 1.0);
    out.settings.tint = number ("tint", start.tint, 0.0, 1.0);
    const auto meters = o.value (QStringLiteral ("meters")).toString();
    if (meters == QLatin1String ("palette"))
        out.settings.meters = themegen::Meters::palette;
    else if (meters == QLatin1String ("classic"))
        out.settings.meters = themegen::Meters::classic;
    out.settings.variation = 0;

    // Only known keys and valid colours; anything else is ignored rather than refused.
    const auto colors = o.value (QStringLiteral ("colors")).toObject();
    const auto& keys = themegen::colorKeys();
    for (auto it = colors.begin(); it != colors.end(); ++it)
    {
        themegen::Rgb c;
        const auto key = it.key().toStdString();
        if (std::find (keys.begin(), keys.end(), key) != keys.end() && themegen::fromHex (it.value().toString().toStdString(), c))
            out.colors[key] = c;
    }
    return true;
}

Client::Client (QObject* parent) : QObject (parent), network (new QNetworkAccessManager (this)) {}

Client::~Client() { cancel(); }

void Client::cancel()
{
    if (reply == nullptr)
        return;
    auto* r = reply;
    reply = nullptr;
    r->disconnect (this);
    r->abort();
    r->deleteLater();
}

void Client::describe (const Config& config, const QString& description, const themegen::Settings& start)
{
    cancel();
    replyProvider = config.provider;
    replyStart = start;

    QJsonObject body;
    QNetworkRequest request;
    request.setTransferTimeout (180000);   // a local model on a CPU can take a while
    request.setHeader (QNetworkRequest::ContentTypeHeader, QStringLiteral ("application/json"));

    if (config.provider == Provider::claude)
    {
        auto key = config.apiKey;
        if (key.isEmpty())
            key = qEnvironmentVariable ("ANTHROPIC_API_KEY");
        if (key.isEmpty())
        {
            Q_EMIT failed (tr ("Claude needs an API key: add one in AI Settings, or set ANTHROPIC_API_KEY."));
            return;
        }
        request.setUrl (QUrl (QString::fromLatin1 (claudeEndpoint)));
        request.setRawHeader ("x-api-key", key.toUtf8());
        request.setRawHeader ("anthropic-version", "2026-10-21");
        // When the model declines, the API retries on another model by itself.
        request.setRawHeader ("anthropic-beta", "server-side-fallback-2026-07-01");
        body[QStringLiteral ("model")] = config.model.isEmpty() ? QString::fromLatin1 (claudeModel) : config.model;
        body[QStringLiteral ("max_tokens")] = 16000;
        body[QStringLiteral ("output_config")] = QJsonObject { { QStringLiteral ("effort"), QStringLiteral ("low") } };
        body[QStringLiteral ("fallbacks")] = QStringLiteral ("default");
        body[QStringLiteral ("system")] = instructions();
        body[QStringLiteral ("messages")] = QJsonArray { QJsonObject { { QStringLiteral ("role"), QStringLiteral ("user") },
                                                                        { QStringLiteral ("content"), description } } };
    }
    else
    {
        auto base = config.endpoint.trimmed();
        while (base.endsWith (QLatin1Char ('/')))
            base.chop (1);
        const QUrl url (base + QStringLiteral ("/chat/completions"));
        if (! url.isValid() || base.isEmpty())
        {
            Q_EMIT failed (tr ("The local AI address is not valid: %1").arg (config.endpoint));
            return;
        }
        request.setUrl (url);
        if (! config.apiKey.isEmpty())
            request.setRawHeader ("Authorization", "Bearer " + config.apiKey.toUtf8());
        body[QStringLiteral ("model")] = config.model;
        body[QStringLiteral ("stream")] = false;
        body[QStringLiteral ("messages")] = QJsonArray {
            QJsonObject { { QStringLiteral ("role"), QStringLiteral ("system") }, { QStringLiteral ("content"), instructions() } },
            QJsonObject { { QStringLiteral ("role"), QStringLiteral ("user") }, { QStringLiteral ("content"), description } } };
    }

    reply = network->post (request, QJsonDocument (body).toJson (QJsonDocument::Compact));
    connect (reply, &QNetworkReply::finished, this, &Client::finished);
}

void Client::finished()
{
    auto* r = reply;
    reply = nullptr;
    if (r == nullptr)
        return;
    r->deleteLater();

    const auto data = r->readAll();
    const auto json = QJsonDocument::fromJson (data).object();
    if (r->error() != QNetworkReply::NoError)
    {
        auto message = json.value (QStringLiteral ("error")).toObject().value (QStringLiteral ("message")).toString();
        if (message.isEmpty())
            message = r->errorString();
        if (replyProvider == Provider::local && r->error() == QNetworkReply::ConnectionRefusedError)
            message = tr ("No AI server is answering at %1. Start Ollama (or your local server) and try again.").arg (r->url().toString());
        Q_EMIT failed (message);
        return;
    }

    QString text;
    if (replyProvider == Provider::claude)
    {
        if (json.value (QStringLiteral ("stop_reason")).toString() == QLatin1String ("refusal"))
        {
            Q_EMIT failed (tr ("Claude declined this description. Try describing the look in other words."));
            return;
        }
        for (const auto& block : json.value (QStringLiteral ("content")).toArray())
            if (block.toObject().value (QStringLiteral ("type")).toString() == QLatin1String ("text"))
                text += block.toObject().value (QStringLiteral ("text")).toString();
    }
    else
    {
        const auto choices = json.value (QStringLiteral ("choices")).toArray();
        if (! choices.isEmpty())
            text = choices.first().toObject().value (QStringLiteral ("message")).toObject().value (QStringLiteral ("content")).toString();
    }

    Suggestion s;
    QString error;
    if (! parseSuggestion (text, replyStart, s, error))
    {
        Q_EMIT failed (error);
        return;
    }
    Q_EMIT suggested (s);
}
}
