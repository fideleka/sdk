#!/usr/bin/env python3
"""Run the production gamma diagnostic with a native framebuffer/command recorder."""
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
inline bool active=true,transaction=false;
inline unsigned writes=0,stages=0,expansion=0xf0,gammaMask=1,inputCase=0,inputIndex=0;
inline std::vector<unsigned> modes,gammas;
inline std::vector<uint16_t> pixels,firstPattern;
inline std::vector<int> covered;
inline unsigned pdMS_TO_TICKS(unsigned value){return value;}
inline void vTaskDelay(unsigned value){assert(!transaction&&(value==1||value==10));}
inline void observeFrame(){
 assert(!transaction);
 if(gammas.size()<=stages)return;
 assert(stages<5&&gammaMask==(1u<<(stages%4)));
 assert(expansion==0xf0&&modes==std::vector<unsigned>({0xf0}));
 const int rows[]={16,32,32,32,32};
 const int patternHeight=panelHeight-20;
 for(int row=0;row<5;++row){
  std::set<uint16_t> distinct;
  int y=20+(row*patternHeight/5+(row+1)*patternHeight/5)/2;
  for(int x=0;x<panelWidth;++x)distinct.insert(pixels[y*panelWidth+x]);
  assert(distinct.size()==static_cast<unsigned>(rows[row]));
  assert(pixels[y*panelWidth]==0);
 }
 for(int y=20;y<panelHeight;++y)for(int x=0;x<panelWidth;++x)
  assert(covered[y*panelWidth+x]==1);
 assert(pixels[20*panelWidth+panelWidth/16]==0x0020);
 assert(pixels[(20+2*patternHeight/5)*panelWidth+panelWidth-1]==0xf800);
 assert(pixels[(20+3*patternHeight/5)*panelWidth+panelWidth-1]==0x03e0);
 assert(pixels[(20+4*patternHeight/5)*panelWidth+panelWidth-1]==0x001f);
 std::vector<uint16_t> pattern(pixels.begin()+20*panelWidth,pixels.end());
 if(!stages)firstPattern=pattern;
 else assert(pattern==firstPattern);
 const unsigned labels[4][5]={{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1}};
 for(int y=0;y<20;++y)for(int x=0;x<panelWidth;++x){
  bool lit=x>=4&&x<13&&y>=2&&y<17&&
   (labels[stages%4][(y-2)/3]&(1u<<(2-(x-4)/3)));
  assert(pixels[y*panelWidth+x]==(lit?0xffff:0));
 }
 const char* output=std::getenv("RAW_PATTERN_PPM");
 if(output&&panelWidth==280&&!inputCase){
  char filename[1024];std::snprintf(filename,sizeof(filename),"%s-%u.ppm",output,stages+1);
  FILE* file=std::fopen(filename,"wb");assert(file);
  std::fprintf(file,"P6\n%d %d\n255\n",panelWidth,panelHeight);
  for(uint16_t color:pixels){
   unsigned char rgb[]={static_cast<unsigned char>(((color>>11)&31)*255/31),
    static_cast<unsigned char>(((color>>5)&63)*255/63),
    static_cast<unsigned char>((color&31)*255/31)};
   assert(std::fwrite(rgb,1,3,file)==3);
  }
  std::fclose(file);
 }
 ++stages;
}
namespace lilka {
namespace colors{constexpr uint16_t Black=0;}
struct Button{bool justPressed=false;};
struct Input{Button a,b;};
struct Controller{
 bool oldA=false,oldB=false;
 Input getState(){
  observeFrame();
  static const bool normal[][2]={
   {true,false},{true,false},{false,false},{true,false},
   {true,false},{false,false},{true,false},{false,false},
   {true,false},{false,false},{true,false},{false,false},{true,true}};
  static const bool early[][2]={{false,false},{false,true}};
  const unsigned size=inputCase?2:13;assert(inputIndex<size);
  const auto& next=inputCase?early[inputIndex++]:normal[inputIndex++];
  Input result;result.a.justPressed=next[0]&&!oldA;result.b.justPressed=next[1]&&!oldB;
  oldA=next[0];oldB=next[1];return result;
 }
};
inline Controller controller;
struct Devices{bool internalOutput(){return active;}};
inline Devices devices;
struct Serial{void log(const char*){}};
inline Serial serial;
struct Bus{
 void beginWrite(){assert(!transaction);transaction=true;}
 void endWrite(){assert(transaction);transaction=false;}
 void writeC8D16(uint8_t command,uint16_t data){
  assert(transaction&&command==0xb0&&data==0x00f0);
  expansion=data;modes.push_back(data);
 }
 void writeC8D8(uint8_t command,uint8_t data){
  assert(transaction&&command==0x26&&(data==1||data==2||data==4||data==8));
  gammaMask=data;gammas.push_back(data);
 }
 void writeRepeat(uint16_t color,uint32_t count){
  assert(transaction&&count==static_cast<uint32_t>(rectWidth*rectHeight));
  ++writes;
  for(int y=top;y<top+rectHeight;++y)for(int x=left;x<left+rectWidth;++x){
   assert(x>=0&&x<panelWidth&&y>=0&&y<panelHeight);
   pixels[y*panelWidth+x]=color;++covered[y*panelWidth+x];
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
 assert(argc==5);panelWidth=std::atoi(argv[1]);panelHeight=std::atoi(argv[2]);
 inputCase=std::atoi(argv[4]);
 active=std::atoi(argv[3]);pixels.assign(panelWidth*panelHeight,0xabcd);
 covered.assign(pixels.size(),0);
 lilka::Display display;display.showNativeGrayDiagnostic();assert(!transaction);
 if(active){
  assert(stages==(inputCase?1u:5u)&&expansion==0xf0&&gammaMask==1);
  assert(inputIndex==(inputCase?2u:13u));
  assert(modes==std::vector<unsigned>({0xf0,0xf0}));
  assert(gammas==(inputCase?std::vector<unsigned>({1,1}):std::vector<unsigned>({1,2,4,8,1,1})));
  for(uint16_t pixel:pixels)assert(pixel==0);
 }else{
  assert(!stages&&!writes&&modes.empty()&&gammas.empty());
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
    for width, height, active, input_case in (
        (280, 240, 1, 0), (240, 280, 1, 0), (280, 240, 0, 0), (280, 240, 1, 1)
    ):
        subprocess.run([str(binary), str(width), str(height), str(active), str(input_case)], check=True)
print('PASS: A cycles/wraps without hold-repeat, boot edge consumed, B wins/exits, labels/pattern/F0/restoration, inactive skip')
