# SPDX-License-Identifier: AGPL-3.0-only
cmake_minimum_required(VERSION 3.22)

project(Phi VERSION 0.5.0 LANGUAGES C CXX)

# The app's names live here so a rename touches one place.
set(APP_ID "phi" CACHE STRING "Lowercase id: binary, config folder, Wayland app-id")
set(APP_DISPLAY_NAME "PHI" CACHE STRING "Name shown to users (ASCII; window titles add the letter phi, see Source/ui/Brand.h)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "${APP_DISPLAY_NAME} targets Linux only.")
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Concurrent)

# JUCE provides the audio engine only (devices, codecs, DSP, plugin hosting); no JUCE UI.
# Use a local checkout (needed for offline package builds) or fetch the pinned tag.
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout")
# Patched: a VST3's inactive buses get silent buffers instead of null pointers,
# which crashed yabridge-bridged plugins that have a side chain.
set(juce_patch ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/juce-vst3-inactive-buses.patch)
if(JUCE_DIR)
    execute_process(COMMAND ${CMAKE_COMMAND} -DPATCH=${juce_patch} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyPatch.cmake
                    WORKING_DIRECTORY ${JUCE_DIR})
    add_subdirectory(${JUCE_DIR} juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE
        PATCH_COMMAND ${CMAKE_COMMAND} -DPATCH=${juce_patch} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyPatch.cmake)
    FetchContent_MakeAvailable(juce)
endif()

# A JUCE console target lets us supply our own main() and run the Qt event loop.
juce_add_console_app(${APP_ID}
    PRODUCT_NAME "${APP_ID}"
    VERSION ${PROJECT_VERSION})

