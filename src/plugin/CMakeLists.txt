smtg_add_vst3plugin(FeedbackFromWhenceYouCame
    controller.cpp
    controller.h
    factory.cpp
    plugin_ids.h
    processor.cpp
    processor.h
    version.h
)

target_link_libraries(FeedbackFromWhenceYouCame PRIVATE sdk fwyc::dsp)
target_compile_definitions(FeedbackFromWhenceYouCame PRIVATE
    FWYC_VERSION_STR="${PROJECT_VERSION}"
)
fwyc_configure_target(FeedbackFromWhenceYouCame)
