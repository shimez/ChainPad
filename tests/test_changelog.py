"""Release history remains version-bound, complete, deterministic and bounded."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("changelog", ROOT / "scripts/changelog.py")
history = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(history)
COMMIT = "a" * 40


def section(version, feature="追加機能です。", improvement="なし"):
    return f"## {version}\n\n### 追加機能\n- {feature}\n\n### 修正・改善\n- {improvement}\n"


class ChangelogTests(unittest.TestCase):
    def setUp(self):
        self.text = "# Changelog\n\n" + section("0.10.0") + "\n" + section("0.9.0", "以前の機能です。")
        self.document = history.generate_changes(self.text, "0.10.0", COMMIT)

    def test_numeric_order_and_future_filtering(self):
        text = section("0.9.0") + section("1.0.0") + section("0.10.0")
        result = history.generate_changes(text, "0.10.0", COMMIT)
        self.assertEqual([record["version"] for record in result["releases"]], ["0.10.0", "0.9.0"])
        self.assertEqual(result["firmwareVersion"], "0.10.0")
        self.assertEqual(result["firmwareCommit"], COMMIT)

    def test_none_means_empty_list(self):
        result = history.generate_changes(section("1.0.0", "なし", "なし"), "1.0.0", COMMIT)
        self.assertEqual(result["releases"][0], {"version": "1.0.0", "features": [], "improvements": []})

    def test_wrapping_bom_crlf_and_notes(self):
        text = "\ufeff# Changelog\n\n" + section("0.10.0", "最初の行\n  続きの行。")
        text += "\n### 補足\n自由な技術説明。\n```text\n## 9.9.9\n### 追加機能\n- 表示しない\n```\n"
        result = history.generate_changes(text.replace("\n", "\r\n"), "0.10.0", COMMIT)
        self.assertEqual(result["releases"][0]["features"], ["最初の行 続きの行。"])
        self.assertNotIn("技術説明", json.dumps(result, ensure_ascii=False))

    def test_unreleased_is_not_a_release(self):
        text = "## Unreleased\n\n### 追加機能\n- 開発途中\n\n" + self.text
        result = history.generate_changes(text, "0.10.0", COMMIT)
        self.assertEqual(result, self.document)

    def test_empty_or_missing_sections_are_errors(self):
        for text in ["", "# Changelog\n", "## 0.10.0\n- Old free-form entry\n",
                     "## 0.10.0\n### 追加機能\n- なし\n",
                     "## 0.10.0\n### 追加機能\n### 修正・改善\n- なし\n"]:
            with self.subTest(text=text), self.assertRaises(ValueError):
                history.parse_changelog(text)

    def test_duplicate_versions_and_categories_are_errors(self):
        for text in [self.text + section("0.10.0"), self.text + "### 追加機能\n- Duplicate\n"]:
            with self.assertRaises(ValueError):
                history.parse_changelog(text)

    def test_none_cannot_hide_other_items(self):
        with self.assertRaises(ValueError):
            history.parse_changelog(section("0.10.0", "なし\n- 実際には変更あり"))

    def test_complex_or_unbulleted_history_is_not_silently_dropped(self):
        for item in ["最初\n  - Nested", "最初\n#### Nested", "最初\n```\ncode\n```",
                     "最初\n説明が箇条書きになっていない"]:
            with self.subTest(item=item), self.assertRaises(ValueError):
                history.parse_changelog(section("0.10.0", item))

    def test_selected_release_must_exist(self):
        for version in ["0.8.0", "0.11.0"]:
            with self.assertRaises(ValueError):
                history.generate_changes(self.text, version, COMMIT)

    def test_versions_match_browser_numeric_rules(self):
        for value in ["v0.10.0", "0.10", "00.10.0", "0.10.0-rc1", "0.10.0+abc", "0.１０.0",
                      "9007199254740992.0.0", "-1.0.0", "0.10.0\n", 1, None]:
            with self.subTest(value=value), self.assertRaises(ValueError):
                history.version_key(value)
        self.assertGreater(history.version_key("0.10.0"), history.version_key("0.9.0"))

    def test_commit_and_version_binding(self):
        for version, commit in [("0.9.0", COMMIT), ("0.10.0", "b" * 40), ("0.10.0", "abc"),
                                ("0.10.0", "A" * 40), ("0.10.0", None)]:
            with self.assertRaises(ValueError):
                history.validate_changes(self.document, version, commit)

    def test_schema_and_record_types(self):
        for mutate in [lambda d: d.update(schemaVersion=2), lambda d: d.update(schemaVersion=True),
                       lambda d: d.update(extra=True), lambda d: d.pop("firmwareCommit"),
                       lambda d: d.update(releases=[]), lambda d: d.update(releases=None),
                       lambda d: d["releases"].__setitem__(0, None),
                       lambda d: d["releases"][0].pop("features"),
                       lambda d: d["releases"][0].update(extra="field"),
                       lambda d: d["releases"][0].update(features="not a list"),
                       lambda d: d["releases"][0].update(features=[None])]:
            data = copy.deepcopy(self.document)
            mutate(data)
            with self.assertRaises(ValueError):
                history.validate_changes(data, "0.10.0", COMMIT)

    def test_invalid_json_order_duplicates_future_or_missing_current(self):
        for mutate in [lambda records: records.reverse(),
                       lambda records: records.append(copy.deepcopy(records[-1])),
                       lambda records: records[0].update(version="0.11.0"),
                       lambda records: records.pop(0)]:
            data = copy.deepcopy(self.document)
            mutate(data["releases"])
            with self.assertRaises(ValueError):
                history.validate_changes(data, "0.10.0", COMMIT)

    def test_encoding_is_deterministic_and_roundtrips(self):
        first = history.encode_changes(self.document, "0.10.0", COMMIT)
        reversed_source = section("0.9.0", "以前の機能です。") + section("0.10.0")
        second = history.encode_changes(history.generate_changes(reversed_source, "0.10.0", COMMIT), "0.10.0", COMMIT)
        self.assertEqual(first, second)
        self.assertEqual(history.decode_changes(first, "0.10.0", COMMIT), self.document)
        self.assertTrue(first.endswith(b"\n"))
        self.assertIn("追加機能です。".encode(), first)

    def test_invalid_unicode_control_characters_or_empty_text(self):
        for text in ["", "  ", "a\x00b", "a\nb", "a\x7fb", "\ud800"]:
            data = copy.deepcopy(self.document)
            data["releases"][0]["features"] = [text]
            with self.assertRaises(ValueError):
                history.validate_changes(data, "0.10.0", COMMIT)

    def test_bounds_reject_instead_of_truncating_history(self):
        data = copy.deepcopy(self.document)
        data["releases"][0]["features"] = ["字" * history.MAX_TEXT_LENGTH]
        history.validate_changes(data, "0.10.0", COMMIT)
        data["releases"][0]["features"][0] += "字"
        with self.assertRaises(ValueError):
            history.validate_changes(data, "0.10.0", COMMIT)
        data["releases"][0]["features"] = ["機能"] * (history.MAX_ITEMS + 1)
        with self.assertRaises(ValueError):
            history.validate_changes(data, "0.10.0", COMMIT)
        with patch.object(history, "MAX_RELEASES", 1):
            with self.assertRaises(ValueError):
                history.generate_changes(self.text, "0.10.0", COMMIT)
        with patch.object(history, "MAX_JSON_BYTES", 10):
            with self.assertRaises(ValueError):
                history.encode_changes(self.document, "0.10.0", COMMIT)
            with self.assertRaises(ValueError):
                history.decode_changes(b" " * 11, "0.10.0", COMMIT)

    def test_bad_utf8_duplicate_json_keys_and_invalid_json(self):
        payload = history.encode_changes(self.document, "0.10.0", COMMIT)
        duplicate = payload.replace(b'"schemaVersion": 1', b'"schemaVersion": 1, "schemaVersion": 1')
        for data in [b"\xff", b"{", b"[]", duplicate]:
            with self.assertRaises(ValueError):
                history.decode_changes(data, "0.10.0", COMMIT)

    def test_repository_history_uses_formal_tag_boundaries(self):
        text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        result = history.generate_changes(text, "0.9.0", COMMIT)
        self.assertEqual([r["version"] for r in result["releases"]], ["0.9.0", "0.8.0", "0.7.0", "0.6.0"])
        by_version = {r["version"]: " ".join(r["features"]) for r in result["releases"]}
        self.assertIn("通知", by_version["0.9.0"])
        self.assertIn("音量", by_version["0.8.0"])
        self.assertIn("メディア", by_version["0.7.0"])
        self.assertIn("v0.6.0タグ公開後", by_version["0.7.0"])
        self.assertNotIn("音量", by_version["0.7.0"])
        self.assertNotIn("アイテム", by_version["0.6.0"])
        self.assertNotIn("volumePercent", json.dumps(result))
        self.assertLess(len(history.encode_changes(result, "0.9.0", COMMIT)), history.MAX_JSON_BYTES)

    def test_cli_uses_build_provenance_and_preserves_output_on_error(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            changelog, build, output = root / "CHANGELOG.md", root / "build.json", root / "changes.json"
            changelog.write_text(self.text, encoding="utf-8")
            build.write_text(json.dumps({"version": "0.10.0", "commit": COMMIT}), encoding="utf-8")
            args = [sys.executable, str(ROOT / "scripts/changelog.py"), "--changelog", str(changelog),
                    "--build", str(build), "--output", str(output)]
            result = subprocess.run(args, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            saved = output.read_bytes()
            self.assertEqual(history.decode_changes(saved, "0.10.0", COMMIT), self.document)
            build.write_text(json.dumps({"version": "0.11.0", "commit": COMMIT}), encoding="utf-8")
            result = subprocess.run(args, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), saved)


if __name__ == "__main__":
    unittest.main()
