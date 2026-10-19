// SPDX-License-Identifier: AGPL-3.0-only
#include "MilkDrop.h"

#include <projectM-4/playlist.h>
#include <projectM-4/projectM.h>

#include <QDir>
#include <QFileInfo>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QStandardPaths>

#include <algorithm>

bool MilkDrop::builtIn() { return true; }

QString MilkDrop::userPresetFolder()
{
    return QStandardPaths::writableLocation (QStandardPaths::GenericDataLocation) + QStringLiteral ("/" APP_ID "/milkdrop");
}

QStringList MilkDrop::presetFolders()
{
    QStringList found;
    for (const auto& folder : { userPresetFolder(), QStringLiteral ("/usr/share/projectM/presets"), QStringLiteral ("/usr/share/projectm/presets"),
                                QStringLiteral ("/usr/local/share/projectM/presets"), QStringLiteral ("/usr/share/projectm-presets"),
                                QStringLiteral ("/usr/share/milkdrop/presets") })
        if (QFileInfo (folder).isDir())
            found << folder;
    return found;
}

struct MilkDrop::State
{
    QOpenGLContext context;
    QOffscreenSurface surface;
    std::unique_ptr<QOpenGLFramebufferObject> target;
    projectm_handle pm = nullptr;
    projectm_playlist_handle playlist = nullptr;
    QString error;
    bool switched = false;
    bool isLocked = false;
    std::vector<float> interleaved;

    bool makeCurrent() { return context.makeCurrent (&surface); }
};

namespace
{
    void presetSwitched (bool, unsigned int, void* user) { static_cast<bool*> (user)[0] = true; }
}

MilkDrop::MilkDrop() : state (std::make_unique<State>())
{
    auto& s = *state;
    QSurfaceFormat format;
    format.setRenderableType (QSurfaceFormat::OpenGL);
    format.setVersion (3, 3);
    format.setProfile (QSurfaceFormat::CoreProfile);
    s.context.setFormat (format);
    s.surface.setFormat (format);
    s.surface.create();
    if (! s.context.create() || ! s.makeCurrent())
    {
        s.error = QObject::tr ("OpenGL 3.3 is not available, which MilkDrop needs");
        return;
    }

    s.pm = projectm_create();
    if (s.pm == nullptr)
    {
        s.error = QObject::tr ("projectM could not start (it needs OpenGL 3.3)");
        s.context.doneCurrent();
        return;
    }
    projectm_set_preset_duration (s.pm, 30.0);
    projectm_set_soft_cut_duration (s.pm, 3.0);
    projectm_set_hard_cut_enabled (s.pm, false);
    projectm_set_mesh_size (s.pm, 64, 48);
    projectm_set_fps (s.pm, 30);

    s.playlist = projectm_playlist_create (s.pm);
    projectm_playlist_set_shuffle (s.playlist, true);
    projectm_playlist_set_preset_switched_event_callback (s.playlist, presetSwitched, &s.switched);
    s.context.doneCurrent();
    reloadPresets();
}

MilkDrop::~MilkDrop()
{
    auto& s = *state;
    if (s.pm != nullptr && s.makeCurrent())
    {
        if (s.playlist != nullptr)
            projectm_playlist_destroy (s.playlist);
        projectm_destroy (s.pm);
        s.target.reset();
        s.context.doneCurrent();
    }
}

void MilkDrop::reloadPresets()
{
    auto& s = *state;
    if (s.playlist == nullptr || ! s.makeCurrent())
        return;
    projectm_playlist_clear (s.playlist);
    std::vector<QByteArray> textureFolders;
    for (const auto& folder : presetFolders())
    {
        projectm_playlist_add_path (s.playlist, QFile::encodeName (folder).constData(), true, false);
        textureFolders.push_back (QFile::encodeName (folder));
        for (const auto& extra : { QStringLiteral ("/textures"), QStringLiteral ("/../textures") })
            if (QFileInfo (folder + extra).isDir())
                textureFolders.push_back (QFile::encodeName (QFileInfo (folder + extra).canonicalFilePath()));
    }
    std::vector<const char*> paths;
    for (const auto& t : textureFolders)
        paths.push_back (t.constData());
    projectm_set_texture_search_paths (s.pm, paths.data(), paths.size());
    if (projectm_playlist_size (s.playlist) > 0)
        projectm_playlist_play_next (s.playlist, true);
    s.context.doneCurrent();
}

bool MilkDrop::ready() const { return state->pm != nullptr; }
QString MilkDrop::error() const { return state->error; }
int MilkDrop::presetCount() const { return state->playlist != nullptr ? (int) projectm_playlist_size (state->playlist) : 0; }
int MilkDrop::position() const { return state->playlist != nullptr ? (int) projectm_playlist_get_position (state->playlist) : 0; }

