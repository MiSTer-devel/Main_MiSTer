#ifndef MINIMIG_A2065_DEBUG_H
#define MINIMIG_A2065_DEBUG_H

#include <stdio.h>

/* Runtime debug switch, defined in minimig_a2065_registers.cpp. Off by
 * default; set it to 1 while tracing the LANCE register/ring paths. DBG(...)
 * is a drop-in for fprintf(stderr, ...) that only emits when enabled, so the
 * hot paths stay quiet. */
extern int a2065_debug;

/* Writes a "yyyymmdd-hhmmss.xxx " timestamp prefix to stderr (local time,
 * milliseconds). Defined in minimig_a2065_registers.cpp. */
void a2065_log_prefix(void);

/* LOG: always emitted (lifecycle/errors). DBG: only when a2065_debug is set.
 * Both prefix every line with a timestamp. */
#define LOG(...) do { a2065_log_prefix(); fprintf(stderr, __VA_ARGS__); } while (0)
#define DBG(...) do { if (a2065_debug) { a2065_log_prefix(); fprintf(stderr, __VA_ARGS__); } } while (0)

#endif
