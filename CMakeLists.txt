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

# ------------------------------------------------------------------------------
# Engine sources, shared by the app and the tests.
# An INTERFACE library: each target compiles them with its own JUCE modules,
# which avoids linking JUCE twice.
# ------------------------------------------------------------------------------
add_library(EncoreEngine INTERFACE)

target_sources(EncoreEngine INTERFACE
    Source/engine/PeakMeter.h
    Source/engine/blocks/EqBlock.cpp
    Source/engine/blocks/EqBlock.h
    Source/engine/blocks/NoiseGateBlock.cpp
    Source/engine/blocks/NoiseGateBlock.h
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
    Source/engine/dsp/Biquad.h
    Source/engine/dsp/SafetyLimiter.cpp
    Source/engine/dsp/SafetyLimiter.h)

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
    Source/engine/AudioEngine.cpp
    Source/engine/AudioEngine.h
    Source/ui/BlockPanel.cpp
    Source/ui/BlockPanel.h
    Source/ui/LevelMeter.cpp
    Source/ui/LevelMeter.h)

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
        juce::juce_audio_utils
        juce::juce_audio_devices
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
        Tests/DspTests.cpp
        Tests/EngineCoreTests.cpp
        Tests/TestSignals.h)

    target_compile_definitions(EncoreTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_UNIT_TESTS=1)

    target_link_libraries(EncoreTests
        PRIVATE
            EncoreEngine
            juce::juce_audio_basics
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
