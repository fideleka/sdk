"""Production display code must omit profiling storage and calls, not only logs."""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]/'lib/lilka/src/lilka'
for name in ('devices.h', 'devices.cpp'):
    source = (root/name).read_text()
    source = re.sub(r'^\s*#\s*include .*$', '', source, flags=re.M)
    for enabled in (0, 1):
        text = subprocess.run(['g++', '-E', '-P', '-x', 'c++', '-DLILKA_VERSION=2',
                               '-DLILKA_MEASURE_DISPLAY='+str(enabled), '-'],
                              input=source, text=True, capture_output=True, check=True).stdout
        for token in ('captureTiming', 'captureUs', 'transferCount', 'transferPixels'):
            assert (token in text) == bool(enabled), (name, enabled, token)
        if name.endswith('.cpp'):
            assert ('perf-ext:' in text) == bool(enabled)
            for operation in ('DeviceManager::capture(', 'presentNativePixels',
                              'slot.descriptor->present(', 'slot.descriptor->present_rows(',
                              'DeviceManager::finish(', 'spiFault = true'):
                assert operation in text, operation
print('PASS: production display omits counters/storage/reports; real presentation and DMA faults retained')
