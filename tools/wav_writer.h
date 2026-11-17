#pragma once

#include <string>
#include <vector>

namespace fwyc::tools {

// Writes mono 32-bit float WAV (no clipping or dither concerns for processed output).
// Returns false and sets error on failure.
bool writeWavFloat (const std::string& path, const std::vector<float>& samples, double sampleRate,
                    std::string& error);

} // namespace fwyc::tools
