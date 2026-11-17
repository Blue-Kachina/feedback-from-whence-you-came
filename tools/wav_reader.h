#pragma once

#include <string>
#include <vector>

namespace fwyc::tools {

struct WavFile
{
    double sampleRate = 0.0;
    int channels = 0;
    int bitsPerSample = 0;
    bool isFloat = false;
    std::vector<float> samples; // mono: all channels averaged, or the one selected
};

// Reads PCM 16/24/32-bit integer and 32-bit float WAV files, including WAVE_FORMAT_EXTENSIBLE.
// channel < 0 averages all channels; otherwise that channel (0-based) is used.
// Returns false and sets error on failure.
bool readWav (const std::string& path, WavFile& out, std::string& error, int channel = -1);

} // namespace fwyc::tools
