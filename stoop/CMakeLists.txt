# STOOP — VST3 / AU / standalone build.
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release            # fetches JUCE
#   cmake -B build -DSTOOP_JUCE_PATH=/path/to/JUCE    # or use a local one
#   cmake --build build --target Stoop_VST3 -j
#   cmake --build build --target test_core && ./build/test_core   # DSP tests
#
# The DSP in core/ has no dependency on JUCE and is tested on its own; this file
# only builds the wrapper around it.
cmake_minimum_required(VERSION 3.22)
project(STOOP VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Windows: link the C++ runtime into the plugin, so it loads on a machine that
# has no Visual C++ redistributable installed (most Live users never install one)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(STOOP_JUCE_PATH "" CACHE PATH "Path to an existing JUCE checkout")
set(STOOP_COMPANY "Stoop" CACHE STRING "Company name baked into the plugin")

if(STOOP_JUCE_PATH)
    add_subdirectory(${STOOP_JUCE_PATH} juce_build)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(MSVC)
    # The sources are UTF-8 (the em dashes in labels, tooltips and preset names).
    # Without /utf-8, MSVC reads them in the system code page and the panel
    # shows mojibake. _USE_MATH_DEFINES gives core/ its M_PI.
    add_compile_options(/utf-8 /bigobj)
    add_compile_definitions(_USE_MATH_DEFINES)
endif()

# Copying into the system plug-in folder after each build is handy on a Mac and
# Linux. On Windows that folder (C:/Program Files/Common Files/VST3) needs an
# administrator, so the build would fail; there the installer does it.
if(WIN32)
    set(STOOP_COPY_AFTER_BUILD FALSE)
else()
    set(STOOP_COPY_AFTER_BUILD TRUE)
endif()

set(STOOP_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND STOOP_FORMATS AU)
endif()

juce_add_plugin(Stoop
    COMPANY_NAME              "${STOOP_COMPANY}"
    PRODUCT_NAME              "STOOP"
    PLUGIN_MANUFACTURER_CODE  Frct
    PLUGIN_CODE               Stp1
    FORMATS                   ${STOOP_FORMATS}
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   ${STOOP_COPY_AFTER_BUILD}
    VST3_CATEGORIES           "Fx" "Distortion"
    AU_MAIN_TYPE              kAudioUnitType_Effect)

target_sources(Stoop PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

# GUIDE.md, compiled in: the Guide button shows the same file people read on disk
juce_add_binary_data(StoopGuide NAMESPACE StoopGuide HEADER_NAME StoopGuide.h SOURCES GUIDE.md)

target_include_directories(Stoop PRIVATE core Source)

target_compile_definitions(Stoop PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(Stoop
    PRIVATE StoopGuide juce::juce_audio_utils juce::juce_dsp
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
    JucePlugin_Name="STOOP"
    JucePlugin_VersionString="${PROJECT_VERSION}"
    JucePlugin_ManufacturerCode=0x46726374
    JucePlugin_PluginCode=0x53747031)
target_link_libraries(host_smoke
    PRIVATE StoopGuide juce::juce_audio_utils juce::juce_dsp juce::juce_gui_basics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
