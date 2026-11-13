// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>

#include <cstdio>
#include "sdkconfig.h"

namespace bringup::ethernet {
#if CONFIG_BRINGUP_ETHERNET
void start();
bool services_ok();
#if CONFIG_BRINGUP_WAVESHARE_7E
void display_status(char *summary, std::size_t summary_size, char *ip, std::size_t ip_size);
#endif
void print_status();
bool uses_pin(int pin);
#else
inline void start() {}
inline void print_status() { std::puts("ethernet=disabled board_profile=unset health_http=off"); }
inline bool uses_pin(int) { return false; }
#endif
}
