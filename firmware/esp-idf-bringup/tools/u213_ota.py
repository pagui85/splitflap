#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Upload a Splitflap U213 app over HTTPS using a USB-provisioned certificate pin."""
import argparse
from dataclasses import dataclass
import getpass
import hashlib
import hmac
import http.client
import ipaddress
import json
from pathlib import Path
import re
import ssl
import sys
import time

DESCRIPTOR = b"splitflap-u213-ota:v1\0"
APP_DESC_OFFSET = 32
ELF_HASH_OFFSET = APP_DESC_OFFSET + 144
PROFILE_OFFSET = APP_DESC_OFFSET + 256
MAX_IMAGE = 4 * 1024 * 1024
HEX_SHA256 = re.compile(r"[0-9a-f]{64}")


@dataclass
class UploadRejected(Exception):
    status: int
    reason: str

    def __str__(self):
        return f"device rejected upload (HTTP {self.status}): {self.reason}"


class PinnedConnection(http.client.HTTPSConnection):
    def __init__(self, host, pin):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        # Device identity is provisioned over USB. Check its exact certificate
        # before any HTTP data (including Authorization) is sent.
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        super().__init__(host, port=443, timeout=12, context=context)
        self.pin = pin

    def connect(self):
        super().connect()
        der = self.sock.getpeercert(binary_form=True)
        if not der or not hmac.compare_digest(hashlib.sha256(der).hexdigest(), self.pin):
            self.close()
            raise ValueError("TLS certificate does not match the USB-provisioned SHA-256 pin")


def image_build(image):
    if not PROFILE_OFFSET + len(DESCRIPTOR) <= len(image) <= MAX_IMAGE:
        raise ValueError("image does not fit the 4 MiB OTA slot")
    if (image[0] != 0xE9 or image[3] >> 4 != 4 or image[23] != 1
            or int.from_bytes(image[12:14], "little") != 18
            or not 0 <= int.from_bytes(image[15:17], "little") <= int.from_bytes(image[17:19], "little") < 200
            or image[APP_DESC_OFFSET:APP_DESC_OFFSET + 4] != b"\x32\x54\xcd\xab"
            or image[PROFILE_OFFSET:PROFILE_OFFSET + len(DESCRIPTOR)] != DESCRIPTOR):
        raise ValueError("use an early-P4 U213 OTA app-only image")
    return image[ELF_HASH_OFFSET:ELF_HASH_OFFSET + 32].hex()


def read_json(response):
    raw = response.read(4097)
    if len(raw) > 4096:
        raise ValueError("oversized response from board")
    result = json.loads(raw)
    if not isinstance(result, dict):
        raise ValueError("unexpected response from board")
    return result


def health(host, pin):
    connection = PinnedConnection(host, pin)
    try:
        connection.request("GET", "/health")
        response = connection.getresponse()
        if response.status != 200:
            raise OSError(f"health HTTP {response.status}")
        return read_json(response)
    finally:
        connection.close()


def upload(host, pin, token, image, interrupt_after=None):
    connection = PinnedConnection(host, pin)
    try:
        connection.putrequest("POST", "/ota")
        connection.putheader("Content-Type", "application/octet-stream")
        connection.putheader("Content-Length", str(len(image)))
        connection.putheader("Authorization", f"Bearer {token}")
        connection.putheader("X-Image-SHA256", hashlib.sha256(image).hexdigest())
        connection.endheaders()
        limit = interrupt_after or len(image)
        print(f"Uploading {limit:,} of {len(image):,} bytes to {host} over pinned HTTPS", flush=True)
        for offset in range(0, limit, 16384):
            end = min(offset + 16384, limit)
            connection.send(image[offset:end])
            print(f"\r{end:,}/{len(image):,} bytes", end="", flush=True)
        print(flush=True)
        if interrupt_after:
            return None
        response = connection.getresponse()
        reply = read_json(response)
        if response.status != 202:
            raise UploadRejected(response.status, reply.get("error", "unknown error"))
        if reply.get("status") != "staged" or reply.get("partition") not in ("ota_0", "ota_1"):
            raise ValueError("unexpected staging response")
        return reply
    finally:
        connection.close()


def confirm_trial(host, pin, token, build):
    connection = PinnedConnection(host, pin)
    try:
        connection.request("POST", "/ota/confirm", body=b"", headers={
            "Authorization": f"Bearer {token}", "X-Build-ID": build,
        })
        response = connection.getresponse()
        reply = read_json(response)
        if response.status == 409:
            return
        if response.status != 200 or reply.get("status") != "confirmed":
            raise ValueError(f"confirmation failed (HTTP {response.status}): {reply}")
    finally:
        connection.close()


