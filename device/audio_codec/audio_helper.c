#include "audio_helper.h"

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

/**
 * @brief Convert mono audio samples to stereo audio samples.
 *
 * Each mono sample is duplicated into both the left and right channels
 * of the stereo output buffer.
 *
 * @param[in]  mono_buffer   Pointer to the input mono audio sample buffer.
 * @param[out] stereo_buffer Pointer to the output stereo audio sample buffer.
 * @param[in]  mono_samples  Number of samples contained in the mono buffer.
 *
 * @return
 *     This function does not return a value.
 */
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
