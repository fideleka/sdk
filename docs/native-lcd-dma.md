# Opt-in native pixel DMA (Lilka v2 / ESP32-S3)

`Display::enablePixelDMA()` is an additive startup API. Call it after
`lilka::begin()` and before starting display/SD workers. Other consumers retain
the original polling default. It reports `Native LCD pixels: DMA` or
`polling fallback`. The default 80 MHz native clock (40 MHz breadboard) is unchanged.

This implementation extends Arduino_HWSPI rather than registering an IDF SPI
master on the already-owned Arduino SPI3 bus. Arduino beginWrite/endWrite retain
SPI1's transaction mutex and LCD CS; the SD card cannot reconfigure the bus
until pixels reach the wire. Commands/repeated colors/small pixel writes keep
the existing transport. Only writePixels of at least 64 pixels uses DMA.

The ESP32-S3 path allocates two 4,092-byte internal DMA staging buffers plus one
persistent descriptor/state, a GDMA TX channel, a semaphore and a SPI3 interrupt.
It never DMA-reads PSRAM directly. Exact RGB565 endian packing of chunk N+1
can overlap transmission of N. One descriptor remains in flight at a time;
completion is SPI transaction-done, not GDMA EOF. The writer blocks on a
semaphore while shifting rather than polling the wire. Source pixels remain
owned by the caller until writePixels returns. DMA/user/bit-length registers
are restored before returning to CPU FIFO commands and SD transactions.

The small GDMA adapter uses ESP-IDF 4.4.7 private GDMA and S3 register definitions
present in the pinned Arduino 2.0.17 / PlatformIO espressif32 6.12.0 environment.
It is not portable to other chips or an unreviewed framework upgrade. No
second peripheral/bus owner is installed. Native SD remains on Arduino SPI1.
External SPI2 remains independently managed by Keira's existing IDF driver.

Setup/allocation failure cleans up partial resources and leaves polling active.
An uncertain active transfer (100 ms timeout, missing completion, DMA start/stop
failure) restarts *without releasing the shared SPI lock or reusing DMA memory*.
Attempting to fall back mid-transfer could corrupt both LCD and SD commands.
Successful resources are owned for the singleton display's process lifetime.

Validation: production-source host transport tests cover packing/alignment,
odd chunk tails, immutable in-flight buffers, wire-end completion, lock retention,
register restoration, setup rollback and timeout fail-closed behavior. Installed
Xtensa C++11 syntax checks pass and the pinned IDF archive exports the GDMA
symbols used. These are not a full firmware link or a hardware timing test.

Remaining acceptance: clean v2 build with matching Keira, physical boot, LCD/SD
mixed I/O and audio soak, all rotations, Internal/External/Both switching,
stack/heap watermarks, and same-scene performance comparison. DMA frees CPU;
it does not increase SPI payload bandwidth or guarantee 60 FPS.
