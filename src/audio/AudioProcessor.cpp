#include "audio/AudioProcessor.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace lightrec {

void AudioProcessor::setup(int sampleRate, int channels)
{
    m_sampleRate = std::max(8000, sampleRate);
    m_channels = std::max(1, channels);
    m_state.assign(static_cast<size_t>(m_channels), ChannelState());

    // RBJ high-pass biquad, 80 Hz, Q = 0.707
    const double freq = 80.0;
    const double q = 0.70710678;
    const double omega = 2.0 * M_PI * freq / static_cast<double>(m_sampleRate);
    const double alpha = std::sin(omega) / (2.0 * q);
    const double cosOmega = std::cos(omega);
    const double a0 = 1.0 + alpha;

    m_b0 = (1.0 + cosOmega) / 2.0 / a0;
    m_b1 = -(1.0 + cosOmega) / a0;
    m_b2 = (1.0 + cosOmega) / 2.0 / a0;
    m_a1 = -2.0 * cosOmega / a0;
    m_a2 = (1.0 - alpha) / a0;
}

void AudioProcessor::setGainDb(double gainDb)
{
    m_gain = std::pow(10.0, gainDb / 20.0);
}

void AudioProcessor::setNoiseSuppression(bool enabled)
{
    m_noiseSuppression = enabled;
}

void AudioProcessor::reset()
{
    for (ChannelState &state : m_state)
        state = ChannelState();
    m_gateEnvelope = 1.0;
}

void AudioProcessor::process(float *interleaved, int frames, int channels)
{
    if (!interleaved || frames <= 0 || channels <= 0)
        return;
    if (static_cast<int>(m_state.size()) != channels)
        setup(m_sampleRate, channels);

    double rms = 0.0;
    for (int i = 0; i < frames * channels; ++i)
        rms += static_cast<double>(interleaved[i]) * interleaved[i];
    rms = std::sqrt(rms / std::max(1, frames * channels));

    // -50 dBFS gate, 5 ms attack, 250 ms release.
    const double threshold = 0.00316;
    const double target = (m_noiseSuppression && rms < threshold) ? 0.12 : 1.0;
    const double coeff = target < m_gateEnvelope
                             ? 1.0 - std::exp(-1.0 / (0.005 * m_sampleRate))
                             : 1.0 - std::exp(-1.0 / (0.25 * m_sampleRate));

    for (int frame = 0; frame < frames; ++frame) {
        m_gateEnvelope += (target - m_gateEnvelope) * coeff;
        for (int ch = 0; ch < channels; ++ch) {
            ChannelState &state = m_state[static_cast<size_t>(ch)];
            const double x = static_cast<double>(interleaved[frame * channels + ch]);
            const double y = m_b0 * x + m_b1 * state.hpX1 + m_b2 * state.hpX2
                             - m_a1 * state.hpY1 - m_a2 * state.hpY2;
            state.hpX2 = state.hpX1;
            state.hpX1 = x;
            state.hpY2 = state.hpY1;
            state.hpY1 = y;

            double sample = y * m_gain * m_gateEnvelope;
            // Soft limiter
            sample = std::tanh(sample);
            interleaved[frame * channels + ch] = static_cast<float>(sample);
        }
    }
}

} // namespace lightrec
