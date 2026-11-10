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
    Source/engine/PeakMeter.h
    Source/ui/LevelMeter.cpp
    Source/ui/LevelMeter.h)

target_include_directories(Encore PRIVATE Source)

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
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_basics
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    # Encore is the startup project when opening the generated solution.
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Encore)
endif()
