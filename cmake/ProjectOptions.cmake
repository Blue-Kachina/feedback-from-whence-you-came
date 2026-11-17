# Shared compiler settings for this project's own targets (not applied to the SDK).

option(FWYC_BUILD_TESTS "Build the DSP unit tests" ON)
option(FWYC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# fwyc_configure_target(<target>)
#   C++20, strict conformance and warnings.
function(fwyc_configure_target target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)

    if(MSVC)
        # SDK headers are reached through include_directories(); treat them as external so
        # their warnings don't leak into ours.
        target_compile_options(${target} PRIVATE
            /W4 /permissive- /utf-8 /Zc:__cplusplus
            /external:W0 /external:I${vst3sdk_SOURCE_DIR}
        )
        if(FWYC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(FWYC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
