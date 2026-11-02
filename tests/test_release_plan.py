"""Main never deploys development firmware; old tags never downgrade Pages."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))
SPEC = importlib.util.spec_from_file_location("release_plan", SCRIPTS / "release_plan.py")
policy = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(policy)
sys.path.pop(0)


class PlanTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        (root / "src").mkdir()
        (root / "src/main.cpp").write_text('doc["version"] = "0.10.0";', encoding="utf-8")
        (root / "CHANGELOG.md").write_text("## 0.10.0\n\n- Feature\n", encoding="utf-8")
        self.root = root
        self.commit = "a" * 40
        self.info = {"version": "0.9.0", "commit": "b" * 40}
        patches = [patch.object(policy.site, "ROOT", root),
                   patch.object(policy.site, "fetch", side_effect=lambda *args: json.dumps(self.info).encode()),
                   patch.object(policy.site, "reuse_published"),
                   patch.object(policy.site, "firmware_changed", return_value=False),
                   patch.object(policy, "command", return_value=self.commit)]
        self.mocks = [p.start() for p in patches]
        for p in patches:
            self.addCleanup(p.stop)
        self.fetch, self.reuse, self.changed, self.command = self.mocks[1:]

    def test_main_docs_only_reuses_firmware_without_build(self):
        self.assertEqual(policy.plan("refs/heads/main", "c" * 40),
                         {"rebuild": False, "deploy": True, "release": False})
        self.reuse.assert_called_once()
        self.changed.assert_called_once_with("c" * 40)

    def test_main_source_change_builds_but_keeps_published_firmware(self):
        self.changed.return_value = True
        result = policy.plan("refs/heads/main", "c" * 40)
        self.assertEqual(result, {"rebuild": True, "deploy": True, "release": False})
        self.reuse.assert_called_once()

    def test_main_manual_or_forced_build_never_publishes_release(self):
        for before, force in [("", False), ("0" * 40, False), ("c" * 40, True)]:
            self.assertEqual(policy.plan("refs/heads/main", before, force),
                             {"rebuild": True, "deploy": True, "release": False})

    def test_new_tag_builds_and_publishes_both(self):
        self.assertEqual(policy.plan("refs/tags/v0.10.0"),
                         {"rebuild": True, "deploy": True, "release": True})
        self.reuse.assert_not_called()
        self.command.assert_any_call("git", "merge-base", "--is-ancestor", self.commit, "origin/main")

    def test_old_tag_publishes_release_only(self):
        self.info["version"] = "1.0.0"
        self.assertEqual(policy.plan("refs/tags/v0.10.0"),
                         {"rebuild": True, "deploy": False, "release": True})

    def test_same_version_retry_requires_same_commit(self):
        self.info["version"] = "0.10.0"
        with self.assertRaises(ValueError):
            policy.plan("refs/tags/v0.10.0")
        self.info["commit"] = self.commit
        self.assertTrue(policy.plan("refs/tags/v0.10.0")["deploy"])

    def test_wrong_tag_version_or_branch_stops_before_fetch(self):
        for ref in ["refs/tags/v0.11.0", "refs/tags/v0.10.0-rc1", "refs/tags/0.10.0", "refs/heads/feature"]:
            with self.assertRaises(ValueError):
                policy.plan(ref)
        self.fetch.assert_not_called()

    def test_changelog_required_before_publication(self):
        (self.root / "CHANGELOG.md").write_text("## 0.9.0\nOld\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            policy.plan("refs/tags/v0.10.0")
        self.fetch.assert_not_called()

    def test_unavailable_publication_does_not_silently_replace_firmware(self):
        self.fetch.side_effect = RuntimeError("offline")
        for ref in ["refs/heads/main", "refs/tags/v0.10.0"]:
            with self.assertRaises(RuntimeError):
                policy.plan(ref)
        self.reuse.assert_not_called()


if __name__ == "__main__":
    unittest.main()
