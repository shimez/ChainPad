"""Generate verbatim handler excerpts and test-only ArduinoJson include shim."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
source = (root / 'src/main.cpp').read_text(encoding='utf-8')
routes = [('/api/wifi', 'HTTP_PUT'), ('/api/wifi', 'HTTP_GET'), ('/api/config', 'HTTP_GET'),
          ('/api/config/begin', 'HTTP_POST'), ('/api/config/chain', 'HTTP_PUT'),
          ('/api/config/rotation', 'HTTP_PUT'), ('/api/config/commit', 'HTTP_POST'),
          ('/api/factory-reset', 'HTTP_GET'), ('/api/factory-reset', 'HTTP_POST')]
chunks = []
locations = []
for path, method in routes:
    pattern = r'^  web\.on\("' + re.escape(path) + r'", ' + method + r', \[\] \{\n.*?^  \}\);'
    matches = list(re.finditer(pattern, source, re.M | re.S))
    if len(matches) != 1:
        raise RuntimeError(f'Expected exactly one handler: {path} {method}')
    match = matches[0]
    chunks.append(match.group())
    locations.append({'route': path, 'method': method, 'line': source.count('\n', 0, match.start()) + 1})
panic = re.findall(r'^void panic\(\).*$', source, re.M)
restart = re.findall(r'^  web\.on\("/api/restart".*$', source, re.M)
if len(restart) != 1:
    raise RuntimeError('Restart extraction changed')
chunks += restart
if len(panic) != 1:
    raise RuntimeError('panic extraction changed; review required')
helpers = []
for signature in ['void jsonResponse', 'void result', 'bool resetGuard']:
    matches = re.findall(r'^' + signature + r'\(.*?^\}', source, re.M | re.S)
    if len(matches) != 1:
        raise RuntimeError(f'Helper extraction changed: {signature}')
    helpers.append(matches[0])
(out / 'handlers.inc').write_text('\n'.join(helpers + panic) + '\nvoid registerHandlers(){\n' + '\n'.join(chunks) + '\n}\n', encoding='utf-8')
json_header = root / '.pio/libdeps/xiao_esp32s3/ArduinoJson/src/ArduinoJson.h'
adapter = root / 'tests/storage_fault/json_fault.h'
(out / 'ArduinoJson.h').write_text(f'#pragma once\n#include "{json_header.as_posix()}"\n#include "{adapter.as_posix()}"\n', encoding='utf-8')
# A second executable models a firmware update whose builtin Default changed.
# Production source is verbatim; only its local generated data differs.
(out / 'config_store_alternate.cpp').write_text((root / 'src/config_store.cpp').read_text(encoding='utf-8'), encoding='utf-8')
original = (root / 'src/builtin_config_data.h').read_text(encoding='utf-8')
alternate = original.replace('"usage":30', '"usage":4')
if original == alternate:
    raise RuntimeError('Review changed-Default fixture: expected KEY1 usage 30')
(out / 'builtin_config_data.h').write_text(alternate, encoding='utf-8')
manifest = {'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
            'handlers': locations,
            'sha256': {p: hashlib.sha256((root / p).read_bytes()).hexdigest()
                       for p in sorted(str(p.relative_to(root)).replace('\\', '/')
                                       for folder in ['src', 'web'] for p in (root / folder).rglob('*') if p.is_file())}}
(out / 'sources.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
print(json.dumps(manifest, indent=2))
