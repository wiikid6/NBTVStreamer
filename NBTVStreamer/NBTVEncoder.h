#pragma once
#include <vector>
#include <cstdint>

// Encodes video frames to NBTV (Narrow Band Television) audio signals.
// Standard: 32 lines, 12.5 fps, AM modulation (luminance -> amplitude).
// Line sync  = 12.5% silence at start of each line (black level).
// Frame sync = 4 silent line-periods before the first active line.
class NBTVEncoder
{
public:
    static constexpr int   NBTV_LINES       = 32;
    static constexpr float NBTV_FPS         = 12.5f;
    static constexpr int   FRAME_SYNC_LINES = 5.9;   // leading silent lines per frame

    explicit NBTVEncoder(int audioSampleRate);

    // Encode one BGR24 or RGB24 video frame to float PCM [-1, +1].
    // outSamples is resized to GetSamplesPerFrame().
    void EncodeFrame(const uint8_t* pixels, int width, int height,
                     int stride, bool isBGR,
                     std::vector<float>& outSamples) const;

    int GetSamplesPerFrame()    const { return m_samplesPerFrame;  }
    int GetSamplesPerLine()     const { return m_samplesPerLine;   }
    int GetSyncSamples()        const { return m_syncSamples;      }
    int GetActiveSamples()      const { return m_activeSamples;    }
    int GetAudioSampleRate()    const { return m_sampleRate;       }

private:
    int m_sampleRate;
    int m_samplesPerFrame;
    int m_samplesPerLine;
    int m_syncSamples;
    int m_activeSamples;
};
