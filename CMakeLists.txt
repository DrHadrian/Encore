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
# Engine sources, shared by the app and the tests.
# An INTERFACE library: each target compiles them with its own JUCE modules,
# which avoids linking JUCE twice.
# ------------------------------------------------------------------------------
add_library(EncoreEngine INTERFACE)

target_sources(EncoreEngine INTERFACE
    Source/engine/AudioEngine.cpp
    Source/engine/AudioEngine.h
    Source/engine/PeakMeter.h
    Source/engine/blocks/CabIrBlock.cpp
    Source/engine/blocks/CabIrBlock.h
    Source/engine/blocks/EqBlock.cpp
    Source/engine/blocks/EqBlock.h
    Source/engine/blocks/NamAmpBlock.cpp
    Source/engine/blocks/NamAmpBlock.h
    Source/engine/blocks/NoiseGateBlock.cpp
    Source/engine/blocks/NoiseGateBlock.h
    Source/engine/core/BackgroundJobs.h
    Source/engine/core/Block.cpp
    Source/engine/core/Block.h
    Source/engine/core/Chain.cpp
    Source/engine/core/Chain.h
    Source/engine/core/ChainHandover.cpp
    Source/engine/core/ChainHandover.h
    Source/engine/core/LockFreeFifo.h
    Source/engine/core/Parameter.cpp
    Source/engine/core/Parameter.h
    Source/engine/core/Realtime.cpp
    Source/engine/core/Realtime.h
    Source/engine/core/RealtimeHandover.h
    Source/engine/dsp/Biquad.h
    Source/engine/dsp/SafetyLimiter.cpp
    Source/engine/dsp/SafetyLimiter.h
    Source/engine/dsp/StreamResampler.cpp
    Source/engine/dsp/StreamResampler.h
    Source/engine/nam/NamModel.cpp
    Source/engine/nam/NamModel.h
    Source/model/ShowFolder.cpp
    Source/model/ShowFolder.h
    Source/model/ShowJson.cpp
    Source/model/ShowJson.h
    Source/model/ShowModel.cpp
    Source/model/ShowModel.h
    Source/platform/SystemTuning.cpp
    Source/platform/SystemTuning.h
    Source/session/PatchBuilder.cpp
    Source/session/PatchBuilder.h
    Source/session/ShowSession.cpp
    Source/session/ShowSession.h)

target_include_directories(EncoreEngine INTERFACE Source)

# ------------------------------------------------------------------------------
# The app
# ------------------------------------------------------------------------------
juce_add_gui_app(Encore
    PRODUCT_NAME "Encore"
    COMPANY_NAME "Encore"
    VERSION ${PROJECT_VERSION})

target_sources(Encore PRIVATE
    Source/Main.cpp
    Source/app/AppSettings.cpp
    Source/app/AppSettings.h
    Source/app/MainComponent.cpp
    Source/app/MainComponent.h
    Source/ui/BlockPanel.cpp
    Source/ui/BlockPanel.h
    Source/ui/ChainStrip.cpp
    Source/ui/ChainStrip.h
    Source/ui/LevelMeter.cpp
    Source/ui/LevelMeter.h
    Source/ui/PatchList.cpp
    Source/ui/PatchList.h)

target_compile_definitions(Encore PRIVATE
    # Audio drivers. ASIO is the one we play through; WASAPI stays as a fallback
    # for laptops without an ASIO driver. JUCE ships the ASIO header itself.
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=0
    # Features we don't use: keep the binary lean and avoid network code.
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Encore,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Encore,JUCE_VERSION>")

target_link_libraries(Encore
    PRIVATE
        EncoreEngine
        nam_core
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# Tests: a console app that runs every engine unit test.
#   cmake --build --preset debug --target EncoreTests && ctest --preset debug
# ------------------------------------------------------------------------------
option(ENCORE_BUILD_TESTS "Build the engine unit tests" ON)

if(ENCORE_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(EncoreTests PRODUCT_NAME "EncoreTests")

    target_sources(EncoreTests PRIVATE
        Tests/TestMain.cpp
        Tests/AllocationCounter.cpp
        Tests/AllocationCounter.h
        Tests/CabTests.cpp
        Tests/DspTests.cpp
        Tests/EngineCoreTests.cpp
        Tests/ModelTests.cpp
        Tests/NamTests.cpp
        Tests/PatchBuilderTests.cpp
        Tests/ResamplerTests.cpp
        Tests/SessionTests.cpp
        Tests/TestSignals.h)

    target_compile_definitions(EncoreTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_UNIT_TESTS=1
        JUCE_MODAL_LOOPS_PERMITTED=1     # tests pump the message loop themselves
        JUCE_ALSA=0                      # no audio hardware needed (or headers, on Linux CI)
        JUCE_JACK=0
        # Real captures shipped with NeuralAmpModelerCore, used as test fixtures.
        ENCORE_NAM_EXAMPLE_MODELS="${nam_SOURCE_DIR}/example_models")

    target_link_libraries(EncoreTests
        PRIVATE
            EncoreEngine
            nam_core
            juce::juce_audio_basics
            juce::juce_audio_devices
            juce::juce_audio_formats
            juce::juce_dsp
            juce::juce_events
            juce::juce_core
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME EncoreTests COMMAND EncoreTests)
endif()

if(MSVC)
    # Encore is the startup project when opening the generated solution.
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Encore)
endif()
