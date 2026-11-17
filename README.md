# Feedback From Whence You Came

A zero-latency VST3 feedback suppressor for karaoke microphones, built on the native
Steinberg VST3 SDK (3.8.1, MIT) in C++20.

> Status: Phase 1, a mono pass-through shell with a Bypass parameter, state persistence, and 0 samples of reported latency.

## Setup (once)

Requires Windows 10/11 x64 with [winget](https://learn.microsoft.com/windows/package-manager/winget/).

```powershell
git clone <repo-url>
cd <repo>
.\scripts\setup.ps1
```

This installs whatever is missing: Git, CMake ≥ 3.25, and Visual Studio 2022 Build Tools
with the components in [`.vsconfig`](.vsconfig). If you already have VS 2022, the script
only adds missing components. It then fetches the pinned SDK submodule. You can re-run it safely.

## Build

```powershell
.\scripts\build.ps1                    # Release: configure, build, validate, test
.\scripts\build.ps1 -Config Debug
.\scripts\build.ps1 -Install           # also copy to %LOCALAPPDATA%\Programs\Common\VST3
.\scripts\build.ps1 -Clean
```

Output: `build\windows-x64\VST3\<Config>\FeedbackFromWhenceYouCame.vst3`

Steinberg's validator runs after every plug-in build, so validation failures fail the build.
IDEs that support CMake Presets (Visual Studio, CLion, VS Code) can open the folder directly.

## Layout

| Path | Purpose |
| --- | --- |
| `src/dsp/` | Feedback-suppression engine. Standard C++ only, no SDK dependency. |
| `src/plugin/` | VST3 processor/controller wrapping the engine |
| `tests/` | Headless DSP tests (CTest) |
| `external/vst3sdk/` | Steinberg SDK, a git submodule pinned to `v3.8.1_build_84` |
| `cmake/` | Shared CMake config (SDK options, compiler flags) |
| `.github/workflows/` | Clean-checkout Windows CI build |

## Updating the SDK

```powershell
git -C external/vst3sdk fetch --tags
git -C external/vst3sdk checkout <new-tag>
.\scripts\setup.ps1 -SkipToolchain
git add external/vst3sdk
```
