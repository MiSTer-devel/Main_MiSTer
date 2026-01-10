#include "debug_log.h"
#include <time.h>

static FILE *log_file = NULL;

void DebugLog(const char *fmt, ...) {
  if (!log_file) {
    log_file = fopen("/tmp/mcd_debug.log", "w");
  }

  if (log_file) {
    // Timestamp
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(log_file, "[%lu.%03lu] ", ts.tv_sec % 1000, ts.tv_nsec / 1000000);

    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);

    // Ensure immediate write so we don't lose logs on crash/reset
    fflush(log_file);
  }
}
