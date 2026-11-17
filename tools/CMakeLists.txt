# Offline developer tools (not shipped with the plug-in).

add_library(fwyc_wav STATIC
    wav_reader.cpp
    wav_reader.h
    wav_writer.cpp
    wav_writer.h
)
add_library(fwyc::wav ALIAS fwyc_wav)
target_include_directories(fwyc_wav PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
fwyc_configure_target(fwyc_wav)

add_executable(fwyc_analyze analyze_wav.cpp)
target_link_libraries(fwyc_analyze PRIVATE fwyc::dsp fwyc::wav)
fwyc_configure_target(fwyc_analyze)
