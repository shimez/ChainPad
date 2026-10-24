"""Exact, fail-closed WebSockets 2.7.2 patch for the S3 OBS backend.

Only environment-local libdeps are touched. Delete that dependency directory to
restore upstream. Reviewed patch text is kept alongside this script.
"""
from pathlib import Path
import hashlib
import difflib

HASHES = {
    'WebSockets.h': 'fad33ca86ea752b8cdc5f593b7fe4d05d6f5a2a936ae5838199952d6bcf5d4da',
    'WebSockets.cpp': '2f6cab57d93192bb15c0b3fbc708198ce71125f77c812b538d8b5143af928248',
}

def transform(name, text):
    if name == 'WebSockets.h':
        old = '#define WEBSOCKETS_MAX_DATA_SIZE (15 * 1024)'
        assert old in text
        return text.replace(old, '#ifndef WEBSOCKETS_MAX_DATA_SIZE\n' + old + '\n#endif', 1)
    old = '            case WSop_close: {\n'
    assert text.count(old) == 1
    return text.replace(old, old + '                // ChainPad: expose raw Close BEFORE normal teardown.\n'
                        '                messageReceived(client, header->opCode, payload, header->payloadLen, header->fin);\n', 1)

def patch(directory):
    prepared = []
    for name, expected in HASHES.items():
        target = directory / name
        original = directory / (name + '.upstream-2.7.2')
        current = target.read_text(encoding='utf-8')
        upstream = original.read_text(encoding='utf-8') if original.exists() else current
        if hashlib.sha256(upstream.encode()).hexdigest() != expected:
            raise RuntimeError('Unexpected WebSockets 2.7.2 source: ' + str(target))
        modified = transform(name, upstream)
        if current not in (upstream, modified):
            raise RuntimeError('Refusing unfamiliar dependency edits: ' + str(target))
        prepared.append((target, original, upstream, modified))
    for target, original, upstream, modified in prepared:
        if not original.exists():
            original.write_text(upstream, encoding='utf-8', newline='\n')
        if target.read_text(encoding='utf-8') != modified:
            target.write_text(modified, encoding='utf-8', newline='\n')
        diff = ''.join(difflib.unified_diff(upstream.splitlines(True), modified.splitlines(True),
                    fromfile=target.name + '@2.7.2', tofile=target.name + '@chainpad'))
        (directory / (target.name + '.chainpad.patch')).write_text(diff, encoding='utf-8', newline='\n')

if __name__ == '__main__':
    import sys
    patch(Path(sys.argv[1]))
elif 'Import' in globals():
    Import('env')
    if env.subst('$PIOENV') not in ('xiao_esp32s3', 'xiao_esp32s3_obs_test', 'xiao_esp32s3_obs_trace'):
        raise RuntimeError('OBS patch must run only in an explicit S3 environment')
    patch(Path(env.subst('$PROJECT_LIBDEPS_DIR')) / env.subst('$PIOENV') / 'WebSockets' / 'src')
