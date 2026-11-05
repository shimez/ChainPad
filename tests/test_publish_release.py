"""Release publication uses CI firmware, immutable tags, and resumable drafts."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("publish_release", Path(__file__).resolve().parents[1] / "scripts/publish_release.py")
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)
sys.path.pop(0)


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.directory = Path(temp.name)
        (self.directory / "firmware").mkdir()
        self.commit = "a" * 40
        self.info = {"version": "0.10.0", "commit": self.commit, "files": []}
        self.changelog = "## 0.10.0\n### 追加機能\n- New feature.\n### 修正・改善\n- なし\n\n## 0.9.0\n### 追加機能\n- Previous.\n### 修正・改善\n- なし\n"
        document = release.history.generate_changes(self.changelog, "0.10.0", self.commit)
        payload = release.history.encode_changes(document, "0.10.0", self.commit)
        self.info["changes"] = release.history.make_reference(payload)
        (self.directory / "changes.json").write_bytes(payload)
        self.manifest = {"version": "0.10.0+aaaaaaa", "builds": []}
        for family in release.FAMILIES:
            data = bytearray(0x10001)
            data[0x2000 if family == "ESP32-C5" else 0] = data[0x10000] = 0xE9
            path = f"firmware/ChainPad-{family}.bin"
            (self.directory / path).write_bytes(data)
            self.info["files"].append({"chipFamily": family, "path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
            self.manifest["builds"].append({"chipFamily": family, "parts": [{"path": path, "offset": 0}]})
        self.write_metadata()
        self.calls, self.releases, self.refs = [], [], []

    def write_metadata(self):
        for name, data in [("build.json", self.info), ("manifest.json", self.manifest)]:
            (self.directory / name).write_text(json.dumps(data), encoding="utf-8")

    def fake_command(self, *args):
        self.calls.append(args)
        if args[:2] == ("git", "show"):
            if args[2].endswith(":src/main.cpp"):
                return 'doc["version"] = "0.10.0";'
            return self.changelog
        if args[:2] == ("git", "merge-base"):
            return ""
        if args[:2] == ("gh", "api"):
            if "--paginate" in args:
                return json.dumps([self.releases])
            if "matching-refs" in args[-1]:
                return json.dumps(self.refs)
            if "/git/tags/" in args[-1]:
                return json.dumps({"object": {"type": "commit", "sha": self.commit}})
        if args[:2] == ("gh", "release"):
            return ""
        raise AssertionError(args)

    def publish(self):
        with patch.object(release, "command", side_effect=self.fake_command):
            release.publish(self.directory, "owner/repo")

    def mutations(self):
        return [call for call in self.calls if call[:2] == ("gh", "release")]

    def test_new_release_uploads_all_assets_before_publishing(self):
        self.releases = [{"tag_name": "v0.9.0", "draft": False, "prerelease": False}]
        self.publish()
        create, upload, publish = self.mutations()
        self.assertEqual([c[2] for c in self.mutations()], ["create", "upload", "edit"])
        self.assertIn("--draft", create)
        self.assertEqual(create[create.index("--target") + 1], self.commit)
        for name in ["build.json", "manifest.json", "changes.json", *[f"ChainPad-{family}.bin" for family in release.FAMILIES]]:
            self.assertTrue(any(arg.endswith(name) for arg in upload))
        self.assertIn("--draft=false", publish)
        self.assertIn("--latest=true", publish)
        notes = (self.directory / "release-notes.md").read_text(encoding="utf-8")
        self.assertIn("New feature.", notes)
        self.assertNotIn("Previous.", notes)
        self.assertIn(self.commit, notes)

    def test_published_release_is_never_modified(self):
        self.releases = [{"tag_name": "v0.10.0", "draft": False}]
        self.publish()
        self.assertEqual(self.mutations(), [])

    def test_draft_is_resumed_without_recreating_release(self):
        self.releases = [{"tag_name": "v0.10.0", "draft": True, "target_commitish": self.commit}]
        self.publish()
        self.assertEqual([c[2] for c in self.mutations()], ["upload", "edit"])

    def test_different_tag_or_draft_commit_is_rejected(self):
        self.refs = [{"ref": "refs/tags/v0.10.0", "object": {"type": "commit", "sha": "b" * 40}}]
        with self.assertRaises(ValueError):
            self.publish()
        self.assertEqual(self.mutations(), [])
        self.refs = []
        self.releases = [{"tag_name": "v0.10.0", "draft": True, "target_commitish": "b" * 40}]
        with self.assertRaises(ValueError):
            self.publish()
        self.assertEqual(self.mutations(), [])

    def test_matching_annotated_tag_is_accepted(self):
        self.refs = [{"ref": "refs/tags/v0.10.0", "object": {"type": "tag", "sha": "b" * 40}}]
        self.publish()
        self.assertEqual(len(self.mutations()), 3)

    def test_old_version_does_not_replace_latest(self):
        self.releases = [{"tag_name": "v1.0.0", "draft": False, "prerelease": False}]
        self.publish()
        self.assertIn("--latest=false", self.mutations()[-1])

    def test_upload_failure_leaves_draft_unpublished(self):
        def fail_upload(*args):
            result = self.fake_command(*args)
            if args[:3] == ("gh", "release", "upload"):
                raise subprocess.CalledProcessError(1, args)
            return result
        with patch.object(release, "command", side_effect=fail_upload):
            with self.assertRaises(subprocess.CalledProcessError):
                release.publish(self.directory, "owner/repo")
        self.assertEqual([c[2] for c in self.mutations()], ["create", "upload"])

    def test_corrupt_image_is_rejected_before_network_access(self):
        (self.directory / self.info["files"][0]["path"]).write_bytes(b"corrupted")
        with self.assertRaises(ValueError):
            self.publish()
        self.assertEqual(self.calls, [])

    def test_source_version_mismatch_stops_before_github(self):
        with patch.object(release, "command", return_value='doc["version"] = "0.9.0";') as command:
            with self.assertRaises(ValueError):
                release.publish(self.directory, "owner/repo")
        self.assertEqual(command.call_count, 1)

    def test_required_tag_must_match_trigger_and_artifact(self):
        with patch.dict(release.os.environ, {"GITHUB_REF": "refs/heads/main"}):
            with self.assertRaises(ValueError):
                release.publish(self.directory, "owner/repo", require_tag=True)
        self.assertEqual(self.mutations(), [])

    def test_required_tag_must_exist_remotely(self):
        def command(*args):
            if args == ("git", "rev-parse", "HEAD"):
                return self.commit
            return self.fake_command(*args)
        with patch.dict(release.os.environ, {"GITHUB_REF": "refs/tags/v0.10.0"}), \
             patch.object(release, "command", side_effect=command):
            with self.assertRaises(ValueError):
                release.publish(self.directory, "owner/repo", require_tag=True)
            self.assertEqual(self.mutations(), [])
            self.refs = [{"ref": "refs/tags/v0.10.0", "object": {"type": "commit", "sha": self.commit}}]
            release.publish(self.directory, "owner/repo", require_tag=True)
        self.assertEqual(len(self.mutations()), 3)

    def test_api_failure_is_not_treated_as_missing_release(self):
        def fail_api(*args):
            if args[:2] == ("gh", "api"):
                raise subprocess.CalledProcessError(1, args)
            return self.fake_command(*args)
        with patch.object(release, "command", side_effect=fail_api):
            with self.assertRaises(subprocess.CalledProcessError):
                release.publish(self.directory, "owner/repo")
        self.assertEqual(self.mutations(), [])

    def test_metadata_mismatches_and_path_traversal_are_rejected(self):
        for mutate in [lambda: self.info.update(version="0.10.0-rc1"),
                       lambda: self.info.update(commit="not-a-sha"),
                       lambda: self.manifest.update(version="0.9.0+aaaaaaa"),
                       lambda: self.info["files"][0].update(path="../firmware.bin"),
                       lambda: self.manifest["builds"][0]["parts"][0].update(offset=4096),
                       lambda: self.info["files"].pop()]:
            info, manifest = json.dumps(self.info), json.dumps(self.manifest)
            mutate()
            self.write_metadata()
            with self.assertRaises(ValueError):
                release.validate(self.directory)
            self.info, self.manifest = json.loads(info), json.loads(manifest)

    def test_notes_require_exact_version_section(self):
        for text in ["## 0.10.01\nWrong\n", "## 0.9.0\nWrong\n", "## 0.10.0\n\n"]:
            with self.assertRaises(ValueError):
                release.release_notes(text, "0.10.0", self.commit)


if __name__ == "__main__":
    unittest.main()
