// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_err.h"

namespace splitflap::platform {
// Never erase/format on initialization failure. No legacy calibration is loaded.
esp_err_t initialize_storage();
}
