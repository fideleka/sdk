#!/usr/bin/env python3
"""Verify original startup, v2-only shadow values, bus order and unchanged clock."""
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'lib/lilka/src/lilka/display.cpp').read_text()
start = source.index('void Display::begin() {')
begin = source[start:source.index('\nbool Display::enablePixelDMA()', start)]
start = source.index('#if LILKA_VERSION == 2\nvoid Display::tftInit() {')
init = source[start:source.index('\n#endif', start) + len('\n#endif')]
dependencies = json.loads((ROOT / 'lib/lilka/library.json').read_text())['dependencies']
assert dependencies['moononournation/GFX Library for Arduino'] == '1.6.0'
MOCK = r'''
#include <cassert>
#include <cstdint>
#include <vector>
constexpr int FONT_10x20 = 1;
inline unsigned baseCalls = 0;
inline bool transaction = false, cleared = false;
inline int clockHz = 0;
inline std::vector<uint8_t> commands;
inline std::vector<std::vector<uint8_t>> writes;
namespace lilka {
namespace colors {
constexpr uint16_t Black = 0;
} // namespace colors
struct Serial {
    void log(const char*) {
    }
};
inline Serial serial;
struct Bus {
    void beginWrite() {
        assert(baseCalls == 1 && !transaction && !cleared);
        transaction = true;
    }
    void endWrite() {
        assert(transaction);
        transaction = false;
    }
    void writeCommand(uint8_t command) {
        assert(transaction && commands.size() == writes.size());
        commands.push_back(command);
    }
    void writeBytes(uint8_t* bytes, uint32_t count) {
        assert(transaction && count == 14 && commands.size() == writes.size() + 1);
        writes.emplace_back(bytes, bytes + count);
    }
};
struct Arduino_ST7789 {
    Bus bus;
    Bus* _bus = &bus;
    virtual void tftInit() {
        assert(!transaction && !baseCalls);
        ++baseCalls;
    }
    void begin(int speed) {
        clockHz = speed;
        tftInit();
    }
    void fillScreen(uint16_t color) {
        assert(!transaction && baseCalls == 1 && color == 0);
        cleared = true;
    }
};
struct Display : Arduino_ST7789 {
    void begin();
    void setFont(int) {
    }
    void setUTF8Print(bool) {
    }
#if LILKA_VERSION == 2
    void tftInit() override;
#endif
};
} // namespace lilka
'''
MAIN = r'''
int main() {
    lilka::Display display;
    display.begin();
    assert(baseCalls == 1 && cleared && !transaction);
#ifdef LILKA_BREADBOARD
    assert(clockHz == 40000000);
#else
    assert(clockHz == 80000000);
#endif
#if LILKA_VERSION == 2
    assert(commands == std::vector<uint8_t>({0xe0, 0xe1}) && writes.size() == 2);
    const uint8_t original[2][14] = {
        {0xf0, 9, 0x13, 0x12, 0x12, 0x2b, 0x3c, 0x44, 0x4b, 0x1b, 0x18, 0x17, 0x1d, 0x21},
        {0xf0, 9, 0x13, 0x0c, 0x0d, 0x27, 0x3b, 0x44, 0x4d, 0x0b, 0x17, 0x17, 0x1d, 0x21}
    };
    for (unsigned polarity = 0; polarity < 2; ++polarity) {
        for (unsigned index = 0; index < 14; ++index) {
            uint8_t expected = index == 12 ? 0x35 : (index == 13 ? 0x37 : original[polarity][index]);
            assert(writes[polarity][index] == expected);
        }
    }
#else
    assert(commands.empty() && writes.empty());
#endif
}
'''
with tempfile.TemporaryDirectory(prefix='lilka-native-shadow-init-') as temporary:
    directory = Path(temporary)
    test = directory / 'test.cpp'
    test.write_text(MOCK + '\nnamespace lilka {\n' + begin + init + '\n}\n' + MAIN)
    binary = directory / 'test'
    for flags in (['-DLILKA_VERSION=2'], ['-DLILKA_VERSION=1'],
                  ['-DLILKA_VERSION=2', '-DLILKA_BREADBOARD=1']):
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                        *flags, str(test), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print('PASS: 3 startup cases; original init first, exact accepted V61/V62 only, v1 and clocks preserved')
