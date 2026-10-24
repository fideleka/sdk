#pragma once

// Match Keira's compile-time language selection: Ukrainian by default,
// LANG_EN for English, and an explicit LANG_UK takes precedence.
#if defined(LANG_EN) && !defined(LANG_UK)
#    define LILKA_S_DISPLAY_INTERNAL "Internal"
#    define LILKA_S_DISPLAY_EXTERNAL "External"
#    define LILKA_S_DISPLAY_BOTH     "Both"
#else
#    define LILKA_S_DISPLAY_INTERNAL "Внутрішній"
#    define LILKA_S_DISPLAY_EXTERNAL "Зовнішній"
#    define LILKA_S_DISPLAY_BOTH     "Обидва"
#endif
