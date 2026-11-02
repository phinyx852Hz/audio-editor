// SPDX-License-Identifier: AGPL-3.0-only
#include "ReaperImport.h"

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <functional>

namespace reaperimport
{
    namespace
    {
        enum class Lang { lua, python };

        // What one Reaper function becomes. A template has $1..$9 for the call's arguments;
        // a helper is written once at the top of the script and called as R.Name (Lua) or
        // _R_Name (Python); removed calls are ones that only refresh Reaper's own windows.
        enum class Kind { templ, helper, removed };

        struct Mapping
        {
            const char* name;
            Kind kind;
            const char* lua;      // the template, or the helper's definition
            const char* python;
            const char* needs = nullptr;   // a helper this one calls
        };

        const Mapping mappings[] {
            // Output.
            { "ShowConsoleMsg", Kind::helper,
              "function R.ShowConsoleMsg(s) print((tostring(s):gsub(\"\\n$\", \"\"))) end",
              "def _R_ShowConsoleMsg(s):\n    print(str(s).rstrip(\"\\n\"))" },
            { "ClearConsole", Kind::removed, "", "None" },
            { "ShowMessageBox", Kind::helper,
              "function R.ShowMessageBox(msg, title, kind)\n"
              "    kind = kind or 0\n"
              "    if kind == 1 then return phi.confirm(tostring(msg)) and 1 or 2 end\n"
              "    if kind == 3 or kind == 4 then return phi.confirm(tostring(msg)) and 6 or 7 end\n"
              "    if kind == 5 then return phi.confirm(tostring(msg)) and 4 or 2 end\n"
              "    phi.message(tostring(msg), title)\n"
              "    return 1\n"
              "end",
              "def _R_ShowMessageBox(msg, title=\"\", kind=0):\n"
              "    if kind == 1:\n        return 1 if phi.confirm(str(msg)) else 2\n"
              "    if kind in (3, 4):\n        return 6 if phi.confirm(str(msg)) else 7\n"
              "    if kind == 5:\n        return 4 if phi.confirm(str(msg)) else 2\n"
              "    phi.message(str(msg), title)\n    return 1" },
            { "MB", Kind::helper, "function R.MB(...) return R.ShowMessageBox(...) end",
              "def _R_MB(*args):\n    return _R_ShowMessageBox(*args)", "ShowMessageBox" },
            { "GetUserInputs", Kind::helper,
              "function R.GetUserInputs(title, num, captions, defaults)\n"
              "    local caps, defs, out = {}, {}, {}\n"
              "    for c in (tostring(captions) .. \",\"):gmatch(\"([^,]*),\") do caps[#caps + 1] = c end\n"
              "    for d in (tostring(defaults or \"\") .. \",\"):gmatch(\"([^,]*),\") do defs[#defs + 1] = d end\n"
              "    for i = 1, num do\n"
              "        local a = phi.ask(caps[i] or (\"Value \" .. i), defs[i] or \"\")\n"
              "        if a == nil then return false, \"\" end\n"
              "        out[i] = a\n"
              "    end\n"
              "    return true, table.concat(out, \",\")\n"
              "end",
              "def _R_GetUserInputs(title, num, captions, defaults=\"\", size=0):\n"
              "    caps, defs, out = str(captions).split(\",\"), str(defaults).split(\",\"), []\n"
              "    for i in range(int(num)):\n"
              "        a = phi.ask(caps[i] if i < len(caps) else f\"Value {i + 1}\", defs[i] if i < len(defs) else \"\")\n"
              "        if a is None:\n            return (0, title, num, captions, \"\", size)\n"
              "        out.append(a)\n"
              "    return (1, title, num, captions, \",\".join(out), size)" },

            // Cursor and time selection.
            { "GetCursorPosition", Kind::templ, "phi.cursor()", "phi.cursor()" },
            { "GetCursorPositionEx", Kind::templ, "phi.cursor()", "phi.cursor()" },
            { "SetEditCurPos", Kind::templ, "phi.set_cursor($1)", "phi.set_cursor($1)" },
            { "SetEditCurPos2", Kind::templ, "phi.set_cursor($2)", "phi.set_cursor($2)" },
            { "GetSet_LoopTimeRange", Kind::helper,
              "function R.GetSet_LoopTimeRange(isSet, isLoop, s, e, seek)\n"
              "    if isSet then phi.set_selection(s, e) return s, e end\n"
              "    return phi.selection()\n"
              "end",
              "def _R_GetSet_LoopTimeRange(isSet, isLoop, s=0, e=0, seek=False):\n"
              "    if isSet:\n        phi.set_selection(s, e)\n    else:\n        s, e = phi.selection()\n"
              "    return (isSet, isLoop, s, e, seek)" },
            { "GetSet_LoopTimeRange2", Kind::helper,
              "function R.GetSet_LoopTimeRange2(proj, ...) return R.GetSet_LoopTimeRange(...) end",
              "def _R_GetSet_LoopTimeRange2(proj, *args):\n    return (proj,) + _R_GetSet_LoopTimeRange(*args)", "GetSet_LoopTimeRange" },

            // Markers and regions: PHI's marker ids stand in for Reaper's marker numbers.
            { "AddProjectMarker", Kind::helper,
              "function R.AddProjectMarker(proj, isrgn, pos, rgnend, name)\n"
              "    if name == \"\" then name = nil end\n"
              "    if isrgn then return phi.add_region(pos, rgnend, name) end\n"
              "    return phi.add_marker(pos, name)\n"
              "end",
              "def _R_AddProjectMarker(proj, isrgn, pos, rgnend, name=\"\", *rest):\n"
              "    name = name or None\n"
              "    return phi.add_region(pos, rgnend, name) if isrgn else phi.add_marker(pos, name)" },
            { "AddProjectMarker2", Kind::helper, "function R.AddProjectMarker2(...) return R.AddProjectMarker(...) end",
              "def _R_AddProjectMarker2(*args):\n    return _R_AddProjectMarker(*args)", "AddProjectMarker" },
            { "CountProjectMarkers", Kind::helper,
              "function R.CountProjectMarkers(proj)\n"
              "    local m, regions = phi.markers(), 0\n"
              "    for _, x in ipairs(m) do if x.region then regions = regions + 1 end end\n"
              "    return #m, #m - regions, regions\n"
              "end",
              "def _R_CountProjectMarkers(proj, *rest):\n"
              "    m = phi.markers()\n    regions = sum(1 for x in m if x[\"region\"])\n"
              "    return (len(m), proj, len(m) - regions, regions)" },
            { "EnumProjectMarkers", Kind::helper,
              "function R.EnumProjectMarkers(idx)\n"
              "    local m = phi.markers()[idx + 1]\n"
              "    if not m then return 0, false, 0, 0, \"\", 0 end\n"
              "    return idx + 1, m.region, m.start, m.finish, m.name, m.id\n"
              "end",
              "def _R_EnumProjectMarkers(idx, *rest):\n"
              "    m = phi.markers()\n"
              "    if idx >= len(m):\n        return (0, idx, False, 0.0, 0.0, \"\", 0)\n"
              "    x = m[idx]\n"
              "    return (idx + 1, idx, x[\"region\"], x[\"start\"], x[\"finish\"], x[\"name\"], x[\"id\"])" },
            { "EnumProjectMarkers2", Kind::helper, "function R.EnumProjectMarkers2(proj, idx) return R.EnumProjectMarkers(idx) end",
              "def _R_EnumProjectMarkers2(proj, idx, *rest):\n    return (_R_EnumProjectMarkers(idx)[0], proj) + _R_EnumProjectMarkers(idx)[1:]", "EnumProjectMarkers" },
            { "EnumProjectMarkers3", Kind::helper,
              "function R.EnumProjectMarkers3(proj, idx)\n"
              "    local a, b, c, d, e, f = R.EnumProjectMarkers(idx)\n"
              "    return a, b, c, d, e, f, 0\n"
              "end",
              "def _R_EnumProjectMarkers3(proj, idx, *rest):\n    return _R_EnumProjectMarkers2(proj, idx) + (0,)", "EnumProjectMarkers2" },
            { "SetProjectMarker", Kind::helper,
              "function R.SetProjectMarker(id, isrgn, pos, rgnend, name)\n"
              "    phi.move_marker(id, pos, rgnend)\n"
              "    if name and name ~= \"\" then phi.rename_marker(id, name) end\n"
              "    return true\n"
              "end",
              "def _R_SetProjectMarker(id, isrgn, pos, rgnend, name=\"\", *rest):\n"
              "    phi.move_marker(id, pos, rgnend)\n"
              "    if name:\n        phi.rename_marker(id, name)\n"
              "    return True" },
            { "SetProjectMarker2", Kind::helper, "function R.SetProjectMarker2(proj, ...) return R.SetProjectMarker(...) end",
              "def _R_SetProjectMarker2(proj, *args):\n    return _R_SetProjectMarker(*args)", "SetProjectMarker" },
            { "SetProjectMarker3", Kind::helper, "function R.SetProjectMarker3(proj, ...) return R.SetProjectMarker(...) end",
              "def _R_SetProjectMarker3(proj, *args):\n    return _R_SetProjectMarker(*args)", "SetProjectMarker" },
            { "DeleteProjectMarker", Kind::helper,
              "function R.DeleteProjectMarker(proj, id) return (pcall(phi.delete_marker, id)) end",
              "def _R_DeleteProjectMarker(proj, id, *rest):\n"
              "    try:\n        phi.delete_marker(id)\n        return True\n"
              "    except phi.PhiError:\n        return False" },

            // Undo.
            { "Undo_BeginBlock", Kind::removed, "", "None" },
            { "Undo_BeginBlock2", Kind::removed, "", "None" },
            { "Undo_EndBlock", Kind::templ, "phi.set_undo_name($1)", "phi.set_undo_name($1)" },
            { "Undo_EndBlock2", Kind::templ, "phi.set_undo_name($2)", "phi.set_undo_name($2)" },
            { "Undo_OnStateChange", Kind::templ, "phi.set_undo_name($1)", "phi.set_undo_name($1)" },
            { "Undo_OnStateChange2", Kind::templ, "phi.set_undo_name($2)", "phi.set_undo_name($2)" },
            { "Undo_DoUndo2", Kind::templ, "phi.undo()", "phi.undo()" },
            { "Undo_DoRedo2", Kind::templ, "phi.redo()", "phi.redo()" },

            // Playback and the project.
            { "OnPlayButton", Kind::templ, "phi.play()", "phi.play()" },
            { "OnPlayButtonEx", Kind::templ, "phi.play()", "phi.play()" },
            { "OnStopButton", Kind::templ, "phi.stop()", "phi.stop()" },
            { "OnStopButtonEx", Kind::templ, "phi.stop()", "phi.stop()" },
            { "GetPlayState", Kind::templ, "(phi.is_playing() and 1 or 0)", "(1 if phi.is_playing() else 0)" },
            { "GetPlayStateEx", Kind::templ, "(phi.is_playing() and 1 or 0)", "(1 if phi.is_playing() else 0)" },
            { "GetProjectLength", Kind::templ, "phi.duration()", "phi.duration()" },
            { "Master_GetTempo", Kind::templ, "(tonumber(phi.tag(\"bpm\")) or 120)", "(float(phi.tag(\"bpm\") or 120))" },
            { "Main_OnCommand", Kind::helper,
              "function R.Main_OnCommand(cmd)\n"
              "    local actions = {\n"
              "        [40029] = phi.undo, [40030] = phi.redo, [1007] = phi.play, [1016] = phi.stop,\n"
              "        [40044] = function() if phi.is_playing() then phi.stop() else phi.play() end end,\n"
              "        [40042] = function() phi.set_cursor(0) end,\n"
              "        [40043] = function() phi.set_cursor(phi.duration()) end,\n"
              "        [40157] = function() phi.add_marker(phi.cursor()) end,\n"
              "        [40174] = function() local a, b = phi.selection() if b > a then phi.add_region(a, b) end end,\n"
              "        [40635] = function() phi.set_cursor(phi.cursor()) end,\n"
              "        [40026] = function() phi.save() end,\n"
              "    }\n"
              "    local f = actions[cmd]\n"
              "    if f then f() return end\n"
              "    R.unsupported(\"Main_OnCommand(\" .. tostring(cmd) .. \")\", \"a Reaper action PHI has no match for\")\n"
              "end",
              "def _R_Main_OnCommand(cmd, *rest):\n"
              "    def toggle():\n        phi.stop() if phi.is_playing() else phi.play()\n"
              "    def region():\n        a, b = phi.selection()\n        if b > a:\n            phi.add_region(a, b)\n"
              "    actions = {40029: phi.undo, 40030: phi.redo, 1007: phi.play, 1016: phi.stop, 40044: toggle,\n"
              "               40042: lambda: phi.set_cursor(0), 40043: lambda: phi.set_cursor(phi.duration()),\n"
              "               40157: lambda: phi.add_marker(phi.cursor()), 40174: region,\n"
              "               40635: lambda: phi.set_cursor(phi.cursor()), 40026: phi.save}\n"
              "    if cmd in actions:\n        actions[cmd]()\n        return\n"
              "    _R_unsupported(f\"Main_OnCommand({cmd})\", \"a Reaper action PHI has no match for\")",
              "unsupported" },
            { "Main_OnCommandEx", Kind::helper, "function R.Main_OnCommandEx(cmd) return R.Main_OnCommand(cmd) end",
              "def _R_Main_OnCommandEx(cmd, *rest):\n    return _R_Main_OnCommand(cmd)", "Main_OnCommand" },

            // Mixer tracks: a track is its index.
            { "CountTracks", Kind::templ, "phi.track_count()", "phi.track_count()" },
            { "GetTrack", Kind::templ, "($2)", "($2)" },
            { "GetMediaTrackInfo_Value", Kind::helper,
              "function R.GetMediaTrackInfo_Value(tr, parm)\n"
              "    local t = phi.track(tr)\n"
              "    if parm == \"D_VOL\" then return 10 ^ (t.volume / 20) end\n"
              "    if parm == \"D_PAN\" then return t.pan end\n"
              "    if parm == \"B_MUTE\" then return t.mute and 1 or 0 end\n"
              "    if parm == \"I_SOLO\" then return t.solo and 1 or 0 end\n"
              "    if parm == \"IP_TRACKNUMBER\" then return tr + 1 end\n"
              "    R.unsupported(\"GetMediaTrackInfo_Value \" .. tostring(parm), \"a track setting PHI does not have\")\n"
              "end",
              "def _R_GetMediaTrackInfo_Value(tr, parm):\n"
              "    t = phi.track(tr)\n"
              "    values = {\"D_VOL\": 10 ** (t[\"volume\"] / 20), \"D_PAN\": t[\"pan\"], \"B_MUTE\": 1 if t[\"mute\"] else 0,\n"
              "              \"I_SOLO\": 1 if t[\"solo\"] else 0, \"IP_TRACKNUMBER\": tr + 1}\n"
              "    if parm in values:\n        return values[parm]\n"
              "    _R_unsupported(f\"GetMediaTrackInfo_Value {parm}\", \"a track setting PHI does not have\")",
              "unsupported" },
            { "SetMediaTrackInfo_Value", Kind::helper,
              "function R.SetMediaTrackInfo_Value(tr, parm, v)\n"
              "    if parm == \"D_VOL\" then phi.set_track(tr, { volume = v > 0 and 20 * math.log(v, 10) or -100 })\n"
              "    elseif parm == \"D_PAN\" then phi.set_track(tr, { pan = v })\n"
              "    elseif parm == \"B_MUTE\" then phi.set_track(tr, { mute = v ~= 0 })\n"
              "    elseif parm == \"I_SOLO\" then phi.set_track(tr, { solo = v ~= 0 })\n"
              "    else R.unsupported(\"SetMediaTrackInfo_Value \" .. tostring(parm), \"a track setting PHI does not have\") end\n"
              "    return true\n"
              "end",
              "def _R_SetMediaTrackInfo_Value(tr, parm, v):\n"
              "    import math\n"
              "    settings = {\"D_VOL\": {\"volume\": 20 * math.log10(v) if v > 0 else -100}, \"D_PAN\": {\"pan\": v},\n"
              "                \"B_MUTE\": {\"mute\": v != 0}, \"I_SOLO\": {\"solo\": v != 0}}\n"
              "    if parm not in settings:\n        _R_unsupported(f\"SetMediaTrackInfo_Value {parm}\", \"a track setting PHI does not have\")\n"
              "    phi.set_track(tr, settings[parm])\n    return True",
              "unsupported" },
            { "GetTrackName", Kind::helper, "function R.GetTrackName(tr) return true, phi.track(tr).name end",
              "def _R_GetTrackName(tr, buf=\"\", size=0):\n    return (True, tr, phi.track(tr)[\"name\"], size)" },
            { "GetSetMediaTrackInfo_String", Kind::helper,
              "function R.GetSetMediaTrackInfo_String(tr, parm, value, set)\n"
              "    if parm == \"P_NAME\" and not set then return true, phi.track(tr).name end\n"
              "    R.unsupported(\"GetSetMediaTrackInfo_String \" .. tostring(parm), \"renaming tracks, or a setting PHI does not have\")\n"
              "end",
              "def _R_GetSetMediaTrackInfo_String(tr, parm, value=\"\", set=False):\n"
              "    if parm == \"P_NAME\" and not set:\n        return (True, tr, parm, phi.track(tr)[\"name\"], set)\n"
              "    _R_unsupported(f\"GetSetMediaTrackInfo_String {parm}\", \"renaming tracks, or a setting PHI does not have\")",
              "unsupported" },

            // The rest.
            { "time_precise", Kind::templ, "os.clock()", "__import__(\"time\").perf_counter()" },
            { "GetAppVersion", Kind::templ, "phi.version()", "phi.version()" },
            { "format_timestr", Kind::helper,
              "function R.format_timestr(t) return string.format(\"%d:%06.3f\", math.floor(t / 60), t % 60) end",
              "def _R_format_timestr(t, *rest):\n    return f\"{int(t // 60)}:{t % 60:06.3f}\"" },
            { "format_timestr_pos", Kind::helper, "function R.format_timestr_pos(t) return R.format_timestr(t) end",
              "def _R_format_timestr_pos(t, *rest):\n    return _R_format_timestr(t)", "format_timestr" },
            { "defer", Kind::helper,
              "-- PHI has no background scripts: a deferred function runs once, at once.\n"
              "function R.defer(f)\n"
              "    if R.deferring then return end\n"
              "    R.deferring = true\n"
              "    f()\n"
              "    R.deferring = false\n"
              "end",
              "# PHI has no background scripts: a deferred function runs once, at once.\n"
              "_R_deferring = False\n"
              "def _R_defer(f):\n"
              "    global _R_deferring\n"
              "    if _R_deferring:\n        return\n"
              "    _R_deferring = True\n"
              "    eval(f) if isinstance(f, str) else f()\n"
              "    _R_deferring = False" },
            { "atexit", Kind::removed, "", "None" },
            { "UpdateArrange", Kind::removed, "", "None" },
            { "UpdateTimeline", Kind::removed, "", "None" },
            { "TrackList_AdjustWindows", Kind::removed, "", "None" },
            { "PreventUIRefresh", Kind::removed, "", "None" },
            { "MarkProjectDirty", Kind::removed, "", "None" },
        };