target_sources(${APP_ID} PRIVATE
    Source/main.cpp
    Source/core/AudioData.cpp
    Source/core/AudioDocument.cpp
    Source/core/Dsp.cpp
    Source/core/MidSide.cpp
    Source/sampler/SamplerEngine.cpp
    Source/sampler/SlicerEngine.cpp
    Source/mixer/MidiFile.cpp
    Source/mixer/Instruments.cpp
    Source/mixer/MixerEngine.cpp
    Source/patchbay/PatchBay.cpp
    Source/core/Ixml.cpp
    Source/core/Metadata.cpp
    Source/core/MiniXml.cpp
    Source/core/Mp4Xmp.cpp
    Source/core/RiffChunks.cpp
    Source/core/Xmp.cpp
    Source/core/Repair.cpp
    Source/core/Restoration.cpp
    Source/core/Fft.cpp
    Source/core/SpectrumView.cpp
    Source/core/Generate.cpp
    Source/core/Pitch.cpp
    Source/core/PitchFix.cpp
    Source/core/TimeStretch.cpp
    Source/core/Quantize.cpp
    Source/core/Sonify.cpp
    Source/core/Binaural.cpp
    Source/core/Ambisonics.cpp
    Source/core/SpatialStudio.cpp
    Source/core/PrismSplit.cpp
    Source/core/Phi.cpp
    Source/core/Nyquist.cpp
    Source/core/Album.cpp
    Source/core/MasterAssist.cpp
    Source/engine/AudioCapture.cpp
    Source/engine/AudioEngine.cpp
    Source/engine/BuiltinEffects.cpp
    Source/engine/PhiEffects.cpp
    Source/engine/MidiInputs.cpp
    Source/engine/ClapFormat.cpp
    Source/engine/Codec.cpp
    Source/engine/Export.cpp
    Source/engine/Batch.cpp
    Source/engine/TagIO.cpp
    Source/engine/MasterChain.cpp
    Source/engine/Prism.cpp
    Source/engine/PluginInstrument.cpp
    Source/engine/PluginEffect.cpp
    Source/engine/StockFrame.cpp
    Source/engine/InstrumentFrame.cpp
    Source/engine/StockPlugins.cpp
    Source/engine/VampHost.cpp
    Source/engine/X11Windows.cpp
    Source/ui/CommandRegistry.cpp
    Source/ui/Dialogs.cpp
    Source/ui/ExportDialog.cpp
    Source/ui/MetadataEditor.cpp
    Source/ui/MainWindow.cpp
    Source/ui/GainHud.cpp
    Source/ui/LiveEdits.cpp
    Source/ui/Analysis.cpp
    Source/ui/ReferencePanel.cpp
    Source/ui/ReferenceSection.cpp
    Source/ui/ExportSection.cpp
    Source/ui/BatchWindow.cpp
    Source/ui/AlbumEditor.cpp
    Source/ui/AlbumFile.cpp
    Source/ui/AlbumView.cpp
    Source/ui/ChainRow.cpp
    Source/ui/PrismEditor.cpp
    Source/ui/DesktopShortcuts.cpp
    Source/ui/MasterPanel.cpp
    Source/ui/MasterAssistant.cpp
    Source/ui/Panels.cpp
    Source/ui/RecordDialog.cpp
    Source/ui/Recovery.cpp
    Source/ui/Spectrogram.cpp
    Source/ui/Theme.cpp
    Source/ui/TransportBar.cpp
    Source/ui/InfoStrip.cpp
    Source/ui/TimeRuler.cpp
    Source/ui/Overview.cpp
    Source/ui/FindPanel.cpp
    Source/ui/MidSideDialog.cpp
    Source/ui/SamplerSection.cpp
    Source/ui/InstrumentsSection.cpp
    Source/ui/SamplerWindow.cpp
    Source/ui/SlicerWindow.cpp
    Source/ui/MixerWindow.cpp
    Source/ui/MixerSection.cpp
    Source/ui/PanelNavigation.cpp
    Source/ui/PatchBayWindow.cpp
    Source/ui/PatchBaySection.cpp
    Source/ui/ProjectSection.cpp
    Source/ui/Audition.cpp
    Source/ui/Goniometer.cpp
    Source/ui/AmbiVisualizer.cpp
    Source/ui/AnalysisWindow.cpp
    Source/ui/AnalysisReport.cpp
    Source/ui/NyquistUi.cpp
    Source/ui/WaveformView.cpp
    Source/ui/SpectrogramDialog.cpp
    Source/ui/GenerateSection.cpp
    Source/ui/PitchSection.cpp
    Source/ui/MacroSection.cpp
    Source/ui/Macros.cpp
    Source/ui/ToolsSection.cpp
    Source/ui/AmbiDialogs.cpp
    Source/ui/AmbiScene.cpp
    Source/ui/AmbiStudio.cpp
    Source/ui/SpeakerArrays.cpp
    Source/ui/Visualizer.cpp
    Source/ui/VisualizerSection.cpp
    Source/ui/SonifyPanel.cpp
    Source/ui/SonifySection.cpp
    Source/ui/TrackerSection.cpp
    Source/ui/WarpSection.cpp
    Source/ui/QuantizeSection.cpp
    Source/ui/QuantizePanel.cpp
    Source/ui/TrackerWindow.cpp
    Source/tracker/PatternText.cpp
    Source/tracker/TrackerEngine.cpp
    Source/tracker/TrackerEdit.cpp
    Source/ui/BinauralDialog.cpp
    Source/ui/SpatialSection.cpp
    Source/performance/MediaWriter.cpp
    Source/performance/Cameras.cpp
    Source/performance/VideoFile.cpp
    Source/performance/Recorder.cpp
    Source/ui/PerformanceDialog.cpp
    Source/ui/PerformanceSection.cpp)

# CLAP plugin hosting needs only the CLAP headers: a local checkout, the
# system's (clap/clap.h, e.g. the clap package), or the pinned release.
set(CLAP_DIR "" CACHE PATH "Path to a local CLAP SDK checkout (the folder holding include/clap)")
if(CLAP_DIR)
    set(PHI_CLAP_INCLUDE "${CLAP_DIR}/include")
else()
    find_path(PHI_CLAP_INCLUDE clap/clap.h)
    if(NOT PHI_CLAP_INCLUDE)
        include(FetchContent)
        FetchContent_Declare(clap
            GIT_REPOSITORY https://github.com/free-audio/clap.git
            GIT_TAG 1.2.7
            GIT_SHALLOW TRUE)
        FetchContent_GetProperties(clap)
        if(NOT clap_POPULATED)
            FetchContent_Populate(clap)
        endif()
        set(PHI_CLAP_INCLUDE "${clap_SOURCE_DIR}/include")
    endif()
endif()
target_include_directories(${APP_ID} SYSTEM PRIVATE ${PHI_CLAP_INCLUDE})

# JACK headers come from jack2 or pipewire-jack. JUCE loads libjack at run time,
# so JACK output works with either; without headers PHI builds ALSA-only.
include(CheckIncludeFile)
check_include_file(jack/jack.h PHI_HAVE_JACK_HEADERS)
if(PHI_HAVE_JACK_HEADERS)
    set(PHI_JACK 1)
