// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>

namespace bringup::display {
int console(int argc, char **argv);
int health_fields(char *output, std::size_t capacity);
}
