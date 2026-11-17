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

# Hosts the built plug-in, plays a WAV through it and screenshots the editor (Windows only).
if(WIN32)
    # sdk_hosting leaves the module loader and PlugProvider to each host (as the SDK's validator does).
    set(hosting "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting")
    add_executable(fwyc_editor_preview
        editor_preview.cpp
        "${hosting}/module.cpp"
        "${hosting}/module_win32.cpp"
        "${hosting}/plugprovider.cpp"
    )
    target_link_libraries(fwyc_editor_preview PRIVATE sdk_hosting fwyc::wav gdiplus)
    fwyc_configure_target(fwyc_editor_preview)
    add_dependencies(fwyc_editor_preview FeedbackFromWhenceYouCame)
endif()