else()
    set(PHI_JACK 0)
    message(STATUS "jack/jack.h not found: building without the JACK backend (ALSA and PipeWire's ALSA still work)")
endif()

# Recording (desktop audio, inputs, single apps) talks to PipeWire directly.
# Without libpipewire-0.3 PHI still builds; it just cannot record.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PIPEWIRE QUIET IMPORTED_TARGET libpipewire-0.3)
endif()
if(PIPEWIRE_FOUND)
    set(PHI_PIPEWIRE 1)
    target_include_directories(${APP_ID} SYSTEM PRIVATE ${PIPEWIRE_INCLUDE_DIRS})
    target_link_libraries(${APP_ID} PRIVATE PkgConfig::PIPEWIRE)
else()
    set(PHI_PIPEWIRE 0)
    message(STATUS "libpipewire-0.3 not found: building without recording")
endif()

# Export and import of compressed formats (FFmpeg) and their tags (TagLib). The Performance
# menu's video recording and streaming also use FFmpeg: libswscale for the picture and
# libavdevice for cameras.
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil libswresample libswscale libavdevice)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib>=1.12)
target_link_libraries(${APP_ID} PRIVATE PkgConfig::FFMPEG PkgConfig::TAGLIB)

target_compile_definitions(${APP_ID} PRIVATE
    PHI_PIPEWIRE=${PHI_PIPEWIRE}
    APP_ID="${APP_ID}"
    APP_DISPLAY_NAME="${APP_DISPLAY_NAME}"
    APP_VERSION="${PROJECT_VERSION}"
    QT_NO_KEYWORDS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_ALSA=1
    JUCE_JACK=${PHI_JACK}
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_PLUGINHOST_LADSPA=1)

