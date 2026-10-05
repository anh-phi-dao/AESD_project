#include "audio_helper.h"

void audio_mono_to_stereo(const int16_t *mono_buffer, int16_t *stereo_buffer, size_t mono_samples)
{
    if (!mono_buffer || !stereo_buffer || mono_samples == 0)
    {
        return;
    }

    for (size_t i = 0; i < mono_samples; i++)
    {
        stereo_buffer[i * 2]     = mono_buffer[i]; // Left channel
        stereo_buffer[i * 2 + 1] = mono_buffer[i]; // Right channel
    }
}