        const char* unsupportedLua = "function R.unsupported(name, what) error(\"not supported in PHI: reaper.\" .. name .. \" (\" .. what .. \")\", 2) end";
        const char* unsupportedPython = "def _R_unsupported(name, what):\n    raise phi.PhiError(f\"not supported in PHI: RPR_{name} ({what})\")";

        const Mapping* findMapping (const QString& name)
        {
            static const QHash<QString, const Mapping*> byName = []
            {
                QHash<QString, const Mapping*> h;
                for (const auto& m : mappings)
                    h.insert (QString::fromLatin1 (m.name), &m);
                return h;
            }();
            return byName.value (name, nullptr);
        }

        // What a Reaper function PHI has no match for needs, from its name.
        QString lackFor (const QString& name)
        {
            const auto has = [&name] (const char* part) { return name.contains (QLatin1String (part)); };
            if (has ("ImGui"))
                return QStringLiteral ("a script window (ReaImGui)");
            if (has ("MediaItem") || has ("Item") || has ("Take") || has ("PCM_") || has ("Source"))
                return QStringLiteral ("Reaper's media items and takes");
            if (has ("Envelope") || has ("Automation"))
                return QStringLiteral ("envelopes");
            if (has ("FX"))
                return QStringLiteral ("plug-ins on tracks (FX)");
            if (has ("MIDI"))
                return QStringLiteral ("MIDI editing");
            if (has ("ExtState"))
                return QStringLiteral ("saved script settings (ExtState)");
            if (has ("Selected") || has ("Selection"))
                return QStringLiteral ("selected tracks or items");
            if (has ("Send") || has ("Receive"))
                return QStringLiteral ("track sends");
            if (has ("Project") || has ("Proj"))
                return QStringLiteral ("Reaper's project settings");
            if (has ("Named") || has ("Command"))
                return QStringLiteral ("Reaper's actions");
            return QStringLiteral ("a Reaper function PHI has no match for");
        }

