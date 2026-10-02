# Menu foreground host regression

Run from the SDK root (Python 3 and a C++17 compiler; no PlatformIO/dependencies):

    python3 tests/menu/run.py
    python3 tests/menu/run.py --sanitize

The runner compiles the **entire production menu.cpp unchanged**, using the actual
Menu declaration extracted from ui.h and recording host graphics/controller mocks.
It does not copy or reimplement Menu::draw. Temporary sources/binaries are removed
on exit. Set CXX to choose another installed compiler.

The mocks record foreground at println, preserve it across background/bitmap
operations, and propagate marquee text records through framebuffer composition.
They use deterministic monospaced widths, not hardware fonts or pixel rasterization.
The checks cover mixed item colors, selected first/later rows, repeated cursor moves,
real update() scrolling, fitting and marquee titles/headers, icons, postfixes, custom
Menu::setColor, and unchanged text font/size/bounds/row positions.

To prove a previous revision fails without altering the checkout, export its source
to a temporary file and pass its path to --source:

    git show <revision>:lib/lilka/src/lilka/menu.cpp > /tmp/menu-before.cpp
    python3 tests/menu/run.py --source /tmp/menu-before.cpp

A failure has nonzero exit status. This is a host renderer regression, not a firmware
build or hardware verification. The compiler may warn about the existing PageUp
cursor assignment in Menu::update; this unrelated code is not changed here.
