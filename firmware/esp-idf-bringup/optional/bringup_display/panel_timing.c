// SPDX-License-Identifier: Apache-2.0
#include "esp_lcd_dsi.h"

// The pinned Waveshare timing macro uses C nested designated initializers.
esp_lcd_dpi_panel_config_t waveshare_7e_dpi_config(void)
{
    esp_lcd_dpi_panel_config_t config = DSI_PANEL_DPI_7_INCH_E_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    config.flags.use_dma2d = false;
    return config;
}
