"""Clean-source reproducibility, reviewed diff parity, idempotence, fail-closed."""
from pathlib import Path
import importlib.util
import tempfile
import difflib

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('patch_ws', root / 'scripts/patch_websockets.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
source = root / '.pio/libdeps/xiao_esp32s3/WebSockets/src'
reviewed = ''
scratch = root / '.pio/obs-tests'
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch) as name:
    target = Path(name)
    for file in module.HASHES:
        text = (source / (file + '.upstream-2.7.2')).read_text(encoding='utf-8')
        (target / file).write_text(text, encoding='utf-8', newline='\n')
        reviewed += ''.join(difflib.unified_diff(text.splitlines(True), module.transform(file, text).splitlines(True),
                                                fromfile=file+'@2.7.2', tofile=file+'@chainpad'))
    assert reviewed == (root / 'scripts/patches/WebSockets-2.7.2.patch').read_text(encoding='utf-8')
    module.patch(target)
    first = {f: (target / f).read_bytes() for f in module.HASHES}
    module.patch(target)
    assert first == {f: (target / f).read_bytes() for f in module.HASHES}
    (target / 'WebSockets.cpp').write_bytes(first['WebSockets.cpp'] + b'\n// unknown edit\n')
    try:
        module.patch(target)
    except RuntimeError:
        pass
    else:
        raise AssertionError('Unknown edited source accepted')
    (target / 'WebSockets.cpp.upstream-2.7.2').write_text('different upstream', encoding='utf-8')
    try:
        module.patch(target)
    except RuntimeError:
        pass
    else:
        raise AssertionError('Unknown upstream accepted')
print('PASS WebSockets patch: clean original hashes, reviewed diff, idempotence, unknown source/edits rejected')
