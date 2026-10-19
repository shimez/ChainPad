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
        guide = (self.output / "getting-started/index.html").read_text(encoding="utf-8")
        self.assertIn("<h1", guide)
        self.assertIn("Getting Started", guide)
        self.assertIn('href="../favicon.svg"', guide)
        self.assertIn('href="../installer/"', guide)
        self.assertIn('href="getting-started/"', portal)
        self.assertNotIn("<!-- DOCUMENT_CONTENT -->", guide)
        self.assertFalse((self.output / "getting-started/index.md").exists())
        manual = (self.output / "manual/index.html").read_text(encoding="utf-8")
        self.assertIn("マニュアル — ChainPad", manual)
        self.assertIn("<h1", manual)
        self.assertIn('href="../getting-started/"', manual)
        self.assertIn('href="manual/"', portal)
        self.assertIn('href="../manual/"', guide)
        self.assertNotIn("<!-- DOCUMENT_CONTENT -->", manual)
        self.assertFalse((self.output / "manual/index.md").exists())
        self.assertFalse((self.output / "manual/images/.gitkeep").exists())

    def test_guide_renders_markdown_and_copies_images(self):
        root = Path(self.temp.name) / "source"
        (root / "scripts/templates").mkdir(parents=True)
        for page in ("getting-started", "manual"):
            guide = root / "site" / page
            (guide / "images").mkdir(parents=True)
            (guide / "images/sample.svg").write_text('<svg xmlns="http://www.w3.org/2000/svg"/>')
            (guide / "index.md").write_text('# Guide\n\n[TOC]\n\n## 接続\n\n![画像](images/sample.svg)\n\n```text\n<x>\n```\n\n| 項目 | 値 |\n| --- | --- |\n| A | B |\n', encoding="utf-8")
            (root / f"scripts/templates/{page}.html").write_text('<main><!-- DOCUMENT_CONTENT --></main>')
        with patch.object(site, "ROOT", root), patch.object(site, "OUTPUT", self.output), patch.object(site.subprocess, "check_output", return_value="a" * 40):
            site.copy_site()
        for page in ("getting-started", "manual"):
            html = (self.output / page / "index.html").read_text(encoding="utf-8")
            self.assertIn('<div class="toc">', html)
            self.assertIn("<table>", html)
            self.assertIn("&lt;x&gt;", html)
            self.assertIn('src="images/sample.svg"', html)
            self.assertTrue((self.output / page / "images/sample.svg").is_file())

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

    def test_old_four_board_release_forces_rebuild(self):
        old = json.loads(self.payloads["build.json"])
        old["files"].append({"path": "firmware/ChainPad-ESP32-C3.bin", "chipFamily": "ESP32-C3"})
        metadata = json.dumps(old).encode()
        result = Path(self.temp.name) / "github-output"
        with patch.object(site, "fetch", return_value=metadata) as fetch, patch.object(site, "firmware_changed", return_value=False), patch.object(site, "reuse_published") as reuse, patch.dict(site.os.environ, {"GITHUB_OUTPUT": str(result)}):
            site.prepare("https://example.invalid/")
        self.assertEqual(result.read_text().strip(), "rebuild=true")
        self.assertEqual(fetch.call_count, 1)
        reuse.assert_not_called()
        with patch.object(site, "fetch", side_effect=lambda base, path: self.payloads[path]):
            with self.assertRaisesRegex(ValueError, "file set"):
                site.reuse_published("https://example.invalid/", metadata)

    def test_reuse_removes_stale_c3_binary(self):
        stale = self.output / 'firmware/ChainPad-ESP32-C3.bin'
        stale.parent.mkdir(parents=True)
        stale.write_bytes(b'old C3 binary')
        self.reuse()
        self.assertFalse(stale.exists())
        self.assertEqual({b['chipFamily'] for b in json.loads((self.output / 'manifest.json').read_bytes())['builds']}, {'ESP32-S3', 'ESP32-C5', 'ESP32-C6'})

    def test_packaging_excludes_c3_even_when_built(self):
        root = Path(self.temp.name) / 'source'
        (root / 'src').mkdir(parents=True)
        (root / 'src/main.cpp').write_text('doc["version"] = "test";')
        for chip in ('s3', 'c3', 'c6', 'c5'):
            target = root / f'.pio/build/xiao_esp32{chip}/firmware.factory.bin'
            target.parent.mkdir(parents=True)
            image = bytearray(0x10001)
            image[0x2000 if chip == 'c5' else 0] = image[0x10000] = 0xe9
            target.write_bytes(image)
        stale = self.output / 'firmware/ChainPad-ESP32-C3.bin'
        stale.parent.mkdir(parents=True)
        stale.write_bytes(b'old C3 binary')
        with patch.object(site, 'ROOT', root), patch.object(site, 'OUTPUT', self.output), patch.object(site, 'copy_site'), patch.object(site.subprocess, 'check_output', return_value='a' * 40):
            site.main()
        self.assertFalse(stale.exists())
        expected = {'ESP32-S3', 'ESP32-C5', 'ESP32-C6'}
        self.assertEqual({b['chipFamily'] for b in json.loads((self.output / 'manifest.json').read_bytes())['builds']}, expected)
        self.assertEqual({f['chipFamily'] for f in json.loads((self.output / 'build.json').read_bytes())['files']}, expected)
        self.assertEqual(len(list((self.output / 'firmware').glob('*.bin'))), 3)

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
