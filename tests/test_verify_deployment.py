"""A green deploy must not release while the public bytes are stale or mixed."""
import sys
from pathlib import Path
import unittest
from unittest.mock import patch

import test_publish_release as fixtures

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import verify_deployment as gate


class DeploymentTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.ReleaseTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.root = self.fixture.directory
        for name in ["site-build.json", "installer/index.html", "installer/metadata.js"]:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())

    def test_all_public_bytes_match(self):
        with patch.object(gate.site, "fetch", side_effect=lambda base, path: (self.root / path).read_bytes()) as fetch:
            gate.verify(self.root, "https://example.test/")
        self.assertEqual(fetch.call_count, 9)

    def test_stale_or_mixed_files_block_publication(self):
        for broken in ["build.json", "manifest.json", "changes.json", "firmware/ChainPad-ESP32-C6.bin", "site-build.json", "installer/metadata.js"]:
            with self.subTest(path=broken), patch.object(gate.site, "fetch", side_effect=lambda base, path: b"stale" if path == broken else (self.root / path).read_bytes()):
                with self.assertRaisesRegex(ValueError, broken):
                    gate.verify(self.root, "https://example.test/")

    def test_transient_failure_recovers_but_permanent_failure_raises(self):
        with patch.object(gate, "verify", side_effect=[OSError("offline"), None]) as verify, patch.object(gate.time, "sleep"):
            gate.wait_for_deployment(self.root, "https://example.test/", attempts=2)
            self.assertEqual(verify.call_count, 2)
        with patch.object(gate, "verify", side_effect=ValueError("stale")), patch.object(gate.time, "sleep"):
            with self.assertRaisesRegex(ValueError, "stale"):
                gate.wait_for_deployment(self.root, "https://example.test/", attempts=2)