        // Skips a comment or string starting at i; returns i when there is none.
        qsizetype skipNonCode (const QString& s, qsizetype i, Lang lang)
        {
            const auto n = s.size();
            const auto at = [&] (qsizetype k, const char* text) { return s.mid (k, (qsizetype) qstrlen (text)) == QLatin1String (text); };
            if (lang == Lang::lua)
            {
                // A long bracket [[ ]] or [==[ ]==], after -- for a comment or alone for a string.
                const auto longBracket = [&] (qsizetype k) -> qsizetype
                {
                    if (k >= n || s[k] != QLatin1Char ('['))
                        return -1;
                    qsizetype level = 0, j = k + 1;
                    while (j < n && s[j] == QLatin1Char ('='))
                        ++level, ++j;
                    if (j >= n || s[j] != QLatin1Char ('['))
                        return -1;
                    const auto close = QStringLiteral ("]") + QString (level, QLatin1Char ('=')) + QStringLiteral ("]");
                    const auto end = s.indexOf (close, j + 1);
                    return end < 0 ? n : end + close.size();
                };
                if (at (i, "--"))
                {
                    const auto end = longBracket (i + 2);
                    if (end >= 0)
                        return end;
                    const auto newline = s.indexOf (QLatin1Char ('\n'), i);
                    return newline < 0 ? n : newline;
                }
                if (const auto end = longBracket (i); end >= 0)
                    return end;
            }
            else
            {
                if (s[i] == QLatin1Char ('#'))
                {
                    const auto newline = s.indexOf (QLatin1Char ('\n'), i);
                    return newline < 0 ? n : newline;
                }
                for (const char* triple : { "\"\"\"", "'''" })
                    if (at (i, triple))
                    {
                        const auto end = s.indexOf (QLatin1String (triple), i + 3);
                        return end < 0 ? n : end + 3;
                    }
            }
            if (s[i] == QLatin1Char ('"') || s[i] == QLatin1Char ('\''))
            {
                const auto quote = s[i];
                qsizetype j = i + 1;
                while (j < n && s[j] != quote && s[j] != QLatin1Char ('\n'))
                    j += s[j] == QLatin1Char ('\\') ? 2 : 1;
                return std::min (j + 1, n);
            }
            return i;
        }

