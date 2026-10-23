#!/usr/bin/env python3
"""Exercise production SDK/board startup and Display::begin with retained LCD RAM.

Peripheral stubs check ordering; tests/backlight covers the real PWM implementation.
No firmware compilation, dependency downloads, or hardware access.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'lib/lilka/src'
MOCK = r'''
#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>
#include "board.h"
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define LILKA_SLEEP 46
#define LILKA_GPIO_SELECT 0
#define FONT_10x20 1
constexpr int LILKA_EXT_PINS[]={1,2,3,4,5,6};
constexpr int TX=43,RX=44;
using gpio_num_t=int;
using TaskHandle_t=void*;
inline std::vector<int> events;
inline int savedMode=0,scenario=0,pin=-1;
inline bool nativeLight=false,externalLight=false,cleared=false,held=true;
inline void pinMode(int,int){}
inline void digitalWrite(int p,int value){if(p==LILKA_SLEEP){pin=value;if(LILKA_INDEPENDENT_BACKLIGHT)assert(!value);}}
inline int gpio_get_level(int){return scenario==3?0:1;}
inline TaskHandle_t xTaskGetIdleTaskHandleForCPU(int){return nullptr;}
inline void esp_task_wdt_delete(void*){}
namespace lilka {
namespace colors {constexpr uint16_t Black=0;}
struct Serial {void begin(){} void log(const char*){}};
inline Serial serial;
struct Subsystem {void begin(){}};
inline Subsystem multiboot,buzzer,audio,battery;
inline void spi_begin(){}
struct Files {void begin(){events.push_back(2);}};
inline Files fileutils;
struct Brightness {
 bool begin(bool active){assert(!active);nativeLight=active;events.push_back(1);return LILKA_INDEPENDENT_BACKLIGHT;}
 bool isEnabled(){return LILKA_INDEPENDENT_BACKLIGHT;}
 bool setActiveDisplays(bool n,bool e){assert(cleared);nativeLight=n;externalLight=e;events.push_back(7);return true;}
 void suspend(){} void resume(){assert(events.back()==7);held=false;events.push_back(8);}
};
inline Brightness brightness;
struct Native {
 std::vector<uint16_t> pixels=std::vector<uint16_t>(280*240,0x1234);
 void begin(int){assert(events.back()==2);if(LILKA_INDEPENDENT_BACKLIGHT)assert(!nativeLight&&pin==0);events.push_back(3);}
 void fillScreen(uint16_t c){assert(c==0);std::fill(pixels.begin(),pixels.end(),c);cleared=true;events.push_back(4);}
};
using Arduino_ST7789=Native;
struct Display:Native {
 void begin();void setFont(int){} void setUTF8Print(bool){}
 void showNativeGrayDiagnostic(){assert(events.back()==8&&!held);}
 void enableRouting(){assert(events.back()==5);assert(held);events.push_back(6);}
 void displayOff(){}void displayOn(){}
};
inline Display display;
struct Controller {void begin(){assert(cleared);}};
inline Controller controller;
struct Devices {
 bool internal=true,external=false;
 bool begin(bool skip){
   assert(cleared&&std::all_of(display.pixels.begin(),display.pixels.end(),[](uint16_t p){return !p;}));
   if(LILKA_INDEPENDENT_BACKLIGHT)assert(!nativeLight);
   assert(skip==(scenario==3));events.push_back(5);
   external=!skip&&scenario!=4&&scenario!=5&&savedMode!=0;
   internal=!external||savedMode!=1;
   return scenario!=5;
 }
 bool internalOutput(){return internal;}bool externalOutput(){return external;}void setSleeping(bool){}
};
inline Devices devices;
struct Settings {void begin(){assert(events.back()==8);}};
inline Settings displaySettings;
void begin();
}
'''
TEST = r'''
#include "mock.h"
#include <cstdlib>
int main(int argc,char**argv){
 assert(argc==3);savedMode=std::atoi(argv[1]);scenario=std::atoi(argv[2]);
 lilka::begin();
 assert(events==std::vector<int>({1,2,3,4,5,6,7,8}));
 assert(nativeLight==lilka::devices.internalOutput());
 assert(externalLight==lilka::devices.externalOutput());
 if(scenario>=3)assert(nativeLight&&!externalLight);
}
'''

display = (SOURCE / 'lilka/display.cpp').read_text()
start = display.index('void Display::begin() {')
end = display.index('\nbool Display::enablePixelDMA()', start)
with tempfile.TemporaryDirectory(prefix='lilka-display-startup-') as directory:
    tmp = Path(directory)
    (tmp / 'mock.h').write_text(MOCK)
    (tmp / 'board.h').write_text((SOURCE / 'lilka/board.h').read_text())
    (tmp / 'board.cpp').write_text((SOURCE / 'lilka/board.cpp').read_text())
    (tmp / 'startup.cpp').write_text((SOURCE / 'lilka.cpp').read_text())
    (tmp / 'display_begin.cpp').write_text('#include "mock.h"\nnamespace lilka {\n' + display[start:end] + '\n}\n')
    (tmp / 'test.cpp').write_text(TEST)
    for name in ('lilka.h', 'brightness.h', 'display.h', 'config.h', 'devices.h',
                 'Arduino.h', 'esp_wifi.h', 'esp_task_wdt.h', 'driver/gpio.h'):
        path = tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#pragma once\n#include "mock.h"\n')
    for version, modified, raw in ((2, 1, 0), (2, 0, 0), (2, 1, 1), (2, 0, 1)):
        command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                   f'-DLILKA_VERSION={version}', f'-DLILKA_INDEPENDENT_BACKLIGHT={modified}',
                   f'-DLILKA_NATIVE_PANEL_RAW_DIAGNOSTIC={raw}',
                   '-I' + str(tmp), *(str(tmp / f) for f in
                     ('startup.cpp', 'board.cpp', 'display_begin.cpp', 'test.cpp')),
                   '-o', str(tmp / 'test')]
        subprocess.run(command, check=True)
        for mode in range(3):
            for scenario in (0, 3, 4, 5):
                subprocess.run([str(tmp / 'test'), str(mode), str(scenario)], check=True)
print('Display startup: 48 sanitized mode/recovery/hardware/raw-hook cases passed')
