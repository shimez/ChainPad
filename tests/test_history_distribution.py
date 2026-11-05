"""Exercise the main/tag history boundary and pre-publication provenance checks."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import build_site as site
import changelog as history
import publish_release as release
import release_plan as policy
sys.path.pop(0)


def section(version, text='Published feature'):
    return f'## {version}\n### 追加機能\n- {text}\n### 修正・改善\n- なし\n'


class DistributionTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.output = self.root / 'output'
        self.commit = 'a' * 40
        self.text = section('0.10.0') + section('0.9.0')
        self.info = {'version': '0.10.0', 'commit': self.commit, 'files': []}
        self.payloads = {}
        builds = []
        for family in release.FAMILIES:
            data = bytearray(0x10001)
            data[0x2000 if family == 'ESP32-C5' else 0] = data[0x10000] = 0xe9
            data = bytes(data)
            path = f'firmware/ChainPad-{family}.bin'
            self.payloads[path] = data
            self.info['files'].append({'chipFamily': family, 'path': path, 'bytes': len(data),
                                       'sha256': release.hashlib.sha256(data).hexdigest()})
            builds.append({'chipFamily': family, 'parts': [{'path': path, 'offset': 0}]})
        self.payloads['manifest.json'] = json.dumps({'version': '0.10.0+aaaaaaa', 'builds': builds}).encode()
        self.set_history()
        for module, name, value in [(site, 'OUTPUT', self.output), (site, 'ROOT', self.root)]:
            p = patch.object(module, name, value)
            p.start()
            self.addCleanup(p.stop)

    def set_history(self, document=None):
        document = document or history.generate_changes(self.text, self.info['version'], self.info['commit'])
        data = (json.dumps(document, ensure_ascii=False, indent=2) + '\n').encode()
        self.payloads['changes.json'] = data
        self.info['changes'] = history.make_reference(data)

    def fetch(self, base, path):
        if path == 'build.json':
            return json.dumps(self.info).encode()
        return self.payloads[path]

    def reuse(self):
        with patch.object(site, 'fetch', side_effect=self.fetch):
            site.reuse_published('https://example.invalid', json.dumps(self.info).encode())

    def test_main_preserves_exact_published_history_despite_future_edits(self):
        (self.root / 'CHANGELOG.md').write_text(section('0.11.0', 'Unpublished') + self.text.replace('Published', 'Edited'), encoding='utf-8')
        with patch.object(site, 'fetch', side_effect=self.fetch), patch.object(site, 'firmware_changed', return_value=True):
            result = policy.plan('refs/heads/main', 'b' * 40)
        self.assertEqual(result, {'rebuild': True, 'deploy': True, 'release': False})
        self.assertEqual((self.output / 'changes.json').read_bytes(), self.payloads['changes.json'])
        self.assertNotIn(b'Unpublished', (self.output / 'changes.json').read_bytes())

    def test_document_overlay_cannot_overwrite_reserved_assets(self):
        self.reuse()
        shutil.copytree(ROOT / 'site', self.root / 'site')
        shutil.copytree(ROOT / 'scripts/templates', self.root / 'scripts/templates')
        for name in ('changes.json', 'build.json', 'manifest.json', 'site-build.json'):
            (self.root / 'site' / name).write_text('unpublished')
        (self.root / 'site/firmware').mkdir(exist_ok=True)
        (self.root / 'site/firmware/rogue.bin').write_bytes(b'unpublished')
        with patch.object(site.subprocess, 'check_output', return_value='b' * 40):
            site.copy_site()
        for name, data in self.payloads.items():
            self.assertEqual((self.output / name).read_bytes(), data)
        self.assertEqual(json.loads((self.output / 'build.json').read_bytes()), self.info)
        self.assertFalse((self.output / 'firmware/rogue.bin').exists())
        self.assertEqual(json.loads((self.output / 'site-build.json').read_bytes())['commit'], 'b' * 40)

    def test_legacy_never_fetches_history_and_removes_stale_file(self):
        del self.info['changes']
        del self.payloads['changes.json']
        self.output.mkdir()
        (self.output / 'changes.json').write_bytes(b'unpublished')
        self.reuse()
        self.assertFalse((self.output / 'changes.json').exists())
        info, assets = release.validate(self.output)
        self.assertNotIn('changes', info)
        self.assertEqual(len(assets), 5)
        with self.assertRaisesRegex(ValueError, 'required'):
            release.validate(self.output, require_history=True)

    def test_missing_or_corrupt_referenced_history_stops_reuse(self):
        del self.payloads['changes.json']
        with self.assertRaises(KeyError):
            self.reuse()
        self.payloads['changes.json'] = b'corrupt'
        with self.assertRaisesRegex(ValueError, 'SHA256'):
            self.reuse()
        self.assertFalse(self.output.exists())

    def test_invalid_descriptors_are_not_legacy(self):
        original = copy.deepcopy(self.info['changes'])
        bad = [None, {}, {**original, 'path': '../changes.json'}, {**original, 'schemaVersion': True},
               {**original, 'schemaVersion': 2}, {**original, 'bytes': True}, {**original, 'bytes': 0},
               {**original, 'bytes': history.MAX_JSON_BYTES + 1}, {**original, 'sha256': 'bad'},
               {**original, 'extra': 1}]
        for descriptor in bad:
            with self.subTest(descriptor=descriptor):
                self.info['changes'] = descriptor
                with self.assertRaises(ValueError):
                    self.reuse()
        self.assertFalse(self.output.exists())

    def test_matching_hash_does_not_hide_schema_version_or_commit_mismatch(self):
        original = json.loads(self.payloads['changes.json'])
        for change in ({'schemaVersion': 2}, {'firmwareVersion': '0.9.0'}, {'firmwareCommit': 'b' * 40}):
            self.set_history({**original, **change})
            with self.assertRaises(ValueError):
                self.reuse()
        self.assertFalse(self.output.exists())

    def test_local_distribution_requires_referenced_history_and_correct_size(self):
        self.reuse()
        path = self.output / 'changes.json'
        path.unlink()
        with self.assertRaises(FileNotFoundError):
            release.validate(self.output, require_history=True)
        path.write_bytes(self.payloads['changes.json'] + b' ')
        with self.assertRaises(ValueError):
            release.validate(self.output)

    def test_source_provenance_checks_actual_committed_changelog(self):
        def git(*args):
            return subprocess.check_output(['git', *args], cwd=self.root, text=True, encoding='utf-8').strip()
        git('init')
        git('config', 'user.email', 'test@example.invalid')
        git('config', 'user.name', 'Test')
        (self.root / 'src').mkdir()
        (self.root / 'src/main.cpp').write_text('doc["version"] = "0.10.0";')
        (self.root / 'CHANGELOG.md').write_text(self.text, encoding='utf-8')
        git('add', '.')
        git('commit', '-m', 'firmware')
        self.info['commit'] = git('rev-parse', 'HEAD')
        manifest = json.loads(self.payloads['manifest.json'])
        manifest['version'] = '0.10.0+' + self.info['commit'][:7]
        self.payloads['manifest.json'] = json.dumps(manifest).encode()
        self.set_history()
        self.reuse()
        (self.root / 'CHANGELOG.md').write_text(section('0.11.0') + self.text, encoding='utf-8')
        git('add', 'CHANGELOG.md')
        git('commit', '-m', 'future main history')
        with patch.object(release, 'ROOT', self.root):
            release.verify_source(self.output, self.info)
            shutil.copytree(ROOT / 'scripts', self.root / 'scripts', ignore=shutil.ignore_patterns('__pycache__'))
            env = {key: value for key, value in os.environ.items() if key != 'GITHUB_REPOSITORY'}
            result = subprocess.run([sys.executable, str(self.root / 'scripts/publish_release.py'),
                                     str(self.output), '--validate-only'], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('Validated distribution', result.stdout)
            self.set_history(history.generate_changes(self.text.replace('Published', 'Altered'), '0.10.0', self.info['commit']))
            self.reuse()
            release.validate(self.output)  # Internally consistent but not the committed history.
            with self.assertRaisesRegex(ValueError, "firmware commit"):
                release.verify_source(self.output, self.info)


if __name__ == '__main__':
    unittest.main()
