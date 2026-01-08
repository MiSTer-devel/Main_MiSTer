/*
Copyright 2005, 2006, 2007 Dennis van Weeren
Copyright 2008, 2009 Jakub Bednarski
Copyright 2012 Till Harbaum

This file is part of Minimig

Minimig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Minimig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "cdrom_io.h"
#include "fpga_io.h"
#include "input.h"
#include "menu.h"
#include "offload.h"
#include "osd.h"
#include "scheduler.h"
#include "user_io.h"
#include <ctype.h>
#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *version = "$VER:" VDATE;

int main(int argc, char *argv[]) {
  // --- DEBUG: REDIRECT LOGS TO FILE ---
  // Tenta abrir arquivo de log no SD card (/media/fat/mister_debug.log)
  FILE *log_file = freopen("/media/fat/mister_debug.log", "a+", stdout);
  if (log_file) {
    setvbuf(stdout, NULL, _IONBF, 0);     // Sem buffer no stdout
    dup2(fileno(stdout), fileno(stderr)); // Stderr também vai pro arquivo
    setvbuf(stderr, NULL, _IONBF, 0);     // Sem buffer no stderr

    printf("\n==========================================\n");
    printf("[DEBUG] MiSTer Log Started at %s\n", VDATE);
    printf("==========================================\n");
  } else {
    // Fallback: Tenta garantir que o console mostre algo se falhar abrir o
    // arquivo
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    fprintf(stderr,
            "\n[DEBUG] Failed to open log file, using console. Built: %s\n",
            VDATE);
  }
  // ------------------------------------

  // Always pin main worker process to core #1 as core #0 is the
  // hardware interrupt handler in Linux.  This reduces idle latency
  // in the main loop by about 6-7x.
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(1, &set);
  sched_setaffinity(0, sizeof(set), &set);

  offload_start();

  fpga_io_init();

  DISKLED_OFF;

  printf("\nMinimig by Dennis van Weeren");
  printf("\nARM Controller by Jakub Bednarski");
  printf("\nMiSTer code by Sorgelig\n\n");

  printf("Version %s\n\n", version + 5);

  if (argc > 1)
    printf("Core path: %s\n", argv[1]);
  if (argc > 2)
    printf("XML path: %s\n", argv[2]);

  if (!is_fpga_ready(1)) {
    printf(
        "\nGPI[31]==1. FPGA is uninitialized or incompatible core loaded.\n");
    printf("Quitting. Bye bye...\n");
    exit(0);
  }

  FindStorage();

  // Iniciar monitoramento de CD-ROM
  startCDROMMonitoring([](int index, bool present) {
    char msg[64];
    sprintf(msg, "CD-ROM %d %s", index, present ? "conectado" : "desconectado");
    // Usar log para console, já que OSD pode não estar visível no SSH
    printf("[CD-CHANGE-CALLBACK] %s\n", msg);

    // Tenta mandar pro OSD também
    OsdWrite(16, "", 1);
    OsdWrite(17, msg, 1);
    OsdWrite(18, "", 1);
  });

  user_io_init((argc > 1) ? argv[1] : "", (argc > 2) ? argv[2] : NULL);

#ifdef USE_SCHEDULER
  scheduler_init();
  scheduler_run();
#else
  while (1) {
    if (!is_fpga_ready(1)) {
      fpga_wait_to_reset();
    }

    user_io_poll();
    input_poll(0);
    HandleUI();
    OsdUpdate();
  }
#endif

  // Parar monitoramento de CD-ROM antes de sair
  stopCDROMMonitoring();
  return 0;
}
