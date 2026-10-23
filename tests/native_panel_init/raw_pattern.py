#!/usr/bin/env python3
"""Exercise the production raw diagnostic against a framebuffer bus recorder."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'lib/lilka/src/lilka/display.cpp').read_text()
start = source.index('void Display::showNativeGrayDiagnostic() {')
end = source.index('\n#endif', start)
production = source[start:end]
MOCK = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>
inline int panelWidth=280,panelHeight=240,left=0,top=0,rectWidth=0,rectHeight=0;
inline bool active=true,transaction=false,captured=false;
inline unsigned writes=0;
inline std::vector<uint16_t> pixels;
inline std::vector<int> covered;
inline unsigned pdMS_TO_TICKS(unsigned value){return value;}
inline void vTaskDelay(unsigned value){
 assert(!transaction);
 if(value!=20000)return;
 captured=true;
 for(int count:covered)assert(count==1);
 const int rows[]={16,32,32,32,32};
 for(int row=0;row<5;++row){
  std::set<uint16_t> distinct;
  int y=(row*panelHeight/5+(row+1)*panelHeight/5)/2;
  for(int x=0;x<panelWidth;++x)distinct.insert(pixels[y*panelWidth+x]);
  assert(distinct.size()==static_cast<unsigned>(rows[row]));
  assert(pixels[y*panelWidth]==0);
 }
 assert(pixels[panelWidth/16]==0x0020); // RGB8(4,4,4) quantizes to this word.
 assert(pixels[2*panelHeight/5*panelWidth+panelWidth-1]==0xf800);
 assert(pixels[3*panelHeight/5*panelWidth+panelWidth-1]==0x07c0);
 assert(pixels[4*panelHeight/5*panelWidth+panelWidth-1]==0x001f);
 const char* output=std::getenv("RAW_PATTERN_PPM");
 if(output){
  FILE* file=std::fopen(output,"wb");assert(file);
  std::fprintf(file,"P6\n%d %d\n255\n",panelWidth,panelHeight);
  for(uint16_t color:pixels){
   unsigned char rgb[]={static_cast<unsigned char>(((color>>11)&31)*255/31),
    static_cast<unsigned char>(((color>>5)&63)*255/63),
    static_cast<unsigned char>((color&31)*255/31)};
   assert(std::fwrite(rgb,1,3,file)==3);
  }
  std::fclose(file);
 }
}
namespace lilka {
namespace colors{constexpr uint16_t Black=0;}
struct Devices{bool internalOutput(){return active;}};
inline Devices devices;
struct Serial{void log(const char*){}};
inline Serial serial;
struct Bus{
 void beginWrite(){assert(!transaction);transaction=true;}
 void endWrite(){assert(transaction);transaction=false;}
 void writeRepeat(uint16_t color,uint32_t count){
  assert(transaction&&count==static_cast<uint32_t>(rectWidth*rectHeight));
  ++writes;
  for(int y=top;y<top+rectHeight;++y)for(int x=left;x<left+rectWidth;++x){
   assert(x>=0&&x<panelWidth&&y>=0&&y<panelHeight);
   pixels[y*panelWidth+x]=color;
   if(!captured)++covered[y*panelWidth+x];
  }
 }
};
struct Arduino_ST7789{
 Bus bus;Bus* _bus=&bus;
 int16_t width(){return panelWidth;}int16_t height(){return panelHeight;}
 virtual void writeAddrWindow(int16_t x,int16_t y,uint16_t w,uint16_t h){
  assert(transaction);left=x;top=y;rectWidth=w;rectHeight=h;
 }
};
struct Display:Arduino_ST7789{
 void showNativeGrayDiagnostic();
 void writeAddrWindow(int16_t,int16_t,uint16_t,uint16_t)override{assert(false);}
};
}
'''
MAIN = r'''
int main(int argc,char**argv){
 assert(argc==4);panelWidth=std::atoi(argv[1]);panelHeight=std::atoi(argv[2]);
 active=std::atoi(argv[3]);pixels.assign(panelWidth*panelHeight,0xabcd);
 covered.assign(pixels.size(),0);
 lilka::Display display;display.showNativeGrayDiagnostic();
 assert(!transaction);
 if(active){
  assert(captured&&writes==145);
  for(uint16_t pixel:pixels)assert(pixel==0); // Black restored before app startup.
 }else{
  assert(!captured&&writes==0);
  for(uint16_t pixel:pixels)assert(pixel==0xabcd);
 }
}
'''
with tempfile.TemporaryDirectory(prefix='lilka-raw-pattern-') as temporary:
    folder = Path(temporary)
    cpp = folder / 'test.cpp'
    cpp.write_text(MOCK + '\nnamespace lilka {\n' + production + '\n}\n' + MAIN)
    binary = folder / 'test'
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                    str(cpp), '-o', str(binary)], check=True)
    for width, height, active in ((280, 240, 1), (240, 280, 1), (280, 240, 0)):
        subprocess.run([str(binary), str(width), str(height), str(active)], check=True)
print('PASS: raw RGB565 coverage, 16/32 distinct steps, routing bypass, black restore, inactive-output skip')
