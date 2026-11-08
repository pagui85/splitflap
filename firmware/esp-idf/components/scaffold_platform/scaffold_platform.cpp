// SPDX-License-Identifier: Apache-2.0
#include "scaffold_platform.hpp"
#include "nvs_flash.h"

namespace splitflap::platform {
esp_err_t initialize_storage()
{
    // Deliberately omit the common erase-and-retry example. Future credentials
    // and configuration must survive NO_FREE_PAGES/NEW_VERSION_FOUND failures.
    return nvs_flash_init();
}
}
