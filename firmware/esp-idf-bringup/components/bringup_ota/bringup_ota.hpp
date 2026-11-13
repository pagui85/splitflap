// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include "esp_http_server.h"
#include "sdkconfig.h"

namespace bringup::ota {
void initialize();
void verify_boot(bool services_ok);
esp_err_t start_https(esp_err_t (*health_handler)(httpd_req_t *));
#if CONFIG_BRINGUP_WAVESHARE_7E
void display_state(char *output, std::size_t capacity);
#endif
int console_command(int argc, char **argv);
// Appends JSON fields to a buffer ending immediately before the final brace.
int health_fields(char *output, std::size_t capacity);
}
