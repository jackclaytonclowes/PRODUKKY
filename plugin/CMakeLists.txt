# FRACTURE — VST3 / AU / standalone build.
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release            # fetches JUCE
#   cmake -B build -DFRACTURE_JUCE_PATH=/path/to/JUCE    # or use a local one
#   cmake --build build --target Fracture_VST3 -j
#   cmake --build build --target test_core && ./build/test_core   # DSP tests
#
# The DSP in core/ has no dependency on JUCE and is tested on its own; this file
# only builds the wrapper around it.
cmake_minimum_required(VERSION 3.22)
project(FRACTURE VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(FRACTURE_JUCE_PATH "" CACHE PATH "Path to an existing JUCE checkout")
set(FRACTURE_COMPANY "Fracture" CACHE STRING "Company name baked into the plugin")

if(FRACTURE_JUCE_PATH)
    add_subdirectory(${FRACTURE_JUCE_PATH} juce_build)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(FRACTURE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND FRACTURE_FORMATS AU)
endif()

juce_add_plugin(Fracture
    COMPANY_NAME              "${FRACTURE_COMPANY}"
    PRODUCT_NAME              "FRACTURE"
    PLUGIN_MANUFACTURER_CODE  Frct
    PLUGIN_CODE               Frcd
    FORMATS                   ${FRACTURE_FORMATS}
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   TRUE
    VST3_CATEGORIES           "Fx" "Distortion"
    AU_MAIN_TYPE              kAudioUnitType_Effect)

target_sources(Fracture PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

# GUIDE.md, compiled in: the Guide button shows the same file people read on disk
juce_add_binary_data(FractureGuide NAMESPACE FractureGuide HEADER_NAME FractureGuide.h SOURCES GUIDE.md)

target_include_directories(Fracture PRIVATE core Source)

target_compile_definitions(Fracture PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(Fracture
    PRIVATE FractureGuide juce::juce_audio_utils juce::juce_dsp
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# ---- the DSP tests, which need nothing but a compiler
add_executable(test_core tests/test_core.cpp)
target_include_directories(test_core PRIVATE core)
target_compile_features(test_core PRIVATE cxx_std_17)

# ---- a host-level smoke test: runs the processor, walks the presets, and
# renders the editor to a PNG so the interface can be looked at without a DAW
juce_add_console_app(host_smoke PRODUCT_NAME "host_smoke")
target_sources(host_smoke PRIVATE tests/host_smoke.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(host_smoke PRIVATE core Source)
target_compile_definitions(host_smoke PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="FRACTURE"
    JucePlugin_VersionString="${PROJECT_VERSION}"
    JucePlugin_ManufacturerCode=0x46726374
    JucePlugin_PluginCode=0x46726364)
target_link_libraries(host_smoke
    PRIVATE FractureGuide juce::juce_audio_utils juce::juce_dsp juce::juce_gui_basics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
