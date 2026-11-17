# Headless DSP tests: no plug-in host needed. Each test is a small executable returning non-zero on failure.

function(fwyc_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE fwyc::dsp)
    fwyc_configure_target(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

fwyc_add_test(passthrough)
