#!/usr/bin/env python3
"""Test actual shared display settings and LCD-owner idle policy against host HAL."""
from pathlib import Path
import ast
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'lib/lilka/src/lilka'
# Reuse the checked-in, bounded RTOS/NVS mock without executing another test runner.
tree = ast.parse((ROOT / 'tests/backlight/run.py').read_text())
mock = next(ast.literal_eval(node.value) for node in tree.body
            if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'MOCK' for t in node.targets))
mock = mock.replace('!strcmp(key,"level")', '!strcmp(key,"timeoutSeconds")')
mock += '\nextern int offCalls,onCalls;\nnamespace lilka {struct MockBoard {void enablePowerSavingMode(){++offCalls;}void disablePowerSavingMode(){++onCalls;}};extern MockBoard board;}\n'
test = r'''
#include "mock.h"
#define private public
#include "display_settings.h"
#undef private
uint32_t now=1000,stored=120,duty=0;int reads=0,writes=0,locks=0,pinHigh=0,fail=0;
int offCalls=0,onCalls=0;bool failSave=false;void(*writeHook)()=nullptr;
namespace lilka {MockBoard board;}
int main(int argc,char** argv){
 using lilka::displaySettings;
 if(argc>1&&argv[1][0]=='3'){
  fail=3;assert(!displaySettings.begin());assert(!displaySettings.setTimeoutSeconds(10));
  now+=200000;displaySettings.serviceIdle(true);assert(offCalls==0);return 0;
 }
 if(argc>1&&argv[1][0]=='5')stored=UINT32_MAX;
 assert(displaySettings.begin()&&displaySettings.begin()&&reads==1);
 assert(displaySettings.getTimeoutSeconds()==120);
 displaySettings.noteInput(1,now+1);displaySettings.serviceIdle(true);assert(!displaySettings.isSleeping()); // Newer concurrent input is not overdue.
 now=121000;displaySettings.serviceIdle(false);assert(offCalls==0);
 now=240999;displaySettings.serviceIdle(true);assert(offCalls==0);
 ++now;displaySettings.serviceIdle(true);assert(offCalls==1&&displaySettings.isSleeping());
 displaySettings.noteInput(1,now+5);now+=5;
 assert(displaySettings.wakePending());assert(displaySettings.serviceIdle(true));
 assert(onCalls==1&&!displaySettings.isSleeping());
 displaySettings.setTimeoutSeconds(30);assert(displaySettings.getTimeoutSeconds()==30&&writes==0);
 now+=29999;displaySettings.serviceIdle(true);assert(offCalls==1);
 ++now;displaySettings.serviceIdle(true);assert(offCalls==2);
 displaySettings.setTimeoutSeconds(0);assert(displaySettings.wakePending());
 assert(displaySettings.serviceIdle(true));now+=1000000;displaySettings.serviceIdle(true);
 assert(!displaySettings.isSleeping()&&offCalls==2);
 displaySettings.servicePersistence();assert(writes==1&&stored==0);
 displaySettings.setTimeoutSeconds(60);displaySettings.servicePersistence();assert(writes==1);
 now+=601;failSave=true;displaySettings.servicePersistence();assert(stored==0);
 failSave=false;displaySettings.servicePersistence();assert(stored==60);
 displaySettings.setTimeoutSeconds(120);now+=601;
 writeHook=[](){lilka::displaySettings.setTimeoutSeconds(300);};displaySettings.servicePersistence();assert(stored==120);
 now+=601;displaySettings.servicePersistence();assert(stored==300);
 assert(displaySettings.setTimeoutSeconds(UINT32_MAX)&&displaySettings.getTimeoutSeconds()==3600);
 now=UINT32_MAX-100;displaySettings.setTimeoutSeconds(1);
 now=898;displaySettings.serviceIdle(true);assert(!displaySettings.isSleeping());
 now=899;displaySettings.serviceIdle(true);assert(displaySettings.isSleeping());
 assert(displaySettings.serviceIdle(false)&&!displaySettings.isSleeping());
 // Held keys continuously renew inactivity, independently of event consumption.
 displaySettings.setTimeoutSeconds(1);
 for(int i=0;i<10;++i){now+=500;displaySettings.noteInput(1,now);displaySettings.serviceIdle(true);assert(!displaySettings.isSleeping());}
}
'''
with tempfile.TemporaryDirectory(prefix='lilka-display-settings-') as directory:
    tmp=Path(directory)
    for name in ('display_settings.cpp','display_settings.h'):
        (tmp/name).write_text((SOURCE/name).read_text())
    (tmp/'mock.h').write_text(mock)
    (tmp/'test.cpp').write_text(test)
    for name in ('Arduino.h','Preferences.h','freertos/FreeRTOS.h','freertos/task.h','board.h'):
        p=tmp/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#pragma once\n#include "mock.h"\n')
    for sanitize in (False,True):
        flags=['-fsanitize=address,undefined','-fno-pie','-no-pie'] if sanitize else []
        subprocess.run([os.environ.get('CXX','g++'),'-std=c++11','-Wall','-Wextra','-Werror',*flags,
                        '-I'+str(tmp),str(tmp/'display_settings.cpp'),str(tmp/'test.cpp'),'-o',str(tmp/'test')],check=True)
        for scenario in ('0','3','5'):
            subprocess.run([str(tmp/'test'),scenario],check=True)
print('Shared timeout, safe wake, active-work inhibition, persistence/retry/concurrency and rollover PASS')
