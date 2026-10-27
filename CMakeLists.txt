cmake_minimum_required(VERSION 3.22)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # no VC++ redistributable needed
project(TabMidi VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/utf-8)   # the sources contain UTF-8 (Russian) string literals
endif()

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.8 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(TABMIDI_CORE Source/TabModel.cpp Source/GpxImporter.cpp)

# ---- the plugin -------------------------------------------------------------
if(NOT TABMIDI_TESTS_ONLY)
juce_add_plugin(TabMidi
    COMPANY_NAME "desNotto"
    COMPANY_COPYRIGHT "desNotto"
    BUNDLE_ID com.desNotto.TabMidi
    PLUGIN_MANUFACTURER_CODE Tbmd
    PLUGIN_CODE Tbmi
    FORMATS VST3 Standalone
    PRODUCT_NAME "TabMidi"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Instrument
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(TabMidi PRIVATE ${TABMIDI_CORE} Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(TabMidi PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(TabMidi PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()

# ---- developer test (GPX import without any GUI) ----------------------------
juce_add_console_app(TabMidiTest PRODUCT_NAME "TabMidiTest")
target_sources(TabMidiTest PRIVATE ${TABMIDI_CORE} tests/TestMain.cpp)
target_compile_definitions(TabMidiTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(TabMidiTest PRIVATE juce::juce_core juce::juce_audio_basics juce::juce_recommended_config_flags)

# ---- developer tool: render the window to PNG (cmake -DTABMIDI_SNAPSHOT=ON) ----
if(TABMIDI_SNAPSHOT AND NOT TABMIDI_TESTS_ONLY)
add_executable(TabMidiSnap tests/Snapshot.cpp)
target_link_libraries(TabMidiSnap PRIVATE TabMidi)
endif()