QStringList MilkDrop::presets() const
{
    QStringList out;
    if (state->playlist == nullptr)
        return out;
    const auto count = projectm_playlist_size (state->playlist);
    auto** items = projectm_playlist_items (state->playlist, 0, count);
    for (auto** i = items; i != nullptr && *i != nullptr; ++i)
        out << QFile::decodeName (*i);
    projectm_playlist_free_string_array (items);
    return out;
}

QString MilkDrop::presetName() const
{
    if (presetCount() == 0)
        return {};
    auto* item = projectm_playlist_item (state->playlist, (uint32_t) position());
    const auto name = item != nullptr ? QFileInfo (QFile::decodeName (item)).completeBaseName() : QString();
    projectm_playlist_free_string (item);
    return name;
}

QString MilkDrop::categoryOf (const QString& path)
{
    // The folders under the preset folder it came from, such as "Fractal" or "Fractal/Nested".
    const auto dir = QFileInfo (path).absolutePath();
    for (const auto& root : presetFolders())
    {
        const auto relative = QDir (root).relativeFilePath (dir);
        if (! relative.startsWith (QStringLiteral ("..")))
        {
            auto parts = relative.split (QLatin1Char ('/'), Qt::SkipEmptyParts);
            parts.removeAll (QStringLiteral ("."));
            if (! parts.isEmpty() && parts.first() == QStringLiteral ("cream-of-the-crop"))
                parts.removeFirst();
            return parts.join (QLatin1Char ('/'));
        }
    }
    return {};
}

QString MilkDrop::presetCategory() const
{
    if (presetCount() == 0)
        return {};
    auto* item = projectm_playlist_item (state->playlist, (uint32_t) position());
    const auto category = item != nullptr ? categoryOf (QFile::decodeName (item)) : QString();
    projectm_playlist_free_string (item);
    return category;
}

bool MilkDrop::changed()
{
    const bool was = state->switched;
    state->switched = false;
    return was;
}

void MilkDrop::addSamples (const float* left, const float* right, size_t count)
{
    auto& s = *state;
    if (s.pm == nullptr || count == 0)
        return;
    s.interleaved.resize (count * 2);
    for (size_t i = 0; i < count; ++i)
    {
        s.interleaved[i * 2] = left[i];
        s.interleaved[i * 2 + 1] = right[i];
    }
    const auto most = projectm_pcm_get_max_samples();
    for (size_t at = 0; at < count; at += most)
        projectm_pcm_add_float (s.pm, s.interleaved.data() + at * 2, (unsigned int) std::min<size_t> (most, count - at), PROJECTM_STEREO);
}

QImage MilkDrop::render (QSize size)
{
    auto& s = *state;
    if (s.pm == nullptr || size.isEmpty() || ! s.makeCurrent())
        return {};
    if (s.target == nullptr || s.target->size() != size)
    {
        s.target = std::make_unique<QOpenGLFramebufferObject> (size);
        projectm_set_window_size (s.pm, (size_t) size.width(), (size_t) size.height());
    }
    s.target->bind();
    auto* gl = s.context.functions();
    gl->glViewport (0, 0, size.width(), size.height());
    gl->glClearColor (0.0f, 0.0f, 0.0f, 1.0f);
    gl->glClear (GL_COLOR_BUFFER_BIT);
    projectm_opengl_render_frame_fbo (s.pm, s.target->handle());
    auto image = s.target->toImage();   // the right way up
    s.target->release();
    s.context.doneCurrent();
    return image;
}

void MilkDrop::next()
{
    if (state->playlist != nullptr && presetCount() > 0 && state->makeCurrent())
    {
        projectm_playlist_play_next (state->playlist, true);
        state->context.doneCurrent();
    }
}

void MilkDrop::previous()
{
    if (state->playlist != nullptr && presetCount() > 0 && state->makeCurrent())
    {
        projectm_playlist_play_previous (state->playlist, true);
        state->context.doneCurrent();
    }
}

void MilkDrop::choose (int index)
{
    if (state->playlist != nullptr && index >= 0 && index < presetCount() && state->makeCurrent())
    {
        projectm_playlist_set_position (state->playlist, (uint32_t) index, true);
        state->context.doneCurrent();
    }
}

void MilkDrop::setLocked (bool on)
{
    state->isLocked = on;
    if (state->pm != nullptr)
        projectm_set_preset_locked (state->pm, on);
}

bool MilkDrop::locked() const { return state->isLocked; }
