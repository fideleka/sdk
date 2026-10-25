#pragma once

// Production keeps errors. Informational/debug output is an explicit build opt-in.
// 1 = errors only; 2 = errors and informational records (including profiling).
#ifndef LILKA_LOG_LEVEL
#    define LILKA_LOG_LEVEL 1
#endif
#if LILKA_LOG_LEVEL != 1 && LILKA_LOG_LEVEL != 2
#    error "LILKA_LOG_LEVEL must be 1 or 2"
#endif
