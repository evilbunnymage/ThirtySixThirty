cmake_minimum_required(VERSION 3.22)

# Universal binary on macOS; ignored elsewhere. Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(SixThirty VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime so the .vst3 has no redistributable dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(SIXTHIRTY_BUILD_TESTS "Build the standalone DSP tests" ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SixThirty
    COMPANY_NAME "Homebrew"
    PLUGIN_MANUFACTURER_CODE Hmbr
    PLUGIN_CODE Sx30
    FORMATS VST3
    PRODUCT_NAME "SixThirty"
    VST3_CATEGORIES Fx Dynamics
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    # Set TRUE to auto-copy into the system VST3 folder after build
    # (on Windows this needs an elevated shell).
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(SixThirty PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_include_directories(SixThirty PRIVATE src)

target_compile_definitions(SixThirty PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SixThirty
    PRIVATE
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(SIXTHIRTY_BUILD_TESTS)
    enable_testing()
    add_executable(sixthirty_dsp_tests tests/dsp_tests.cpp)
    target_include_directories(sixthirty_dsp_tests PRIVATE src)
    add_test(NAME dsp COMMAND sixthirty_dsp_tests)
endif()
