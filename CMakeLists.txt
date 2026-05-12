cmake_minimum_required(VERSION 3.22)

project(NukedSC55JUCE
    VERSION 0.1.0
    DESCRIPTION "JUCE-based standalone host for the Nuked-SC55 emulator"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

#===============================================================================
# JUCE — prefer a local install (mirrors the convention in GrooveGrid)
#===============================================================================
if(WIN32)
    set(JUCE_LOCAL_PATH "C:/JUCE" CACHE PATH "Local JUCE checkout")
elseif(APPLE)
    set(JUCE_LOCAL_PATH "$ENV{HOME}/JUCE" CACHE PATH "Local JUCE checkout")
else()
    set(JUCE_LOCAL_PATH "$ENV{HOME}/JUCE" CACHE PATH "Local JUCE checkout")
endif()

if(EXISTS "${JUCE_LOCAL_PATH}/CMakeLists.txt")
    message(STATUS "JUCE: using local install at ${JUCE_LOCAL_PATH}")
    add_subdirectory(${JUCE_LOCAL_PATH} "${CMAKE_CURRENT_BINARY_DIR}/JUCE")
else()
    message(STATUS "JUCE: no local install at ${JUCE_LOCAL_PATH}, fetching from GitHub")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

#===============================================================================
# Nuked-SC55 backend — built from the submodule under external/Nuked-SC55.
# We intentionally do NOT add_subdirectory() the upstream CMakeLists because
# it pulls SDL2 / RtMidi for its standard frontend. We only need the backend.
#===============================================================================
set(NUKED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/Nuked-SC55)

if(NOT EXISTS "${NUKED_DIR}/src/backend/emu.h")
    message(FATAL_ERROR
        "Nuked-SC55 submodule not found at ${NUKED_DIR}.\n"
        "Did you clone with --recurse-submodules?  Run:\n"
        "    git submodule update --init --recursive")
endif()

# config.h.in expects @CMAKE_PROJECT_VERSION@ etc. — those resolve to this
# project's version (0.1.0). Nuked's own version (0.6.1 at pin time) is
# recorded by the submodule SHA, not by this generated header.
set(NUKED_SOURCE "nuked-sc55-juce" CACHE STRING "Origin string baked into the Nuked build")
set(NUKED_ENABLE_ASIO OFF)
configure_file(
    ${NUKED_DIR}/src/backend/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/backend/config.h
    @ONLY)

add_library(nuked_sc55_backend STATIC
    ${NUKED_DIR}/src/backend/config.cpp
    ${NUKED_DIR}/src/backend/diagnostics.cpp
    ${NUKED_DIR}/src/backend/emu.cpp
    ${NUKED_DIR}/src/backend/file_hashing.cpp
    ${NUKED_DIR}/src/backend/file_io.cpp
    ${NUKED_DIR}/src/backend/lcd.cpp
    ${NUKED_DIR}/src/backend/mcu_interrupt.cpp
    ${NUKED_DIR}/src/backend/mcu_opcodes.cpp
    ${NUKED_DIR}/src/backend/mcu_timer.cpp
    ${NUKED_DIR}/src/backend/mcu.cpp
    ${NUKED_DIR}/src/backend/pcm.cpp
    ${NUKED_DIR}/src/backend/rom_io.cpp
    ${NUKED_DIR}/src/backend/rom.cpp
    ${NUKED_DIR}/src/backend/standard_romsets.cpp
    ${NUKED_DIR}/src/backend/submcu.cpp
    ${NUKED_DIR}/src/backend/sha/sha224-256.c)

target_include_directories(nuked_sc55_backend PUBLIC
    ${NUKED_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR})

target_compile_features(nuked_sc55_backend PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(nuked_sc55_backend PRIVATE /w)
    target_compile_definitions(nuked_sc55_backend PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(nuked_sc55_backend PRIVATE -w)
endif()

#===============================================================================
# Standalone JUCE GUI app
#===============================================================================
juce_add_gui_app(NukedSC55JUCE
    PRODUCT_NAME "Nuked SC-55 JUCE"
    COMPANY_NAME "nuked-sc55-juce")

target_sources(NukedSC55JUCE PRIVATE
    src/Main.cpp
    src/MainComponent.cpp
    src/NukedEngine.cpp
    src/MidiFilePlayer.cpp)

target_include_directories(NukedSC55JUCE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(NukedSC55JUCE PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_WIN_PER_MONITOR_DPI_AWARE=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:NukedSC55JUCE,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:NukedSC55JUCE,JUCE_VERSION>")

target_link_libraries(NukedSC55JUCE PRIVATE
    nuked_sc55_backend
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
