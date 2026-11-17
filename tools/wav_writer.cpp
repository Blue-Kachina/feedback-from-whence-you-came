#include "wav_writer.h"

#include <cstdint>
#include <fstream>

namespace fwyc::tools {

namespace {

void putU16 (std::ofstream& f, uint16_t v)
{
    const char b[2] = {static_cast<char> (v & 0xFF), static_cast<char> (v >> 8)};
    f.write (b, 2);
}

void putU32 (std::ofstream& f, uint32_t v)
{
    const char b[4] = {static_cast<char> (v & 0xFF), static_cast<char> ((v >> 8) & 0xFF),
                       static_cast<char> ((v >> 16) & 0xFF), static_cast<char> (v >> 24)};
    f.write (b, 4);
}

} // namespace

bool writeWavFloat (const std::string& path, const std::vector<float>& samples, double sampleRate,
                    std::string& error)
{
    std::ofstream f (path, std::ios::binary);
    if (!f)
    {
        error = "cannot write " + path;
        return false;
    }

    const auto rate = static_cast<uint32_t> (sampleRate + 0.5);
    const auto dataBytes = static_cast<uint32_t> (samples.size () * sizeof (float));

    f.write ("RIFF", 4);
    putU32 (f, 36 + dataBytes);
    f.write ("WAVE", 4);

    f.write ("fmt ", 4);
    putU32 (f, 16);
    putU16 (f, 3); // IEEE float
    putU16 (f, 1); // mono
    putU32 (f, rate);
    putU32 (f, rate * 4);
    putU16 (f, 4);
    putU16 (f, 32);

    f.write ("data", 4);
    putU32 (f, dataBytes);
    f.write (reinterpret_cast<const char*> (samples.data ()), static_cast<std::streamsize> (dataBytes));

    if (!f)
    {
        error = "write failed: " + path;
        return false;
    }
    return true;
}

} // namespace fwyc::tools
