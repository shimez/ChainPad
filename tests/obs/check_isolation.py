"""Inspect linked artifacts, not merely preprocessor guards. Build all five first."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
packages = Path.home() / '.platformio/packages'

def nm(chip, path):
    arch = 'xtensa' if chip == 's3' else 'riscv32'
    package = 'toolchain-xtensa-esp-elf' if chip == 's3' else 'toolchain-riscv32-esp'
    tool = next((packages / package).rglob(f'{arch}-esp-elf-nm.exe'))
    return subprocess.check_output([str(tool), '-C', '--defined-only', str(path)], text=True)

for chip in ('s3', 'c3', 'c5', 'c6'):
    env = 'xiao_esp32' + chip
    build = root / '.pio/build' / env
    symbols = nm(chip, build / 'firmware.elf')
    image = (build / 'firmware.bin').read_bytes()
    assert 'obsTestByte' not in symbols and b'OBS TEST ' not in image, env
    assert 'obs_poc' not in symbols and b'poc-' not in image, env
    if chip in ('s3', 'c5', 'c6'):
        assert 'WebSocketsClient' in symbols and 'chimera::obs::Protocol::receive' in symbols
        assert b'chainpad-obs' in image and b'SetCurrentProgramScene' in image
    else:
        # Portable Action codecs retain schema helpers on unsupported targets.
        assert 'WebSockets' not in symbols and 'chimera::obs::Protocol::' not in symbols
        assert 'chimera::obs::authentication' not in symbols
        # Diagnostics can name an absent task; require no executable Worker.
        assert 'chimera::obsBegin' not in symbols and b'SetCurrentProgramScene' not in image
        assert not list(build.glob('lib*/libWebSockets.a'))
        assert not (root / '.pio/libdeps' / env / 'WebSockets').exists()
        for name in ('obs_backend', 'obs_protocol', 'obs_auth', 'obs_test_console'):
            assert not nm(chip, build / 'src' / (name + '.cpp.o')).strip(), (env, name)
    print('PASS', env, 'linked OBS gating / no test console / no PoC symbols')

build = root / '.pio/build-obs-test/xiao_esp32s3_obs_test'
symbols = nm('s3', build / 'firmware.elf')
image = (build / 'firmware.bin').read_bytes()
factory = (build / 'firmware.factory.bin').read_bytes()
assert 'obsTestByte' in symbols and 'chimera::obsDispatch' in symbols and 'chimera::obsApplySettings' in symbols
assert b'OBS TEST ' in image and b'EXAMPLE_ONLY' not in image and b'192.0.2.1' not in image
assert b'obs_poc' not in image
assert image[0] == 0xE9 and image[3] >> 4 == 3
assert factory[0x10000:0x10000 + len(image)] == image
partitions = (build / 'partitions.bin').read_bytes()
assert factory[0x8000:0x8000 + len(partitions)] == partitions
print('PASS explicit S3 test image: backend API linked, test-only console, no example credentials, image placement')

# Integration boundaries: preserve scheduling, storage and Wi-Fi/reset policy.
unchanged = ['src/engine.cpp', 'src/engine.h', 'src/config_store.cpp',
             'src/wifi_setup_flow.h', 'src/factory_reset.cpp']
assert not subprocess.check_output(['git', 'diff', '6faa8641dee13c005a76562e2c6d5d87d55ef596', '--', *unchanged], cwd=root)
print('PASS integration boundaries: Engine/storage/Wi-Fi/reset policy unchanged')
