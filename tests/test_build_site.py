"""Pages-only deployments must retain all verified firmware and its provenance."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("build_site", Path(__file__).resolve().parents[1] / "scripts/build_site.py")
site = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(site)


class PagesTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name) / "output"
        self.info = {"version": "test", "commit": "a" * 40, "files": []}
        self.payloads = {}
        builds = []
        for chip in site.CHIPS:
            family = f"ESP32-{chip.upper()}"
            path = f"firmware/ChainPad-{family}.bin"
            data = family.encode()
            self.payloads[path] = data
            self.info["files"].append({"chipFamily": family, "path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
            builds.append({"chipFamily": family, "parts": [{"path": path, "offset": 0}]})
        self.payloads["build.json"] = json.dumps(self.info).encode()
        self.payloads["manifest.json"] = json.dumps({"version": "test+aaaaaaa", "builds": builds}).encode()

    def reuse(self):
        with patch.object(site, "OUTPUT", self.output), patch.object(site, "fetch", side_effect=lambda base, path: self.payloads[path]):
            site.reuse_published("https://example.invalid/", self.payloads["build.json"])

    def test_reuse_preserves_firmware_and_metadata_and_nested_site(self):
        self.reuse()
        with patch.object(site, "OUTPUT", self.output):
            site.copy_site()
        for path, data in self.payloads.items():
            self.assertEqual((self.output / path).read_bytes(), data)
        portal = (self.output / "index.html").read_text(encoding="utf-8")
        installer = (self.output / "installer/index.html").read_text(encoding="utf-8")
        self.assertIn('href="installer/"', portal)
        self.assertNotIn("esp-web-install-button", portal)
        self.assertIn('manifest="../manifest.json"', installer)
        self.assertIn("fetch('../build.json')", installer)
        self.assertTrue((self.output / "favicon.svg").is_file())

    def test_corrupt_binary_rejected(self):
        self.payloads[self.info["files"][0]["path"]] = b"corrupt"
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.reuse()
        self.assertFalse(self.output.exists())

    def test_mixed_manifest_rejected(self):
        manifest = json.loads(self.payloads["manifest.json"])
        manifest["builds"][0]["parts"][0]["offset"] = 65536
        self.payloads["manifest.json"] = json.dumps(manifest).encode()
        with self.assertRaisesRegex(ValueError, "manifest"):
            self.reuse()

    def test_prepare_skips_build_and_preserves_provenance(self):
        result = Path(self.temp.name) / "github-output"
        with patch.object(site, "OUTPUT", self.output), patch.object(site, "fetch", side_effect=lambda base, path: self.payloads[path]), patch.object(site, "firmware_changed", return_value=False), patch.dict(site.os.environ, {"GITHUB_OUTPUT": str(result)}):
            site.prepare("https://example.invalid/")
        self.assertEqual(result.read_text().strip(), "rebuild=false")
        self.assertEqual(json.loads((self.output / "build.json").read_bytes())["commit"], self.info["commit"])

    def test_prepare_requests_build_for_firmware_changes(self):
        result = Path(self.temp.name) / "github-output"
        with patch.object(site, "fetch", return_value=self.payloads["build.json"]) as fetch, patch.object(site, "firmware_changed", return_value=True), patch.dict(site.os.environ, {"GITHUB_OUTPUT": str(result)}):
            site.prepare("https://example.invalid/")
        self.assertEqual(fetch.call_count, 1)
        self.assertEqual(result.read_text().strip(), "rebuild=true")

    def test_real_git_changes_distinguish_site_and_device_ui(self):
        root = Path(self.temp.name) / "repo"
        root.mkdir()
        def git(*args):
            return subprocess.check_output(["git", *args], cwd=root, stderr=subprocess.DEVNULL, text=True).strip()
        git("init")
        git("config", "user.email", "test@example.invalid")
        git("config", "user.name", "Test")
        for folder in ("site", "web"):
            (root / folder).mkdir()
            (root / folder / "index.html").write_text("initial")
        git("add", ".")
        git("commit", "-m", "initial")
        published = git("rev-parse", "HEAD")
        (root / "site/index.html").write_text("portal")
        git("add", ".")
        git("commit", "-m", "site only")
        with patch.object(site, "ROOT", root):
            self.assertFalse(site.firmware_changed(published))
        (root / "web/index.html").write_text("device UI")
        git("add", ".")
        git("commit", "-m", "firmware UI")
        with patch.object(site, "ROOT", root):
            self.assertTrue(site.firmware_changed(published))


if __name__ == "__main__":
    unittest.main()
