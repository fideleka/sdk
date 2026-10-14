#!/usr/bin/env python3
"""Build freestanding SD drivers using the ESP32-S3 toolchain installed by PlatformIO."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ABI = 1


def validate_binary(path):
    """Check the linker layout/budgets before packaging; runtime performs stricter preflight."""
    data = path.read_bytes()
    if len(data) < 52 or len(data) > 65536 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise RuntimeError("Driver is not a bounded ELF32 little-endian module")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    if header[1:3] != (3, 94) or header[11] != 40 or not 0 < header[12] <= 64:
        raise RuntimeError("Driver ELF architecture/section layout is incompatible")
    start, count, names_index = header[6], header[12], header[13]
    if start + count * 40 > len(data) or names_index >= count:
        raise RuntimeError("Driver ELF section table is invalid")
    sections = [struct.unpack_from("<10I", data, start + index * 40) for index in range(count)]
    names = sections[names_index]
    strings = data[names[4]:names[4] + names[5]]
    text, allocated_data = 0, 0
    for section in sections:
        name = strings[section[0]:].split(b"\0", 1)[0]
        if name == b".text":
            text = section[5]
        elif name in (b".data", b".rodata", b".bss", b".data.rel.ro"):
            allocated_data += section[5]
    if not 0 < text <= 8192 or allocated_data > 32768:
        raise RuntimeError("Driver exceeds executable/data RAM budgets")


def build_drivers(toolchain, output):
    root = Path(__file__).resolve().parents[1]
    compiler = Path(toolchain) / ('xtensa-esp32s3-elf-gcc.exe' if os.name == 'nt' else 'xtensa-esp32s3-elf-gcc')
    if not compiler.is_file():
        raise RuntimeError(f'ESP32-S3 compiler missing: {compiler}')
    results = []
    for manifest_path in sorted((root / 'drivers').glob('*/manifest.json')):
        metadata = json.loads(manifest_path.read_text(encoding='utf-8'))
        if metadata['abi'] != ABI or metadata['module'] != 'driver.so':
            raise RuntimeError(f'Unsupported driver manifest: {manifest_path}')
        package = manifest_path.parent
        with tempfile.TemporaryDirectory(prefix='lilka-sd-driver-') as temporary:
            binary = Path(temporary) / 'driver.so'
            command = [str(compiler), '-std=c11', '-Os', '-ffreestanding', '-fno-builtin', '-fPIC',
                       '-mtext-section-literals', '-mlongcalls', '-nostdlib', '-shared',
                       '-Wl,--no-relax', '-Wl,-Bsymbolic', '-Wl,--no-undefined', '-Wl,-e,main', '-Wl,-T,' + str(root / 'drivers/driver.ld'),
                       '-Wl,--hash-style=sysv', '-o', str(binary), str(package / 'driver.c')]
            subprocess.run(command, check=True)
            validate_binary(binary)
            destination = Path(output) / 'drivers' / package.name
            destination.mkdir(parents=True, exist_ok=True)
            shutil.copy2(binary, destination / 'driver.so')
            shutil.copy2(manifest_path, destination / 'manifest.json')
            results.append({'package': package.name, 'abi': ABI, 'type': metadata['type'],
                            'device_uuid': metadata['device_uuid'], 'files': {
                                str(path.relative_to(output)): hashlib.sha256(path.read_bytes()).hexdigest()
                                for path in (destination / 'driver.so', destination / 'manifest.json')}})
    if not results:
        raise RuntimeError('No driver sources/manifests found in selected Keira branch')
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', required=True, type=Path, help='ESP32-S3 toolchain bin directory')
    parser.add_argument('--output', required=True, type=Path, help='SD-card staging root')
    arguments = parser.parse_args()
    results = build_drivers(arguments.toolchain, arguments.output)
    (arguments.output / 'driver-build.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    print(f'Built {len(results)} SD driver package(s): {arguments.output}')
