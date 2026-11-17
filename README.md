# Feedback From Whence You Came

A zero-latency VST3 feedback suppressor for karaoke microphones, built on the native
Steinberg VST3 SDK (3.8.1, MIT) in C++20.

> Status: Phase 3. Detected feedback is cut by up to 8 adaptive notch filters; without
> feedback, audio passes through bit-exact. Bypass (crossfaded), state persistence, 0 samples of reported latency.

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

## Analysing recordings

`fwyc_analyze` runs the full suppressor over a WAV file offline, block by block as the
plug-in would, and reports each detection (when, frequency, level, and the features behind it),
each notch (engaged, deepened, released), and the level reduction while feedback was present.

```powershell
.\build\windows-x64\bin\Release\fwyc_analyze.exe recording.wav
.\build\windows-x64\bin\Release\fwyc_analyze.exe recording.wav --out processed.wav  # listen to the result
.\build\windows-x64\bin\Release\fwyc_analyze.exe recording.wav --timeline 100       # levels, notches, candidates
.\build\windows-x64\bin\Release\fwyc_analyze.exe recording.wav --csv frames.csv     # every frame, for plotting
```

A recording has no live acoustic loop, so notches can't make the feedback in it fade and always
deepen to their maximum. In a room they stop deepening once the feedback starts to decay.

Reads 16/24/32-bit PCM and 32-bit float WAV; multichannel files are averaged unless `--channel N` is given.

## Layout

| Path | Purpose |
| --- | --- |
| `src/dsp/` | Feedback-suppression engine. Standard C++ only, no SDK dependency. |
| `src/plugin/` | VST3 processor/controller wrapping the engine |
| `tests/` | Headless DSP tests (CTest) |
| `tools/` | Offline developer tools (`fwyc_analyze`, WAV reader) |
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
