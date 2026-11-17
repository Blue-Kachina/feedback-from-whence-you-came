smtg_add_vst3plugin(FeedbackFromWhenceYouCame
    controller.cpp
    controller.h
    factory.cpp
    meter_frame.h
    params.h
    plugin_ids.h
    processor.cpp
    processor.h
    version.h
    ui/editor.cpp
    ui/editor.h
    ui/theme.h
    ui/views.cpp
    ui/views.h
)

target_link_libraries(FeedbackFromWhenceYouCame PRIVATE sdk vstgui_support fwyc::dsp)
target_compile_definitions(FeedbackFromWhenceYouCame PRIVATE
    FWYC_VERSION_STR="${PROJECT_VERSION}"
)
fwyc_configure_target(FeedbackFromWhenceYouCame)
