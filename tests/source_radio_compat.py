"""Source-only initializer-order compiler regression; no firmware build."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
src=root/'lib/lilka/src/lilka'

def initializer(name,file):
    text=(src/file).read_text()
    match=re.search(name+r'::'+name+r'\(\)\s*:\s*(.*?)\s*\{\s*\n',text,re.S)
    assert match,name
    return match.group(1)

controller=initializer('Controller','controller.cpp')
multiboot=initializer('MultiBoot','multiboot.cpp')
# Models use the real declaration order and the production initializer lists.
ch=(src/'controller.h').read_text();assert ch.index('SemaphoreHandle_t semaphore;')<ch.index('State state;')
mh=(src/'multiboot.h').read_text()
fields=['String path;','FILE* file;','esp_ota_handle_t ota_handle;','const esp_partition_t* current_partition;','const esp_partition_t* ota_partition;','int bytesWritten;','int bytesTotal;']
assert [mh.index(f) for f in fields]==sorted(mh.index(f) for f in fields)
cpp='''#include <cstdio>
#include <string>
#include <cassert>
using SemaphoreHandle_t=void*;
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(){return nullptr;}
struct State{int value;};
struct Controller{SemaphoreHandle_t semaphore;State state;Controller();};
using String=std::string;using esp_ota_handle_t=int;
struct esp_partition_t{};
struct MultiBoot{String path;FILE* file;esp_ota_handle_t ota_handle;const esp_partition_t* current_partition;const esp_partition_t* ota_partition;int bytesWritten;int bytesTotal;MultiBoot();};
'''+ 'Controller::Controller() : '+controller+' {}'+chr(10)+'MultiBoot::MultiBoot() : '+multiboot+' {}'+chr(10)+'''int main(){Controller c;MultiBoot b;assert(c.state.value==0 && !c.semaphore);assert(b.path.empty()&&!b.file&&!b.current_partition&&!b.ota_partition&&b.ota_handle==0&&b.bytesWritten==0&&b.bytesTotal==0);}
'''
with tempfile.TemporaryDirectory(prefix='sdk-radio-order-') as td:
    p=Path(td);(p/'test.cpp').write_text(cpp)
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror=reorder',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('Production initializer lists match real SDK declaration order PASS; host model, not firmware')