        bool identChar (QChar c) { return c.isLetterOrNumber() || c == QLatin1Char ('_'); }

        struct Converter
        {
            Lang lang;
            Result& result;
            QSet<QString> helpers;   // to write at the top
            bool window = false;

            QString prefix() const { return lang == Lang::lua ? QStringLiteral ("reaper.") : QStringLiteral ("RPR_"); }

            void need (const QString& name)
            {
                if (helpers.contains (name))
                    return;
                helpers.insert (name);
                if (name == QLatin1String ("unsupported"))
                    return;
                if (const auto* m = findMapping (name); m != nullptr && m->needs != nullptr)
                    need (QString::fromLatin1 (m->needs));
            }

            QString unsupportedCall (const QString& name, const QString& what)
            {
                need (QStringLiteral ("unsupported"));
                const auto quoted = [] (QString t) { return QLatin1Char ('"') + t.replace (QLatin1Char ('"'), QStringLiteral ("'")) + QLatin1Char ('"'); };
                return (lang == Lang::lua ? QStringLiteral ("R.unsupported(") : QStringLiteral ("_R_unsupported(")) + quoted (name) + QStringLiteral (", ") + quoted (what) + QLatin1Char (')');
            }

            // The arguments of a call whose "(" is at open: their texts and where the ")" is.
            std::pair<QStringList, qsizetype> arguments (const QString& s, qsizetype open)
            {
                QStringList args;
                int depth = 0;
                qsizetype start = open + 1;
                for (qsizetype i = open; i < s.size();)
                {
                    const auto skipped = skipNonCode (s, i, lang);
                    if (skipped != i)
                    {
                        i = skipped;
                        continue;
                    }
                    const auto c = s[i];
                    if (c == QLatin1Char ('(') || c == QLatin1Char ('[') || c == QLatin1Char ('{'))
                        ++depth;
                    else if (c == QLatin1Char (')') || c == QLatin1Char (']') || c == QLatin1Char ('}'))
                    {
                        if (--depth == 0)
                        {
                            const auto last = s.mid (start, i - start).trimmed();
                            if (! last.isEmpty() || ! args.isEmpty())
                                args << last;
                            return { args, i };
                        }
                    }
                    else if (c == QLatin1Char (',') && depth == 1)
                    {
                        args << s.mid (start, i - start).trimmed();
                        start = i + 1;
                    }
                    ++i;
                }
                return { args, -1 };
            }

