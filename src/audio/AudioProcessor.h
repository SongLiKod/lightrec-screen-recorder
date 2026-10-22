#pragma once

#include <vector>

namespace lightrec {

/**
 * Lightweight real-time microphone pre-processing:
 *  - 80 Hz high-pass filter (removes desk rumble / DC)
 *  - noise gate with attack/release envelope
 *  - gain in dB
 *  - soft limiter to avoid clipping
 */
class AudioProcessor
{
public:
    void setup(int sampleRate, int channels);
    void setGainDb(double gainDb);
    void setNoiseSuppression(bool enabled);
    void process(float *interleaved, int frames, int channels);
    void reset();

private:
    struct ChannelState {
        double hpX1 = 0.0;
        double hpX2 = 0.0;
        double hpY1 = 0.0;
        double hpY2 = 0.0;
    };

    int m_sampleRate = 48000;
    int m_channels = 2;
    bool m_noiseSuppression = false;
    double m_gain = 1.0;
    double m_gateEnvelope = 1.0;

    // high-pass biquad coefficients
    double m_b0 = 1.0;
    double m_b1 = 0.0;
    double m_b2 = 0.0;
    double m_a1 = 0.0;
    double m_a2 = 0.0;
    std::vector<ChannelState> m_state;
};

} // namespace lightrec
