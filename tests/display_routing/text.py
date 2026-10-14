#!/usr/bin/env python3
"""Real Arduino_GFX U8g2 decoding through production Display::drawChar.

Uses installed dependency sources read-only; no firmware build or downloads.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
gfx = root.parent / "keira/.pio/libdeps/v2/GFX Library for Arduino/src"
fonts = root / "lib/lilka/.pio/libdeps/v2/U8g2/src/clib/u8g2_fonts.c"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, default=root / "lib/lilka/src/lilka/display_graphics_routing.cpp")
parser.add_argument("--sanitized-only", action="store_true")
args = parser.parse_args()
source = args.source.read_text()
start = source.index("void Display::drawChar(")
end = source.index("\n} // namespace lilka", start)
method = source[start:end]
font_source = fonts.read_text(encoding="latin1")
start = font_source.index("const uint8_t u8g2_font_6x12_t_cyrillic[")
font = font_source[start : font_source.index(";\n", start) + 1]
arduino = r"""
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
using String=std::string;
class __FlashStringHelper;
using std::min;using std::max;
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_word(p) (*(const uint16_t*)(p))
#define pgm_read_dword(p) (*(const uint32_t*)(p))
#define pgm_read_ptr(p) (*(void* const*)(p))
inline void yield(){}
"""
printing = r"""
#pragma once
#include <cstddef>
#include <cstdint>
class Print {public:virtual ~Print(){};virtual size_t write(uint8_t)=0;
size_t print(const char*p){size_t n=0;while(*p)n+=write(uint8_t(*p++));return n;}};
"""
harness = (
    r"""
#include "Arduino_GFX.h"
#include <cassert>
#include <vector>
#include <cstdio>
extern const uint8_t u8g2_font_6x12_t_cyrillic[4690];
namespace lilka {
using Arduino_ST7789 = Arduino_GFX;
struct Canvas : Arduino_GFX {
 std::vector<uint16_t> pixels;
 Canvas():Arduino_GFX(160,80),pixels(160*80,0){setFont(u8g2_font_6x12_t_cyrillic);
 // Direct drawChar on this unprepared decoder caused the real device panic.
 _u8g2_decode_ptr=reinterpret_cast<uint8_t*>(0x96e28896);_u8g2_char_width=8;}
 bool begin(int32_t=-1)override{return true;}
 void writePixelPreclipped(int16_t x,int16_t y,uint16_t c)override{drawPixel(x,y,c);}
 void drawPixel(int16_t x,int16_t y,uint16_t c){if(x>=0&&y>=0&&x<160&&y<80)pixels[y*160+x]=c;}
};
struct Display : Arduino_GFX {
 Canvas retained;std::vector<uint16_t> pixels;bool native=true;unsigned frames=0;
 Display():Arduino_GFX(160,80),pixels(160*80,0){}
 bool begin(int32_t=-1)override{return true;}
 void writePixelPreclipped(int16_t x,int16_t y,uint16_t c)override{drawPixel(x,y,c);}
 bool nativeOutput(){return native;}Canvas*routingCanvas(){return &retained;}
 bool routingDirty=false,nativeBypass=false;
 void drawPixel(int16_t x,int16_t y,uint16_t c){retained.drawPixel(x,y,c);if(native&&x>=0&&y>=0&&x<160&&y<80)pixels[y*160+x]=c;}
 void drawChar(int16_t,int16_t,unsigned char,uint16_t,uint16_t)override;
};
struct DisplayFrame{Display&d;DisplayFrame(Display&d):d(d){}~DisplayFrame(){++d.frames;}};
"""
    + method
    + r"""
}
int main(){
 for(int scale:{1,2})for(bool opaque:{false,true}){
 lilka::Display both,external;
 external.native=false;
 for(auto*d:{&both,&external}){
 d->setFont(u8g2_font_6x12_t_cyrillic);d->setUTF8Print(true);
 d->setTextSize(scale);d->setCursor(2,24);
 d->setTextColor(0xffff,opaque?0x1234:0xffff);
 d->print("Keira / DOOM / Потік");
 }
 assert(both.frames>0&&both.frames==external.frames);
 assert(both.pixels==both.retained.pixels);
 assert(both.pixels==external.retained.pixels);
 assert(std::count(both.pixels.begin(),both.pixels.end(),0xffff)>0);
 assert(std::count(external.pixels.begin(),external.pixels.end(),0)==160*80);
 }
 puts("Real Arduino_GFX/U8g2 prepared glyph routing: ASCII/UTF8, scales, transparency, Both/External PASS");
}
"""
)
with tempfile.TemporaryDirectory(prefix="sdk-real-text-") as folder:
    tmp = Path(folder)
    (tmp / "Arduino.h").write_text(arduino)
    (tmp / "Print.h").write_text(printing)
    (tmp / "U8g2lib.h").write_text("#pragma once\n#define U8G2_FONT_SECTION(x)\n")
    (tmp / "font.c").write_text("#include <stdint.h>\n#define U8G2_FONT_SECTION(x)\n" + font)
    (tmp / "test.cpp").write_text(harness)
    subprocess.run(["gcc", "-c", str(tmp / "font.c"), "-o", str(tmp / "font.o")], check=True)
    for flags in (
        (["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"],)
        if args.sanitized_only
        else ([], ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"])
    ):
        subprocess.run(
            [
                "g++",
                "-std=c++11",
                "-g",
                *flags,
                "-I" + str(tmp),
                "-I" + str(gfx),
                str(gfx / "Arduino_GFX.cpp"),
                str(gfx / "Arduino_G.cpp"),
                str(tmp / "test.cpp"),
                str(tmp / "font.o"),
                "-o",
                str(tmp / "test"),
            ],
            check=True,
        )
        subprocess.run([str(tmp / "test")], check=True)
