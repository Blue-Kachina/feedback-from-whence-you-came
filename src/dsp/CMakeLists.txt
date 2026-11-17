add_library(fwyc_dsp STATIC
    feedback_detector.cpp
    feedback_detector.h
    feedback_suppressor.cpp
    feedback_suppressor.h
    fft.cpp
    fft.h
)
add_library(fwyc::dsp ALIAS fwyc_dsp)

target_include_directories(fwyc_dsp PUBLIC "${PROJECT_SOURCE_DIR}/src")
fwyc_configure_target(fwyc_dsp)
