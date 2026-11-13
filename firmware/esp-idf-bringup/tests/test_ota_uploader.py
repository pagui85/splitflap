# SPDX-License-Identifier: Apache-2.0
import hashlib
import http.client
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import u213_ota as ota


class UploaderTests(unittest.TestCase):
    def test_pinning_closes_mismatched_peer(self):
        peer = b"test peer certificate"
        pin = hashlib.sha256(peer).hexdigest()
        for candidate in (pin, "0" * 64):
            connection = ota.PinnedConnection("127.0.0.1", candidate)
            with patch.object(http.client.HTTPSConnection, "connect"), patch.object(connection, "close") as close:
                class Socket:
                    def getpeercert(self, *, binary_form):
                        assert binary_form
                        return peer

                connection.sock = Socket()
                if candidate == pin:
                    connection.connect()
                    close.assert_not_called()
                else:
                    with self.assertRaisesRegex(ValueError, "does not match"):
                        connection.connect()
                    close.assert_called_once()

    def test_generic_image_has_no_ota_contract(self):
        with self.assertRaisesRegex(ValueError, "early-P4 U213 OTA"):
            ota.image_build(bytes(4096))

    def test_pending_app_is_confirmed_only_after_matching_pinned_health(self):
        before = {"running_partition": "factory", "build_id": "old"}
        staged = {"partition": "ota_0"}
        pending = {"running_partition": "ota_0", "build_id": "new", "ota_trial_ready": True,
                   "ota_image_state": "pending_verify", "last_update_result": "staged"}
        confirmed = {**pending, "ota_image_state": "valid", "last_update_result": "confirmed"}
        with patch.object(ota, "health", side_effect=[{**pending, "build_id": "other"}, pending, confirmed]), \
                patch.object(ota, "confirm_trial") as confirm, patch.object(ota.time, "sleep"):
            ota.verify_reboot("host", "pin", "token", before, staged, "new", False, 5)
            confirm.assert_called_once_with("host", "pin", "token", "new")

    def test_expected_rollback_does_not_confirm(self):
        before = {"running_partition": "factory", "build_id": "old"}
        staged = {"partition": "ota_0"}
        restored = {**before, "last_update_result": "rolled_back"}
        with patch.object(ota, "health", return_value=restored), \
                patch.object(ota, "confirm_trial") as confirm, patch.object(ota.time, "sleep"):
            ota.verify_reboot("host", "pin", "token", before, staged, "new", True, 5)
            confirm.assert_not_called()


if __name__ == "__main__":
    unittest.main()