def verify_reboot(host, pin, token, before, staged, build, expect_rollback, timeout):
    print("Waiting for authenticated /health after reboot", flush=True)
    deadline = time.monotonic() + timeout
    latest = {}
    while time.monotonic() < deadline:
        time.sleep(1)
        try:
            latest = health(host, pin)
        except (OSError, http.client.HTTPException, json.JSONDecodeError):
            continue
        result = latest.get("last_update_result")
        slot = latest.get("running_partition")
        actual = latest.get("build_id")
        if result == "rolled_back" and slot == before["running_partition"] and actual == before["build_id"]:
            if expect_rollback:
                print(f"Rollback verified on {slot} ({latest.get('version')}, {actual})")
                return
            raise ValueError(f"board rolled back to {slot} ({actual})")
        if (not expect_rollback and slot == staged["partition"] and actual == build
                and latest.get("ota_image_state") == "pending_verify" and latest.get("ota_trial_ready")):
            try:
                confirm_trial(host, pin, token, build)
            except (OSError, http.client.HTTPException):
                continue
        if (result == "confirmed" and slot == staged["partition"] and actual == build
                and latest.get("ota_image_state") == "valid"):
            if expect_rollback:
                raise ValueError("test image was confirmed when rollback was expected")
            print(f"Verified {latest.get('version')} on {slot} ({actual}), state valid")
            return
    raise OSError(f"post-reboot verification timed out, last status: {latest}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ip", help="U213 DHCP IPv4 address")
    parser.add_argument("image", type=Path, help="app-only .bin file")
    parser.add_argument("--pin", required=True, help="ota_certificate_sha256 from USB console")
    parser.add_argument("--wait", type=int, default=120, help="post-reboot deadline in seconds")
    tests = parser.add_mutually_exclusive_group()
    tests.add_argument("--expect-rollback", action="store_true", help="verify return to previous app")
    tests.add_argument("--interrupt-after", type=int, help="send part of the image and disconnect")
    tests.add_argument("--test-corrupt", action="store_true", help="damage the image digest and verify device rejection")
    args = parser.parse_args()

    try:
        host = str(ipaddress.IPv4Address(args.ip))
        pin = args.pin.lower()
        if not HEX_SHA256.fullmatch(pin):
            raise ValueError("certificate pin must be exactly 64 hexadecimal characters")
        if args.wait < 1:
            raise ValueError("verification timeout must be positive")
        with args.image.open("rb") as stream:
            image = stream.read(MAX_IMAGE + 1)
        build = image_build(image)
        if args.interrupt_after is not None and not 4096 <= args.interrupt_after < len(image):
            raise ValueError("interrupt point must be at least 4096 bytes and below the image length")
        before = health(host, pin)
        if not before.get("ota_provisioned") or not before.get("build_id") or not before.get("running_partition"):
            raise ValueError("device did not report a provisioned OTA profile")
        token = getpass.getpass("USB-provisioned OTA token: ")
        if not HEX_SHA256.fullmatch(token):
            raise ValueError("OTA token must be exactly 64 lowercase hexadecimal characters")
        if args.test_corrupt:
            image = image[:-1] + bytes([image[-1] ^ 1])
        try:
            staged = upload(host, pin, token, image, args.interrupt_after)
        except UploadRejected as error:
            if not args.test_corrupt or error.status != 422 or error.reason != "invalid_esp_image":
                raise
            after = health(host, pin)
            if (after.get("build_id") != before["build_id"] or
                    after.get("running_partition") != before["running_partition"] or
                    after.get("last_update_result") != "rejected_image"):
                raise ValueError("device did not report rejection with the previous app still running")
            print("Corrupt image rejected; previous app still running")
            return 0
        if args.test_corrupt:
            raise ValueError("device accepted a corrupt test image")
        if staged is None:
            print("Upload interrupted by request. Check /health for the original build and interrupted result.")
            return 0
        if staged.get("build_id") != build:
            raise ValueError("staged build identifier differs from the uploaded app")
        verify_reboot(host, pin, token, before, staged, build, args.expect_rollback, args.wait)
        return 0
    except KeyboardInterrupt:
        print("\nInterrupted. Check /health before retrying.", file=sys.stderr)
        return 130
    except (OSError, ValueError, http.client.HTTPException, EOFError, UploadRejected) as error:
        print(f"OTA failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
