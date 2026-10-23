#!/usr/bin/env python3
"""Exercise production near-black A/B; forbid all unrelated register writes."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'lib/lilka/src/lilka/display.cpp').read_text()
start = source.index('void Display::showNativeBlackStepDiagnostic() {')
production = source[start:source.index('\n#endif', start)]
MOCK = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>
constexpr int headerHeight = 28;
inline int panelWidth = 280, panelHeight = 240, left = 0, top = 0, rectWidth = 0, rectHeight = 0;
inline bool active = true, transaction = false;
inline unsigned stages = 0, inputCase = 0, inputIndex = 0, writes = 0;
inline uint8_t command = 0;
inline std::vector<std::vector<uint8_t>> tables;
inline std::vector<uint16_t> pixels, firstPattern;
inline std::vector<int> covered;
constexpr uint8_t original[2][14] = {
    {0xf0, 9, 0x13, 0x12, 0x12, 0x2b, 0x3c, 0x44, 0x4b, 0x1b, 0x18, 0x17, 0x1d, 0x21},
    {0xf0, 9, 0x13, 0x0c, 0x0d, 0x27, 0x3b, 0x44, 0x4d, 0x0b, 0x17, 0x17, 0x1d, 0x21}
};
inline unsigned pdMS_TO_TICKS(unsigned value) {
    return value;
}
inline void vTaskDelay(unsigned value) {
    assert(!transaction && (value == 1 || value == 10));
}
inline void observeFrame() {
    assert(!transaction);
    if (tables.size() / 2 <= stages) return;
    assert(tables.size() == 2 * (stages + 1));
    const bool adjusted = stages % 2;
    for (unsigned polarity = 0; polarity < 2; ++polarity) {
        const auto& data = tables[2 * stages + polarity];
        for (unsigned i = 0; i < 14; ++i)
            assert(data[i] == (adjusted && i == 1 ? 0 : (adjusted && i == 2 ? 2 : original[polarity][i])));
    }
    const int patternHeight = panelHeight - headerHeight;
    for (int row = 0; row < 2; ++row) {
        const int columns = row ? 64 : 16;
        const int y = headerHeight + (row * patternHeight / 2 + (row + 1) * patternHeight / 2) / 2;
        std::set<uint16_t> distinct;
        for (int level = 0; level < columns; ++level) {
            int x = (level * panelWidth / columns + (level + 1) * panelWidth / columns) / 2;
            uint16_t word = pixels[y * panelWidth + x];
            distinct.insert(word);
            unsigned green = (word >> 5) & 63;
            unsigned red = (((word >> 11) & 31) << 1) | (green & 1);
            unsigned blue = ((word & 31) << 1) | (green & 1);
            assert(green == static_cast<unsigned>(level) && red == green && blue == green);
        }
        assert(distinct.size() == static_cast<unsigned>(columns));
    }
    for (int y = headerHeight; y < panelHeight; ++y)
        for (int x = 0; x < panelWidth; ++x)
            assert(covered[y * panelWidth + x] == 1);
    std::vector<uint16_t> pattern(pixels.begin() + headerHeight * panelWidth, pixels.end());
    if (!stages) firstPattern = pattern;
    else assert(pattern == firstPattern);
    const unsigned labels[2][5] = {{2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}};
    const int digitLeft = (panelWidth - 12) / 2;
    for (int y = 0; y < headerHeight; ++y)
        for (int x = 0; x < panelWidth; ++x) {
            bool lit = x >= digitLeft && x < digitLeft + 12 && y >= 4 && y < 24 &&
                       (labels[adjusted][(y - 4) / 4] & (1u << (2 - (x - digitLeft) / 4)));
            assert(pixels[y * panelWidth + x] == (lit ? 0xffff : 0));
        }
    const char* output = std::getenv("BLACK_STEP_PPM");
    if (output && panelWidth == 280 && !inputCase && stages < 2) {
        char filename[1024];
        std::snprintf(filename, sizeof(filename), "%s-%u.ppm", output, stages + 1);
        FILE* file = std::fopen(filename, "wb");
        assert(file);
        std::fprintf(file, "P6\n%d %d\n255\n", panelWidth, panelHeight);
        // Render expected neutral frame-RAM values under the unchanged F0 expansion.
        for (uint16_t word : pixels) {
            unsigned char gray = static_cast<unsigned char>(((word >> 5) & 63) * 255 / 63);
            unsigned char rgb[] = {gray, gray, gray};
            assert(std::fwrite(rgb, 1, 3, file) == 3);
        }
        std::fclose(file);
    }
    ++stages;
}
namespace lilka {
namespace colors {
constexpr uint16_t Black = 0;
} // namespace colors
struct Button {
    bool justPressed = false;
};
struct Input {
    Button a, b, c;
};
struct Controller {
    bool oldA = false, oldB = false, oldC = false;
    Input getState() {
        observeFrame();
        static const bool normal[][3] = {
            {true, false, false},
            {true, false, false},
            {false, false, false},
            {true, false, false},
            {true, false, false},
            {false, false, false},
            {true, false, false},
            {false, false, false},
            {true, false, false},
            {false, false, false},
            {true, false, false},
            {false, false, false},
            {true, true, true}
        };
        static const bool early[][3] = {{false, false, false}, {false, true, false}};
        static const bool keep[][3] = {
            {false, false, false},
            {true, false, false},
            {false, false, false},
            {true, false, false},
            {false, false, false},
            {true, false, false},
            {false, false, false},
            {false, false, true}
        };
        const unsigned size = inputCase == 2 ? 8 : (inputCase == 1 ? 2 : 13);
        assert(inputIndex < size);
        const auto& next =
            inputCase == 2 ? keep[inputIndex++] : (inputCase == 1 ? early[inputIndex++] : normal[inputIndex++]);
        Input result;
        result.a.justPressed = next[0] && !oldA;
        result.b.justPressed = next[1] && !oldB;
        result.c.justPressed = next[2] && !oldC;
        oldA = next[0];
        oldB = next[1];
        oldC = next[2];
        return result;
    }
};
inline Controller controller;
struct Devices {
    bool internalOutput() {
        return active;
    }
};
inline Devices devices;
struct Serial {
    void log(const char*) {
    }
};
inline Serial serial;
struct Bus {
    void beginWrite() {
        assert(!transaction);
        transaction = true;
    }
    void endWrite() {
        assert(transaction);
        transaction = false;
    }
    void writeCommand(uint8_t value) {
        assert(transaction && value == (tables.size() % 2 ? 0xe1 : 0xe0));
        command = value;
    }
    void writeBytes(uint8_t* data, uint32_t length) {
        assert(transaction && length == 14 && command == (tables.size() % 2 ? 0xe1 : 0xe0));
        tables.emplace_back(data, data + length);
        command = 0;
    }
    void writeRepeat(uint16_t color, uint32_t count) {
        assert(transaction && count == static_cast<uint32_t>(rectWidth * rectHeight));
        ++writes;
        for (int y = top; y < top + rectHeight; ++y)
            for (int x = left; x < left + rectWidth; ++x) {
                assert(x >= 0 && x < panelWidth && y >= 0 && y < panelHeight);
                pixels[y * panelWidth + x] = color;
                ++covered[y * panelWidth + x];
            }
    }
};
struct Arduino_ST7789 {
    Bus bus;
    Bus* _bus = &bus;
    int16_t width() {
        return panelWidth;
    }
    int16_t height() {
        return panelHeight;
    }
    virtual void writeAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) {
        assert(transaction);
        left = x;
        top = y;
        rectWidth = w;
        rectHeight = h;
    }
};
struct Display : Arduino_ST7789 {
    void showNativeBlackStepDiagnostic();
    void writeAddrWindow(int16_t, int16_t, uint16_t, uint16_t) override {
        assert(false);
    }
};
} // namespace lilka
'''
MAIN = r'''
int main(int argc, char** argv) {
    assert(argc == 5);
    panelWidth = std::atoi(argv[1]);
    panelHeight = std::atoi(argv[2]);
    active = std::atoi(argv[3]);
    inputCase = std::atoi(argv[4]);
    pixels.assign(panelWidth * panelHeight, 0xabcd);
    covered.assign(pixels.size(), 0);
    lilka::Display display;
    display.showNativeBlackStepDiagnostic();
    assert(!transaction && !command);
    if (active) {
        assert(stages == (inputCase == 2 ? 4u : (inputCase == 1 ? 1u : 5u)));
        assert(inputIndex == (inputCase == 2 ? 8u : (inputCase == 1 ? 2u : 13u)));
        assert(tables.size() == 2 * stages + (inputCase == 2 ? 0 : 2));
        for (unsigned polarity = 0; polarity < 2; ++polarity) {
            const auto& data = tables[tables.size() - 2 + polarity];
            for (unsigned i = 0; i < 14; ++i)
                assert(
                    data[i] == (inputCase == 2 && i == 1 ? 0 : (inputCase == 2 && i == 2 ? 2 : original[polarity][i]))
                );
        }
        for (uint16_t pixel : pixels)
            assert(pixel == 0);
    } else {
        assert(!stages && !writes && tables.empty());
        for (uint16_t pixel : pixels)
            assert(pixel == 0xabcd);
    }
}
'''
with tempfile.TemporaryDirectory(prefix='lilka-black-step-') as temporary:
    folder = Path(temporary)
    cpp = folder / 'test.cpp'
    cpp.write_text(MOCK + '\nnamespace lilka {\n' + production + '\n}\n' + MAIN)
    binary = folder / 'test'
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                    str(cpp), '-o', str(binary)], check=True)
    for width, height, active, input_case in (
        (280, 240, 1, 0), (240, 280, 1, 0), (280, 240, 0, 0),
        (280, 240, 1, 1), (280, 240, 1, 2), (240, 280, 1, 2)
    ):
        subprocess.run([str(binary), str(width), str(height), str(active), str(input_case)], check=True)
print('PASS: only V1/V2 differ; original restored on B, retained on C; A toggle/hold, neutral 16/64 levels, safe labels, inactive skip')
