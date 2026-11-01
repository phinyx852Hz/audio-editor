# Scripts

Status: built (2026-11-01). Owner's ask: "In the Tools menu, we have Macros which is great. Could we
also have a script programming function like Reaper?", then "so various supported file types, yes?
.lua etc...". The ideas come from Reaper's ReaScript: scripts in a real language, an editor to write
them in, an API for the file, the mixer and every command, and each script an action that can have a
shortcut. The API, its names and the code are PHI's own.

## Where it is

Tools > Scripts:

| Item | What it does |
| --- | --- |
| Script Editor... (`Ctrl+Shift+F9`) | The editor: the scripts folder on the left, the script in the middle, its output below |
| Run Script... | Runs a script file without opening it |
| Import Reaper Script... | A ReaScript (.lua, .py) rewritten for PHI, opened in the editor (below) |
| Stop Script | Ends the script that is running (also the Stop Script button in the status bar) |
| *each script* | Every script in the scripts folder, by name; runs it |
| Open Scripts Folder | `~/.config/phi/scripts` |

Each script in the folder is also a command, `script.<file name>`: the command palette finds it,
Options > Keyboard Shortcuts can give it a key, and a macro can have it as a step.

## Languages

| File | Language | Runs on |
| --- | --- | --- |
| `.lua` | Lua 5.4 | Built into PHI (cmake/Lua.cmake), nothing to install |
| `.py` | Python 3 | The computer's own `python3`, so numpy and the rest are there to use |
| `.ny` | Nyquist | PHI's Nyquist, as the Nyquist Prompt runs it on the selection |

Lua and Python get the same `phi` module, made from one list (Source/script/ScriptApi.cpp), so a
function added there works in both. Lua runs inside PHI; Python runs as a child process that sends
each call to PHI as a line of JSON and reads the answer back (Source/script/ScriptRunner.cpp).
Either way the event loop keeps turning while a script runs, so PHI stays responsive and Stop ends
even a script stuck in a loop. EEL (Reaper's own language) may come later.

## The Script Editor

- New (Lua, Python or Nyquist), Open, Save (`Ctrl+S`), Save As.
- Run (`F5` or `Ctrl+Enter`) runs the script as it is in the editor, saved or not, on the open file.
  Stop (`Shift+F5`).
- API Reference (`F1`): every function with what it takes and gives, made from the same list.
- Keywords, strings, comments, numbers and `phi.` calls are coloured by language; Tab indents four
  spaces and Enter keeps the indent.
- The first time the folder is made, PHI puts five examples in it: Markers every few seconds, Fade
  out the last two seconds, Reset the mixer (Lua), Gain the selection and List commands (Python).

## Import Reaper Script

Owner's ask (2026-11-01): "Can Phi import reaper scripts and adapt them to fit Phi DAW", choosing a
converter over a compatibility layer, "but an error message in case it is entirely uncompatible".

Tools > Scripts > Import Reaper Script (also New > From a Reaper Script in the editor) reads a
ReaScript in Lua (`reaper.*`) or Python (`RPR_*`) and opens it, rewritten, as a new unsaved script
(Source/script/ReaperImport.cpp). The output pane lists what each Reaper call became.

- Calls PHI matches one for one become `phi` calls in place (`reaper.GetCursorPosition()` becomes
  `phi.cursor()`); ones that need a few `phi` calls go through short helpers written at the top of
  the script (`R.AddProjectMarker` in Lua, `_R_AddProjectMarker` in Python).
- Matched: the console and message boxes, GetUserInputs, the cursor and time selection, markers and
  regions (PHI's marker ids stand in for Reaper's marker numbers), undo blocks, play and stop, the
  project length, the tempo (the BPM tag), mixer tracks (a track is its index; D_VOL, D_PAN, B_MUTE,
  I_SOLO and the name), a few common actions by number (undo, redo, play, stop, go to start or end,
  insert marker, region from time selection, remove time selection, save), and `defer`, which runs
  its function once since PHI has no background scripts. Calls that only refresh Reaper's windows
  are removed.
- A call PHI cannot match becomes one that stops the script with "not supported in PHI: reaper.X
  (what it needs)", and the script's header lists each with its line, so it can be changed by hand.
- Refused, with the reasons: EEL scripts, scripts that draw a window of their own (gfx or
  ReaImGui), and scripts none of whose Reaper calls PHI can match (media items and takes,
  envelopes, track FX, MIDI editing and so on), or that call none at all.

The mapping was written from Reaper's public API documentation; no code of Reaper's is used.

## The phi API

Times are in seconds and sample positions in frames. Channels, mixer tracks and tabs count from 0.
A call that cannot be done raises an error that stops the script with its line number, unless the
script catches it (Lua: `pcall`; Python: `except phi.PhiError`).

All the changes a script makes to the file it started on are one step in Edit > Undo, named
"Script: <name>" unless the script calls `phi.set_undo_name`.

| Group | Functions |
| --- | --- |
| Output and questions | `print`, `message`, `ask`, `ask_number`, `confirm`, `choose`, `wait`, `version` |
| The file | `is_open`, `path`, `name`, `sample_rate`, `channels`, `frames`, `duration`, `to_frames`, `to_seconds`, `open`, `save`, `new_file`, `tab_count`, `tab`, `set_tab` |
| Cursor and selection | `cursor`, `set_cursor`, `selection`, `has_selection`, `set_selection` |
| Samples | `get_samples`, `set_samples`, `peak` |
| Markers | `markers`, `add_marker`, `add_region`, `rename_marker`, `move_marker`, `delete_marker` |
| Tags | `tag`, `set_tag` |
| Commands | `commands`, `run`, `run_macro` |
| Undo | `undo`, `redo`, `set_undo_name` |
| Playback | `play`, `stop`, `is_playing` |
| Mixer | `track_count`, `track`, `set_track` |

`phi.run(id)` runs any command PHI has, by the id `phi.commands()` lists (the same ids macros keep),
so a script can do anything the menus do; windows the command opens appear as usual. A script cannot
start another script.

```lua
-- Lua: a marker every 10 seconds
for t = 0, phi.duration(), 10 do
    phi.add_marker(t, ("%d s"):format(t))
end
```

```python
# Python: halve the level of channel 0
n = phi.frames()
phi.set_samples(0, 0, [x * 0.5 for x in phi.get_samples(0, 0, n)])
```

## Not yet

- Scripts with windows of their own (Reaper's gfx) and scripts that keep running in the background
  (Reaper's defer).
- EEL scripts.
- The mixer's clips, inserts and sends, and the Master Section's plugins, from scripts.

## Tests

`phi-script-tests` (tests/ScriptTests.cpp) runs Lua and Python scripts against a dispatch of its own:
values both ways, lists and tables, several results, errors with their line, pcall and PhiError,
and Stop ending a runaway loop; and Reaper scripts converted (Lua and Python) and run, the call PHI
lacks named with its line, and EEL, window and all-unsupported scripts refused. The Python part is skipped where there is no python3. The core
tests check that a script's steps squash into one undo step.
