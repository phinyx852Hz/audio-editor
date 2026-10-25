# Installing PHI

PHI is built from source. On Arch Linux and Omarchy that takes a handful of commands, and the first build fetches and builds everything PHI needs that your system does not have.

## What you need

- **Linux** with a Wayland or X11 desktop. PHI is made for Arch Linux and Omarchy with Hyprland, and also builds on Ubuntu 24.04.
- **PipeWire** for sound and for recording. It is the default on Arch and Omarchy. PHI plays through PipeWire's JACK support (`pipewire-jack`) or straight through ALSA.
- **A processor with several cores**. The first build of the stock plugins is long: up to an hour or two on an older laptop. Later builds take seconds to minutes.
- About **5 GB of disk** for the build.

## Building on Arch Linux and Omarchy {#build-arch}

[]{.ix t="Installing!on Arch Linux"}

::: steps
1. Install the build tools and libraries:

   ```sh
   sudo pacman -S --needed base-devel git cmake ninja qt6-base qt6-wayland alsa-lib libpipewire ladspa clap \
     freetype2 fontconfig libx11 libxrandr libxinerama libxcursor libxcomposite libxext libxi mesa cairo libsndfile clang \
     ffmpeg taglib python rust xcb-util-wm fftw curl wget
   ```

2. Get the source:

   ```sh
   git clone https://github.com/phinyx852Hz/audio-editor
   cd audio-editor
   ```

3. Configure and build:

   ```sh
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

4. Start PHI:

   ```sh
   ./build/phi_artefacts/Release/phi
   ```
:::

To install it for every user instead, with a launcher in your application menu, use the package recipe: `cd packaging/arch && makepkg -si`.

::: important
Do not install `jack2` on Omarchy or any other PipeWire system. It conflicts with `pipewire-jack`, which PHI uses for JACK output. Keep `pipewire-jack`.
:::

To update later, pull and build again:

```sh
git pull
cmake --build build
```

## What the first build downloads

The first configure downloads JUCE 8.0.15, the audio engine PHI is built on, and the CLAP headers if the `clap` package is missing. The first build then downloads and builds, at pinned versions:

- the **stock plugins**: ToneShiftEQ, Wolf Shaper, the LSP clippers and oscilloscope, the ZL Audio plugins and Airwindows Consolidated (see [Built-in and Stock Effects](#effects));
- the **instruments**: GRNLR, Ami Sampler, Vaporizer2, Cardinal, Overtonium, CA-72, Helm and OctaSine (see [Instruments](#instruments));
- **Furnace**, the second tracker, and the **QM Vamp plugins** for analysis;
- **Rubber Band**, **libmysofa** and **projectM**, unless your system already has them.

The ZL plugins need Clang 17 or later and are left out without it. CA-72 and OctaSine need Rust. The stock plugins build after PHI itself, one at a time, with one build job per 3 GB of memory.

## Build options

[]{.ix t="Build options"}Pass these to the first `cmake` command as `-DNAME=VALUE`.

| Option | Effect |
|---|---|
| `PHI_STOCK_PLUGINS=OFF` | Builds without the stock plugins: much faster, for trying PHI out |
| `PHI_STOCK_INSTRUMENTS=OFF` | Leaves out every instrument |
| `PHI_STOCK_JOBS=N` | How many build jobs the stock plugins may use |
| `PHI_WEB_BACKDROP=OFF` | Leaves out Visualizer > Web Page even when Qt WebEngine is installed |
| `JUCE_DIR=/path` | Uses a JUCE checkout of your own instead of downloading one |
| `CLAP_DIR=/path` | Uses CLAP headers of your own |

## Optional extras {#optional-extras}

- **Web Page behind PHI** (Visualizer > Web Page) needs Qt WebEngine: `sudo pacman -S qt6-webengine` before you configure.
- **4K video recording** needs a graphics card that encodes H.265: on AMD and Intel the VAAPI driver (`mesa` or `libva-mesa-driver` for AMD, `intel-media-driver` for Intel), on NVIDIA the proprietary driver. `vainfo` from `libva-utils` should list `VAProfileHEVCMain`. Without one, PHI encodes on the processor at 1080p.
- **More plugins.** `sudo pacman -S lsp-plugins` gives a large free mastering set; `dragonfly-reverb-clap` adds reverbs; `clap-plugins` installs every CLAP plugin Arch packages. Windows plugins work through yabridge (see [Windows plugins](#yabridge)).
- **Your audio interface's mixer.** For Focusrite Scarlett and Clarett interfaces, `yay -S alsa-scarlett-gui`, then Options > Audio Interface Mixer opens it from PHI.

## Building on Ubuntu

[]{.ix t="Installing!on Ubuntu"}Install the equivalent packages and build the same way:

```sh
sudo apt install ninja-build qt6-base-dev libgl1-mesa-dev libasound2-dev libjack-jackd2-dev libpipewire-0.3-dev \
  ladspa-sdk libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
  libxcomposite-dev libxext-dev libcairo2-dev libsndfile1-dev libxi-dev clang libavcodec-dev libavformat-dev \
  libavutil-dev libswresample-dev libswscale-dev libavdevice-dev libtag1-dev zlib1g-dev libxcb-icccm4-dev \
  libxcb-dri2-0-dev libx11-xcb-dev libxcb1-dev libfftw3-dev libcurl4-openssl-dev
```

## Running the tests

`ctest --test-dir build` runs PHI's core tests: the editing, DSP, file and engine code, checked without a window or sound card.

## Setting up Hyprland {#hyprland}

[]{.ix t="Hyprland"}PHI runs natively on Wayland. Its window class is `phi`, so you can give it rules in `hyprland.conf`:

```
windowrulev2 = workspace 3, class:^(phi)$
```

Panels you pop out share that class and have titles such as `Meters - PHI`, so they can be placed on their own:

```
windowrulev2 = monitor 1, title:^(Meters - PHI)$
```

Plugin windows have titles like `MVerb - PHI`. PHI opens them floating at a standard size, through Hyprland's own socket, and sets their opacity the same way. To place them yourself, match their titles:

```
windowrulev2 = workspace 4, title:^(.* - PHI)$
```

PHI reads Hyprland's own key bindings when it starts and warns you if one of its shortcuts is taken by the desktop, since such keys never reach a program. See [Working from the Keyboard](#keyboard).

## When PHI was called PHINYX

PHI was called PHINYX before. The first time PHI starts under its new name, it moves the old `~/.config/phinyx`, `~/.local/share/phinyx` and `~/.cache/phinyx` folders to `phi`. Projects, albums, batches and chains saved by PHINYX open as before.