            QString run (const QString& s, int firstLine)
            {
                QString out;
                out.reserve (s.size() + s.size() / 4);
                const auto pre = prefix();
                int line = firstLine;
                for (qsizetype i = 0; i < s.size();)
                {
                    const auto skipped = skipNonCode (s, i, lang);
                    if (skipped != i)
                    {
                        const auto piece = s.mid (i, skipped - i);
                        line += (int) piece.count (QLatin1Char ('\n'));
                        out += piece;
                        i = skipped;
                        continue;
                    }
                    const bool boundary = i == 0 || (! identChar (s[i - 1]) && s[i - 1] != QLatin1Char ('.'));
                    if (boundary && lang == Lang::lua && s.mid (i, 4) == QLatin1String ("gfx.") )
                        window = true;
                    if (boundary && s.mid (i, pre.size()) == pre)
                    {
                        qsizetype j = i + pre.size();
                        while (j < s.size() && identChar (s[j]))
                            ++j;
                        const auto name = s.mid (i + pre.size(), j - i - pre.size());
                        if (lang == Lang::python && name == QLatin1String ("gfx"))
                            window = true;
                        qsizetype k = j;
                        while (k < s.size() && (s[k] == QLatin1Char (' ') || s[k] == QLatin1Char ('\t')))
                            ++k;
                        if (! name.isEmpty() && k < s.size() && s[k] == QLatin1Char ('('))
                        {
                            auto [args, close] = arguments (s, k);
                            if (close >= 0)
                            {
                                const auto argsText = s.mid (k + 1, close - k - 1);
                                for (auto& a : args)
                                    a = run (a, line);
                                out += replace (name, args, line);
                                line += (int) argsText.count (QLatin1Char ('\n'));
                                i = close + 1;
                                continue;
                            }
                        }
                        else if (! name.isEmpty())
                        {
                            // Used as a value, not called (local f = reaper.ShowConsoleMsg).
                            out += reference (name, line);
                            i = j;
                            continue;
                        }
                    }
                    if (s[i] == QLatin1Char ('\n'))
                        ++line;
                    out += s[i];
                    ++i;
                }
                return out;
            }

