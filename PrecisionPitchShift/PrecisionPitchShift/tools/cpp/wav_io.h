// PrecisionPitchShift — minimal WAV I/O (PCM16/24/32 + float32/64, 1-8 ch).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pps::wav {

struct AudioFile {
    double sampleRate = 0.0;
    int numChannels = 0;
    // Non-interleaved float64, full scale [-1, 1]-ish (no clipping applied).
    std::vector<std::vector<double>> channels;
};

bool read(const std::string& path, AudioFile& out, std::string& error);
bool write(const std::string& path, const AudioFile& in, int bitDepth /*16|24|32*/,
           std::string& error);

} // namespace pps::wav
