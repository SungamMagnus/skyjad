cmake_minimum_required(VERSION 3.22)

# Must precede project(): these drive the compiler checks.
#
# Build a universal binary. Left unset, CMake targets its OWN architecture — and
# an x86_64 CMake (Intel Homebrew under Rosetta) then silently produces an
# Intel-only plug-in, which native arm64 hosts skip without any error. arm64
# macOS starts at 11.0, so the deployment target has to clear that.
if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING
            "macOS build architectures" FORCE)
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
            "Minimum macOS version" FORCE)
    endif()
endif()

project(CloudiusVST VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: either a sibling/absolute checkout (-DJUCE_DIR=...) or the macOS
# default install location.
set(JUCE_DIR "/Applications/JUCE" CACHE PATH "Path to a JUCE source checkout")
if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${JUCE_DIR} — pass -DJUCE_DIR=/path/to/JUCE")
endif()
add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

juce_add_plugin(Cloudius
    COMPANY_NAME            "Sungam"
    BUNDLE_ID               "com.sungam.cloudius"
    PLUGIN_MANUFACTURER_CODE Sngm
    PLUGIN_CODE             Cldu
    FORMATS                 VST3 AU Standalone
    PRODUCT_NAME            "Cloudius"
    DESCRIPTION             "Granular texture processor"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES         Fx Delay Reverb "Pitch Shift"
    AU_MAIN_TYPE            kAudioUnitType_Effect)

set(CLOUDIUS_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/CloudsEngine.cpp
    Source/Panel.cpp)

# The Clouds DSP, verbatim from the firmware tree. This is exactly the file
# list the module's own off-target makefile builds, and TEST is the define it
# compiles them with — it swaps the STM32 debug pin and the .ramtext attribute
# for nothing at all.
set(CLOUDIUS_DSP_SOURCES
    eurorack/clouds/dsp/correlator.cc
    eurorack/clouds/dsp/granular_processor.cc
    eurorack/clouds/dsp/mu_law.cc
    eurorack/clouds/dsp/pvoc/frame_transformation.cc
    eurorack/clouds/dsp/pvoc/phase_vocoder.cc
    eurorack/clouds/dsp/pvoc/stft.cc
    eurorack/clouds/resources.cc
    eurorack/stmlib/dsp/atan.cc
    eurorack/stmlib/dsp/units.cc
    eurorack/stmlib/utils/random.cc)

target_sources(Cloudius PRIVATE ${CLOUDIUS_PLUGIN_SOURCES} ${CLOUDIUS_DSP_SOURCES})

target_compile_definitions(Cloudius PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    TEST=1)

# SYSTEM so the firmware's own warning profile doesn't bleed into JUCE's
# stricter one.
target_include_directories(Cloudius SYSTEM PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/eurorack")
target_include_directories(Cloudius PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source")

target_link_libraries(Cloudius PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Dev tool: render the faceplate to PNGs without a host. -DCLOUDIUS_PANEL_SHOT=ON
option(CLOUDIUS_PANEL_SHOT "Build the offscreen faceplate renderer" OFF)
if(CLOUDIUS_PANEL_SHOT)
    juce_add_console_app(panel_shot PRODUCT_NAME "panel_shot" NEEDS_CURL FALSE)
    target_sources(panel_shot PRIVATE
        tools/panel_shot.cpp ${CLOUDIUS_PLUGIN_SOURCES} ${CLOUDIUS_DSP_SOURCES})
    target_compile_definitions(panel_shot PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 TEST=1
        JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_include_directories(panel_shot SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/eurorack")
    target_include_directories(panel_shot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
    target_link_libraries(panel_shot PRIVATE juce::juce_audio_utils juce::juce_dsp)
endif()
