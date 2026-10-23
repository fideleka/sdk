#!/usr/bin/env python3
"""Exercise production display routing on a deterministic native-LCD/Canvas model."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = root / 'lib/lilka/src/lilka'
mock = r'''
#pragma once
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cstring>
namespace lilka {
namespace colors { constexpr uint16_t Black=0; }
struct VolumeOverlaySnapshot { int level=0; uint32_t adjustedAt=0; bool valid=false; bool visible(uint32_t now)const{return valid&&now-adjustedAt<1200;} };
extern uint32_t now;
inline uint32_t millis(){return now;}
class Canvas {
    int w,h;
public:
    enum class Memory { Default, PSRAMOnly };
    std::vector<uint16_t> pixels;
    Canvas(int w,int h):w(w),h(h),pixels(w*h,0){}
    Canvas(int w,int h,Memory memory):Canvas(w,h){assert(memory==Memory::PSRAMOnly);}
    int width(){return w;}int height(){return h;}
    uint16_t*getFramebuffer(){return pixels.data();}
    void fillScreen(uint16_t color){std::fill(pixels.begin(),pixels.end(),color);}
    void drawPixel(int x,int y,uint16_t color){if(x>=0&&y>=0&&x<w&&y<h)pixels[y*w+x]=color;}
    void fillRect(int x,int y,int rw,int rh,uint16_t color){for(int yy=y;yy<y+rh;++yy)for(int xx=x;xx<x+rw;++xx)drawPixel(xx,yy,color);}
    void draw16bitRGBBitmap(int x,int y,const uint16_t*p,int rw,int rh){for(int yy=0;yy<rh;++yy)for(int xx=0;xx<rw;++xx)drawPixel(x+xx,y+yy,p[yy*rw+xx]);}
};
struct MockDevices {
    VolumeOverlaySnapshot getOverlay(){return {};}
    bool registered=false,internal=true,external=false,sleeping=false;
    uint32_t generation=1;unsigned captures=0;
    std::vector<uint16_t> published;
    void setPresentationReady(bool ready){if(!ready){internal=true;external=false;}}
    bool isSleeping()const{return sleeping;}
    bool hasRegisteredDriver()const{return registered;}
    bool internalOutput()const{return internal;}
    bool externalOutput()const{return external;}
    uint32_t invalidation()const{return generation;}
    bool capture(Canvas*,Canvas*c,const VolumeOverlaySnapshot&,bool){assert(c);++captures;published=c->pixels;return true;}
};
extern MockDevices devices;
struct MockSettings { bool sleeping=false; bool isSleeping()const{return sleeping;} unsigned calls=0;bool eligible=true;void serviceIdle(bool value){++calls;eligible=value;} };
extern MockSettings displaySettings;
struct MockAudio {VolumeOverlaySnapshot getVolumeOverlay(){return {};}};
extern MockAudio audio;
struct MockBrightness {VolumeOverlaySnapshot getOverlay(){return {};}};
extern MockBrightness brightness;
class Arduino_ST7789 {
public:
    unsigned begins=0,ends=0,writes=0;bool bus=false;
    int nx=0,ny=0,nw=0,nh=0;unsigned no=0;
    std::vector<uint16_t> native=std::vector<uint16_t>(280*240,0);
    bool panelOn=true;
    virtual void displayOn(){assert(!bus);panelOn=true;}
    virtual void displayOff(){assert(!bus);panelOn=false;}
    virtual void startWrite(){++begins;bus=true;}
    virtual void endWrite(){assert(bus);++ends;bus=false;}
    virtual void writeAddrWindow(int16_t x,int16_t y,uint16_t w,uint16_t h){nx=x;ny=y;nw=w;nh=h;no=0;}
    virtual void writePixels(uint16_t*p,uint32_t n){assert(bus);++writes;for(unsigned i=0;i<n;++i){if(no>=unsigned(nw*nh))break;int x=nx+no%nw,y=ny+no/nw;if(x>=0&&x<280&&y>=0&&y<240)native[y*280+x]=p[i];++no;}}
    virtual void writePixelPreclipped(int16_t x,int16_t y,uint16_t c){native[y*280+x]=c;}
    virtual void writeFillRectPreclipped(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c){for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)native[yy*280+xx]=c;}
    virtual void writeFastHLine(int16_t,int16_t,int16_t,uint16_t){}
    virtual void writeFastVLine(int16_t,int16_t,int16_t,uint16_t){}
    void writeFillRect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c){int r=std::min(280,int(x+w)),b=std::min(240,int(y+h));x=std::max<int16_t>(0,x);y=std::max<int16_t>(0,y);if(r>x&&b>y)writeFillRectPreclipped(x,y,r-x,b-y,c);}
    virtual void draw16bitRGBBitmap(int16_t x,int16_t y,uint16_t*p,int16_t w,int16_t h){startWrite();writeAddrWindow(x,y,w,h);writePixels(p,w*h);endWrite();}
    virtual void draw16bitRGBBitmap(int16_t,int16_t,const uint16_t*,int16_t,int16_t){}
};
class Display:public Arduino_ST7789 {
public:
    int width(){return 280;}int height(){return 240;}
    void serviceOutputPower();void displayOn()override;void displayOff()override;
    bool enableRouting();void setLayeredPresentation(bool);void beginFrame();void endFrame();
    void startWrite()override;void endWrite()override;
    void writeAddrWindow(int16_t,int16_t,uint16_t,uint16_t)override;
    void writePixels(uint16_t*,uint32_t)override;
    void writePixelPreclipped(int16_t,int16_t,uint16_t)override;
    void writeFillRectPreclipped(int16_t,int16_t,int16_t,int16_t,uint16_t)override;
    void writeFastHLine(int16_t,int16_t,int16_t,uint16_t)override;
    void writeFastVLine(int16_t,int16_t,int16_t,uint16_t)override;
    void draw16bitRGBBitmap(int16_t,int16_t,uint16_t*,int16_t,int16_t)override;
    void draw16bitRGBBitmap(int16_t,int16_t,const uint16_t*,int16_t,int16_t)override;
    void presentNativePixels(int16_t,int16_t,uint16_t*,int16_t,int16_t);
    ~Display(){delete routedCanvas;}
private:
    Canvas*routedCanvas=nullptr;
    bool layeredPresentation=false,routingDirty=false,nativeBypass=false,nativeWrite=true,routingStarted=false,routingAllocationFailed=false,nativePanelOn=true;
    bool idleEligible=true;
    unsigned frameDepth=0,writeDepth=0;
    int16_t windowX=0,windowY=0;uint16_t windowWidth=0,windowHeight=0;
    uint32_t windowOffset=0,routeGeneration=0;
    bool nativeOutput()const;Canvas*routingCanvas();void publishRouting();
};
}
'''
header = (source/'display.h').read_text()
setter_start = header.index('    void setIdleEligible(')
setter_end = header.index('\n    }', setter_start) + len('\n    }')
mock = mock.replace('    void startWrite()override;void endWrite()override;', header[setter_start:setter_end] + '\n    void startWrite()override;void endWrite()override;')
checks = r'''
#include "display.h"
namespace lilka { uint32_t now=0;MockDevices devices;MockSettings displaySettings;MockAudio audio;MockBrightness brightness; }
int main(){using namespace lilka;
 Display output;
 uint16_t pixels[]={1,2,3,4,5,6};
 assert(output.enableRouting());
 output.draw16bitRGBBitmap(2,3,pixels,3,2);assert(devices.captures==0&&output.native[3*280+2]==1);
 devices.registered=true;devices.internal=false;devices.external=true;++devices.generation;
 output.beginFrame();
 output.writeFillRectPreclipped(0,0,280,240,42);
 output.draw16bitRGBBitmap(-1,3,pixels,3,2);
 assert(devices.captures==0);output.endFrame();
 assert(!output.panelOn);
 assert(devices.captures==1&&devices.published[0]==42&&devices.published[3*280]==2&&devices.published[4*280+1]==6);
 const auto retainedNative=output.native;
 // Doom-style sequential scanlines fill one address window, with no per-row publication.
 output.beginFrame();output.startWrite();output.writeAddrWindow(10,20,3,2);
 output.writePixels(pixels,3);output.writePixels(pixels+3,3);output.endWrite();
 assert(devices.captures==1);output.endFrame();assert(devices.captures==2);
 assert(devices.published[20*280+10]==1&&devices.published[21*280+12]==6&&output.native==retainedNative);
 // Mode change restores the retained native frame on the presentation owner.
 devices.internal=true;++devices.generation;output.beginFrame();output.endFrame();
 assert(output.native==devices.published&&devices.captures==3&&!output.bus&&output.panelOn);
 // Route may change while a physical native transaction is open: end must still match start.
 output.beginFrame();output.startWrite();const auto starts=output.begins;
 devices.internal=false;output.writeAddrWindow(0,0,3,1);output.writePixels(pixels,3);output.endWrite();output.endFrame();
 assert(output.begins==starts&&output.ends==output.begins&&!output.bus);
 // Sleep suppresses native transfers; wake/mode invalidation restores the latest retained pixels.
 devices.internal=true;devices.sleeping=true;displaySettings.sleeping=true;
 const auto writes=output.writes;output.beginFrame();output.draw16bitRGBBitmap(0,0,pixels,3,2);output.endFrame();assert(output.writes==writes);
 devices.sleeping=false;displaySettings.sleeping=false;++devices.generation;output.beginFrame();output.endFrame();assert(output.native[280+2]==6);
 // Doom's startup, loading, gameplay and paused/static frames remain ineligible,
 // including a frame after a gap longer than any saved timeout. Nested frames
 // service timers once, and re-enabling the policy restores the default behavior.
 output.setIdleEligible(false);
 for(unsigned gap:{16u,30000u,60000u,3600001u}){
  now+=gap;auto calls=displaySettings.calls;
  output.beginFrame();output.beginFrame();output.endFrame();output.endFrame();
  assert(displaySettings.calls==calls+1&&!displaySettings.eligible);
 }
 output.setIdleEligible(true);output.beginFrame();output.endFrame();assert(displaySettings.eligible);
 // Shared-compositor path avoids a third retained framebuffer and recursive capture.
 output.setLayeredPresentation(true);const auto captures=devices.captures;
 output.presentNativePixels(0,0,pixels,3,2);assert(devices.captures==captures&&output.native[280+2]==6);
 puts("PASS: native-only, clipped rectangles/bitmaps, immutable complete-frame publication, Doom scanlines, route latching, sleep/wake and compositor bypass");
}
'''
with tempfile.TemporaryDirectory(prefix='sdk-display-route-') as directory:
    tmp = Path(directory)
    (tmp/'display.h').write_text(mock)
    for name in ['devices.h','display_settings.h','audio.h','brightness.h']:
        (tmp/name).write_text('#pragma once\n#include "display.h"\n')
    (tmp/'display_routing.cpp').write_text((source/'display_routing.cpp').read_text())
    (tmp/'test.cpp').write_text(checks)
    for flags in [[], ['-fsanitize=address,undefined','-fno-pie','-no-pie']]:
        subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror',*flags,'-I'+str(tmp),str(tmp/'display_routing.cpp'),str(tmp/'test.cpp'),'-o',str(tmp/'test')],check=True)
        subprocess.run([str(tmp/'test')],check=True)