target_link_libraries(${APP_ID}
    PRIVATE
        Qt6::Widgets
        Qt6::Concurrent
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# The stock plugins patched to take the PHI frame's colour as their background
# find it with dlsym (Source/engine/StockFrame.cpp).
target_link_options(${APP_ID} PRIVATE -Wl,--export-dynamic-symbol=phi_plugin_background)

include(GNUInstallDirs)

# Vamp analysis plugins (cmake/Vamp.cmake)
include(cmake/Vamp.cmake)
target_link_libraries(${APP_ID} PRIVATE phi_vamp_host)

# Rubber Band (cmake/RubberBand.cmake): pitch correction.
include(cmake/RubberBand.cmake)
target_link_libraries(${APP_ID} PRIVATE phi_rubberband)

# libmysofa (cmake/Mysofa.cmake): HRTF sets for binaural rendering, with the MIT KEMAR set.
include(cmake/Mysofa.cmake)
target_link_libraries(${APP_ID} PRIVATE phi_mysofa)
target_compile_definitions(${APP_ID} PRIVATE PHI_DEFAULT_HRTF="${PHI_DEFAULT_HRTF}")

# projectM (cmake/ProjectM.cmake): MilkDrop presets in the Visualizer.
include(cmake/ProjectM.cmake)
if(PHI_HAVE_PROJECTM)
    target_sources(${APP_ID} PRIVATE Source/ui/MilkDrop.cpp)
    target_link_libraries(${APP_ID} PRIVATE phi_projectm)
endif()
install(FILES "${PHI_DEFAULT_HRTF}" DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_ID}/hrtf")

# A web page behind PHI (Visualizer > Web Page) needs Qt WebEngine, a built-in Chromium of a
# few hundred MB. It is built when Qt WebEngine is installed (qt6-webengine), unless turned off.
option(PHI_WEB_BACKDROP "Build the web page backdrop when Qt WebEngine is installed" ON)
set(PHI_HAVE_WEB 0)
if(PHI_WEB_BACKDROP)
    find_package(Qt6 QUIET COMPONENTS WebEngineWidgets)
    if(Qt6WebEngineWidgets_FOUND)
        set(PHI_HAVE_WEB 1)
        target_sources(${APP_ID} PRIVATE Source/ui/WebBackdrop.cpp)
        target_link_libraries(${APP_ID} PRIVATE Qt6::WebEngineWidgets)
    else()
        message(STATUS "Qt WebEngine not found: building without the web page backdrop")
    endif()
endif()
target_compile_definitions(${APP_ID} PRIVATE PHI_WEB_BACKDROP=${PHI_HAVE_WEB})

# MilkyPlay (cmake/MilkyPlay.cmake): MilkyTracker's module player, for the Tracker.
include(cmake/MilkyPlay.cmake)
target_link_libraries(${APP_ID} PRIVATE phi_milkyplay)

# Nyquist effects (cmake/Nyquist.cmake): libnyquist, its runtime and the bundled plug-ins.
include(cmake/Nyquist.cmake)
target_link_libraries(${APP_ID} PRIVATE phi_nyquist)
file(RELATIVE_PATH nyquist_relative "/${CMAKE_INSTALL_BINDIR}" "/${CMAKE_INSTALL_DATADIR}/${APP_ID}")
target_compile_definitions(${APP_ID} PRIVATE
    PHI_SHARE_RELATIVE="${nyquist_relative}"
    PHI_SHARE_SOURCE_DIR="${CMAKE_SOURCE_DIR}/share")

# Stock plugins (cmake/StockPlugins.cmake). Off builds PHI without them.
option(PHI_STOCK_PLUGINS "Build and bundle the stock plugins" ON)
if(PHI_STOCK_PLUGINS)
    include(cmake/StockPlugins.cmake)
else()
    set(PHI_STOCK_DIR "${CMAKE_BINARY_DIR}/stock")
    set(PHI_STOCK_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/${APP_ID}/stock")
endif()
file(RELATIVE_PATH stock_relative "/${CMAKE_INSTALL_BINDIR}" "/${PHI_STOCK_INSTALL_DIR}")
target_compile_definitions(${APP_ID} PRIVATE
    PHI_STOCK_RELATIVE="${stock_relative}"
    PHI_STOCK_BUILD_DIR="${PHI_STOCK_DIR}")

# The Φ logo: built in for the windows, and installed for the desktop's launcher.
qt_add_resources(${APP_ID} brand PREFIX "/" FILES packaging/phi.svg)

install(TARGETS ${APP_ID} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/phi.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES packaging/phi.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/phi-project.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)

# Core tests: plain C++, no JUCE or Qt. Run with ctest.
option(PHI_BUILD_TESTS "Build the core tests" ON)
if(PHI_BUILD_TESTS)
    enable_testing()
    add_executable(phi-core-tests
        tests/CoreTests.cpp
        Source/core/AudioData.cpp
        Source/core/AudioDocument.cpp
        Source/core/Dsp.cpp
        Source/core/MidSide.cpp
        Source/sampler/SamplerEngine.cpp
        Source/sampler/SlicerEngine.cpp
        Source/mixer/MidiFile.cpp
        Source/mixer/Instruments.cpp
        Source/mixer/MixerEngine.cpp
        Source/patchbay/PatchBay.cpp
        Source/core/Ixml.cpp
        Source/core/Metadata.cpp
        Source/core/MiniXml.cpp
        Source/core/RiffChunks.cpp
        Source/core/Xmp.cpp
        Source/core/Repair.cpp
        Source/core/Restoration.cpp
        Source/core/Fft.cpp
        Source/core/SpectrumView.cpp
        Source/core/Generate.cpp
        Source/core/Pitch.cpp
        Source/core/PitchFix.cpp
        Source/core/TimeStretch.cpp
    Source/core/Quantize.cpp
    Source/core/Sonify.cpp
    Source/core/Binaural.cpp
    Source/core/Ambisonics.cpp
    Source/core/SpatialStudio.cpp
        Source/core/PrismSplit.cpp
        Source/core/Phi.cpp
        Source/core/Nyquist.cpp
        Source/core/Album.cpp
        Source/core/MasterAssist.cpp)
    target_link_libraries(phi-core-tests PRIVATE phi_nyquist phi_rubberband phi_mysofa)
    target_compile_definitions(phi-core-tests PRIVATE PHI_DEFAULT_HRTF="${PHI_DEFAULT_HRTF}")
    target_compile_definitions(phi-core-tests PRIVATE PHI_SHARE_SOURCE_DIR="${CMAKE_SOURCE_DIR}/share")
    add_test(NAME core COMMAND phi-core-tests)

    # The video writer: an MP4 with H.265 (or H.264) and several AAC tracks, read back.
    add_executable(phi-performance-tests
        tests/PerformanceTests.cpp
        Source/performance/MediaWriter.cpp)
    target_link_libraries(phi-performance-tests PRIVATE PkgConfig::FFMPEG)
    add_test(NAME performance COMMAND phi-performance-tests)
endif()
