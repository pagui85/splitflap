# SPDX-License-Identifier: Apache-2.0
"""Verify console builds keep Chainlink inactive across every profile."""
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1])
profile = sys.argv[2] if len(sys.argv) > 2 else "s3"
p4 = profile != "s3"
display = profile == "p4-u213-waveshare-7e"
ota = display or profile in ("p4-u213-ota", "p4-u213-ota-fail")
u213 = profile == "p4-u213-early" or ota
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
assert any(source.endswith("/bringup_display/display.cpp") for source in sources) == display
assert any("/lvgl__lvgl/" in source for source in sources) == display

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
assert ("CONFIG_BRINGUP_WAVESHARE_7E=y" in config) == display
flash_size = "16MB" if u213 else "2MB"
assert f'CONFIG_ESPTOOLPY_FLASHSIZE="{flash_size}"' in config
if u213:
    for setting in ("PHY=3", "PHY_ADDR=1", "PHY_RESET=51", "MDC=31", "MDIO=52",
                    "CLK_IN=50", "CLK_OUT=-1", "TX_EN=49", "TXD0=34", "TXD1=35",
                    "CRS_DV=28", "RXD0=29", "RXD1=30"):
        assert f"CONFIG_BRINGUP_ETH_{setting}\n" in config, setting
    for setting in ("CONFIG_SPIRAM_MODE_HEX=y", f"CONFIG_SPIRAM_SPEED={80 if display else 20}",
                    "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=360"):
        assert setting + "\n" in config, setting
    versions = {"p4-u213-ota-fail": "0.6.0-rollback-test", "p4-u213-ota": "0.6.0",
                "p4-u213-waveshare-7e": "0.7.0", "p4-u213-early": "0.5.0"}
    version = versions[profile]
    assert f'CONFIG_APP_PROJECT_VER="{version}"\n' in config
    assert b"board_profile=m5stack_u213_early" in (build / f"{image}.bin").read_bytes()
    flash = json.loads((build / "flasher_args.json").read_text())
    assert flash["flash_settings"] == {"flash_mode": "dio", "flash_size": "16MB", "flash_freq": "40m"}
    expected_files = {
        "0x2000": "bootloader/bootloader.bin",
        "0x8000": "partition_table/partition-table.bin",
        "0x10000": f"{image}.bin",
    }
    if ota:
        expected_files["0xe000"] = "ota_data_initial.bin"
        assert (build / "ota_data_initial.bin").read_bytes() == b"\xff" * 8192
        raw_table = (build / "partition_table/partition-table.bin").read_bytes()
        partitions = {}
        for magic, kind, subtype, address, size, label, flags in struct.iter_unpack("<HBBII16sI", raw_table):
            if magic != 0x50AA:
                break
            partitions[label.rstrip(b"\0").decode("ascii")] = (kind, subtype, address, size, flags)
        assert partitions == {
            "nvs": (1, 2, 0x9000, 0x5000, 0),
            "otadata": (1, 0, 0xE000, 0x2000, 0),
            "factory": (0, 0, 0x10000, 0x300000, 0),
            "ota_0": (0, 0x10, 0x310000, 0x400000, 0),
            "ota_1": (0, 0x11, 0x710000, 0x400000, 0),
            "splitcfg": (1, 2, 0xB10000, 0x10000, 0),
            "otakeys": (1, 2, 0xB20000, 0x10000, 0),
        }
        assert "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y" in config
        assert 'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_u213_ota.csv"' in config
        assert "CONFIG_ESP_HTTPS_SERVER_ENABLE=y" in config
        assert ("CONFIG_BRINGUP_OTA_TEST_FAIL=y" in config) == profile.endswith("-fail")
        image_bytes = (build / f"{image}.bin").read_bytes()
        icon = (root / "components/bringup_ethernet/assets/favicon.ico").read_bytes()
        reserved, kind, count = struct.unpack_from("<HHH", icon)
        assert reserved == 0 and kind == 1 and count > 0
        assert len(icon) >= 6 + count * 16
        for index in range(count):
            length, offset = struct.unpack_from("<II", icon, 6 + index * 16 + 8)
            assert length > 0 and 6 + count * 16 <= offset <= len(icon) - length
        assert icon in image_bytes, "complete ICO payload must be embedded"
        assert image_bytes[288:].startswith(b"splitflap-u213-ota:v1\0")
    assert flash["flash_files"] == expected_files

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
for name in ("esp_ota_write", "httpd_ssl_start", "esp_ota_mark_app_valid_cancel_rollback"):
    assert bool(re.search(rf"\b{name}$", symbols, re.MULTILINE)) == ota, name
for name in ("esp_lcd_new_dsi_bus", "lv_display_create", "esp_lcd_touch_new_i2c_gt911"):
    assert bool(re.search(rf"\b{name}$", symbols, re.MULTILINE)) == display, name
assert not re.search(r"\besp_lcd_new_panel_dsi$", symbols, re.MULTILINE), "vendor GPIO7/8 constructor"
if u213:
    assert re.search(r"\besp_eth_phy_new_ip101$", symbols, re.MULTILINE)
print(f"PASS: {profile} build isolation, console, memory/revision profile and UTC metadata")
