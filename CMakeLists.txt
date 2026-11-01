cmake_minimum_required(VERSION 3.22)

project(Floorplan VERSION 0.1.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(FLOORPLAN_BUILD_PLUGIN     "Build the JUCE plugin (needs JUCE)" ON)
option(FLOORPLAN_BUILD_TESTS      "Build unit tests"            ON)
option(FLOORPLAN_BUILD_BENCHMARKS "Build performance benchmarks" ON)
set(FLOORPLAN_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE" CACHE PATH "Path to a JUCE checkout")

if(FLOORPLAN_BUILD_PLUGIN)
    if(NOT EXISTS "${FLOORPLAN_JUCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "JUCE not found at ${FLOORPLAN_JUCE_DIR}. Run: git submodule update --init --recursive")
    endif()
    add_subdirectory("${FLOORPLAN_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
endif()

# ---------------------------------------------------------------------------
# floorplan_core: JUCE-free DSP, instruments, sequencer and engine.
# Everything that runs on the audio thread lives here so it can be tested and
# benchmarked without the plugin wrapper.
# ---------------------------------------------------------------------------
add_library(floorplan_core STATIC
    Source/DSP/SineTable.cpp
    Source/Instruments/Voices.cpp
    Source/Sequencer/Pattern.cpp
    Source/Sequencer/Sequencer.cpp
    Source/Audio/DrumEngine.cpp)

target_include_directories(floorplan_core PUBLIC Source)
target_compile_features(floorplan_core PUBLIC cxx_std_17)
# Linked into the VST3 shared object, so it must be position-independent (Linux).
set_target_properties(floorplan_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(floorplan_core PRIVATE /W4 /fp:fast)
else()
    target_compile_options(floorplan_core PRIVATE -Wall -Wextra -ffast-math -fno-finite-math-only)
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(FLOORPLAN_BUILD_PLUGIN)
juce_add_plugin(Floorplan
    COMPANY_NAME "Floorplan Contributors"
    COMPANY_WEBSITE "https://github.com/Kuzuw/Floorplan"
    BUNDLE_ID "io.github.kuzuw.floorplan"
    PLUGIN_MANUFACTURER_CODE Flpn
    PLUGIN_CODE Fdm1
    FORMATS VST3 Standalone
    PRODUCT_NAME "Floorplan"
    DESCRIPTION "Lightweight House & EDM drum machine"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    # App icon (taskbar, Dock, shortcuts). Rendered by: floorplan_plugin_tests --icon <png> <size>
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon-small.png"
    VST3_CATEGORIES Instrument Drum
    MICROPHONE_PERMISSION_ENABLED FALSE)

set(FLOORPLAN_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters/ParameterLayout.cpp
    Source/Presets/PatternStore.cpp
    Source/Presets/PatternSerializer.cpp
    Source/Presets/FactoryContent.cpp
    Source/Presets/PresetManager.cpp
    Source/Presets/SampleLoader.cpp
    Source/UI/LookAndFeel.cpp
    Source/UI/Widgets.cpp
    Source/UI/HeaderComponent.cpp
    Source/UI/InstrumentComponent.cpp
    Source/UI/SequencerComponent.cpp
    Source/UI/SoundDesignComponent.cpp
    Source/UI/GrooveComponent.cpp
    Source/UI/MixerComponent.cpp
    Source/UI/MainComponent.cpp)
list(TRANSFORM FLOORPLAN_PLUGIN_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
set(FLOORPLAN_PLUGIN_SOURCES ${FLOORPLAN_PLUGIN_SOURCES} CACHE INTERNAL "")

set(FLOORPLAN_JUCE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    FLOORPLAN_VERSION_STRING="${PROJECT_VERSION}"
    CACHE INTERNAL "")

target_sources(Floorplan PRIVATE ${FLOORPLAN_PLUGIN_SOURCES})
target_compile_definitions(Floorplan PUBLIC ${FLOORPLAN_JUCE_DEFINITIONS})

target_link_libraries(Floorplan
    PRIVATE
        floorplan_core
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
# Tests and benchmarks
# ---------------------------------------------------------------------------
if(FLOORPLAN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

if(FLOORPLAN_BUILD_BENCHMARKS)
    add_subdirectory(Benchmarks)
endif()
