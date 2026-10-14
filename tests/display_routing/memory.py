"""Production Canvas memory policy: PSRAM-only framebuffer or clean failure."""

from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "lib/lilka/src/lilka/display.cpp").read_text()
start = source.index("Canvas::Canvas(uint16_t width, uint16_t height, Memory memory)")
constructor = source[start : source.index("\n}", start) + 2]
host = (
    r"""
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#define LILKA_VERSION 2
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
constexpr int u8g2_font_10x20_t_cyrillic=0;
bool fail=false;unsigned psramCalls=0,defaultCalls=0;
void*heap_caps_aligned_alloc(size_t alignment,size_t bytes,int caps){
 assert(alignment==16&&caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 ++psramCalls;if(fail)return nullptr;void*p=nullptr;assert(!posix_memalign(&p,alignment,bytes));return p;
}
namespace lilka {
struct Arduino_Canvas{
 uint16_t*_framebuffer=nullptr;unsigned w,h;
 Arduino_Canvas(unsigned w,unsigned h,void*):w(w),h(h){}
 ~Arduino_Canvas(){std::free(_framebuffer);}
 bool begin(){++defaultCalls;_framebuffer=(uint16_t*)std::malloc(w*h*2);return _framebuffer;}
 void setFont(int){}void setUTF8Print(bool){}
};
struct Canvas:Arduino_Canvas{
 enum class Memory{Default,PSRAMOnly};
 Canvas(uint16_t,uint16_t,Memory);
};
"""
    + constructor
    + r"""
}
int main(){
 using C=lilka::Canvas;
 {C canvas(280,240,C::Memory::PSRAMOnly);assert(canvas._framebuffer&&psramCalls==1&&defaultCalls==0);}
 fail=true;
 {C canvas(280,240,C::Memory::PSRAMOnly);assert(!canvas._framebuffer&&psramCalls==2&&defaultCalls==0);}
 {C canvas(0,240,C::Memory::PSRAMOnly);assert(!canvas._framebuffer&&psramCalls==2&&defaultCalls==0);}
 {C canvas(4,4,C::Memory::Default);assert(canvas._framebuffer&&defaultCalls==1&&psramCalls==2);}
 puts("Production Canvas PSRAM-only allocation/alignment/no internal fallback/failure/default policy PASS");
}
"""
)
with tempfile.TemporaryDirectory(prefix="sdk-canvas-memory-") as directory:
    tmp = Path(directory)
    (tmp / "test.cpp").write_text(host)
    for flags in ([], ["-fsanitize=address,undefined", "-fno-pie", "-no-pie"]):
        subprocess.run(["g++", "-std=c++11", "-g", *flags, str(tmp / "test.cpp"), "-o", str(tmp / "test")], check=True)
        subprocess.run([str(tmp / "test")], check=True)
