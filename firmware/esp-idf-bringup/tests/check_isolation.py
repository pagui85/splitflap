# SPDX-License-Identifier: Apache-2.0
"""Verify console builds keep Chainlink inactive across every profile."""
import json
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1])
profile = sys.argv[2] if len(sys.argv) > 2 else "s3"
p4 = profile != "s3"
u213 = profile == "p4-u213-early"
ethernet = u213 or profile in ("p4-v3-ethernet", "p4-early-ethernet-uart0")
early = u213 or profile in ("p4-early", "p4-early-ethernet-uart0")
uart = profile in ("s3", "p4-early-ethernet-uart0")
image = f"splitflap_{'p4' if p4 else 's3'}_bringup"

commands = json.loads((build / "compile_commands.json").read_text())
sources = [entry["file"] for entry in commands]
for source in sources:
    assert not any(part in source for part in
                   ("/chainlink_bus/", "/firmware/esp32/", "/firmware/src/", "/esp_wifi/", "/wpa_supplicant/")), source
assert any(source.endswith("/bringup_main.cpp") for source in sources)
assert any(source.endswith("/bringup_ethernet/ethernet.cpp") for source in sources) == ethernet

metadata = (build / "esp-idf/main/generated/build_metadata.hpp").read_text()
stamp = re.search(r'kBuildUtc\[\] = "([^"]+)"', metadata).group(1)
assert stamp.encode() in (build / f"{image}.bin").read_bytes(), "UTC metadata/header mismatch"
config = (build / "sdkconfig").read_text()
target = "esp32p4" if p4 else "esp32s3"
console = "UART_DEFAULT" if uart else "USB_SERIAL_JTAG"
for expected in (f'CONFIG_IDF_TARGET="{target}"', 'CONFIG_ESPTOOLPY_FLASHFREQ="40m"',
                 f'CONFIG_ESP_CONSOLE_{console}=y', 'CONFIG_ESP_CONSOLE_SECONDARY_NONE=y'):
    assert expected in config, expected
if p4:
    assert f"CONFIG_ESP32P4_REV_MIN_FULL={0 if early else 300}\n" in config
    assert f"CONFIG_ESP32P4_REV_MAX_FULL={199 if early else 399}\n" in config
    assert "CONFIG_SPIRAM_IGNORE_NOTFOUND=y" in config
    assert "CONFIG_SPIRAM_USE_CAPS_ALLOC=y" in config
    assert "CONFIG_SPIRAM_MEMTEST=y" not in config
    assert ("CONFIG_BRINGUP_ETHERNET=y" in config) == ethernet
else:
    assert "CONFIG_SPIRAM=y" not in config

assert ("CONFIG_BRINGUP_BOARD_U213=y" in config) == u213
flash_size = "16MB" if u213 else "2MB"
assert f'CONFIG_ESPTOOLPY_FLASHSIZE="{flash_size}"' in config
if u213:
    for setting in ("PHY=3", "PHY_ADDR=1", "PHY_RESET=51", "MDC=31", "MDIO=52",
                    "CLK_IN=50", "CLK_OUT=-1", "TX_EN=49", "TXD0=34", "TXD1=35",
                    "CRS_DV=28", "RXD0=29", "RXD1=30"):
        assert f"CONFIG_BRINGUP_ETH_{setting}\n" in config, setting
    for setting in ("CONFIG_SPIRAM_MODE_HEX=y", "CONFIG_SPIRAM_SPEED=20",
                    "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=360", 'CONFIG_APP_PROJECT_VER="0.5.0"'):
        assert setting + "\n" in config, setting
    assert b"board_profile=m5stack_u213_early" in (build / f"{image}.bin").read_bytes()
    flash = json.loads((build / "flasher_args.json").read_text())
    assert flash["flash_settings"] == {"flash_mode": "dio", "flash_size": "16MB", "flash_freq": "40m"}
    assert flash["flash_files"] == {
        "0x2000": "bootloader/bootloader.bin",
        "0x8000": "partition_table/partition-table.bin",
        "0x10000": f"{image}.bin",
    }

for role in ("MOSI", "MISO", "CLOCK", "LATCH", "OE"):
    assert f"CONFIG_BRINGUP_PIN_{role}=-1" in config
for source in (root / "main").glob("*.cpp"):
    assert not re.search(r"\b(?:gpio_set_\w+|spi_bus_\w+|spi_device_\w+|esp_psram_init|"
                         r"esp_efuse_write_\w+|nvs_flash_init|nvs_set_\w+)\s*\(", source.read_text()), source

# The P4 Ethernet component discovers SDK dependencies at configure time.
# Check the linked image, where unused driver objects must be absent.
cache = (build / "CMakeCache.txt").read_text()
nm = re.search(r"^CMAKE_NM:[^=]+=(.+)$", cache, re.MULTILINE).group(1)
symbols = subprocess.check_output([nm, "--defined-only", str(build / f"{image}.elf")], text=True)
for name in ("spi_bus_initialize", "spi_device_polling_transmit"):
    assert not re.search(rf"\b{name}$", symbols, re.MULTILINE), name
for name in ("esp_eth_driver_install", "httpd_start"):
    assert bool(re.search(rf"\b{name}$", symbols, re.MULTILINE)) == ethernet, name
if u213:
    assert re.search(r"\besp_eth_phy_new_ip101$", symbols, re.MULTILINE)
print(f"PASS: {profile} build isolation, console, memory/revision profile and UTC metadata")
