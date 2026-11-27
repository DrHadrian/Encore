# Encore — live guitar rig host
# Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

cmake_minimum_required(VERSION 3.22)

project(Encore VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# Dependencies
#
# JUCE is pinned to an exact release. FetchContent downloads it on the first
# configure; later configures reuse the copy in the build folder.
# To use a local checkout instead (faster, works offline), configure with
#   -DENCORE_JUCE_DIR=C:/path/to/JUCE
# ------------------------------------------------------------------------------
set(ENCORE_JUCE_VERSION "9.0.5" CACHE STRING "JUCE release tag to download")
set(ENCORE_JUCE_DIR "" CACHE PATH "Optional local JUCE checkout (skips download)")

if(ENCORE_JUCE_DIR)
    add_subdirectory("${ENCORE_JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)
    set(juce_SOURCE_DIR "${ENCORE_JUCE_DIR}")   # where JUCE's module sources are (see ENCORE_PCH)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${ENCORE_JUCE_VERSION}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(juce)
endif()

# NeuralAmpModelerCore (MIT) and the Eigen commit it pins (MPL2).
# Only the sources are fetched (SOURCE_SUBDIR points nowhere), so neither
# project's own build runs; nam_core below compiles exactly what we need.
# Offline or local copies: -DFETCHCONTENT_SOURCE_DIR_NAM=... / _EIGEN=...
include(FetchContent)

FetchContent_Declare(nam
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG        v0.6.0
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""
    SOURCE_SUBDIR  do-not-build)

FetchContent_Declare(eigen
    URL https://gitlab.com/libeigen/eigen/-/archive/bc3b39870ecb690a623a3f49149a358b95c5781d/eigen-bc3b39870ecb690a623a3f49149a358b95c5781d.tar.gz
    SOURCE_SUBDIR do-not-build
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(nam eigen)

file(GLOB NAM_CORE_SOURCES CONFIGURE_DEPENDS
    "${nam_SOURCE_DIR}/NAM/*.cpp"
    "${nam_SOURCE_DIR}/NAM/*/*.cpp")

# An OBJECT library, not STATIC: NAM registers each architecture (WaveNet,
# LSTM...) from a static initialiser, which a linker drops from a static
# library because nothing references it. Object files are always linked.
# Link it directly into each executable (objects don't pass through INTERFACE libraries).
add_library(nam_core OBJECT ${NAM_CORE_SOURCES})
target_compile_features(nam_core PUBLIC cxx_std_20)
# SYSTEM: third-party headers. Compilers don't report warnings from them in
# our files (MSVC via /external:I), so Encore's own warnings stay visible.
target_include_directories(nam_core SYSTEM PUBLIC
    "${nam_SOURCE_DIR}"                              # for #include "NAM/dsp.h"
    "${nam_SOURCE_DIR}/Dependencies/nlohmann")       # NAM's headers include "json.hpp"
target_include_directories(nam_core SYSTEM PUBLIC "${eigen_SOURCE_DIR}")
target_compile_definitions(nam_core
    PUBLIC
        NAM_SAMPLE_FLOAT                             # process float directly, no conversion
    PRIVATE
        NAM_ENABLE_A2_FAST
        EIGEN_NO_DEBUG                               # Eigen's asserts make Debug unplayable
        $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>)

# Neural nets are unusable without optimisation, so NAM is optimised even in
# Debug builds. That lets you play through a Debug build, where the real-time
# allocation tripwire is active. (/RTC1 is incompatible with /O2; see below.)
if(MSVC)
    target_compile_options(nam_core PRIVATE /bigobj /W0 $<$<CONFIG:Debug>:/O2 /Ob2>)
else()
    target_compile_options(nam_core PRIVATE -w $<$<CONFIG:Debug>:-O2>)
endif()

if(MSVC)
    # MSVC's run-time checks (/RTC1, on by default in Debug) forbid optimised
    # code. Drop them project-wide; the allocation tripwire and the tests catch
    # what matters for real-time code.
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
endif()

# ------------------------------------------------------------------------------
# JUCE, compiled ONCE for the app and the tests, in ONE configuration: the tests
# run the same JUCE (and, below, the same Encore code) as the app players get,
# and CI compiles it once instead of once per program. OBJECT libraries (like
# nam_core): every object is linked, so no static initialiser can be dropped.
# Each program links EncoreJuce, EncoreEngine and EncoreUi directly (objects
# don't pass through other libraries).
# ------------------------------------------------------------------------------
add_library(EncoreJuce OBJECT)
target_link_libraries(EncoreJuce
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_gui_basics
        juce::juce_events
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
target_compile_definitions(EncoreJuce
    PUBLIC
        JUCE_STANDALONE_APPLICATION=1
        # Audio drivers. ASIO is the one we play through; WASAPI stays as a fallback
        # for laptops without an ASIO driver. JUCE ships the ASIO header itself.
        JUCE_ASIO=1
        JUCE_WASAPI=1
        JUCE_USE_WINRT_MIDI=1          # Bluetooth MIDI pedals (Windows 10+)
        JUCE_DIRECTSOUND=0
        # Linux only builds the tests (no audio hardware or its headers needed).
        $<$<PLATFORM_ID:Linux>:JUCE_ALSA=0>
        $<$<PLATFORM_ID:Linux>:JUCE_JACK=0>
        JUCE_PLUGINHOST_VST3=1         # VST3 plugins as blocks (milestone 10)
        # Features we don't use: keep the binary lean and avoid network code.
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        # The tests pump the message loop (runDispatchLoopUntil), which needs this.
        # It only makes JUCE's modal-loop functions available: Encore never runs a
        # modal loop (nothing in the show path is blocking or modal).
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_APPLICATION_NAME_STRING="Encore"
        JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    INTERFACE
        $<TARGET_PROPERTY:EncoreJuce,COMPILE_DEFINITIONS>)
target_include_directories(EncoreJuce INTERFACE $<TARGET_PROPERTY:EncoreJuce,INCLUDE_DIRECTORIES>)

# ------------------------------------------------------------------------------
# Engine sources (compiled once; the app and the tests link the same objects).
# ------------------------------------------------------------------------------
add_library(EncoreEngine OBJECT)

target_sources(EncoreEngine PRIVATE
    Source/engine/AudioEngine.cpp
    Source/engine/AudioEngine.h
    Source/engine/PeakMeter.h
    Source/engine/Tuner.cpp
    Source/engine/Tuner.h
    Source/engine/blocks/CabIrBlock.cpp
    Source/engine/blocks/CabIrBlock.h
    Source/engine/blocks/DelayBlock.cpp
    Source/engine/blocks/DelayBlock.h
    Source/engine/blocks/EngineProbeBlock.cpp
    Source/engine/blocks/EngineProbeBlock.h
    Source/engine/blocks/EqBlock.cpp
    Source/engine/blocks/EqBlock.h
    Source/engine/blocks/NamAmpBlock.cpp
    Source/engine/blocks/NamAmpBlock.h
    Source/engine/blocks/NoiseGateBlock.cpp
    Source/engine/blocks/NoiseGateBlock.h
    Source/engine/blocks/PluginBlock.cpp
    Source/engine/blocks/PluginBlock.h
    Source/engine/blocks/SplitBlocks.cpp
    Source/engine/blocks/SplitBlocks.h
    Source/engine/core/BackgroundJobs.h
    Source/engine/core/ThreadStop.h
    Source/engine/core/Block.cpp
    Source/engine/core/Block.h
    Source/engine/core/Chain.cpp
    Source/engine/core/Chain.h
    Source/engine/core/LockFreeFifo.h
    Source/engine/core/MessageThread.cpp
    Source/engine/core/MessageThread.h
    Source/engine/core/OutputStrip.cpp
    Source/engine/core/OutputStrip.h
    Source/engine/core/PatchPlayer.cpp
    Source/engine/core/PatchPlayer.h
    Source/engine/core/Parameter.cpp
    Source/engine/core/Parameter.h
    Source/engine/core/Realtime.cpp
    Source/engine/core/Realtime.h
    Source/engine/core/RealtimeHandover.h
    Source/engine/midi/MidiRouter.cpp
    Source/engine/midi/MidiRouter.h
    Source/engine/dsp/Biquad.h
    Source/engine/dsp/Crossfade.h
    Source/engine/dsp/Detector.h
    Source/engine/dsp/FilterBand.cpp
    Source/engine/dsp/FilterBand.h
    Source/engine/dsp/FilterDesign.cpp
    Source/engine/dsp/FilterDesign.h
    Source/engine/dsp/OutputEq.h
    Source/engine/dsp/Oversampler.cpp
    Source/engine/dsp/Oversampler.h
    Source/engine/dsp/PitchDetector.cpp
    Source/engine/dsp/PitchDetector.h
    Source/engine/dsp/SafetyLimiter.cpp
    Source/engine/dsp/SafetyLimiter.h
    Source/engine/dsp/StreamResampler.cpp
    Source/engine/dsp/StreamResampler.h
    Source/engine/dsp/Svf.h
    Source/engine/nam/NamModel.cpp
    Source/engine/nam/NamModel.h
    Source/model/AmpPreset.cpp
    Source/model/AmpPreset.h
    Source/model/MidiMapping.cpp
    Source/model/MidiMapping.h
    Source/model/Routing.cpp
    Source/model/Routing.h
    Source/model/ShowFolder.cpp
    Source/model/ShowFolder.h
    Source/model/ShowJson.cpp
    Source/model/ShowJson.h
    Source/model/ShowModel.cpp
    Source/model/StageLayout.cpp
    Source/model/StageLayout.h
    Source/model/ShowModel.h
    Source/platform/CrashReport.cpp
    Source/platform/CrashReport.h
    Source/platform/SystemTuning.cpp
    Source/platform/SystemTuning.h
    Source/session/AmpLibrary.cpp
    Source/session/AmpLibrary.h
    Source/session/OfflineRender.cpp
    Source/session/OfflineRender.h
    Source/session/PatchBuilder.cpp
    Source/session/PluginCatalog.cpp
    Source/session/PluginCatalog.h
    Source/session/PreShowCheck.cpp
    Source/session/PreShowCheck.h
    Source/session/RecoveryFile.cpp
    Source/session/RecoveryFile.h
    Source/session/PatchBuilder.h
    Source/session/ShowSession.cpp
    Source/session/ShowSession.h)

target_include_directories(EncoreEngine PUBLIC Source)
target_link_libraries(EncoreEngine PUBLIC EncoreJuce nam_core)   # (their usage; the programs link their objects)

# ------------------------------------------------------------------------------
# The app
# ------------------------------------------------------------------------------
# The UI's bundled assets: Inter, the icons and the logo (Resources/ui, copied
# from docs/design/assets). Fonts never come from the system.
juce_add_binary_data(EncoreUiAssets
    HEADER_NAME EncoreUiAssets.h
    NAMESPACE EncoreUiAssets
    SOURCES
        Resources/ui/fonts/Inter-Regular.ttf
        Resources/ui/fonts/Inter-Medium.ttf
        Resources/ui/fonts/Inter-SemiBold.ttf
        Resources/ui/fonts/Inter-Bold.ttf
        Resources/ui/fonts/Inter-ExtraBold.ttf
        Resources/ui/fonts/InterReadout-Regular.ttf
        Resources/ui/fonts/InterReadout-Medium.ttf
        Resources/ui/fonts/InterReadout-SemiBold.ttf
        Resources/ui/fonts/InterReadout-Bold.ttf
        Resources/ui/fonts/InterReadout-ExtraBold.ttf
        Resources/ui/fonts/DejaVuSerif-Bold.ttf
        Resources/ui/logo/encore-wordmark-ui.svg
        Resources/ui/logo/encore-icon-clef.svg
        Resources/ui/icons/alert.svg
        Resources/ui/icons/arrow-right.svg
        Resources/ui/icons/audio-wave.svg
        Resources/ui/icons/check.svg
        Resources/ui/icons/chevron-down.svg
        Resources/ui/icons/chevron-up.svg
        Resources/ui/icons/close.svg
        Resources/ui/icons/duplicate.svg
        Resources/ui/icons/jack.svg
        Resources/ui/icons/lock.svg
        Resources/ui/icons/merge.svg
        Resources/ui/icons/midi-port.svg
        Resources/ui/icons/minus.svg
        Resources/ui/icons/plug.svg
        Resources/ui/icons/plus.svg
        Resources/ui/icons/route.svg
        Resources/ui/icons/sidebar-right.svg
        Resources/ui/icons/settings.svg
        Resources/ui/icons/amp.svg
        Resources/ui/icons/sliders.svg
        Resources/ui/icons/split.svg
        Resources/ui/icons/trash.svg
        Resources/ui/icons/warn.svg
        Resources/ui/icons/window.svg)

# The amp builder's art (design/textures/AMP-BUILDER-LAYERS.md): 227 WebP layers at 2x
# (body, panel, grille, corners; rendered by docs/design/textures/render_amp_parts.py)
# and the knob, switch and light sprites (TEXTURES.md). JUCE decodes WebP itself
# (JUCE_USE_WEBP); kit::ampArt loads them lazily on the message thread.
#
# They go in as ONE zip (built here from the same files; kit::ampArt reads it
# with juce::ZipFile): JUCE makes one source file per resource, and 236 small
# compiles cost minutes on CI where one large one costs seconds. The app stays
# one .exe. The zip is rebuilt when a layer changes.
file(GLOB ENCORE_AMP_PARTS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/ui" CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/Resources/ui/amp_parts/*.webp"
     "${CMAKE_CURRENT_SOURCE_DIR}/Resources/ui/amp_sprites/*.png")
list(SORT ENCORE_AMP_PARTS)
list(TRANSFORM ENCORE_AMP_PARTS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/Resources/ui/" OUTPUT_VARIABLE ENCORE_AMP_PART_FILES)
set(ENCORE_AMP_ZIP "${CMAKE_CURRENT_BINARY_DIR}/amp-art/amp-art.zip")
add_custom_command(
    OUTPUT "${ENCORE_AMP_ZIP}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/amp-art"
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${ENCORE_AMP_ZIP}" --format=zip --mtime=2026-01-01 ${ENCORE_AMP_PARTS}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Resources/ui"
    DEPENDS ${ENCORE_AMP_PART_FILES}
    COMMENT "Packing the amp art (${CMAKE_CURRENT_BINARY_DIR}/amp-art/amp-art.zip)"
    VERBATIM)
juce_add_binary_data(EncoreAmpArt
    HEADER_NAME EncoreAmpArt.h
    NAMESPACE EncoreAmpArt
    SOURCES "${ENCORE_AMP_ZIP}" Resources/amps/amp-templates.json)

# The UI's look (theme) and kit: shared by the app and the UI snapshot tests.
add_library(EncoreUi OBJECT)
target_sources(EncoreUi PRIVATE
    Source/ui/theme/EncoreFonts.cpp
    Source/ui/theme/EncoreFonts.h
    Source/ui/theme/EncoreLookAndFeel.cpp
    Source/ui/theme/EncoreLookAndFeel.h
    Source/ui/theme/EncoreTheme.h
    Source/ui/theme/EncoreAmpTheme.h
    Source/ui/theme/Icons.cpp
    Source/ui/theme/Icons.h
    Source/ui/theme/FocusRings.cpp
    Source/ui/theme/FocusRings.h
    Source/ui/theme/Paint.cpp
    Source/ui/theme/Paint.h
    Source/ui/kit/DialogFrame.cpp
    Source/ui/kit/DialogFrame.h
    Source/ui/kit/FaultCard.cpp
    Source/ui/kit/FaultCard.h
    Source/ui/kit/Group.cpp
    Source/ui/kit/Group.h
    Source/ui/kit/GearPlaceholder.cpp
    Source/ui/kit/GearPlaceholder.h
    Source/ui/kit/GearView.h
    Source/ui/kit/Chip.cpp
    Source/ui/kit/Chip.h
    Source/ui/kit/IconButton.cpp
    Source/ui/kit/IconButton.h
    Source/ui/kit/KeyCap.cpp
    Source/ui/kit/KeyCap.h
    Source/ui/kit/KnobView.cpp
    Source/ui/kit/KnobView.h
    Source/ui/kit/MuteButton.cpp
    Source/ui/kit/MuteButton.h
    Source/ui/kit/LevelBars.cpp
    Source/ui/kit/LevelBars.h
    Source/ui/kit/OutputStrip.cpp
    Source/ui/kit/OutputStrip.h
    Source/ui/kit/SmallKnob.cpp
    Source/ui/kit/SmallKnob.h
    Source/ui/kit/EqCurve.cpp
    Source/ui/kit/EqCurve.h
    Source/ui/kit/Fader.cpp
    Source/ui/kit/Fader.h
    Source/ui/kit/InfoList.cpp
    Source/ui/kit/InfoList.h
    Source/ui/kit/StageBackdrop.cpp
    Source/ui/kit/StageBackdrop.h
    Source/ui/kit/StatusBar.cpp
    Source/ui/kit/StatusBar.h
    Source/ui/kit/DbfsMeter.cpp
    Source/ui/kit/DbfsMeter.h
    Source/ui/kit/PopupField.cpp
    Source/ui/kit/PopupField.h
    Source/ui/kit/PaneTab.cpp
    Source/ui/kit/PaneTab.h
    Source/ui/kit/HealthStatus.cpp
    Source/ui/kit/HealthStatus.h
    Source/ui/kit/MixerStrip.cpp
    Source/ui/kit/MixerStrip.h
    Source/ui/kit/DropBox.cpp
    Source/ui/kit/DropBox.h
    Source/ui/kit/DragImage.cpp
    Source/ui/kit/DragImage.h
    Source/ui/kit/AmpArt.cpp
    Source/ui/kit/AmpArt.h
    Source/ui/kit/AmpFace.cpp
    Source/ui/kit/AmpFace.h
    Source/ui/kit/AmpTemplates.cpp
    Source/ui/kit/AmpTemplates.h
    Source/ui/kit/AmpFaceView.cpp
    Source/ui/kit/AmpFaceView.h
    Source/ui/kit/Lcd.cpp
    Source/ui/kit/Lcd.h
    Source/ui/kit/MeterBar.cpp
    Source/ui/kit/MeterBar.h
    Source/ui/kit/NoticeBar.cpp
    Source/ui/kit/NoticeBar.h
    Source/ui/kit/ParameterList.cpp
    Source/ui/kit/ParameterList.h
    Source/ui/kit/Pane.cpp
    Source/ui/kit/Pane.h
    Source/ui/kit/ProgressBar.cpp
    Source/ui/kit/ProgressBar.h
    Source/ui/kit/RowList.cpp
    Source/ui/kit/RowList.h
    Source/ui/kit/SegmentedControl.cpp
    Source/ui/kit/SegmentedControl.h
    Source/ui/kit/StatusBadge.cpp
    Source/ui/kit/StatusBadge.h
    Source/ui/kit/Toolbar.cpp
    Source/ui/kit/Toolbar.h
    Source/ui/kit/UtilityTile.cpp
    Source/ui/kit/UtilityTile.h)
# The screens: the app's windows and views, also tested off-screen (rendered into images).
target_sources(EncoreUi PRIVATE
    Source/app/AppSettings.cpp
    Source/app/EventLog.cpp
    Source/app/EventLog.h
    Source/app/AppSettings.h
    Source/app/MainComponent.cpp
    Source/app/MainComponent.h
    Source/ui/BlockPanel.cpp
    Source/ui/BlockPanel.h
    Source/ui/AmpBuilderView.cpp
    Source/ui/AmpBuilderView.h
    Source/ui/JackStage.cpp
    Source/ui/JackStage.h
    Source/ui/RoutingView.cpp
    Source/ui/RoutingView.h
    Source/ui/BottomRow.cpp
    Source/ui/BottomRow.h
    Source/ui/ChainStrip.cpp
    Source/ui/ChainStrip.h
    Source/ui/Inspector.cpp
    Source/ui/Inspector.h
    Source/ui/MidiSettings.cpp
    Source/ui/MidiSettings.h
    Source/ui/AudioSettingsView.cpp
    Source/ui/AudioSettingsView.h
    Source/ui/SettingsWindow.cpp
    Source/ui/SettingsWindow.h
    Source/ui/CueDialogs.cpp
    Source/ui/CueDialogs.h
    Source/ui/CueListEditor.cpp
    Source/ui/CueListEditor.h
    Source/ui/PreShowCheckView.cpp
    Source/ui/PreShowCheckView.h
    Source/ui/StageView.cpp
    Source/ui/StageView.h
    Source/ui/TunerDisplay.cpp
    Source/ui/TunerDisplay.h
    Source/ui/PatchList.cpp
    Source/ui/PatchList.h
    Source/ui/PluginSettings.cpp
    Source/ui/PluginSettings.h
    Source/ui/PluginWindow.cpp
    Source/ui/PluginWindow.h
    Source/ui/SetList.cpp
    Source/ui/SetList.h)
target_link_libraries(EncoreUi PUBLIC EncoreEngine EncoreUiAssets EncoreAmpArt)

# ------------------------------------------------------------------------------
# Build speed (decision 0049). Parsing JUCE's headers is ~95 % of compiling a
# typical Encore file: precompile them once per target. JUCE's own module
# sources (each one big file, configured by macros) and generated files keep
# compiling as before.
# ------------------------------------------------------------------------------
option(ENCORE_PCH "Precompile the JUCE headers for Encore's own sources" ON)

function(encore_precompile_headers target)
    if(NOT ENCORE_PCH)
        return()
    endif()

    # C++ only (the target also compiles a few C files).
    target_precompile_headers(${target} PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/Source/EncorePrecompiled.h>")

    file(GLOB_RECURSE juce_module_sources CONFIGURE_DEPENDS
        "${juce_SOURCE_DIR}/modules/*.cpp" "${juce_SOURCE_DIR}/modules/*.mm" "${juce_SOURCE_DIR}/modules/*.c")
    set_source_files_properties(${juce_module_sources} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endfunction()

juce_add_gui_app(Encore
    PRODUCT_NAME "Encore"
    COMPANY_NAME "Encore"
    VERSION ${PROJECT_VERSION}
    ICON_BIG Resources/ui/logo/encore-app-icon-256.png
    ICON_SMALL Resources/ui/logo/encore-app-icon-256.png)

target_sources(Encore PRIVATE
    Source/Main.cpp)

target_link_libraries(Encore
    PRIVATE
        EncoreJuce
        EncoreEngine
        EncoreUi
        nam_core)

# Link-time optimisation for the app players use; CI's quick checks skip it
# (it makes linking the slowest step and isn't needed to test behaviour).
option(ENCORE_LTO "Link-time optimisation for Encore (Release)" ON)

if(ENCORE_LTO)
    # The shared code is compiled for LTO too (the tests then link with it as well).
    foreach(target Encore EncoreJuce EncoreEngine EncoreUi)
        target_link_libraries(${target} PUBLIC juce::juce_recommended_lto_flags)
    endforeach()
endif()

encore_precompile_headers(EncoreEngine)
encore_precompile_headers(EncoreUi)

# ------------------------------------------------------------------------------
# Tests: a console app that runs every engine unit test.
#   cmake --build --preset debug --target EncoreTests && ctest --preset debug
# ------------------------------------------------------------------------------
option(ENCORE_BUILD_TESTS "Build the engine unit tests" ON)

if(ENCORE_BUILD_TESTS)
    enable_testing()

    # A tiny VST3 the tests host (gain + ways to misbehave), never shipped.
    juce_add_plugin(EncoreTestPlugin
        PRODUCT_NAME "Encore Test Plugin"
        COMPANY_NAME "Encore"
        PLUGIN_MANUFACTURER_CODE Enco
        PLUGIN_CODE Etst
        FORMATS VST3
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(EncoreTestPlugin PRIVATE Tests/plugin/EncoreTestPlugin.cpp)

    target_compile_definitions(EncoreTestPlugin PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(EncoreTestPlugin
        PRIVATE
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    juce_add_console_app(EncoreTests PRODUCT_NAME "EncoreTests")
    # The tests load the test plugin when they run, so only linking waits for it:
    # add_dependencies() would hold back every test file until the plugin is
    # built, leaving the tests' compiles as a serial tail at the end of CI.
    set_property(TARGET EncoreTests APPEND PROPERTY LINK_DEPENDS "$<TARGET_FILE:EncoreTestPlugin_VST3>")

    target_sources(EncoreTests PRIVATE
        Tests/TestMain.cpp
        Tests/HangWatchdog.cpp
        Tests/ThreadStopTests.cpp
        Tests/HangWatchdog.h
        Tests/AllocationCounter.cpp
        Tests/AllocationCounter.h
        Tests/CabTests.cpp
        Tests/DspTests.cpp
        Tests/EngineCoreTests.cpp
        Tests/MidiTests.cpp
        Tests/ModuleBaseTests.cpp
        Tests/ModuleDspTests.cpp
        Tests/ModuleTypeTests.cpp
        Tests/RenderTests.cpp
        Tests/ModelTests.cpp
        Tests/NamTests.cpp
        Tests/RobustnessTests.cpp
        Tests/StageTests.cpp
        Tests/StageViewTests.cpp
        Tests/KitGallery.h
        Tests/UiSnapshot.h
        Tests/UiSnapshotTests.cpp
        Tests/BoardFixtures.h
        Tests/PluginFixtures.h
        Tests/EditViewTests.cpp
        Tests/HardeningTests.cpp
        Tests/ShowcaseTests.cpp
        Tests/StageBoardTests.cpp
        Tests/AmpBuilderTests.cpp
        Tests/PatchBuilderTests.cpp
        Tests/PatchPlayerTests.cpp
        Tests/PluginTests.cpp
        Tests/PluginViewTests.cpp
        Tests/ResamplerTests.cpp
        Tests/RoutingEngineTests.cpp
        Tests/RoutingSessionTests.cpp
        Tests/RoutingViewTests.cpp
        Tests/RoutingTests.cpp
        Tests/SessionTests.cpp
        Tests/SplitPathTests.cpp
        Tests/TestSignals.h)

    # JUCE's settings are EncoreJuce's (the app's): the tests run what ships.
    target_compile_definitions(EncoreTests PRIVATE
        # Real captures shipped with NeuralAmpModelerCore, used as test fixtures.
        ENCORE_NAM_EXAMPLE_MODELS="${nam_SOURCE_DIR}/example_models"
        # The test plugin's .vst3 bundle.
        ENCORE_TEST_PLUGIN="$<GENEX_EVAL:$<TARGET_PROPERTY:EncoreTestPlugin_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")

    target_link_libraries(EncoreTests
        PRIVATE
            EncoreJuce
            EncoreEngine
            EncoreUi
            nam_core)

    encore_precompile_headers(EncoreTests)
    # The source scan in ThreadStopTests.cpp reads Source/.
    target_compile_definitions(EncoreTests PRIVATE ENCORE_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
    # Plain C++ that includes windows.h: no JUCE precompiled header.
    set_source_files_properties(Tests/HangWatchdog.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

    if(WIN32)
        # The hang watchdog prints symbolised stacks: DbgHelp, and a PDB in
        # Release too (the objects already carry the debug info, /Z7). /DEBUG
        # would switch off these two optimisations; they stay on.
        target_link_libraries(EncoreTests PRIVATE dbghelp)
        if(MSVC)
            target_link_options(EncoreTests PRIVATE "$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF>")
        endif()
    endif()

    # The tests run as four processes in parallel (ctest -j): the long ones
    # (robustness and its soak, plugins, screens) each on their own, the
    # rest together. ENCORE_TEST_ONLY / ENCORE_TEST_SKIP take names joined by |.
    set(encore_slow_tests "Robustness" "Plugins|Plugins in the UI"
        "Edit view (design v3)|Stage view (design v3)|Design v3 hardening|UI design system|Stage view")
    set(encore_test_groups robustness plugins screens)

    foreach(name tests IN ZIP_LISTS encore_test_groups encore_slow_tests)
        add_test(NAME EncoreTests.${name} COMMAND EncoreTests)
        set_tests_properties(EncoreTests.${name} PROPERTIES ENVIRONMENT "ENCORE_TEST_ONLY=${tests}")
    endforeach()

    list(JOIN encore_slow_tests "|" encore_all_slow)
    add_test(NAME EncoreTests.engine COMMAND EncoreTests)
    set_tests_properties(EncoreTests.engine PROPERTIES ENVIRONMENT "ENCORE_TEST_SKIP=${encore_all_slow}")

    # --------------------------------------------------------------------------
    # EncoreRender: a WAV through one block or a whole patch, offline, from the
    # same engine objects (native modules, decision 0062). Built with the tests
    # and run by ctest on a generated sweep.
    #   EncoreRender --in IN.wav --out OUT.wav --block engineProbe --set gain=6
    # --------------------------------------------------------------------------
    juce_add_console_app(EncoreRender PRODUCT_NAME "EncoreRender")
    target_sources(EncoreRender PRIVATE Tools/EncoreRender/Main.cpp)
    target_link_libraries(EncoreRender
        PRIVATE
            EncoreJuce
            EncoreEngine
            nam_core)
    encore_precompile_headers(EncoreRender)

    set(encore_render_dir "${CMAKE_CURRENT_BINARY_DIR}/render")
    add_test(NAME EncoreRender.sweep
             COMMAND EncoreRender --make-sweep "${encore_render_dir}/sweep.wav" --seconds 3)
    add_test(NAME EncoreRender.probe
             COMMAND EncoreRender --in "${encore_render_dir}/sweep.wav" --out "${encore_render_dir}/sweep-probe.wav"
                     --block engineProbe --set shape=Bell --set freq=800 --set gain=6 --set q=1
                     --set drive=12 --set quality=High --report "${encore_render_dir}/sweep-probe.txt")
    add_test(NAME EncoreRender.null
             COMMAND EncoreRender --in "${encore_render_dir}/sweep.wav" --out "${encore_render_dir}/sweep-null.wav"
                     --block engineProbe --report "${encore_render_dir}/sweep-null.txt")
    set_tests_properties(EncoreRender.sweep PROPERTIES FIXTURES_SETUP encore_render_sweep)
    set_tests_properties(EncoreRender.probe EncoreRender.null PROPERTIES FIXTURES_REQUIRED encore_render_sweep)
endif()

if(MSVC)
    # Encore is the startup project when opening the generated solution.
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Encore)
endif()