            QString replace (const QString& name, const QStringList& args, int line)
            {
                const auto original = prefix() + name;
                if (name.contains (QLatin1String ("ImGui")) || name.startsWith (QLatin1String ("gfx")))
                    window = true;
                const auto* m = findMapping (name);
                if (m == nullptr)
                {
                    const auto what = lackFor (name);
                    result.unsupported << Note { line, original, what };
                    return unsupportedCall (name, what);
                }
                switch (m->kind)
                {
                    case Kind::removed:
                        result.converted << Note { line, original, QStringLiteral ("removed (PHI has no need of it)") };
                        return QString::fromLatin1 (lang == Lang::lua ? m->lua : m->python);
                    case Kind::helper:
                    {
                        need (name);
                        const auto call = (lang == Lang::lua ? QStringLiteral ("R.") : QStringLiteral ("_R_")) + name;
                        result.converted << Note { line, original, QStringLiteral ("%1, a helper at the top").arg (call) };
                        return call + QLatin1Char ('(') + args.join (QStringLiteral (", ")) + QLatin1Char (')');
                    }
                    case Kind::templ:
                    {
                        QString text = QString::fromLatin1 (lang == Lang::lua ? m->lua : m->python);
                        for (int a = 9; a >= 1; --a)
                            text.replace (QStringLiteral ("$%1").arg (a), args.value (a - 1, lang == Lang::lua ? QStringLiteral ("nil") : QStringLiteral ("None")));
                        result.converted << Note { line, original, text };
                        return text;
                    }
                }
                return {};
            }

