# Files and Folders

[]{.ix t="Folders"}Where PHI keeps things, and the kinds of file it writes. PHI follows the desktop's XDG folders, so `~/.config`, `~/.local/share` and `~/.cache` below move with `XDG_CONFIG_HOME`, `XDG_DATA_HOME` and `XDG_CACHE_HOME` when they are set.

## Settings: `~/.config/phi`

| File or folder | Holds |
|---|---|
| `settings.ini` | Every setting and remembered value; see [Settings files](#settings-files) |
| `keymap.ini` | Your own keys; see [Changing shortcuts](#change-shortcuts) |
| `macros/` | One `.phmacro` file for each macro |
| `mixer/` | The mixer's crash-recovery copy of the project, offered back after a crash and removed on a clean quit |
| `stock-plugins.xml` | What PHI found in its stock plugins, so it need not check them again |

## Your additions: `~/.local/share/phi`

| Folder | Put here |
|---|---|
| `themes/` | Your own themes, as `.json`; see [Themes](#themes) |
| `hrtf/` | HRTF sets, as `.sofa`, for Binaural and decoding to headphones |
| `milkdrop/` | MilkDrop presets, as `.milk`, and their textures |
| `plug-ins/` | Your own Nyquist effects, as `.ny` |

Plug-ins are found where each format keeps them: `~/.vst3`, `~/.lv2`, `~/.clap`, `LADSPA_PATH` and the folders under `/usr/lib`, including `~/.clap/yabridge` and `~/.vst3/yabridge` for Windows plug-ins through yabridge. Vamp plugins are found in `~/vamp`, `~/.vamp`, `/usr/local/lib/vamp`, `/usr/lib/vamp` or `VAMP_PATH`.

## Working files: `~/.cache/phi`

| Folder | Holds |
|---|---|
| `recovery/` | Copies of unsaved files, made every minute; see [Recovery](#recovery) |
| `recordings/` | A safety copy of each recording while it is made |

Anything here can be deleted while PHI is closed; it is made again when needed (but recovery copies are then gone).

## In your own folders

| Folder | Holds |
|---|---|
| `~/Music/PHI Recovered` | Recordings recovered after PHI closed unexpectedly |
| `~/Music/PHI Edits` | Clips edited from the mixer, before the mix is saved (afterwards, an `Edits` folder beside the mix) |
| `~/Music/PHI Bounces` | Bounced tracks, before the mix is saved (afterwards, a `Bounces` folder beside the mix) |
| `~/Videos/PHI` | Video recordings from the Performance menu |
| `~/Documents/PHI Analysis` | Saved analysis reports, until you pick another folder |

## PHI's own files {#file-types}

[]{.ix t="File types"}

| Extension | Is | Chapter |
|---|---|---|
| `.phi` | A mixer project: every clip, setting and instrument | [Projects](#projects) |
| `.phinyxmix` | A mix from an earlier PHI; still opens | [Projects](#projects) |
| `.phalbum` | An album: the songs, their order, gaps and fades (older `.album` and `.montage` files still open) | [Albums](#album) |
| `.phbatch` | A Batch Processor job | [The Batch Processor](#batch) |
| `.phmacro` | A macro | [Macros](#macros) |
| `.xm.ami` | Tracker1's links to the Retro Sampler, beside the song | [Tracker1 and the Retro Sampler](#tracker-ami) |
| `.json` | A theme, or an Ambisonic Studio rack | [Themes](#themes), [The Ambisonic Studio](#ambisonic-studio) |

All of them are plain text (JSON), so they can be read, kept in version control and mended by hand.

## Audio files

PHI opens WAV, AIFF, FLAC, Ogg Vorbis and MP3 directly, and anything else your FFmpeg can decode (see [Opening files](#files)). Save and Save As write WAV, AIFF, FLAC and Ogg, and MP3, M4A, Opus and WavPack with their encoder settings. [Export](#export) writes every format your FFmpeg can encode.
