#!/usr/bin/env python3
"""Host-test production brightness code, hardware gating, persistence and chords."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'lib/lilka/src/lilka'
U8G2 = ROOT / 'lib/lilka/.pio/libdeps/v2/U8g2/src'
MOCK = r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
#include <climits>
using SemaphoreHandle_t=void*; using TaskHandle_t=void*; using BaseType_t=int;
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define pdMS_TO_TICKS(x) (x)
constexpr int portMAX_DELAY=-1,pdPASS=1,ESP_OK=0;
extern uint32_t now,stored,duty; extern int reads,writes,locks,pinHigh,fail;
extern bool failSave; extern void (*writeHook)();
inline void portENTER_CRITICAL(int*) {} inline void portEXIT_CRITICAL(int*) {}
inline uint32_t millis(){return now;}
inline void* xSemaphoreCreateMutex(){return reinterpret_cast<void*>(1);}
inline void xSemaphoreTake(void*,int){++locks;}
inline void xSemaphoreGive(void*){--locks;assert(locks==0);}
inline void vTaskDelay(int ms){now+=ms;}
inline int xTaskCreate(void(*)(void*),const char*,int,void*,int,void**){return fail==3?0:1;}
using gpio_num_t=int;constexpr int GPIO_MODE_OUTPUT=1;
inline int gpio_reset_pin(int){return 0;}
inline int gpio_set_direction(int,int){return 0;}
inline int gpio_set_level(int,int value){pinHigh=value;return 0;}
using ledc_mode_t=int;using ledc_channel_t=int;
constexpr int LEDC_LOW_SPEED_MODE=0,LEDC_CHANNEL_7=7,LEDC_TIMER_3=3;
constexpr int LEDC_TIMER_8_BIT=8,LEDC_AUTO_CLK=0,LEDC_INTR_DISABLE=0;
struct ledc_timer_config_t{int speed_mode,timer_num,duty_resolution,freq_hz,clk_cfg;};
struct ledc_channel_config_t{int speed_mode,channel,timer_sel,intr_type,gpio_num;uint32_t duty;};
inline int ledc_timer_config(const ledc_timer_config_t* p){assert(p->freq_hz==20000&&p->timer_num==3);return fail==1?-1:0;}
inline int ledc_channel_config(const ledc_channel_config_t* p){assert(p->gpio_num==46&&p->channel==7);duty=p->duty;return fail==2?-1:0;}
inline int ledc_set_duty(int,int,uint32_t d){if(fail==4)return -1;duty=d;return 0;}
inline int ledc_update_duty(int,int){return 0;}
inline int ledc_stop(int,int,int level){pinHigh=level;return 0;}
class Preferences{public:
 bool begin(const char* name,bool){assert(!strcmp(name,"backlight"));return true;}
 uint32_t getUInt(const char* key,uint32_t){assert(!strcmp(key,"level"));++reads;return stored;}
 unsigned putUInt(const char* key,uint32_t value){assert(!strcmp(key,"level")&&locks==0);++writes;if(writeHook){auto h=writeHook;writeHook=nullptr;h();}if(failSave)return 0;stored=value;return 4;}
 void end(){}
};
'''
TEST = r'''
#include "mock.h"
#define private public
#include "brightness.h"
#undef private
#include "system_shortcuts.h"
#include <cstdio>
uint32_t now=0,stored=37,duty=0;int reads=0,writes=0,locks=0,pinHigh=0,fail=0;
bool failSave=false;void(*writeHook)()=nullptr;
int main(int argc,char** argv){
 using lilka::brightness;
 const int scenario=argc>1?argv[1][0]-'0':0;
 if(scenario>=1&&scenario<=3){fail=scenario;assert(!brightness.begin());assert(!brightness.isEnabled()&&pinHigh==1);return 0;}
 if(scenario==5)stored=0;
#if LILKA_INDEPENDENT_BACKLIGHT && LILKA_VERSION==2
 assert(brightness.begin());assert(brightness.begin());assert(reads==1);
 assert(brightness.getBrightness()==(scenario==5?5:37));
 assert(brightness.isPWMChannelReserved(6)&&brightness.isPWMChannelReserved(7));
 assert(!brightness.isPWMChannelReserved(5));
 brightness.setBrightness(100);assert(duty==256&&writes==0);
 brightness.setBrightness(0);assert(duty==13);brightness.stepBrightnessShortcut(1);
 assert(brightness.getBrightness()==10);brightness.stepBrightnessShortcut(-1);assert(brightness.getBrightness()==5);
 brightness.changeBrightnessLive(INT_MAX);assert(brightness.getBrightness()==100);
 brightness.changeBrightnessLive(INT_MIN);assert(brightness.getBrightness()==5);
 brightness.setBrightness(60);assert(brightness.suspend()&&duty==0);
 brightness.setBrightness(30);assert(duty==0&&brightness.getBrightness()==30);
 assert(brightness.resume()&&duty==77);
 fail=4;assert(!brightness.setBrightness(20));assert(brightness.getBrightness()==30);fail=0;
 auto view=brightness.getOverlay();assert(view.valid&&view.brightness&&view.level==30&&view.visible(now));
 brightness.servicePersistence();assert(writes==0);
 now+=601;brightness.servicePersistence();assert(writes==1&&stored==30);
 brightness.setBrightness(40);now+=601;failSave=true;brightness.servicePersistence();assert(stored==30);
 failSave=false;brightness.servicePersistence();assert(stored==40);
 brightness.setBrightness(50);now+=601;writeHook=[](){lilka::brightness.setBrightness(70);};brightness.servicePersistence();assert(stored==50);
 now+=601;brightness.servicePersistence();assert(stored==70);
 brightness.setBrightness(0);now+=601;brightness.servicePersistence();assert(stored==5&&brightness.getBrightness()==5&&duty==13);
 brightness.setBrightness(75);now+=601;brightness.servicePersistence();const int savedWrites=writes;
 assert(brightness.dim()&&brightness.isDimmed()&&duty==13&&brightness.getBrightness()==75);
 now+=601;brightness.servicePersistence();assert(writes==savedWrites&&stored==75);
 assert(brightness.undim()&&!brightness.isDimmed()&&duty==191);
 assert(brightness.dim()&&brightness.suspend()&&duty==0&&!brightness.isDimmed());
 assert(brightness.resume()&&duty==191);
#else
 assert(!brightness.begin()&&!brightness.isEnabled());
 assert(!brightness.setBrightness(0)&&brightness.getBrightness()==100&&reads==0&&writes==0);
#endif
 lilka::detail::SystemShortcuts chords;
 constexpr uint16_t sel=1<<8,left=1<<2,right=1<<3,start=1<<9;
 chords.scan(sel,10,true,true,true);
 auto c=chords.scan(sel|left,20,true,true,true);
 assert(c.brightnessSteps==-1&&c.volumeSteps==0&&c.selectConsumed&&(c.suppressed&left));
 assert(chords.scan(sel|left,419,true,true,true).brightnessSteps==0);
 assert(chords.scan(sel|left,420,true,true,true).brightnessSteps==-1);
 assert(chords.scan(sel|left|start,430,true,true,true).brightnessSteps==0);
 chords.scan(0,440,true,true,true);chords.scan(sel,450,true,true,true);
 assert(chords.scan(sel|right,460,true,true,true).brightnessSteps==1);
 lilka::detail::SystemShortcuts stock;stock.scan(sel,0,true);c=stock.scan(sel|left,20,true);
 assert(c.brightnessSteps==0&&!(c.suppressed&left));
 puts("backlight host checks passed");
}
'''
with tempfile.TemporaryDirectory(prefix='lilka-backlight-') as directory:
    tmp = Path(directory)
    for name in ('brightness.cpp','brightness.h','config.h','volume_overlay.h','system_shortcuts.h','display_settings.h'):
        (tmp/name).write_text((SOURCE/name).read_text())
    (tmp/'mock.h').write_text(MOCK)
    (tmp/'regression.cpp').write_text(TEST)
    for name in ('Arduino.h','Preferences.h','driver/ledc.h','driver/gpio.h','freertos/semphr.h','freertos/FreeRTOS.h'):
        path=tmp/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('#pragma once\n#include "mock.h"\n')
    for version,enabled in ((2,1),(2,0),(1,1)):
        for sanitize in (False,True):
            flags=['-fsanitize=address,undefined','-fno-pie','-no-pie'] if sanitize else []
            command=[os.environ.get('CXX','g++'),'-std=c++11','-Wall','-Wextra','-Werror',*flags,
                     '-DLILKA_VERSION='+str(version),'-DLILKA_INDEPENDENT_BACKLIGHT='+str(enabled),
                     '-I'+str(tmp),'-I'+str(U8G2),str(tmp/'brightness.cpp'),str(tmp/'regression.cpp'),'-o',str(tmp/'regression')]
            subprocess.run(command,check=True)
            scenarios=('0','1','2','3','5') if version==2 and enabled else ('0',)
            for scenario in scenarios: subprocess.run([str(tmp/'regression'),scenario],check=True)
