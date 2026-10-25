#!/usr/bin/env python3
"""Exercise production manager/driver lifecycle, NVS identity, frames and ELF bounds with host peripherals."""
from pathlib import Path
import subprocess
import tempfile
import sys
import os

root = Path(__file__).resolve().parents[2]
sources = root / "lib/lilka/src/lilka"
mock = r'''
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <algorithm>
#include <functional>
#define LILKA_VERSION 2
#define LILKA_P8 12
#define LILKA_P7 13
#define LILKA_P6 14
#define LILKA_P5 21
#define LILKA_P4 47
#define LILKA_P3 48
using TaskHandle_t=void*;
using BaseType_t=int;
using UBaseType_t=unsigned;
#define configMAX_PRIORITIES 25
#define tskNO_AFFINITY 0x7fffffff
#define portNUM_PROCESSORS 2
#define pdPASS 1
inline int createdCore=-1, createdPriority=-1;
inline bool failWorker=false;
inline int xTaskCreatePinnedToCore(void(*)(void*),const char*,unsigned,void*,unsigned priority,TaskHandle_t*handle,int core){createdCore=core;createdPriority=priority;if(failWorker)return 0;*handle=reinterpret_cast<void*>(1);return pdPASS;}
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_EXEC 4
#define MALLOC_CAP_INTERNAL 8
#define MALLOC_CAP_DMA 16
#define SOC_IRAM_LOW 0x40370000
#define SOC_IRAM_HIGH 0x403e0000
inline std::map<void*,size_t> allocations;
inline bool failFrame=false, failNvs=false, stopWorker=false, failTransfer=false, busBusy=false;
inline bool failDma=false;
inline unsigned frameAllocations=0, failFrameAllocation=0;
inline std::function<void()> duringDma;
inline int busError=0,deviceError=0;
inline unsigned fakeTime=1000, resetCount=0, starts=0;
inline unsigned busInitializations=0, deviceRemovals=0, nvsAttempts=0, busFailures=0;
inline const void* inFlight=nullptr;
inline std::map<std::string,std::vector<uint8_t>> nvm;
inline void* heap_caps_malloc(size_t n,int caps){if(n>=134400 && ++frameAllocations==failFrameAllocation)return nullptr;if((failFrame && n>=134400)||(failDma&&n==8192))return nullptr;void*p=std::malloc(n);if(p)allocations[p]=n;return p;}
inline void* heap_caps_calloc(size_t n,size_t s,int caps){void*p=heap_caps_malloc(n*s,caps);if(p)std::memset(p,0,n*s);return p;}
inline void heap_caps_free(void*p){if(!p)return;assert(allocations.erase(p));std::free(p);}
inline void* heap_caps_realloc(void*p,size_t n,int caps){if(!p)return heap_caps_malloc(n,caps);assert(allocations.erase(p));p=std::realloc(p,n);if(p)allocations[p]=n;return p;}
inline size_t internalFree=1000000, largestExec=1000000;
inline unsigned relocationAttempts=0;
inline size_t heap_caps_get_free_size(int){return internalFree;}
inline size_t heap_caps_get_largest_free_block(int caps){return caps==MALLOC_CAP_EXEC?largestExec:1000000;}
inline size_t heap_caps_get_minimum_free_size(int){return 1000000;}
inline size_t uxTaskGetStackHighWaterMark(void*){return 5000;}
inline uint32_t millis(){return fakeTime;}
inline int64_t esp_timer_get_time(){return fakeTime*1000;}
struct Stop{};
inline void vTaskDelay(unsigned ms){fakeTime+=ms;if(stopWorker)throw Stop{};}
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
#define portMAX_DELAY 0xffffffff
using SemaphoreHandle_t=bool*;
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return new bool(false);}
inline int xSemaphoreTake(SemaphoreHandle_t p,unsigned ticks){if(*p){assert(!ticks);return 0;}*p=true;return 1;}
inline void xSemaphoreGive(SemaphoreHandle_t p){assert(*p);*p=false;}
inline void vSemaphoreDelete(SemaphoreHandle_t p){delete p;}
#define KMTX_LOCK(x) assert(xSemaphoreTake(x,portMAX_DELAY))
#define KMTX_UNLOCK(x) xSemaphoreGive(x)
#define NVS_LOCK
#define NVS_UNLOCK
class String:public std::string {
public:using std::string::string;
 String(const std::string&s):std::string(s){}
 void toCharArray(char*p,size_t n)const{std::snprintf(p,n,"%s",c_str());}
};
inline String StringFormat(const char*f,...){char b[128];va_list v;va_start(v,f);vsnprintf(b,sizeof(b),f,v);va_end(v);return String(b);}
inline size_t strlcpy(char*d,const char*s,size_t n){size_t l=std::strlen(s);if(n){size_t c=std::min(l,n-1);std::memcpy(d,s,c);d[c]=0;}return l;}
class Preferences {
public:
 bool begin(const char*,bool){return true;}void end(){}
 bool isKey(const char*k){return nvm.count(k);}
 size_t getBytesLength(const char*k){return nvm[k].size();}
 size_t getBytes(const char*k,void*p,size_t n){n=std::min(n,nvm[k].size());std::memcpy(p,nvm[k].data(),n);return n;}
 size_t putBytes(const char*k,const void*p,size_t n){++nvsAttempts;if(failNvs)return 0;nvm[k]=std::vector<uint8_t>((const uint8_t*)p,(const uint8_t*)p+n);return n;}
 uint8_t getUChar(const char*k,uint8_t d){return nvm.count(k)&&nvm[k].size()?nvm[k][0]:d;}
 size_t putUChar(const char*k,uint8_t v){return putBytes(k,&v,1);}
 String getString(const char*k,const char*d){return nvm.count(k)?String((const char*)nvm[k].data()):String(d);}
 size_t putString(const char*k,const char*v){return putBytes(k,v,std::strlen(v)+1);}
};
enum {KT_PRIO_DEFAULT=1};
class Service{public:void setktPriority(unsigned p){assert(p==KT_PRIO_DEFAULT);}Service(const char*){}virtual~Service(){}virtual void run(){}void setktStackSize(unsigned){}};
namespace lilka {
struct Serial{template<class...T>void log(const char*,T...){}template<class...T>void err(const char*,T...){}};
inline Serial serial;
struct Brightness {
 bool enabled=true;int pin=-1;bool native=true,external=false;
 bool isEnabled(){return enabled;}
 bool attachExternalBacklight(int value, bool initiallyActive=true){if(!enabled||pin>=0)return false;pin=value;external=initiallyActive;return true;}
 void detachExternalBacklight(){pin=-1;}
 bool setActiveDisplays(bool n,bool e){native=n;external=e;return true;}
};
inline Brightness brightness;
struct Display{
 int w=280,h=240;unsigned writes=0;const uint16_t* lastSource=nullptr;
 std::vector<uint16_t> pixels=std::vector<uint16_t>(280*240);
 int width(){return w;}int height(){return h;}
 void draw16bitRGBBitmap(int,int,const uint16_t*,int,int){assert(false && "Slow flash/const bitmap path");}
 void presentNativePixels(int x,int y,uint16_t* p,int rw,int rh){
  assert(x==0 && rw==w);lastSource=p;++writes;
  std::copy(p,p+rw*rh,pixels.begin()+y*w);
 }
};
inline Display display;
class Canvas{std::vector<uint16_t>data;int px,py,w,h;public:Canvas(int x,int y,int width,int height):data(width*height),px(x),py(y),w(width),h(height){}int width(){return w;}int height(){return h;}int x(){return px;}int y(){return py;}uint16_t*getFramebuffer(){return data.data();}};
struct VolumeOverlaySnapshot{bool valid=false;uint32_t adjustedAt=0;int displayMode=-1;char muteLabel[32]={};bool visible(uint32_t now)const{return valid&&now-adjustedAt<1200;}};
struct VolumeOverlayGeometry{int x,y,width,height;};
inline VolumeOverlayGeometry volumeOverlayGeometry(int,int){return {100,100,40,20};}
struct VolumeOverlayRow{uint16_t*pixels;int x,y,width;void fillRect(int l,int t,int w,int h,uint16_t c){if(y<t||y>=t+h)return;for(int i=std::max(x,l);i<std::min(x+width,l+w);++i)pixels[i-x]=c;}};
template<class T>void drawVolumeOverlay(T&t,const VolumeOverlaySnapshot&s,int,int,uint32_t){if(s.valid)t.fillRect(100,100,40,20,0xffff);}
}
using gpio_num_t=int;
#define GPIO_MODE_OUTPUT 1
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
using esp_err_t=int;
inline const char* esp_err_to_name(int error){return error==ESP_ERR_NO_MEM?"ESP_ERR_NO_MEM":error==ESP_ERR_INVALID_ARG?"ESP_ERR_INVALID_ARG":error==ESP_ERR_INVALID_STATE?"ESP_ERR_INVALID_STATE":"ESP_FAIL";}
#define SPI2_HOST 2
#define SPI_DMA_CH_AUTO 0
#define APB_CLK_FREQ 80000000
inline int spi_get_actual_clock(int apb,int hz,int duty){assert(apb==80000000&&duty==128);return hz>60000000?80000000:hz>=40000000?40000000:hz;}
using spi_device_handle_t=void*;
struct spi_bus_config_t{int mosi_io_num,miso_io_num,sclk_io_num,quadwp_io_num,quadhd_io_num,data4_io_num,data5_io_num,data6_io_num,data7_io_num,max_transfer_sz;};
struct spi_device_interface_config_t{int clock_speed_hz,spics_io_num,queue_size;};
struct spi_transaction_t{unsigned length;const void*tx_buffer;};
inline int spi_bus_initialize(int host,spi_bus_config_t*b,int){++busInitializations;assert(host==2);if(busFailures){--busFailures;return ESP_ERR_INVALID_STATE;}assert(b->miso_io_num==-1&&b->quadwp_io_num==-1&&b->quadhd_io_num==-1&&b->data4_io_num==-1&&b->data5_io_num==-1&&b->data6_io_num==-1&&b->data7_io_num==-1);if(busError)return busError;if(busBusy)return -1;busBusy=true;return 0;}
inline int openedClock=0;
inline int spi_bus_add_device(int,spi_device_interface_config_t*config,spi_device_handle_t*p){if(deviceError)return deviceError;openedClock=config->clock_speed_hz;*p=(void*)1;return 0;}
inline int spi_bus_remove_device(void*){++deviceRemovals;return 0;}
inline int spi_bus_free(int){assert(!inFlight);busBusy=false;return 0;}
inline int submitError=0;inline bool wrongCompletion=false;
inline std::vector<uint8_t> queuedBytes, wireBytes;
inline int currentDc=0;
inline std::vector<std::pair<int,std::vector<uint8_t>>> wireChunks;
inline int failSubmissionAt=0;
inline int spi_device_queue_trans(void*,spi_transaction_t*t,unsigned wait){assert(wait==100);if(submitError)return submitError;if(failSubmissionAt && starts+1==failSubmissionAt)return -1;assert(!inFlight);inFlight=t;queuedBytes.assign((const uint8_t*)t->tx_buffer,(const uint8_t*)t->tx_buffer+(t->length+7)/8);++starts;return 0;}
inline int spi_device_get_trans_result(void*,spi_transaction_t**result,unsigned wait){assert(wait==100);if(failTransfer)return -1;assert(!memcmp(((spi_transaction_t*)inFlight)->tx_buffer,queuedBytes.data(),queuedBytes.size()));wireBytes.insert(wireBytes.end(),queuedBytes.begin(),queuedBytes.end());wireChunks.emplace_back(currentDc,queuedBytes);if(duringDma && ((spi_transaction_t*)inFlight)->length>=4096*8){auto callback=std::move(duringDma);duringDma=nullptr;callback();}*result=wrongCompletion?nullptr:(spi_transaction_t*)inFlight;inFlight=nullptr;return 0;}
inline int gpio_set_direction(int,int){return 0;}inline int gpio_set_level(int pin,int level){if(pin==21){assert(!inFlight);currentDc=level;}return 0;}
inline int gpio_reset_pin(int){++resetCount;return 0;}
struct System{};inline System ksystem;
'''
harness = r'''
#define private public
#include "lilka/devices.h"
#undef private
#include "lilka/devices.cpp"
using namespace lilka;
#include <fstream>
extern "C" int kd_driver_entry(int,char**);
int lilka_elf_init(lilka_elf_t*){return 0;}
int lilka_elf_relocate(lilka_elf_t*,const uint8_t*,size_t){++relocationAttempts;return -1;}
int lilka_elf_run(lilka_elf_t*,int,char**){return -1;}
void lilka_elf_deinit(lilka_elf_t*){}
static void registered(DeviceManager&m,unsigned i){
 auto&s=m.slots[i];s.owner=&m;
 strlcpy(s.uuid,"674db8e0-cd55-4bfb-90b0-1e521e97c1cd",37);
 strlcpy(s.driverUuid,"cfa4bc25-c2d6-4a18-ade9-16506b19acf1",37);
 s.host={sizeof(kd_host),KD_ABI_VERSION,&s,DeviceManager::registerDevice,DeviceManager::setting,
 DeviceManager::spiOpen,DeviceManager::gpioWrite,DeviceManager::spiWrite,DeviceManager::spiPixels,
 DeviceManager::spiClose,DeviceManager::delayMs};
 char*argv[]={reinterpret_cast<char*>(&s.host)};
 assert(!kd_driver_entry(1,argv));assert(DeviceManager::registerDevice(&s,s.descriptor));
 m.deviceCount=i+1;m.workerReady=true;
}
int main(int argc,char**argv){
 assert(argc==2);
 std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t>elf((std::istreambuf_iterator<char>(f)),{});
 assert(validateDriverElf(elf.data(),elf.size()));
 size_t codeBytes=123;
 assert(validateDriverElf(elf.data(),elf.size(),&codeBytes));assert(codeBytes>0&&codeBytes<8192&&codeBytes%4==0);
 // A small valid module must reach the checked allocator with Doom-sized free
 // RAM, even below the old 32 KiB aggregate and 8 KiB executable cutoffs.
 {DeviceManager m;internalFree=19212;largestExec=codeBytes;
  assert(!m.load(m.slots[0],argv[1]));assert(relocationAttempts==1);assert(allocations.empty());
  largestExec=codeBytes-1;assert(!m.load(m.slots[0],argv[1]));assert(relocationAttempts==1);
  assert(strstr(m.slots[0].status,"Insufficient executable RAM"));assert(allocations.empty());
  internalFree=largestExec=1000000;}
 codeBytes=123;assert(!validateDriverElf(elf.data(),1,&codeBytes));assert(codeBytes==0);

 for(size_t i=0;i<sizeof(lilka_elf32_hdr_t);++i)assert(!validateDriverElf(elf.data(),i));
 auto corrupt=elf;corrupt[32]=0xff;corrupt[33]=0xff;assert(!validateDriverElf(corrupt.data(),corrupt.size()));
 corrupt=elf;corrupt[18]=0;assert(!validateDriverElf(corrupt.data(),corrupt.size()));
 lilka_elf32_hdr_t header;memcpy(&header,elf.data(),sizeof(header));
 for(unsigned i=0;i<header.shnum;++i){lilka_elf32_shdr_t section;memcpy(&section,elf.data()+header.shoff+i*sizeof(section),sizeof(section));
  if(section.type==4){corrupt=elf;corrupt[section.offset+4]=0xff;assert(!validateDriverElf(corrupt.data(),corrupt.size()));}}
 {
 DeviceManager m;registered(m,0);
 busError=ESP_ERR_INVALID_STATE;
 assert(!m.setEnabled(0,true));assert(strstr(m.slots[0].status,"SPI bus init: ESP_ERR_INVALID_STATE"));assert(allocations.empty()&&!busBusy);
 busError=0;deviceError=ESP_ERR_INVALID_ARG;
 assert(!m.setEnabled(0,true));assert(strstr(m.slots[0].status,"SPI device setup: ESP_ERR_INVALID_ARG"));assert(allocations.empty()&&!busBusy);
 deviceError=0;failDma=true;
 assert(!m.setEnabled(0,true));assert(strstr(m.slots[0].status,"DMA buffer allocation: ESP_ERR_NO_MEM"));assert(allocations.empty()&&!busBusy);
 failDma=false;
 assert(m.internalOutput()&&!m.externalOutput());assert(allocations.empty());
 m.slots[0].values[5]=2;assert(m.setEnabled(0,true));assert(m.spiHz==40000000);assert(m.slots[0].active&&m.internalOutput()&&!m.frame);
 assert(m.extensionPinOwned(12)&&m.extensionPinOwned(13)&&m.extensionPinOwned(14)&&m.extensionPinOwned(21));
 assert(!m.extensionPinOwned(47)&&!m.extensionPinOwned(48));
 assert(m.setDisplayMode(DeviceManager::Both));assert(m.externalOutput()&&m.internalOutput());
 assert(m.frame&&allocations[m.frames[0]]==134400&&allocations[m.frames[1]]==134400);
 lilka::Canvas panel(0,0,280,24),old(0,24,280,216),small(20,40,40,30);
 std::fill(old.getFramebuffer(),old.getFramebuffer()+280*216,0x1234);
 std::fill(small.getFramebuffer(),small.getFramebuffer()+40*30,0x5678);
 lilka::VolumeOverlaySnapshot none;
 assert(m.capture(&panel,&old,none,false));assert(m.frame[100*280+100]==0x1234);
 assert(m.capture(&panel,&old,none,false)); // No elapsed-time cap.
 assert(m.setDisplayMode(DeviceManager::External));assert(m.setDisplayMode(DeviceManager::Both));
 assert(m.capture(&panel,&small,none,false));assert(m.frame[100*280+100]==0);assert(m.frame[40*280+20]==0x5678);
 assert(xSemaphoreTake(m.frameMutex,0));assert(!m.capture(&panel,&old,none,false));xSemaphoreGive(m.frameMutex);
 assert(m.capture(nullptr,nullptr,none,true));assert(m.frame[40*280+20]==0);
 assert(m.capture(nullptr,&small,none,false));
 stopWorker=true;try{m.run();}catch(Stop&){}stopWorker=false;assert(m.transferCount==1&&!m.pending);
 // Internal consumes the exact shared pointer, with no second capture/copy.
 m.presentInternal();assert(lilka::display.lastSource==m.frame);assert(lilka::display.pixels==std::vector<uint16_t>(m.frame,m.frame+280*240));
 const auto internalWrites=lilka::display.writes;m.presentInternal();assert(lilka::display.writes==internalWrites);
 // Exercise a producer DURING production external DMA. The driver lock is held,
 // but the compositor publishes many latest frames without touching leased pixels.
 std::fill(small.getFramebuffer(),small.getFramebuffer()+40*30,0x6789);
 assert(m.capture(nullptr,&small,none,false));
 duringDma=[&]{
  assert(*m.mutex && !*m.frameMutex && m.externalReading>=0);
  const auto* leased=m.frames[m.externalReading];
  const std::vector<uint16_t> stable(leased,leased+280*240);
  for(unsigned i=0;i<20;++i){
   small.getFramebuffer()[0]=0x8000+i;
   assert(m.capture(nullptr,&small,none,false));assert(m.frame!=leased);
   assert(std::equal(stable.begin(),stable.end(),leased));
   m.presentInternal();assert(lilka::display.pixels[40*280+20]==0x8000+i);
   assert(lilka::display.lastSource==m.frame+40*280);
  }
 };
 stopWorker=true;try{m.run();}catch(Stop&){}stopWorker=false;
 assert(!duringDma && m.externalReading==-1 && m.pending);
 assert(m.frame[40*280+20]==0x8013);
 // The next transfer takes the LATEST frame, not twenty queued stale frames.
 fakeTime+=100;stopWorker=true;try{m.run();}catch(Stop&){}stopWorker=false;assert(!m.pending);
 // Static captures queue no traffic; one changed scanline sends exactly one row.
 const auto baselineTransfers=m.transferCount;
 assert(m.capture(nullptr,&small,none,false)&&!m.pending);
 stopWorker=true;try{m.run();}catch(Stop&){}stopWorker=false;assert(m.transferCount==baselineTransfers);
 small.getFramebuffer()[0]=0xabcd;
 assert(m.capture(nullptr,&small,none,false)&&m.pending);
 const auto pixelsBefore=m.transferPixels,rowsBefore=m.rowTransfers;
 stopWorker=true;try{m.run();}catch(Stop&){}stopWorker=false;
 assert(m.transferPixels-pixelsBefore==280&&m.rowTransfers==rowsBefore+1&&m.frame[40*280+20]==0xabcd);
 // Even an installed 1.1.2 descriptor requesting full flush is ignored.
 assert(m.slots[0].descriptor->setting_count==10&&m.slots[0].descriptor->settings[8].id==9);
 const auto*rowCurrent=m.slots[0].descriptor;kd_device retired=*rowCurrent;
 retired.size=offsetof(kd_device,backlight_pin);retired.prefer_full_frame=[](const void*,void*)->uint32_t{assert(false);return 1;};
 m.slots[0].descriptor=&retired;
 small.getFramebuffer()[0]^=1;assert(m.capture(nullptr,&small,none,false));
 const auto removedFull=m.fullTransfers,removedRows=m.rowTransfers,removedPixels=m.transferPixels;
 stopWorker=true;try{m.run();}catch(Stop&){}stopWorker=false;
 assert(m.fullTransfers==removedFull&&m.rowTransfers==removedRows+1&&m.transferPixels-removedPixels==280);
 m.slots[0].descriptor=rowCurrent;
 // Dirty bits accumulate until presented even if a change is reverted.
 small.getFramebuffer()[40]=0xabcd;assert(m.capture(nullptr,&small,none,false));
 small.getFramebuffer()[40]=0x5678;assert(m.capture(nullptr,&small,none,false));
 assert(m.dirtyRows[41/32]&(uint32_t(1)<<(41%32)));
 stopWorker=true;try{m.run();}catch(Stop&){}stopWorker=false;
 lilka::VolumeOverlaySnapshot shown;shown.valid=true;shown.adjustedAt=fakeTime;
 assert(m.capture(nullptr,&small,shown,false)&&m.frame[100*280+100]==0xffff);
 stopWorker=true;try{m.run();}catch(Stop&){}stopWorker=false;
 assert(m.capture(nullptr,&small,none,false)&&m.frame[100*280+100]==0);
 stopWorker=true;try{m.run();}catch(Stop&){}stopWorker=false;
 // Clip a negative-position app while preserving the uncovered panel.
 lilka::Canvas clipped(-10,-5,20,10);
 std::fill(clipped.getFramebuffer(),clipped.getFramebuffer()+200,0x9876);
 std::fill(panel.getFramebuffer(),panel.getFramebuffer()+280*24,0x3333);
 assert(m.capture(&panel,&clipped,none,false));assert(m.frame[0]==0x9876&&m.frame[10]==0x3333);
 stopWorker=true;try{m.run();}catch(Stop&){}stopWorker=false;
 // Fragmented/high-coverage changes use one full transfer rather than many windows.
 assert(m.capture(&panel,&old,none,false));
 stopWorker=true;try{m.run();}catch(Stop&){}stopWorker=false;
 for(int y=0;y<18;y+=2)old.getFramebuffer()[y*280]^=1;
 assert(m.capture(&panel,&old,none,false));const auto fragmented=m.fullTransfers;
 stopWorker=true;try{m.run();}catch(Stop&){}stopWorker=false;assert(m.fullTransfers==fragmented+1);
 std::fill(old.getFramebuffer(),old.getFramebuffer()+280*216,0x7777);
 assert(m.capture(&panel,&old,none,false));const auto covered=m.fullTransfers;
 stopWorker=true;try{m.run();}catch(Stop&){}stopWorker=false;assert(m.fullTransfers==covered+1);
 // ASan checks a truly short legacy descriptor: never access its optional tail.
 const auto*current=m.slots[0].descriptor;
 auto*legacy=static_cast<kd_device*>(std::malloc(offsetof(kd_device,present_rows)));
 std::memcpy(legacy,current,offsetof(kd_device,present_rows));legacy->size=offsetof(kd_device,present_rows);
 m.slots[0].descriptor=nullptr;assert(!DeviceManager::registerDevice(&m.slots[0],legacy));
 clipped.getFramebuffer()[110]=0x1111;assert(m.capture(&panel,&clipped,none,false));
 const auto legacyPixels=m.transferPixels;
 stopWorker=true;try{m.run();}catch(Stop&){}stopWorker=false;assert(m.transferPixels-legacyPixels==280*240);
 m.slots[0].descriptor=current;std::free(legacy);
 // Old row-capable ABI-1 descriptors have no policy tail; ASan bounds check.
 auto*rowLegacy=static_cast<kd_device*>(std::malloc(offsetof(kd_device,prefer_full_frame)));
 std::memcpy(rowLegacy,current,offsetof(kd_device,prefer_full_frame));rowLegacy->size=offsetof(kd_device,prefer_full_frame);
 m.slots[0].descriptor=nullptr;assert(!DeviceManager::registerDevice(&m.slots[0],rowLegacy));
 clipped.getFramebuffer()[110]^=1;assert(m.capture(&panel,&clipped,none,false));
 const auto oldRows=m.rowTransfers,oldPixels=m.transferPixels;
 stopWorker=true;try{m.run();}catch(Stop&){}stopWorker=false;
 assert(m.rowTransfers==oldRows+1 && m.transferPixels-oldPixels==280);
 m.slots[0].descriptor=current;std::free(rowLegacy);
 const auto previousStarts=starts;
 assert(current->present_rows(&m.slots[0].host,m.slots[0].state,m.frame,280,240,240,1));
 assert(current->present_rows(&m.slots[0].host,m.slots[0].state,m.frame,280,240,239,2));assert(starts==previousStarts);
 assert(m.setDisplayMode(DeviceManager::External));assert(!m.internalOutput());
 failNvs=true;assert(!m.setDisplayMode(DeviceManager::Internal));assert(m.displayMode()==DeviceManager::External);failNvs=false;
 assert(m.setDisplayMode(DeviceManager::Internal));assert(!m.frame&&m.internalOutput());
 assert(m.setEnabled(0,false));assert(allocations.empty()&&!busBusy);assert(resetCount>=4);assert(!m.extensionPinOwned(12));
 failFrameAllocation=frameAllocations+2;assert(m.setDisplayMode(DeviceManager::Both));
 assert(!m.setEnabled(0,true));assert(m.internalOutput()&&!busBusy&&allocations.empty());failFrameAllocation=0;
 failFrame=true;assert(m.setDisplayMode(DeviceManager::Both));assert(!m.setEnabled(0,true));assert(m.internalOutput()&&!busBusy&&allocations.empty());failFrame=false;
 assert(m.setEnabled(0,true));const auto count=allocations.size();
 // Duplicate pins fail activation and restore the previous working setting.
 assert(!m.changeSetting(0,1,-1));assert(m.slots[0].values[1]==1&&m.slots[0].active&&allocations.size()==count);
 assert(m.changeSetting(0,6,1));assert(m.slots[0].values[6]==1);
 for(int i=0;i<20;++i){assert(m.setEnabled(0,false));assert(allocations.empty());assert(m.setEnabled(0,true));assert(allocations.size()==count);}
 assert(m.setEnabled(0,false));
 }
 assert(allocations.empty());
 {
 DeviceManager m;registered(m,0);assert(m.restore(m.slots[0]));assert(m.slots[0].values[6]==1);
 // Persisted settings are matched by stable field IDs, not field order.
 Record record;memcpy(&record,nvm["d00"].data(),sizeof(record));std::swap(record.fields[0],record.fields[5]);
 memcpy(nvm["d00"].data(),&record,sizeof(record));m.slots[0].values[6]=0;assert(m.restore(m.slots[0]));assert(m.slots[0].values[6]==1);
 record.schema=99;memcpy(nvm["d00"].data(),&record,sizeof(record));assert(!m.restore(m.slots[0]));assert(m.slots[0].readOnly);assert(!m.setEnabled(0,true));
 assert(m.resetSettings(0));assert(!m.slots[0].readOnly&&m.slots[0].values[6]==0);
 }
 assert(allocations.empty());
 {
 DeviceManager m;registered(m,0);m.workerReady=false;
 assert(!m.setEnabled(0,true)&&m.internalOutput()&&!m.externalOutput());m.workerReady=true;
 assert(m.setEnabled(0,true));assert(m.setDisplayMode(DeviceManager::Both));
 const uint8_t rejected=0x2c;submitError=ESP_ERR_INVALID_ARG;
 assert(DeviceManager::spiWrite(&m.slots[0],0,&rejected,1));assert(!m.spiFault&&!inFlight);
 assert(strstr(m.slots[0].status,"SPI transfer submit: ESP_ERR_INVALID_ARG"));submitError=0;
 const unsigned claims=allocations.size();failTransfer=true;
 const uint8_t command=0x2c;assert(DeviceManager::spiWrite(&m.slots[0],0,&command,1));
 assert(m.spiFault&&inFlight==&m.transaction);
 m.deactivate();assert(m.internalOutput()&&!m.externalOutput()&&busBusy);
 assert(m.extensionPinOwned(12));assert(allocations.size()==1&&allocations.count(m.dma)); // Quarantined DMA is not freed in flight.
 assert(!m.setEnabled(0,true));assert(allocations.size()==1);
 failTransfer=false;spi_transaction_t* settled=nullptr;assert(!spi_device_get_trans_result(m.spi,&settled,100)); // Simulate reboot/settlement only in fixture.
 m.spiFault=false;DeviceManager::spiClose(&m.slots[0]);assert(allocations.empty()&&!busBusy);
 }
 assert(allocations.empty());
 {
 DeviceManager m;registered(m,0);assert(m.setEnabled(0,true));
 wrongCompletion=true;const uint8_t command=0x2c;
 assert(DeviceManager::spiWrite(&m.slots[0],0,&command,1));assert(m.spiFault);
 m.deactivate();assert(busBusy&&allocations.count(m.dma)&&m.extensionPinOwned(12));
 wrongCompletion=false;m.spiFault=false;DeviceManager::spiClose(&m.slots[0]);
 }
 assert(allocations.empty());
 {
 DeviceManager m;registered(m,0);assert(m.setEnabled(0,true));
 std::vector<uint16_t> pixels(67201);for(unsigned i=0;i<pixels.size();++i)pixels[i]=i*997;
 for(unsigned offset:{0u,1u})for(unsigned length:{0u,1u,63u,2047u,2048u,2049u,4095u,67200u}){
  wireBytes.clear();assert(!DeviceManager::spiPixels(&m.slots[0],pixels.data()+offset,length));assert(!inFlight);
  assert(wireBytes.size()==length*2);for(unsigned i=0;i<length;++i){assert(wireBytes[2*i]==(pixels[offset+i]>>8));assert(wireBytes[2*i+1]==(pixels[offset+i]&255));}
 }
 bool packedAhead=false;duringDma=[&]{for(unsigned i=0;i<2048;++i){assert(m.dma[4096+2*i]==(pixels[2048+i]>>8));assert(m.dma[4096+2*i+1]==(pixels[2048+i]&255));}packedAhead=true;};
 assert(!DeviceManager::spiPixels(&m.slots[0],pixels.data(),4096));assert(packedAhead);
 failSubmissionAt=starts+2;assert(DeviceManager::spiPixels(&m.slots[0],pixels.data(),4096));assert(!inFlight&&!m.spiFault);failSubmissionAt=0;
 assert(!DeviceManager::spiPixels(&m.slots[0],pixels.data(),4096));
 m.deactivate();assert(allocations.empty()&&!busBusy);
 }
 {
 DeviceManager m;registered(m,0);
 assert(m.slots[0].descriptor->settings_version==1);
 assert(m.slots[0].descriptor->settings[5].default_value==1&&m.slots[0].descriptor->settings[5].maximum==3);
 assert(!strcmp(m.slots[0].descriptor->settings[5].choices[2],"40 MHz"));
 assert(!strcmp(m.slots[0].descriptor->settings[5].choices[3],"80 MHz"));
 m.slots[0].state=(void*)1;
 assert(DeviceManager::spiOpen(&m.slots[0],12,13,21,14,80000001));assert(!busBusy&&!m.spiOwner);
 assert(strstr(m.slots[0].status,"Invalid SPI speed"));m.slots[0].state=nullptr;
 m.slots[0].values[5]=2;assert(m.persist(m.slots[0]));m.slots[0].values[5]=1;
 assert(m.restore(m.slots[0])&&m.slots[0].values[5]==2); // Existing saved index remains 40 MHz.
 // Every old SPI choice migrates by stable ID, independent of descriptor order.
 for(int legacy=0;legacy<=6;++legacy){
  assert(m.persist(m.slots[0]));
  for(auto& entry:nvm){if(entry.second.size()==sizeof(Record)){
   Record saved;std::memcpy(&saved,entry.second.data(),sizeof saved);
   if(!strcmp(saved.uuid,m.slots[0].uuid)){
    for(unsigned j=0;j<saved.count;++j)if(saved.fields[j].id==12){saved.fields[j].id=5;saved.fields[j].value=legacy;}
    std::memcpy(entry.second.data(),&saved,sizeof saved);
   }
  }}
  m.slots[0].values[5]=-1;assert(m.restore(m.slots[0]));
  assert(m.slots[0].values[5]==(legacy==6?3:legacy>2?2:legacy));
 }
 m.slots[0].values[5]=2;assert(m.persist(m.slots[0]));
 // Saved 1.1.2 mode=whole (id8) is ignored without resetting pins/saved40MHz.
 for(auto& entry:nvm){if(entry.second.size()==sizeof(Record)){
  Record saved;std::memcpy(&saved,entry.second.data(),sizeof saved);
  if(!strcmp(saved.uuid,m.slots[0].uuid)){saved.count=8;saved.fields[4].id=8;saved.fields[4].value=1;std::memcpy(entry.second.data(),&saved,sizeof saved);}
 }}
 assert(m.restore(m.slots[0])&&m.slots[0].values[5]==2&&m.slots[0].values[8]==0&&!m.slots[0].readOnly);
 assert(m.setEnabled(0,true));assert(m.spiHz==40000000);
 // Backlight field preserves pin/SPI settings from a real old 8-field blob.
 for(auto& entry:nvm){if(entry.second.size()==sizeof(Record)){Record saved;std::memcpy(&saved,entry.second.data(),sizeof saved);saved.count=8;std::memcpy(entry.second.data(),&saved,sizeof saved);entry.second.resize(offsetof(Record,fields)+8*sizeof(Record::Field));}}
 assert(m.restore(m.slots[0])&&m.slots[0].values[5]==2&&m.slots[0].values[4]==0&&m.slots[0].values[9]==0);
 assert(m.persist(m.slots[0]));
 // Conflicting backlight pins must roll back without changing native brightness.
 m.slots[0].values[0]=4;assert(m.activate(0)); // SPI clock temporarily uses GPIO47.
 assert(!m.changeSetting(0,4,1)&&m.slots[0].values[4]==0&&m.active==0);
 m.slots[0].values[0]=0;assert(m.activate(0));
 assert(m.changeSetting(0,4,1));assert(m.backlightPin==47&&m.extensionPinOwned(47));
 assert(lilka::brightness.pin==47);
 assert(m.setDisplayMode(DeviceManager::External));assert(!lilka::brightness.native&&lilka::brightness.external);
 const auto externalFeedback=m.getOverlay();
#if defined(LANG_EN) && !defined(LANG_UK)
 const char* modeNames[]={"Internal","External","Both"};
#else
 const char* modeNames[]={"Внутрішній","Зовнішній","Обидва"};
#endif
 assert(externalFeedback.visible(fakeTime)&&externalFeedback.displayMode==1&&!strcmp(externalFeedback.muteLabel,modeNames[1]));
 fakeTime+=10;assert(m.setDisplayMode(DeviceManager::External));assert(m.getOverlay().adjustedAt==externalFeedback.adjustedAt);
 failNvs=true;assert(!m.setDisplayMode(DeviceManager::Both));failNvs=false;
 assert(m.displayMode()==DeviceManager::External&&m.getOverlay().displayMode==1&&m.getOverlay().adjustedAt==externalFeedback.adjustedAt);
 fakeTime=externalFeedback.adjustedAt+1200;assert(!m.getOverlay().visible(fakeTime));
 assert(m.setDisplayMode(DeviceManager::Both));assert(lilka::brightness.native&&lilka::brightness.external);
 assert(m.getOverlay().displayMode==2&&!strcmp(m.getOverlay().muteLabel,modeNames[2]));
 assert(m.setDisplayMode(DeviceManager::Internal));assert(lilka::brightness.native&&!lilka::brightness.external);
 assert(m.getOverlay().displayMode==0&&!strcmp(m.getOverlay().muteLabel,modeNames[0]));
 m.deactivate();assert(lilka::brightness.pin==-1&&!m.extensionPinOwned(47));
 m.slots[0].values[4]=0;assert(m.activate(0));
 // Panel-drive choices emit only documented B1/B4 overrides; default unchanged.
 for(int mode=0;mode<4;++mode){
  assert(m.setEnabled(0,false));m.slots[0].values[8]=mode;wireChunks.clear();assert(m.setEnabled(0,true));
  std::map<unsigned,std::vector<uint8_t>> regs;unsigned cmd=0;
  for(const auto&chunk:wireChunks){if(!chunk.first){assert(chunk.second.size()==1);cmd=chunk.second[0];regs[cmd].clear();}else regs[cmd].insert(regs[cmd].end(),chunk.second.begin(),chunk.second.end());}
  const uint8_t rates[]={0x18,0x1b,0x1f,0x18};
  assert(regs[0xb1]==std::vector<uint8_t>({0,rates[mode]}));
  if(!mode)assert(!regs.count(0xb4));else assert(regs[0xb4]==std::vector<uint8_t>({uint8_t(mode==3?6:2)}));
  assert(!regs.count(0x21)&&regs[0x3a]==std::vector<uint8_t>({0x55}));
  assert(regs[0xc5]==std::vector<uint8_t>({0x3e,0x28})&&regs[0xc7]==std::vector<uint8_t>({0x86}));
 }
 assert(m.persist(m.slots[0]));m.slots[0].values[8]=0;assert(m.restore(m.slots[0])&&m.slots[0].values[8]==3);
 for(int i=0;i<3;++i)assert(m.changeSetting(0,8,-1));assert(m.slots[0].values[8]==0);
 assert(m.changeSetting(0,5,1));assert(m.slots[0].values[5]==3&&openedClock==80000000&&m.spiHz==80000000);
 assert(m.changeSetting(0,5,-1));assert(m.slots[0].values[5]==2&&m.spiHz==40000000);
 assert(m.spiHz==40000000&&openedClock==40000000);
 m.deactivate();assert(allocations.empty()&&!busBusy);
 }
 {
 wireChunks.clear();
 DeviceManager m;registered(m,0);
 assert(m.setEnabled(0,true));
 // Initialization clears all 76,800 physical pixels with scanout still off.
 bool clearing=false;size_t blackBytes=0;
 for(const auto& chunk:wireChunks){
  if(chunk.first==0){clearing=chunk.second.size()==1&&chunk.second[0]==0x2c;assert(chunk.second.size()!=1||chunk.second[0]!=0x29);}
  else if(clearing){for(auto byte:chunk.second)assert(byte==0);blackBytes+=chunk.second.size();}
 }
 assert(blackBytes>=240*320*2);
 assert(m.setDisplayMode(DeviceManager::Both));
 m.setSleeping(true);assert(m.sleeping&&m.displayMode()==DeviceManager::Both);
 lilka::Canvas app(0,0,280,240);lilka::VolumeOverlaySnapshot overlay;
 assert(m.capture(nullptr,&app,overlay,false));
 const auto transfers=m.transferCount;stopWorker=true;
 try{m.run();}catch(const Stop&){}stopWorker=false;
 assert(m.transferCount==transfers);m.setSleeping(false);assert(!m.sleeping);
 m.requestCycle();assert(m.cycleRequests==1);stopWorker=true;
 try{m.run();}catch(const Stop&){}stopWorker=false;
 assert(m.displayMode()==DeviceManager::Internal&&m.cycleRequests==0);
 m.deactivate();assert(allocations.empty());
 }

 // Diagnostic output must be physically identical with and without traffic.
 nvm.clear();
 {
 DeviceManager m;registered(m,0);assert(m.setEnabled(0,true));
 assert(m.setDisplayMode(DeviceManager::Both));
 assert(m.slots[0].descriptor->settings[9].id==11&&m.slots[0].values[9]==0);
 assert(m.changeSetting(0,9,1)); // fixed image + writes
 lilka::Canvas app(0,0,280,240);lilka::VolumeOverlaySnapshot overlay;
 auto tick=[&]{fakeTime+=100;stopWorker=true;try{m.run();}catch(Stop&){}stopWorker=false;};
 auto image=[] {
  std::vector<uint8_t> result;bool writing=false;
  for(const auto&chunk:wireChunks){
   if(chunk.first==0){writing=chunk.second.size()==1&&chunk.second[0]==0x2c;if(writing)result.clear();}
   else if(writing)result.insert(result.end(),chunk.second.begin(),chunk.second.end());
  }
  return result;
 };
 wireChunks.clear();assert(m.capture(nullptr,&app,overlay,false));tick();
 const auto pattern=image();assert(pattern.size()==320*240*2);
 // Top color bars and lower checker/line areas are genuinely different.
 assert(pattern[0]==0&&pattern[1]==0&&pattern[80]==255&&pattern[81]==255);
 assert(pattern[120*320*2]!=pattern[120*320*2+16]);
 for(int i=0;i<3;++i){
  app.getFramebuffer()[0]^=1;assert(m.capture(nullptr,&app,overlay,false));
  const auto before=starts;wireChunks.clear();tick();assert(starts>before&&image()==pattern);
 }
 assert(m.changeSetting(0,9,1)); // hold: setting reactivation clears state
 // A failed initial pattern must not latch hold and silently keep partial GRAM.
 auto failing=m.slots[0].host;
 failing.spi_pixels=[](void*,const uint16_t*,uint32_t)->int32_t{return -1;};
 assert(m.slots[0].descriptor->present(&failing,m.slots[0].state,app.getFramebuffer(),280,240));
 wireChunks.clear();assert(m.capture(nullptr,&app,overlay,false));tick();assert(image()==pattern);
 const auto held=starts;wireChunks.clear();
 for(int i=0;i<20;++i){
  app.getFramebuffer()[i]^=1;overlay.valid=(i&1);
  assert(m.capture(nullptr,&app,overlay,false));tick();
  assert(starts==held&&wireChunks.empty()&&m.active==0&&m.externalOutput());
 }
 // No orientation commands/clear even if the producer geometry changes.
 auto& slot=m.slots[0];
 assert(!slot.descriptor->present(&slot.host,slot.state,app.getFramebuffer(),240,280));
 assert(starts==held&&wireChunks.empty());
 // Sleep/reroute are deliberately NOT blocked: normal power control survives.
 m.setSleeping(true);assert(starts>held&&wireChunks.back().first==0&&wireChunks.back().second==std::vector<uint8_t>{0x28});
 m.setSleeping(false);assert(wireChunks.back().first==0&&wireChunks.back().second==std::vector<uint8_t>{0x29});
 assert(m.changeSetting(0,9,1));assert(slot.values[9]==0); // wrap back to normal
 wireChunks.clear();assert(m.capture(nullptr,&app,overlay,false));tick();assert(!wireChunks.empty());
 m.deactivate();assert(allocations.empty());
 }
 // Draft editing never touches hardware or NVS; Apply validates the entire pin set.
 nvm.clear();
 {
 DeviceManager m;registered(m,0);assert(m.setEnabled(0,true));
 DeviceManager::SettingsDraft draft;
 assert(m.beginSettingsEdit(0,draft));
 assert(draft.count==10&&draft.schema==m.slots[0].schema);
 for(unsigned field=0;field<draft.count;++field){
  DeviceManager::SettingInfo info;assert(m.settingInfo(0,field,info));
  assert(info.pin==(field<5));
 }
 const auto initialNvs=nvm;
 auto initCount=busInitializations,removeCount=deviceRemovals,writeCount=nvsAttempts;
 draft.values[0]=1; // Half of a Clock/MOSI swap: temporarily duplicates GPIO13.
 DeviceManager::SettingInfo preview;
 assert(m.previewSetting(0,0,draft,preview)&&preview.pin&&!strcmp(preview.choice,"GPIO13"));
 assert(m.slots[0].values[0]==0&&m.slots[0].values[1]==1&&m.slots[0].active);
 assert(busInitializations==initCount&&deviceRemovals==removeCount&&nvsAttempts==writeCount&&nvm==initialNvs);
 assert(!m.applySettings(0,draft)); // Reject before disabling the old driver.
 assert(m.slots[0].active&&m.slots[0].values[0]==0&&m.slots[0].values[1]==1);
 assert(busInitializations==initCount&&deviceRemovals==removeCount&&nvsAttempts==writeCount&&nvm==initialNvs);
 draft.values[1]=0; // The complete swap is valid: one restart, one stored record.
 assert(m.applySettings(0,draft));
 assert(m.slots[0].active&&m.slots[0].values[0]==1&&m.slots[0].values[1]==0);
 assert(busInitializations==initCount+1&&deviceRemovals==removeCount+1&&nvsAttempts==writeCount+1);
 assert(m.clkPin==13&&m.mosiPin==12);
 // Applying unchanged values is a true no-op.
 assert(m.beginSettingsEdit(0,draft));
 initCount=busInitializations;removeCount=deviceRemovals;writeCount=nvsAttempts;
 assert(m.applySettings(0,draft));
 assert(busInitializations==initCount&&deviceRemovals==removeCount&&nvsAttempts==writeCount);
 // Backlight conflicts, invalid ranges and stale snapshots cannot disturb live hardware.
 draft.values[4]=1;draft.values[0]=4; // Both GPIO47.
 assert(!m.applySettings(0,draft));
 assert(busInitializations==initCount&&deviceRemovals==removeCount&&nvsAttempts==writeCount);
 assert(m.beginSettingsEdit(0,draft));draft.values[0]=999;
 assert(!m.previewSetting(0,0,draft,preview)&&!m.applySettings(0,draft));
 assert(busInitializations==initCount&&deviceRemovals==removeCount&&nvsAttempts==writeCount);
 assert(m.beginSettingsEdit(0,draft));
 assert(m.changeSetting(0,6,1)); // Concurrent scalar edit invalidates the snapshot.
 initCount=busInitializations;removeCount=deviceRemovals;writeCount=nvsAttempts;
 assert(!m.applySettings(0,draft)&&strstr(m.slots[0].status,"reopen"));
 assert(busInitializations==initCount&&deviceRemovals==removeCount&&nvsAttempts==writeCount);
 // Activation failure restores old pins and leaves persistent state untouched.
 assert(m.beginSettingsEdit(0,draft));draft.values[0]=0;draft.values[1]=1;
 const auto beforeFailure=nvm;const auto beforeAllocations=allocations.size();
 busFailures=1;assert(!m.applySettings(0,draft));
 assert(m.slots[0].active&&m.clkPin==13&&m.mosiPin==12&&m.slots[0].values[0]==1&&m.slots[0].values[1]==0);
 assert(nvm==beforeFailure&&nvsAttempts==writeCount&&allocations.size()==beforeAllocations);
 // NVS failure after activation also restores the original hardware and values.
 failNvs=true;assert(!m.applySettings(0,draft));failNvs=false;
 assert(m.slots[0].active&&m.clkPin==13&&m.mosiPin==12&&m.slots[0].values[0]==1&&m.slots[0].values[1]==0);
 assert(nvm==beforeFailure&&allocations.size()==beforeAllocations);
 // A retry succeeds with the same draft after recovery.
 assert(m.applySettings(0,draft));assert(m.clkPin==12&&m.mosiPin==13);
 // If both new activation and rollback fail, stay in a safe internal fallback.
 assert(m.beginSettingsEdit(0,draft));draft.values[0]=1;draft.values[1]=0;
 const auto beforeDoubleFailure=nvm;busFailures=2;
 assert(!m.applySettings(0,draft));
 assert(!m.slots[0].active&&m.internalOutput()&&!busBusy&&nvm==beforeDoubleFailure);
 assert(m.slots[0].values[0]==0&&m.slots[0].values[1]==1&&strstr(m.slots[0].status,"could not restart"));
 assert(m.setEnabled(0,true));
 // Unchanged ABI-1 choice metadata still classifies a reserved GPIO as a pin,
 // then rejects that final assignment before touching the existing hardware.
 const auto* originalDescriptor=m.slots[0].descriptor;
 kd_device reserved=*originalDescriptor;
 kd_setting definitions[KD_MAX_SETTINGS];
 std::copy(originalDescriptor->settings,originalDescriptor->settings+originalDescriptor->setting_count,definitions);
 const char* reservedChoices[]={"GPIO0","GPIO13","GPIO14","GPIO21","GPIO47","GPIO48"};
 definitions[0].choices=reservedChoices;reserved.settings=definitions;m.slots[0].descriptor=&reserved;
 assert(m.beginSettingsEdit(0,draft));
 assert(m.previewSetting(0,0,draft,preview)&&preview.pin);
 initCount=busInitializations;writeCount=nvsAttempts;
 assert(!m.applySettings(0,draft)&&busInitializations==initCount&&nvsAttempts==writeCount&&m.slots[0].active);
 m.slots[0].descriptor=originalDescriptor;
 assert(m.setEnabled(0,false));
 }
 assert(allocations.empty());
 puts("PASS: transactional pin drafts, preview/no-op, prevalidation, full swaps, one restart/save, stale rejection and rollback/fallback");
 // Restore the selected route, but Select-held recovery forces Internal.
 nvm.clear();nvm["displayMode"]={DeviceManager::Both};
 {DeviceManager m;registered(m,0);assert(m.begin(false));assert(createdCore==tskNO_AFFINITY&&createdPriority==1);assert(m.displayMode()==DeviceManager::Both);assert(m.begin(false));assert(!m.setWorkerCore(1));assert(!m.setWorkerPriority(2));}
 {DeviceManager m;registered(m,0);assert(!m.setWorkerCore(-1));assert(!m.setWorkerCore(2));assert(m.setWorkerCore(0));assert(m.setWorkerCore(1));assert(m.begin(false));assert(createdCore==1&&createdPriority==1);assert(!m.setWorkerCore(tskNO_AFFINITY));}
 {DeviceManager m;registered(m,0);assert(!m.setWorkerPriority(0));assert(!m.setWorkerPriority(configMAX_PRIORITIES));assert(!m.setWorkerPriority(~0u));assert(m.setWorkerPriority(configMAX_PRIORITIES-1));assert(m.begin(false));assert(createdCore==tskNO_AFFINITY&&createdPriority==configMAX_PRIORITIES-1);assert(!m.setWorkerPriority(1));}
 {DeviceManager m;registered(m,0);assert(m.setWorkerCore(1));assert(m.setWorkerPriority(2));assert(!m.setWorkerPriority(0));assert(!m.setWorkerPriority(configMAX_PRIORITIES));failWorker=true;assert(!m.begin(false));assert(m.internalOutput()&&!m.externalOutput());failWorker=false;assert(m.setWorkerCore(0));assert(m.begin(false));assert(createdCore==0&&createdPriority==2);assert(!m.setWorkerPriority(3));}
 puts("PASS: opt-in display affinity/priority, unchanged defaults, invalid/late rejection and allocation failure retry");
 {DeviceManager m;assert(m.begin(true));assert(m.displayMode()==DeviceManager::Internal);}
 nvm["displayMode"]={255};
 {DeviceManager m;assert(m.begin(false));assert(m.displayMode()==DeviceManager::Internal);}

#if LILKA_MEASURE_DISPLAY
 assert(displayProfile::sample(displayProfile::Stage::Capture).n>0);
 assert(displayProfile::sample(displayProfile::Stage::ExternalPack).bytes>0);
 assert(displayProfile::sample(displayProfile::Stage::ExternalFeed).n>0);
 assert(displayProfile::sample(displayProfile::Stage::ExternalWait).n>0);
#endif
 puts("PASS: shared frames/20 in-flight updates/latest-wins, 40 MHz persistence, queued DMA/quarantine, partial/full frames, legacy ABI, overlays, allocation rollback, 20 toggles, ELF bounds and identical fixed-pattern/zero-SPI hold diagnostic");
}
'''
with tempfile.TemporaryDirectory(prefix='keira-device-host-') as tmp:
    tmp = Path(tmp)
    (tmp / 'mock.h').write_text(mock)
    for name in ['keira/service.h','keira/ksystem.h','keira/utils/string.h','driver/spi_master.h',
                 'driver/gpio.h','esp_err.h','esp_heap_caps.h','esp_timer.h','soc/soc.h']:
        p = tmp / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('#pragma once\n#include "mock.h"\n')
    # SDK loader types are unchanged; use its real public header.
    loader = sources / 'dynloader.h'
    if not loader.is_file():
        raise RuntimeError('Sibling SDK header required')
    (tmp / 'lilka').mkdir()
    (tmp / 'lilka/dynloader.h').write_text(loader.read_text())
    (tmp / 'lilka/rgb565_pack.h').write_text((loader.parent / 'rgb565_pack.h').read_text())
    for name in ['devices.h', 'devices.cpp', 'display_profile.h', 'device_driver.h', 'frame_metrics.h', 'frame_row.h', 'elfcheck.h', 'elfcheck.cpp', 'localization.h']:
        (tmp/'lilka'/name).write_text((sources/name).read_text())
    for name in ['display.h','brightness.h','serial.h','volume_overlay.h','Preferences.h','freertos/FreeRTOS.h','freertos/task.h','freertos/semphr.h']:
        p = tmp/'lilka'/name
        p.parent.mkdir(parents=True,exist_ok=True)
        p.write_text('#pragma once\n#include "mock.h"\n')
    (tmp / 'test.cpp').write_text(harness)
    subprocess.run(['gcc','-std=c11','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Dmain=kd_driver_entry','-Dmemset=kd_module_memset',
                    '-c',str(root/'drivers/ili9341/driver.c'),'-o',str(tmp/'driver.o')],check=True)
    for language in ([], ['-DLANG_EN'], ['-DLANG_UK'], ['-DLANG_EN', '-DLANG_UK'], ['-DLILKA_MEASURE_DISPLAY=1']):
        subprocess.run(['g++','-std=c++17','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer', *language,
                    '-I'+str(tmp),'-I'+str(tmp/'lilka'),'-I'+os.environ.get('ARDUINOJSON_INCLUDE', str(root.parent/'keira/.pio/libdeps/v2/ArduinoJson/src')),
                    str(tmp/'test.cpp'),str(tmp/'lilka/elfcheck.cpp'),str(tmp/'driver.o'),
                    '-o',str(tmp/'test')],check=True)
        subprocess.run([str(tmp/'test'),sys.argv[1]],check=True)