            QString reference (const QString& name, int line)
            {
                const auto original = prefix() + name;
                const auto* m = findMapping (name);
                if (m != nullptr && m->kind == Kind::helper)
                {
                    need (name);
                    const auto ref = (lang == Lang::lua ? QStringLiteral ("R.") : QStringLiteral ("_R_")) + name;
                    result.converted << Note { line, original, ref };
                    return ref;
                }
                const auto what = m != nullptr ? QStringLiteral ("used as a value, which PHI cannot match") : lackFor (name);
                result.unsupported << Note { line, original, what };
                need (QStringLiteral ("unsupported"));
                return lang == Lang::lua ? QStringLiteral ("function() %1 end").arg (unsupportedCall (name, what))
                                         : QStringLiteral ("(lambda *a: %1)").arg (unsupportedCall (name, what));
            }

            // The helpers used, each after the ones it calls.
            QString prelude() const
            {
                if (helpers.isEmpty())
                    return {};
                QString text = lang == Lang::lua ? QStringLiteral ("local R = {}   -- Reaper calls PHI matches with more than one phi call\n")
                                                 : QStringLiteral ("# Reaper calls PHI matches with more than one phi call\n");
                if (helpers.contains (QStringLiteral ("unsupported")))
                    text += QString::fromLatin1 (lang == Lang::lua ? unsupportedLua : unsupportedPython) + QLatin1Char ('\n');
                QSet<QString> written;
                std::function<void (const QString&)> write = [&] (const QString& name)
                {
                    if (written.contains (name) || name == QLatin1String ("unsupported"))
                        return;
                    written.insert (name);
                    const auto* m = findMapping (name);
                    if (m == nullptr)
                        return;
                    if (m->needs != nullptr)
                        write (QString::fromLatin1 (m->needs));
                    text += QString::fromLatin1 (lang == Lang::lua ? m->lua : m->python) + QLatin1Char ('\n');
                };
                for (const auto& m : mappings)
                    if (helpers.contains (QString::fromLatin1 (m.name)))
                        write (QString::fromLatin1 (m.name));
                return text;
            }
        };
    }

