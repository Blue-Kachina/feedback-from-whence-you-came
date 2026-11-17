add_library(fwyc_dsp STATIC
    emergency_gain.cpp
    emergency_gain.h
    feedback_detector.cpp
    feedback_detector.h
    feedback_suppressor.cpp
    feedback_suppressor.h
    fft.cpp
    fft.h
    filter_bank.cpp
    filter_bank.h
    notch_filter.cpp
    notch_filter.h
    spectrum_analyser.cpp
    spectrum_analyser.h
)
add_library(fwyc::dsp ALIAS fwyc_dsp)

target_include_directories(fwyc_dsp PUBLIC "${PROJECT_SOURCE_DIR}/src")
fwyc_configure_target(fwyc_dsp)
