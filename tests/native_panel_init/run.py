#!/usr/bin/env python3
"""Run production native-panel startup methods against a command recorder.

No firmware build, dependency downloads, panel access, or source rewriting.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'lib/lilka/src/lilka'
display = (SOURCE / 'display.cpp').read_text()
start = display.index('void Display::begin() {')
end = display.index('\nbool Display::enablePixelDMA()', start)
production = display[start:end]
MOCK = r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>
#include "config.h"
constexpr int ST7789_SWRESET=1,ST7789_SLPOUT=0x11,ST7789_COLMOD=0x3a;
constexpr int ST7789_NORON=0x13,ST7789_DISPON=0x29;
constexpr int ST7789_RST_DELAY=120,ST7789_SLPOUT_DELAY=120,FONT_10x20=1;
inline std::vector<std::pair<int,int>> events;
inline unsigned transactions=0;
inline void vTaskDelay(unsigned ticks){assert(!transactions);events.emplace_back(-1,ticks);}
inline unsigned pdMS_TO_TICKS(unsigned ms){return ms;}
namespace lilka {
namespace colors {constexpr uint16_t Black=0;}
struct Serial {void log(const char*){}};
inline Serial serial;
struct Bus {
 void beginWrite(){assert(!transactions);++transactions;}
 void endWrite(){assert(transactions==1);--transactions;}
 void sendCommand(int command){assert(!transactions);events.emplace_back(command,-1);}
 void writeC8D8(int command,int value){assert(transactions==1);events.emplace_back(command,value);}
};
inline Bus nativeBus;
struct Arduino_ST7789 {
 Bus* _bus=&nativeBus;
 virtual void tftInit(){events.emplace_back(0xe0,0xf0);events.emplace_back(0xe1,0xf0);}
 void begin(int clock){events.emplace_back(-2,clock);tftInit();events.emplace_back(-3,3);}
 void invertDisplay(bool invert){assert(!invert);_bus->sendCommand(0x21);}
 void fillScreen(uint16_t color){assert(!transactions);assert(color==0);events.emplace_back(-4,0);}
};
struct Display:Arduino_ST7789 {
 void begin();void setFont(int){}void setUTF8Print(bool){}
#if LILKA_VERSION == 2 && LILKA_NATIVE_PANEL_RESET_DEFAULTS
 void tftInit()override;
#endif
};
}
'''
TEST = r'''
#include "mock.h"
int main(){
 lilka::Display display;display.begin();
#ifdef LILKA_BREADBOARD
 const int clock=40000000;
#else
 const int clock=80000000;
#endif
 std::vector<std::pair<int,int>> expected={{-2,clock}};
#if LILKA_VERSION == 2 && LILKA_NATIVE_PANEL_RESET_DEFAULTS
 const std::pair<int,int> minimal[]={
  {0x01,-1},{-1,120},{0x11,-1},{-1,120},{0x3a,0x55},{0x21,-1},
  {0x13,-1},{-1,10},{0x29,-1},{-1,10}};
 expected.insert(expected.end(),std::begin(minimal),std::end(minimal));
 for(const auto& event:events)
  assert(event.first<0||event.first==0x01||event.first==0x11||event.first==0x3a||
   event.first==0x21||event.first==0x13||event.first==0x29);
#else
 expected.emplace_back(0xe0,0xf0);expected.emplace_back(0xe1,0xf0);
#endif
 expected.emplace_back(-3,3);expected.emplace_back(-4,0);
 assert(events==expected&&transactions==0);
}
'''
with tempfile.TemporaryDirectory(prefix='lilka-native-panel-init-') as temporary:
    temp = Path(temporary)
    (temp / 'config.h').write_text((SOURCE / 'config.h').read_text())
    (temp / 'mock.h').write_text(MOCK)
    (temp / 'production.cpp').write_text('#include "mock.h"\nnamespace lilka {\n' + production + '\n}\n')
    (temp / 'test.cpp').write_text(TEST)
    cases = [
        ['-DLILKA_VERSION=2'],
        ['-DLILKA_VERSION=2', '-DLILKA_NATIVE_PANEL_RESET_DEFAULTS=0'],
        ['-DLILKA_VERSION=2', '-DLILKA_NATIVE_PANEL_RESET_DEFAULTS=1'],
        ['-DLILKA_VERSION=2', '-DLILKA_BREADBOARD=1'],
        ['-DLILKA_VERSION=1', '-DLILKA_NATIVE_PANEL_RESET_DEFAULTS=1'],
    ]
    for flags in cases:
        command = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', '-fno-pie', '-no-pie', *flags,
                   '-I' + str(temp), str(temp / 'production.cpp'), str(temp / 'test.cpp'),
                   '-o', str(temp / 'test')]
        subprocess.run(command, check=True)
        subprocess.run([str(temp / 'test')], check=True)
print('PASS: 5 sanitized startup cases; native 80 MHz unchanged; no analog/gamma writes in diagnostic')
