// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_err.h"

namespace splitflap::runtime {
// Call once from app_main after the default ESP event loop exists.
// Starts a diagnostic supervisor only; this is not the motor control task.
esp_err_t start();
}