    QString Result::report() const
    {
        QString text;
        if (! ok)
        {
            text = QStringLiteral ("This Reaper script cannot be converted for PHI:\n");
            for (const auto& r : reasons)
                text += QStringLiteral ("  - %1\n").arg (r);
            return text;
        }
        text = QStringLiteral ("Converted from Reaper: %1 call(s) now work in PHI, %2 cannot.\n").arg (converted.size()).arg (unsupported.size());
        for (const auto& n : unsupported)
            text += QStringLiteral ("  line %1: %2 is not supported (%3); the script stops there if it reaches it\n").arg (n.line).arg (n.function, n.what);
        for (const auto& n : converted)
            text += QStringLiteral ("  line %1: %2 -> %3\n").arg (n.line).arg (n.function, n.what);
        return text;
    }

    Result convert (const QString& code, const QString& fileName)
    {
        Result result;
        const auto suffix = QFileInfo (fileName).suffix().toLower();
        if (suffix == QLatin1String ("eel"))
        {
            result.reasons << QStringLiteral ("It is written in EEL, Reaper's own scripting language, which PHI does not run. "
                                              "Only Lua (.lua) and Python (.py) Reaper scripts can be converted.");
            return result;
        }
        if (suffix != QLatin1String ("lua") && suffix != QLatin1String ("py"))
        {
            result.reasons << QStringLiteral ("PHI converts Reaper scripts in Lua (.lua) and Python (.py).");
            return result;
        }
        const auto lang = suffix == QLatin1String ("lua") ? Lang::lua : Lang::python;
        result.suffix = suffix;

        Converter c { lang, result, {}, false };
        auto body = c.run (code, 1);
        if (lang == Lang::python)
        {
            // The reaper_python module is Reaper's; phi is already there.
            static const QRegularExpression import (QStringLiteral ("^[ \\t]*(from[ \\t]+reaper_python[ \\t]+import[^\\n]*|import[ \\t]+reaper_python[^\\n]*)$"),
                                                    QRegularExpression::MultilineOption);
            body.replace (import, QStringLiteral ("# (Reaper's reaper_python import: not needed in PHI)"));
        }

        if (c.window)
            result.reasons << QStringLiteral ("It draws a window of its own (Reaper's gfx or ReaImGui), and PHI scripts cannot open windows yet.");
        if (result.converted.isEmpty() && result.unsupported.isEmpty())
            result.reasons << QStringLiteral ("It calls no Reaper functions, so it is not a Reaper script, or not one PHI can read.");
        else if (result.converted.isEmpty())
        {
            QStringList lacks;
            for (const auto& n : result.unsupported)
                if (! lacks.contains (n.what))
                    lacks << n.what;
            result.reasons << QStringLiteral ("None of its Reaper calls has a match in PHI. It works with: %1.").arg (lacks.join (QStringLiteral ("; ")));
        }
        if (! result.reasons.isEmpty())
            return result;

        const auto comment = lang == Lang::lua ? QStringLiteral ("-- ") : QStringLiteral ("# ");
        QString header = comment + QStringLiteral ("Converted from the Reaper script \"%1\" by PHI (Tools > Scripts > Import Reaper Script).\n").arg (QFileInfo (fileName).fileName());
        header += comment + QStringLiteral ("%1 Reaper call(s) became phi calls").arg (result.converted.size());
        if (result.unsupported.isEmpty())
            header += QStringLiteral (".\n");
        else
        {
            header += QStringLiteral ("; %1 have no match in PHI and stop the script if it reaches them:\n").arg (result.unsupported.size());
            for (const auto& n : result.unsupported)
                header += comment + QStringLiteral ("  line %1: %2 (%3)\n").arg (n.line).arg (n.function, n.what);
        }
        result.code = header + QLatin1Char ('\n') + c.prelude() + (c.helpers.isEmpty() ? QString() : QStringLiteral ("\n")) + body;
        result.ok = true;
        return result;
    }
}
