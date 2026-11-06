"""Recovery must use published bytes and bind them to the requested tag source."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import test_publish_release as fixtures

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import package_tagged as package


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.ReleaseTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name)
        self.published = True
        self.corrupt = False
        self.commit = self.fixture.commit
        patches = [patch.dict(os.environ, {"CHAINPAD_RELEASE_REF": "refs/tags/v0.10.0"}),
                   patch.object(package.site, "OUTPUT", self.output),
                   patch.object(package.site, "copy_site"),
                   patch.object(package.site, "main"),
                   patch.object(package.release, "command", side_effect=self.command)]
        self.mocks = [p.start() for p in patches]
        for p in patches:
            self.addCleanup(p.stop)

    def command(self, *args):
        if args[:2] == ("gh", "api"):
            return json.dumps([[{"tag_name": "v0.10.0", "draft": False}] if self.published else []])
        if args[:3] == ("gh", "release", "download"):
            target = Path(args[-1])
            for source in self.fixture.directory.rglob("*"):
                if source.is_file():
                    (target / source.name).write_bytes(source.read_bytes())
            if self.corrupt:
                (target / "ChainPad-ESP32-C6.bin").write_bytes(b"corrupt")
            return ""
        if args[:2] == ("git", "rev-parse"):
            return self.commit
        return self.fixture.fake_command(*args)

    def test_recovery_preserves_exact_release_bytes(self):
        package.package("owner/repo")
        self.mocks[3].assert_not_called()
        self.mocks[2].assert_called_once()
        for source in self.fixture.directory.rglob("*"):
            if source.is_file():
                self.assertEqual(source.read_bytes(), (self.output / source.relative_to(self.fixture.directory)).read_bytes())

    def test_new_release_is_packaged_from_source(self):
        self.published = False
        package.package("owner/repo")
        self.mocks[3].assert_called_once()

    def test_corrupt_asset_and_wrong_source_are_rejected(self):
        self.corrupt = True
        with self.assertRaises(ValueError):
            package.package("owner/repo")
        self.corrupt = False
        self.commit = "b" * 40
        with self.assertRaises(ValueError):
            package.package("owner/repo")
        self.mocks[2].assert_not_called()
