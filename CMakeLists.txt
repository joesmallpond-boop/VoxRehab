cmake_minimum_required(VERSION 3.22)
project(VoxRehab VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/JUCE)
endif()
add_subdirectory(${JUCE_DIR} JUCE)

if(APPLE)
    set(VR_FORMATS VST3 AU Standalone)
else()
    set(VR_FORMATS VST3 Standalone)
endif()

juce_add_plugin(VoxRehab
    COMPANY_NAME "Joe Caple"
    PRODUCT_NAME "Vox Rehab"
    BUNDLE_ID com.joecaple.voxrehab
    PLUGIN_MANUFACTURER_CODE Jcap
    PLUGIN_CODE VxRh
    FORMATS ${VR_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Restoration Dynamics EQ
    AU_MAIN_TYPE kAudioUnitType_Effect)

set(VR_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Params.cpp)

target_sources(VoxRehab PRIVATE ${VR_SOURCES})

set(VR_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_compile_definitions(VoxRehab PUBLIC ${VR_DEFS})

target_link_libraries(VoxRehab
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Offline DSP test harness (not needed to build the plugin)
option(VOXREHAB_TESTS "Build offline DSP tests" OFF)
if(VOXREHAB_TESTS)
    juce_add_console_app(VoxRehabTests PRODUCT_NAME "VoxRehabTests")
    target_sources(VoxRehabTests PRIVATE tests/Tests.cpp ${VR_SOURCES})
    target_compile_definitions(VoxRehabTests PRIVATE ${VR_DEFS} JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(VoxRehabTests
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags)
endif()
