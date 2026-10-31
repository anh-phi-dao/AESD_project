#ifndef AUDIO_I2S_BUS_H
#define AUDIO_I2S_BUS_H

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_err.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/* The INMP441 microphone and the NS4168 speaker amplifier share BCLK and LRCLK on the board, so they must be driven
 * by ONE I2S controller (full-duplex). Two masters on the same BCLK/WS pins would fight for the pins. */
#define AUDIO_I2S_PORT        I2S_NUM_0
#define AUDIO_I2S_BCLK_PIN    39 /* I2S_BCLK  */
#define AUDIO_I2S_WS_PIN      40 /* I2S_LRCLK */
#define AUDIO_I2S_DOUT_PIN    16 /* SD_OUT -> NS4168 DATA */
#define AUDIO_I2S_DIN_PIN     17 /* SD_IN  <- INMP441 SD  */
#define AUDIO_I2S_SAMPLE_RATE 16000
#define AUDIO_I2S_DMA_DESC    8
#define AUDIO_I2S_DMA_FRAMES  256

#ifdef __cplusplus
extern "C"
{
#endif

    /*******************************************************************************************************************
     * Public APIs
     ******************************************************************************************************************/

    /**
     * @brief Create the shared full-duplex I2S bus (idempotent).
     *
     * Both channels use 32-bit data in stereo slots, so BCLK is 64 x fs as the INMP441 expects. TX and RX must
     * share the identical slot format. The TX and RX channels are returned disabled; the microphone driver enables RX and audio_i2s_bus_start() enables TX.
     *
     * @param[out] p_tx Optional, receives the TX channel handle.
     * @param[out] p_rx Optional, receives the RX channel handle.
     */
    esp_err_t audio_i2s_bus_init(i2s_chan_handle_t *p_tx, i2s_chan_handle_t *p_rx);

    /**
     * @brief Start the bus clock (idempotent).
     *
     * In full-duplex master mode the TX channel generates BCLK/LRCLK for both directions, so it must be enabled even
     * when only the microphone is used. While the speaker has no data, the TX channel outputs silence.
     */
    esp_err_t audio_i2s_bus_start(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_I2S_BUS_H */
