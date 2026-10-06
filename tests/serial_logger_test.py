"""Extract production logger methods into a host HAL; no firmware build."""
from pathlib import Path
import subprocess, tempfile, os
root = Path(__file__).resolve().parents[1]
src = root / 'lib/lilka/src/lilka'
h = (src/'serial.h').read_text()
s = (src/'serial.cpp').read_text()
cls = h[h.index('class SerialInterface {'):h.index('\n};', h.index('class SerialInterface {'))+3].replace('private:', 'public:')
methods = s[s.index('void SerialInterface::enqueue'):s.index('void SerialInterface::writeGreetingMessage')]
methods += s[s.index('void SerialInterface::drainOnce'):s.index('void SerialInterface::run()')]
prefix = r'''
#include <string>
#include <queue>
#include <atomic>
#include <mutex>
#include <thread>
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <algorithm>
struct String : std::string {
 using std::string::string; using std::string::operator=;
 bool isEmpty() const { return empty(); }
};
using SemaphoreHandle_t = std::mutex*;
thread_local bool held=false;
SemaphoreHandle_t xSemaphoreCreateMutex(){return new std::mutex;}
bool xSemaphoreTake(SemaphoreHandle_t m,unsigned timeout){assert(timeout==0);if(!m->try_lock())return false;held=true;return true;}
void xSemaphoreGive(SemaphoreHandle_t m){held=false;m->unlock();}
unsigned long millis(){return 1234;}
#define SERIAL_BAUD_RATE 115200
#define TX_BUFFER_SIZE 256
#define LILKA_LOG_FORMAT "[L] "
#define LILKA_ERR_FORMAT "[E] "
#define LILKA_IDF_FORMAT "[I] "
struct Output {
 int available=0; size_t limit=999, calls=0; std::string text;
 int availableForWrite(){assert(!held);return available;}
 size_t write(const uint8_t* p,size_t n){assert(!held);++calls;n=std::min(n,limit);text.append(reinterpret_cast<const char*>(p),n);return n;}
} Serial;
'''
body = r'''
SerialInterface::SerialInterface() {}
int main(){
 SerialInterface logger;
 logger.log("%s", "literal %s %n 100%");
 for(unsigned i=0;i<10000;++i)logger.drainOnce();
 assert(Serial.calls==0 && !logger.pending.isEmpty());
 for(unsigned i=0;i<10000;++i)logger.log("record %u",i);
 assert(logger.serialQueue.size()==16 && logger.droppedLogs()==9984);
 Serial.available=7;Serial.limit=2;
 for(unsigned i=0;i<10000;++i)logger.drainOnce();
 assert(logger.pending.isEmpty()&&logger.serialQueue.empty());
 assert(Serial.text.find("literal %s %n 100%")!=std::string::npos);
 Serial.limit=0;logger.log("stalled write");logger.drainOnce();auto offset=logger.pendingOffset;
 for(unsigned i=0;i<1000;++i){logger.drainOnce();}assert(logger.pendingOffset==offset);
 Serial.limit=999;Serial.available=999;logger.drainOnce();
 logger.serialMutex->lock();auto dropped=logger.droppedLogs();logger.err("contended");logger.serialMutex->unlock();assert(logger.droppedLogs()==dropped+1);
 std::thread a([&]{for(int i=0;i<1000;++i)logger.log("A:%d",i);});
 std::thread b([&]{for(int i=0;i<1000;++i)logger.idf("B:%d",i);});a.join();b.join();
 assert(logger.serialQueue.size()<=16);for(int i=0;i<100;++i)logger.drainOnce();
 logger.log("%s",std::string(2000,'x').c_str());logger.drainOnce();
 delete logger.serialMutex;
 puts("Production logger zero-capacity/short/zero-write/queue-cap/contention/concurrent/literal/truncation PASS");
}
'''
assert 'Serial.flush()' not in s
assert 'serial.idf("%s", buffer)' in s and 'serial.log("%s", str.c_str())' in s
assert 'msgbuffer' not in s
with tempfile.TemporaryDirectory(prefix='sdk-serial-host-') as td:
 p=Path(td);(p/'test.cpp').write_text(prefix+cls+methods+body)
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-g','-O1','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
