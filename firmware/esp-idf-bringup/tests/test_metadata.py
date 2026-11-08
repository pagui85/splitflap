# SPDX-License-Identifier: Apache-2.0
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "tools/generate_metadata.py"


class MetadataTest(unittest.TestCase):
    def test_utc_repeatable_and_invalid_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "metadata.hpp"
            env = dict(os.environ, SOURCE_DATE_EPOCH="1709164800", TZ="Pacific/Honolulu")
            subprocess.run([sys.executable, str(SCRIPT), str(output)], env=env, check=True)
            content = output.read_text()
            self.assertIn('"2024-02-29T00:00:00Z"', content)
            self.assertIn("1709164800LL", content)
            before = output.stat().st_mtime_ns
            subprocess.run([sys.executable, str(SCRIPT), str(output)], env=env, check=True)
            self.assertEqual(before, output.stat().st_mtime_ns)
            for invalid in ("", "-1", "invalid", "4102444800", "1e9"):
                result = subprocess.run([sys.executable, str(SCRIPT), str(output)],
                                        env=dict(env, SOURCE_DATE_EPOCH=invalid), capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(output.read_text(), content)


if __name__ == "__main__":
    unittest.main()
