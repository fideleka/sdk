"""Extract production logger methods into a host HAL; no firmware build."""
from pathlib import Path
import subprocess, tempfile, os
root = Path(__file__).resolve().parents[1]
src = root / 'lib/lilka/src/lilka'
h = (src/'serial.h').read_text()
s = (src/'serial.cpp').read_text()
cls = h[h.index('class SerialInterface {'):h.index('\n};', h.index('class SerialInterface {'))+3].replace('private:', 'public:')
methods = s[s.index('void SerialInterface::enqueue'):s.index('#if LILKA_LOG_LEVEL >= 2\nvoid SerialInterface::writeGreetingMessage')]
methods += s[s.index('void SerialInterface::drainOnce'):s.index('void SerialInterface::run()')]
methods += s[s.index('ssize_t SerialInterface::stdio_vfs_write'):s.index('SerialInterface serial;')]
methods += 'SerialInterface serial;\n' + s[s.index('int serial_idf('):s.index('SerialInterface::SerialInterface()')]
prefix = r'''
#include "log_config.h"
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
#define STDIN_FD 0
#define STDOUT_FD 1
#define STDERR_FD 2
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
int callIdf(const char*fmt,...){va_list args;va_start(args,fmt);int n=serial_idf(fmt,args);va_end(args);return n;}
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
 Serial.text.clear();
 assert(callIdf("value %s %u","100% %s %n",42)==int(std::string("value 100% %s %n 42").size()));
 serial.drainOnce();assert(Serial.text.find("value 100% %s %n 42")!=std::string::npos);
 delete logger.serialMutex;
 delete serial.serialMutex;
 puts("Production logger zero-capacity/short/zero-write/queue-cap/contention/concurrent/literal/truncation PASS");
}
'''
assert 'Serial.flush()' not in s
assert 'return serial.idfV(format, args);' in s and 'serial.log("%s", str.c_str())' in s
assert 'msgbuffer' not in s
quiet = r'''
SerialInterface::SerialInterface() {}
int main(){
 SerialInterface logger;
 for(unsigned n=0;n<10000;++n)logger.log("suppressed info %u",n);
 assert(logger.serialQueue.empty() && logger.pending.isEmpty() && logger.droppedLogs()==0);
 Serial.available=999;
 logger.err("visible error %u",42);logger.drainOnce();
 assert(Serial.text.find("visible error 42")!=std::string::npos);
 assert(Serial.text.find("suppressed info")==std::string::npos);
 const char user[]="explicit user console %s %n";
 assert(logger.stdio_vfs_write(STDOUT_FD,user,sizeof(user)-1)==sizeof(user)-1);
 serial.drainOnce();assert(Serial.text.find(user)!=std::string::npos);
 delete logger.serialMutex;delete serial.serialMutex;
 puts("Production error-only logger: no info formatting/queue/backpressure; error retained PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='sdk-serial-host-') as td:
 p=Path(td)
 for level in (1,2):
  (p/'test.cpp').write_text(prefix+cls+'\nextern SerialInterface serial;\n'+methods+(quiet if level==1 else body))
  subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-g','-O1','-pthread',
                  '-DLILKA_LOG_LEVEL='+str(level),'-I'+str(src),
                  '-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',
                  str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
