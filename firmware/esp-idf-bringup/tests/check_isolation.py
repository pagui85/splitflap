# SPDX-License-Identifier: Apache-2.0
"""Check the built diagnostic excludes the legacy/Chainlink/network code paths."""
import json
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1])
commands = json.loads((build / "compile_commands.json").read_text())
for entry in commands:
    source = entry["file"]
    assert not any(part in source for part in
                   ("/chainlink_bus/", "/firmware/esp32/", "/firmware/src/", "/esp_wifi/", "/wpa_supplicant/")), source
    assert not source.endswith("/spi_master.c"), source
assert any(entry["file"].endswith("/bringup_main.cpp") for entry in commands)
metadata = (build / "esp-idf/main/generated/build_metadata.hpp").read_text()
stamp = re.search(r'kBuildUtc\[\] = "([^"]+)"', metadata).group(1)
assert stamp.encode() in (build / "splitflap_s3_bringup.bin").read_bytes(), "UTC metadata/header mismatch"
config = (build / "sdkconfig").read_text()
for expected in ('CONFIG_IDF_TARGET="esp32s3"', 'CONFIG_ESPTOOLPY_FLASHFREQ="40m"',
                 'CONFIG_ESP_CONSOLE_UART_DEFAULT=y', 'CONFIG_ESP_CONSOLE_SECONDARY_NONE=y'):
    assert expected in config, expected
assert "CONFIG_SPIRAM=y" not in config
for role in ("MOSI", "MISO", "CLOCK", "LATCH", "OE"):
    assert f"CONFIG_BRINGUP_PIN_{role}=-1" in config
for source in (root / "main").glob("*.cpp"):
    assert not re.search(r"\b(?:gpio_set_\w+|spi_bus_\w+|spi_device_\w+|esp_psram_init|"
                         r"esp_efuse_write_\w+|nvs_flash_init|nvs_set_\w+)\s*\(", source.read_text()), source
print("PASS: diagnostic build excludes Chainlink/SPI2/network/legacy activation paths; pins unassigned; PSRAM off")
