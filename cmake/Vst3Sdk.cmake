# Configures and pulls in the pinned Steinberg VST3 SDK (git submodule at external/vst3sdk).
#
# Defaults are plain cache entries so CMakePresets.json or -D flags can override them.

set(vst3sdk_SOURCE_DIR "${PROJECT_SOURCE_DIR}/external/vst3sdk")

if(NOT EXISTS "${vst3sdk_SOURCE_DIR}/pluginterfaces/base/funknown.h")
    message(FATAL_ERROR
        "VST3 SDK not found at ${vst3sdk_SOURCE_DIR}.\n"
        "Run scripts/setup.ps1 (or: git submodule update --init --recursive).")
endif()

# Only build what this project needs: the SDK libraries and the validator.
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES  OFF CACHE BOOL "Build Steinberg plug-in examples")
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "Build Steinberg hosting examples (the validator is always built)")
# The editor is built on VSTGUI. Forced, so build trees configured before the editor existed pick it up.
set(SMTG_ENABLE_VSTGUI_SUPPORT        ON  CACHE BOOL "Enable VSTGUI (the editor needs it)" FORCE)

# Validate every plug-in build with Steinberg's validator (post-build step; fails the build on error).
set(SMTG_RUN_VST_VALIDATOR ON CACHE BOOL "Run the VST3 validator after building")

# Symlinking into the system VST3 folder needs admin rights on Windows; scripts/build.ps1 -Install copies instead.
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "Symlink built plug-ins into the system VST3 folder")

add_subdirectory("${vst3sdk_SOURCE_DIR}" "${PROJECT_BINARY_DIR}/vst3sdk" EXCLUDE_FROM_ALL)
smtg_enable_vst3_sdk()
