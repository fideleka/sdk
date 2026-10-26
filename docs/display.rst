``Display``: Дисплей
====================

.. contents::
    :local:
    :depth: 1

Display
-------

.. doxygenvariable:: lilka::display

.. doxygenclass:: lilka::Display
    :members:

Canvas
------

.. doxygenclass:: lilka::Canvas
    :members:

GFX
---

.. doxygenclass:: lilka::GFX
    :members:

Image
-----

.. doxygenclass:: lilka::Image
    :members:

Transform
---------

.. doxygenclass:: lilka::Transform
    :members:

Rendering while outputs are inactive
-------------------------------------

Automatic frames still service the existing inactivity policy and output power
transitions. During full-off sleep, render entry points return before retained
pixel copies, glyph drawing, overlay composition, or frame publication. Native
presentation also checks that the native route is selected. External-only mode
continues to render its shared frame without native panel/overlay work.

Startup initialization and one-time power/mode transitions are not render-loop
work. An already-open native SPI transaction must still finish with a matching
end, even if selection changes during it. Wake invalidation restores/repaints the
retained state on the LCD owner; no worker writes the native LCD directly.
