// PrecisionPitchShift — minimal WAV I/O implementation (little-endian RIFF).
#include "wav_io.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace pps::wav {
namespace {

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}
void wr16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
}
void wr32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>((x >> (8 * i)) & 0xFF));
}

} // namespace

bool read(const std::string& path, AudioFile& out, std::string& error) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        error = "cannot open input";
        return false;
    }
    std::vector<uint8_t> buf;
    {
        std::fseek(f, 0, SEEK_END);
        const long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz < 44) { std::fclose(f); error = "file too small"; return false; }
        buf.resize(static_cast<size_t>(sz));
        if (std::fread(buf.data(), 1, buf.size(), f) != buf.size()) {
            std::fclose(f); error = "read error"; return false;
        }
        std::fclose(f);
    }
    if (std::memcmp(buf.data(), "RIFF", 4) != 0 || std::memcmp(buf.data() + 8, "WAVE", 4) != 0) {
        error = "not a RIFF/WAVE file"; return false;
    }
    uint16_t audioFormat = 0, numCh = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0;
    std::size_t dataPos = 0, dataLen = 0;
    std::size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        char id[5] = {0, 0, 0, 0, 0};
        std::memcpy(id, buf.data() + pos, 4);
        const uint32_t len = rd32(buf.data() + pos + 4);
        if (std::strcmp(id, "fmt ") == 0) {
            if (len < 16) { error = "bad fmt chunk"; return false; }
            audioFormat = rd16(buf.data() + pos + 8);
            numCh = rd16(buf.data() + pos + 10);
            sampleRate = rd32(buf.data() + pos + 12);
            bitsPerSample = rd16(buf.data() + pos + 22);
        } else if (std::strcmp(id, "data") == 0) {
            dataPos = pos + 8;
            dataLen = len;
        }
        pos += 8 + len + (len & 1);
    }
    if (dataPos == 0 || numCh == 0 || numCh > 8 || sampleRate == 0) {
        error = "missing/invalid chunks"; return false;
    }
    const int bytesPerSample = (bitsPerSample == 8) ? 1 : bitsPerSample / 8;
    if (audioFormat != 1 && audioFormat != 3) { error = "only PCM/float supported"; return false; }
    if (audioFormat == 3 && bitsPerSample != 32 && bitsPerSample != 64) {
        error = "float must be 32/64-bit"; return false;
    }
    if (audioFormat == 1 &&
        !(bitsPerSample == 8 || bitsPerSample == 16 || bitsPerSample == 24 || bitsPerSample == 32)) {
        error = "PCM must be 8/16/24/32-bit"; return false;
    }
    const std::size_t frames = dataLen / (static_cast<std::size_t>(numCh) * bytesPerSample);
    out.sampleRate = static_cast<double>(sampleRate);
    out.numChannels = numCh;
    out.channels.assign(numCh, std::vector<double>(frames, 0.0));
    const uint8_t* d = buf.data() + dataPos;
    for (std::size_t n = 0; n < frames; ++n) {
        for (int c = 0; c < numCh; ++c) {
            const uint8_t* p = d + (n * numCh + c) * bytesPerSample;
            double v = 0.0;
            if (audioFormat == 3) {
                if (bitsPerSample == 32) {
                    float x; std::memcpy(&x, p, 4); v = x;
                } else { double x; std::memcpy(&x, p, 8); v = x; }
            } else if (bitsPerSample == 8) {
                v = (static_cast<int>(p[0]) - 128) / 128.0;
            } else if (bitsPerSample == 16) {
                int16_t x = static_cast<int16_t>(rd16(p)); v = x / 32768.0;
            } else if (bitsPerSample == 24) {
                int32_t x = static_cast<int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
                if (x & 0x800000) x |= ~0xFFFFFF;
                v = x / 8388608.0;
            } else {
                int32_t x; std::memcpy(&x, p, 4); v = x / 2147483648.0;
            }
            out.channels[c][n] = v;
        }
    }
    return true;
}

bool write(const std::string& path, const AudioFile& in, int bitDepth, std::string& error) {
    if (in.numChannels <= 0 || in.channels.empty()) { error = "no channels"; return false; }
    if (bitDepth != 16 && bitDepth != 24 && bitDepth != 32) { error = "bitDepth must be 16|24|32"; return false; }
    const std::size_t frames = in.channels[0].size();
    const int bytesPerSample = bitDepth / 8;
    std::vector<uint8_t> data;
    data.reserve(frames * in.numChannels * bytesPerSample);
    for (std::size_t n = 0; n < frames; ++n) {
        for (int c = 0; c < in.numChannels; ++c) {
            double v = std::max(-1.0, std::min(1.0, in.channels[c][n]));
            if (bitDepth == 16) {
                int x = static_cast<int>(std::lround(v * 32767.0));
                x = std::max(-32768, std::min(32767, x));
                wr16(data, static_cast<uint16_t>(x & 0xFFFF));
            } else if (bitDepth == 24) {
                int x = static_cast<int>(std::lround(v * 8388607.0));
                x = std::max(-8388608, std::min(8388607, x));
                data.push_back(static_cast<uint8_t>(x & 0xFF));
                data.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
                data.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
            } else {
                float x = static_cast<float>(v);
                uint32_t u; std::memcpy(&u, &x, 4);
                wr32(data, u);
            }
        }
    }
    std::vector<uint8_t> hdr;
    hdr.insert(hdr.end(), {'R', 'I', 'F', 'F'});
    wr32(hdr, static_cast<uint32_t>(36 + data.size()));
    hdr.insert(hdr.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    wr32(hdr, 16);
    wr16(hdr, (bitDepth == 32) ? 3 : 1);
    wr16(hdr, static_cast<uint16_t>(in.numChannels));
    wr32(hdr, static_cast<uint32_t>(std::lround(in.sampleRate)));
    const uint32_t byteRate = static_cast<uint32_t>(std::lround(in.sampleRate)) *
                              static_cast<uint32_t>(in.numChannels * bytesPerSample);
    wr32(hdr, byteRate);
    wr16(hdr, static_cast<uint16_t>(in.numChannels * bytesPerSample));
    wr16(hdr, static_cast<uint16_t>(bitDepth));
    hdr.insert(hdr.end(), {'d', 'a', 't', 'a'});
    wr32(hdr, static_cast<uint32_t>(data.size()));

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { error = "cannot open output"; return false; }
    const bool ok = std::fwrite(hdr.data(), 1, hdr.size(), f) == hdr.size() &&
                    std::fwrite(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    if (!ok) { error = "write error"; return false; }
    return true;
}

} // namespace pps::wav
