#include "wav_reader.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>

namespace fwyc::tools {

namespace {

constexpr uint16_t kFormatPcm = 1;
constexpr uint16_t kFormatFloat = 3;
constexpr uint16_t kFormatExtensible = 0xFFFE;

uint16_t readU16 (const uint8_t* p)
{
    return static_cast<uint16_t> (p[0] | (p[1] << 8));
}

uint32_t readU32 (const uint8_t* p)
{
    return static_cast<uint32_t> (p[0]) | (static_cast<uint32_t> (p[1]) << 8) |
           (static_cast<uint32_t> (p[2]) << 16) | (static_cast<uint32_t> (p[3]) << 24);
}

float decodeSample (const uint8_t* p, int bits, bool isFloat)
{
    if (isFloat)
    {
        float f;
        std::memcpy (&f, p, sizeof f);
        return f;
    }
    switch (bits)
    {
        case 16: return static_cast<int16_t> (readU16 (p)) / 32768.0f;
        case 24:
        {
            int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
            if (v & 0x800000)
                v -= 0x1000000;
            return static_cast<float> (v) / 8388608.0f;
        }
        case 32: return static_cast<float> (static_cast<int32_t> (readU32 (p)) / 2147483648.0);
    }
    return 0.0f;
}

} // namespace

bool readWav (const std::string& path, WavFile& out, std::string& error, int channel)
{
    std::ifstream file (path, std::ios::binary);
    if (!file)
    {
        error = "cannot open " + path;
        return false;
    }
    const std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (file)), std::istreambuf_iterator<char> ());

    if (bytes.size () < 12 || std::memcmp (bytes.data (), "RIFF", 4) != 0 ||
        std::memcmp (bytes.data () + 8, "WAVE", 4) != 0)
    {
        error = "not a RIFF/WAVE file";
        return false;
    }

    bool haveFormat = false;
    uint16_t format = 0;
    size_t pos = 12;
    while (pos + 8 <= bytes.size ())
    {
        const uint8_t* chunk = bytes.data () + pos;
        const uint32_t size = readU32 (chunk + 4);
        const size_t body = pos + 8;
        const size_t available = std::min<size_t> (size, bytes.size () - body);

        if (std::memcmp (chunk, "fmt ", 4) == 0 && available >= 16)
        {
            format = readU16 (chunk + 8);
            out.channels = readU16 (chunk + 10);
            out.sampleRate = readU32 (chunk + 12);
            out.bitsPerSample = readU16 (chunk + 22);
            if (format == kFormatExtensible && available >= 26)
                format = readU16 (chunk + 32); // first two bytes of the sub-format GUID
            haveFormat = true;
        }
        else if (std::memcmp (chunk, "data", 4) == 0)
        {
            if (!haveFormat)
            {
                error = "data chunk before fmt chunk";
                return false;
            }
            out.isFloat = format == kFormatFloat;
            if ((format != kFormatPcm && format != kFormatFloat) ||
                (out.isFloat && out.bitsPerSample != 32) ||
                (!out.isFloat && out.bitsPerSample != 16 && out.bitsPerSample != 24 && out.bitsPerSample != 32))
            {
                error = "unsupported format " + std::to_string (format) + " / " +
                        std::to_string (out.bitsPerSample) + "-bit";
                return false;
            }
            if (out.channels < 1 || channel >= out.channels)
            {
                error = "channel " + std::to_string (channel) + " not present (file has " +
                        std::to_string (out.channels) + ")";
                return false;
            }

            const size_t bytesPerSample = static_cast<size_t> (out.bitsPerSample / 8);
            const size_t frameBytes = bytesPerSample * static_cast<size_t> (out.channels);
            const size_t frames = available / frameBytes;
            out.samples.resize (frames);
            for (size_t f = 0; f < frames; ++f)
            {
                const uint8_t* frame = chunk + 8 + f * frameBytes;
                if (channel >= 0)
                {
                    out.samples[f] = decodeSample (frame + static_cast<size_t> (channel) * bytesPerSample,
                                                   out.bitsPerSample, out.isFloat);
                }
                else
                {
                    float sum = 0.0f;
                    for (int c = 0; c < out.channels; ++c)
                        sum += decodeSample (frame + static_cast<size_t> (c) * bytesPerSample, out.bitsPerSample,
                                             out.isFloat);
                    out.samples[f] = sum / static_cast<float> (out.channels);
                }
            }
            return true;
        }

        pos = body + size + (size & 1); // chunks are word-aligned
    }

    error = haveFormat ? "no data chunk" : "no fmt chunk";
    return false;
}

} // namespace fwyc::tools
