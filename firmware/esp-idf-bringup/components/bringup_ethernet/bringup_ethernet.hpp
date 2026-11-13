// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdio>
#include "sdkconfig.h"

namespace bringup::ethernet {
#if CONFIG_BRINGUP_ETHERNET
void start();
bool services_ok();
void print_status();
bool uses_pin(int pin);
#else
inline void start() {}
inline void print_status() { std::puts("ethernet=disabled board_profile=unset health_http=off"); }
inline bool uses_pin(int) { return false; }
#endif
}
