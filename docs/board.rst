``Board``: Керування платою
===========================

.. doxygenvariable:: lilka::board

.. doxygenclass:: lilka::Board
    :members:

.. doxygenenum:: lilka::ExtPin


External backlight mirroring
---------------------------

On opted-in Lilka v2 builds with independent native backlight initialized,
``brightness.attachExternalBacklight(pin)`` adds a released-on, low-off external
BLK input on an extension pin. Use a series resistor and only a module whose
control behavior has been electrically checked. GPIO output is open drain;
channel6 shares the reserved 20 kHz timer3 with native channel7. The peripheral
signal is attached without a transient push-pull HIGH setup.

``setActiveDisplays(native, external)`` changes routing, not the saved brightness.
Existing brightness/dim/suspend/resume apply to both outputs. Detach releases the
external pad (module default illumination). ``isPinReserved`` identifies native
and attached external pins to safe wrappers. Stock/unsupported builds remain
unchanged and reject external attachment; no new worker or persistence key exists.
