cmake_minimum_required(VERSION 3.25)

project(FeedbackFromWhenceYouCame
    VERSION 0.1.0
    DESCRIPTION "Zero-latency karaoke feedback suppressor (VST3)"
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(ProjectOptions)
include(Vst3Sdk)

# DSP engine: standard C++ only, no Steinberg/JUCE dependency.
add_subdirectory(src/dsp)

# VST3 wrapper around the DSP engine.
add_subdirectory(src/plugin)

if(FWYC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
