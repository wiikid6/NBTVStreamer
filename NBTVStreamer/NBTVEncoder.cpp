#include "NBTVEncoder.h"
#include <algorithm>
#include <cstring>

NBTVEncoder::NBTVEncoder(int audioSampleRate)
    : m_sampleRate(audioSampleRate)
{
    m_samplesPerFrame = static_cast<int>(audioSampleRate / NBTV_FPS);
    m_samplesPerLine  = m_samplesPerFrame / NBTV_LINES;
    m_syncSamples     = m_samplesPerLine / 8;             // ~12.5 % of line
    m_activeSamples   = m_samplesPerLine - m_syncSamples;
}

void NBTVEncoder::EncodeFrame(const uint8_t* pixels,
                               int width, int height, int stride,
                               bool isBGR,
                               std::vector<float>& outSamples) const
{
    outSamples.assign(m_samplesPerFrame, 0.0f);   // default = black / silence

    int sampleIdx     = 0;
    const int active  = NBTV_LINES - FRAME_SYNC_LINES;

    // Frame sync region is already zero (silence = black level).
    sampleIdx += FRAME_SYNC_LINES * m_samplesPerLine;

    for (int line = 0; line < active; ++line)
    {
        if (sampleIdx >= m_samplesPerFrame) break;

        // Map this output line to a source row.
        int srcRow = (line * height) / active;
        srcRow = std::clamp(srcRow, 0, height - 1);
        const uint8_t* row = pixels + static_cast<ptrdiff_t>(srcRow) * stride;

        // Line sync silence (already zero).
        sampleIdx += m_syncSamples;

        // Active video pixels.
        for (int s = 0; s < m_activeSamples; ++s, ++sampleIdx)
        {
            if (sampleIdx >= m_samplesPerFrame) break;

            int srcCol = (s * width) / m_activeSamples;
            srcCol = std::clamp(srcCol, 0, width - 1);
            const uint8_t* px = row + srcCol * 3;

            float r, g, b;
            if (isBGR) { b = px[0]; g = px[1]; r = px[2]; }
            else        { r = px[0]; g = px[1]; b = px[2]; }

            // BT.601 luma, normalised to [0, 1].
            float luma = (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f;
            outSamples[sampleIdx] = luma;
        }
    }
}
