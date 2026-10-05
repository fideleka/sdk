"""Production Sys partition compatibility and ordinary restart host regression."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
sdk=root/'lib/lilka/src'
assert '#include "lilka/sys.h"' in (sdk/'lilka.h').read_text()
assert '#include "lilka/partitions.h"' in (sdk/'lilka.h').read_text()
pre=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using String=std::string;
struct esp_partition_t{const char* label;int type,subtype;unsigned address,size;};
esp_partition_t entries[]={{"app",0,0,65536,1024},{"data",1,0,131072,2048}};
using esp_partition_iterator_t=esp_partition_t*;
#define ESP_PARTITION_TYPE_ANY 0
#define ESP_PARTITION_SUBTYPE_ANY 0
esp_partition_iterator_t esp_partition_find(int,int,void*){return entries;}
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t p){return p==entries?entries+1:nullptr;}
const esp_partition_t* esp_partition_get(esp_partition_iterator_t p){return p;}
std::vector<int> events;
bool usbInited=false;
bool tud_inited(){return usbInited;}
void tud_disconnect(){events.push_back(1);}
#define USB_SERIAL_JTAG_CONF0_REG 1
#define RTC_CNTL_USB_CONF_REG 2
#define USB_SERIAL_JTAG_USB_PAD_ENABLE 4
#define RTC_CNTL_SW_HW_USB_PHY_SEL 8
#define RTC_CNTL_SW_USB_PHY_SEL 16
#define RTC_CNTL_USB_PAD_ENABLE 32
#define USB_SERIAL_JTAG_PHY_SEL 64
#define CLEAR_PERI_REG_MASK(reg,mask) events.push_back(100+(reg)*100+(mask))
#define SET_PERI_REG_MASK(reg,mask) events.push_back(200+(reg)*100+(mask))
#define pdMS_TO_TICKS(x) (x)
void vTaskDelay(int ticks){assert(ticks==2000);events.push_back(2);}
[[noreturn]] void esp_restart_noos_dig(){events.push_back(3);throw 42;}
[[noreturn]] void esp_restart(){throw 42;}
'''
s=(sdk/'lilka/sys.cpp').read_text().replace('#include "sys.h"','')
h=(sdk/'lilka/sys.h').read_text().replace('#include <Arduino.h>','').replace('#include <esp_partition.h>','')
checks=r'''
int main(){String labels[2];assert(lilka::sys.get_partition_labels(labels)==2);
assert(labels[0]=="app"&&labels[1]=="data");
assert(lilka::sys.get_partition_address("data")==131072);
assert(lilka::sys.get_partition_size("app")==1024);
assert(lilka::sys.get_partition_address("absent")==0);
assert(lilka::sys.get_partition_size("absent")==0);
for(bool active:{false,true}){
 usbInited=active;events.clear();
 try{lilka::sys.restart();}catch(int code){assert(code==42);}
#if CONFIG_IDF_TARGET_ESP32S3
 assert(events.size()==6);
 assert(events[0]==((CONFIG_TINYUSB_ENABLED && active)?1:204));
 assert(events[1]==2 && events[2]==356 && events[3]==264 && events[4]==304 && events[5]==3);
#else
 assert(events.empty());
#endif
}
puts("Retained Sys legacy partition API and ordinary restart PASS");}
'''
with tempfile.TemporaryDirectory(prefix='sdk-upstream-compat-') as d:
 p=Path(d);(p/'test.cpp').write_text(pre+h+s+checks)
 for name in ['esp_private/system_internal.h','soc/rtc_cntl_reg.h','soc/soc.h','soc/usb_serial_jtag_reg.h','tusb.h']:
  header=p/name;header.parent.mkdir(parents=True,exist_ok=True);header.write_text('')
 for target,tiny in [(0,0),(1,0),(1,1)]:
  for flags in ([],['-fsanitize=address,undefined','-fno-pie','-no-pie']):
   subprocess.run(['g++','-std=c++11','-I'+str(p),'-DCONFIG_IDF_TARGET_ESP32S3='+str(target),'-DCONFIG_TINYUSB_ENABLED='+str(tiny),*flags,str(p/'test.cpp'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
