#!/usr/bin/env python3
"""Exercise production native DMA chunking, fallback and profile stages under a host HAL."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'lib/lilka/src/lilka'
MOCK = r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#define LILKA_VERSION 2
#define CONFIG_IDF_TARGET_ESP32S3 1
#define LILKA_DISPLAY_DC 1
#define LILKA_DISPLAY_CS 2
#define LILKA_SPI_SCK 3
#define LILKA_SPI_MOSI 4
#define LILKA_SPI_MISO 5
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_DMA 2
#define ESP_OK 0
#define ETS_SPI3_INTR_SOURCE 3
#define GDMA_CHANNEL_DIRECTION_TX 1
#define GDMA_MAKE_TRIGGER(a,b) (b)
#define GDMA_TRIG_PERIPH_SPI 3
#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY 0xffffffff
#define portYIELD_FROM_ISR() ((void)0)
using BaseType_t=int;
inline int SPI1=1;
inline unsigned fallback=0, resets=0, timerCalls=0;
inline bool failWait=false,failAllocate=false;
inline std::vector<void*> allocated;
inline int64_t esp_timer_get_time(){++timerCalls;return int64_t(timerCalls)*3;}
inline void* heap_caps_malloc(size_t n,int){if(failAllocate)return nullptr;void*p=std::calloc(1,n);allocated.push_back(p);return p;}
inline void heap_caps_free(void*p){for(auto&v:allocated)if(v==p)v=nullptr;std::free(p);}
using SemaphoreHandle_t=bool*;
inline SemaphoreHandle_t doneSemaphore=nullptr;
inline SemaphoreHandle_t xSemaphoreCreateBinary(){doneSemaphore=new bool(false);return doneSemaphore;}
inline int xSemaphoreTake(SemaphoreHandle_t p,unsigned){if(failWait)return 0;assert(*p);*p=false;return 1;}
inline void xSemaphoreGiveFromISR(SemaphoreHandle_t p,int*){*p=true;}
inline void vSemaphoreDelete(SemaphoreHandle_t p){delete p;doneSemaphore=nullptr;}
inline void* xTaskGetCurrentTaskHandle(){return (void*)1;}
inline void vTaskDelay(unsigned){assert(false);}
struct Restart{};
inline void esp_restart(){++resets;throw Restart{};}
using gdma_channel_handle_t=void*; using intr_handle_t=void*;
struct gdma_channel_alloc_config_t{int direction;};
struct lldesc_t{uint8_t*buf;unsigned length,size,owner,eof;};
inline lldesc_t* pendingDescriptor=nullptr;
inline std::vector<uint8_t> wire;
inline void(*interruptHandler)(void*)=nullptr;inline void* interruptContext=nullptr;
inline void launch();
struct Trigger{operator unsigned()const{return 0;}void operator=(unsigned v){if(v)launch();}};
struct AutoClear{operator unsigned()const{return 0;}void operator=(unsigned){}};
struct Reg{uint32_t val=0;unsigned dma_rx_ena=0,dma_tx_ena=0,dma_afifo_rst=0,usr_mosi_highpart=0,usr_miso=0,usr_mosi=0,ms_data_bitlen=0,trans_done=0;};
struct Device{Reg dma_conf,user,ms_dlen,dma_int_raw,dma_int_ena,dma_int_clr;struct {AutoClear update;Trigger usr;}cmd;};
inline Device GPSPI3;
inline void launch(){assert(pendingDescriptor);auto*d=pendingDescriptor;assert(d->owner&&d->eof&&d->length<=4092&&d->size%4==0);wire.insert(wire.end(),d->buf,d->buf+d->length);GPSPI3.dma_int_raw.trans_done=1;interruptHandler(interruptContext);}
inline int gdma_new_channel(gdma_channel_alloc_config_t*,void**p){*p=(void*)1;return 0;}
inline int gdma_connect(void*,int){return 0;}inline void gdma_disconnect(void*){}inline void gdma_del_channel(void*){}
inline int gdma_reset(void*){return 0;}inline int gdma_stop(void*){return 0;}
inline int gdma_start(void*,intptr_t p){pendingDescriptor=(lldesc_t*)p;return 0;}
inline int esp_intr_alloc(int,int,void(*fn)(void*),void*ctx,void**p){interruptHandler=fn;interruptContext=ctx;*p=(void*)1;return 0;}
class Arduino_HWSPI {public:template<class...T>Arduino_HWSPI(T...){}virtual~Arduino_HWSPI(){}virtual void beginWrite(){}virtual void endWrite(){}virtual void writePixels(uint16_t*,uint32_t){++fallback;}};
namespace lilka {struct Serial {void err(const char*){}};inline Serial serial;}
'''
TEST = r'''
#include "mock.h"
#include "pixel_dma.h"
#include "display_profile.h"
int main(){
    lilka::PixelDMABus bus;
    assert(bus.enablePixelDMA());
    std::vector<uint16_t> pixels(5000);for(unsigned i=0;i<pixels.size();++i)pixels[i]=i*17;
    bus.beginWrite();bus.writePixels(pixels.data(),pixels.size());bus.endWrite();
    assert(wire.size()==pixels.size()*2);
    for(unsigned i=0;i<pixels.size();++i){assert(wire[2*i]==uint8_t(pixels[i]>>8));assert(wire[2*i+1]==uint8_t(pixels[i]));}
    bus.beginWrite();bus.writePixels(pixels.data(),32);bus.endWrite();
    bus.writePixels(pixels.data(),100);assert(fallback==2);
#if LILKA_MEASURE_DISPLAY
    using namespace lilka::displayProfile;
    auto p=sample(Stage::NativePack),f=sample(Stage::NativeFeed),w=sample(Stage::NativeWait);
    assert(p.n==3&&f.n==3&&w.n==3&&p.bytes==10000&&p.totalUs>0&&f.totalUs>0&&w.totalUs>0);
#else
    assert(timerCalls==0);
#endif
    failWait=true;bus.beginWrite();
    try{bus.writePixels(pixels.data(),100);assert(false);}catch(const Restart&){assert(resets==1);}
    failWait=false;
    failAllocate=true;lilka::PixelDMABus fallbackBus;assert(!fallbackBus.enablePixelDMA());fallbackBus.writePixels(pixels.data(),100);assert(fallback==3);
    vSemaphoreDelete(doneSemaphore);for(void*p:allocated)std::free(p);
}
'''
with tempfile.TemporaryDirectory(prefix='lilka-native-profile-') as temporary:
    tmp=Path(temporary)
    (tmp/'mock.h').write_text(MOCK)
    for name in ('pixel_dma.cpp','pixel_dma.h','display_profile.h','rgb565_pack.h'):
        (tmp/name).write_text((SOURCE/name).read_text())
    for name in ('config.h','Arduino_GFX_Library.h','spi.h','serial.h','esp_timer.h','esp_heap_caps.h','esp_intr_alloc.h',
                 'esp_system.h','esp_private/gdma.h','freertos/FreeRTOS.h','freertos/semphr.h',
                 'soc/spi_struct.h','soc/lldesc.h','soc/interrupts.h'):
        p=tmp/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#pragma once\n#include "mock.h"\n')
    (tmp/'test.cpp').write_text(TEST)
    for enabled in (0,1):
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie',
                        '-DLILKA_MEASURE_DISPLAY='+str(enabled),'-I'+str(tmp),str(tmp/'pixel_dma.cpp'),str(tmp/'test.cpp'),
                        '-o',str(tmp/'test')],check=True)
        subprocess.run([str(tmp/'test')],check=True)
print('Native DMA: real chunk loop, pixel parity, profiling on/off, fallback and timeout retention passed')
