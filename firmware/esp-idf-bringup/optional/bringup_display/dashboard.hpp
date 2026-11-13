// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_err.h"
#include "lvgl.h"

namespace bringup::display {
void create_dashboard(lv_display_t *display, bool touch_available);
void update_marker(int raw_x, int raw_y, int x, int y, bool pressed);
const char *touch_state();
esp_err_t brightness(unsigned percent);
}
