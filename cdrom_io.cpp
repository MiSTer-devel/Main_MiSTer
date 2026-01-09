#include "cdrom_io.h"
#include "osd.h"
#include <fcntl.h>
#include <linux/cdrom.h>

#ifndef CDROM_DRIVE_STATUS
#define CDROM_DRIVE_STATUS 0x5326
#endif
#ifndef CDS_DISC_OK
#define CDS_DISC_OK 4
#endif
#include <pthread.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Variáveis globais
static CDROMState cdrom_states[4] = {};
static bool monitoring_active = false;
static pthread_t monitor_thread;
static CDROMStatusCallback active_callback = nullptr;
static pthread_mutex_t monitor_mutex = PTHREAD_MUTEX_INITIALIZER;

// Helper to identify disc type
static DiscType identify_disc(int fd) {
  unsigned char buffer[2352]; // Buffer for one sector
  DiscType type = DISC_UNKNOWN;

  // 1. Check Sector 0 (LBA 0) for SEGA/SATURN/NEO-GEO
  // Some systems (like Saturn) have header at LBA 0
  lseek(fd, 0, SEEK_SET);
  if (read(fd, buffer, 2048) > 0) {
    if (memcmp(buffer, "SEGADISCSYSTEM", 14) == 0)
      type = DISC_MEGACD;
    else if (memcmp(buffer, "SEGA SEGASATURN", 15) == 0)
      type = DISC_SATURN;
    // NeoGeo CD check (simple heuristic, can be improved)
    // Checks for "NEO-GEO" string which sometimes appears in header/boot files
    // A more robust check might be needed if this fails.
    // For now, let's check for "NEO-GEO" in the first sector (common in some
    // dumps). else if (memmem(buffer, 2048, "NEO-GEO", 7)) type = DISC_NEOGEO;
  }

  if (type != DISC_UNKNOWN)
    return type;

  // 2. Check Sector 16 (LBA 16) - ISO9660 Header
  // Sega CD often has "SEGADISCSYSTEM" here too.
  lseek(fd, 16 * 2048, SEEK_SET);
  if (read(fd, buffer, 2048) > 0) {
    if (memcmp(buffer + 1, "CD001", 5) == 0) { // ISO9660
      // Check system identifier in ISO header (offset 8)
      if (memcmp(buffer + 8, "SEGADISCSYSTEM", 14) == 0)
        type = DISC_MEGACD;
      else if (memcmp(buffer + 8, "SEGA SEGASATURN", 15) == 0)
        type = DISC_SATURN;
    }
  }

  if (type != DISC_UNKNOWN)
    return type;

  // 3. Check PSX License at Sector 4 (LBA 4 ? or absolute 4)
  // PSX license string is usually at the start of the 4th sector (LBA 4 if
  // cooked, or ~sector 16 if absolute 154?) In psx.cpp, it reads
  // "license_sector = 154" (which is 2 seconds pregap + 4 sectors). USB drives
  // often expose LBA 0 as the start of data track. Let's try LBA 4 and LBA 16
  // just in case.

  // Try LBA 4 (Sector 4)
  lseek(fd, 4 * 2048, SEEK_SET);
  if (read(fd, buffer, 2352) > 0) {
    if (memmem(buffer, 2048, "Sony Computer Entertainment", 27))
      return DISC_PSX;
  }

  return DISC_UNKNOWN;
}

// Verifica mudanças no estado de um CD-ROM específico
bool check_cdrom_state(int index) {
  char path[32];
  sprintf(path, "/dev/sr%d", index);

  struct stat st;
  int stat_res = stat(path, &st);
  bool is_block = (stat_res == 0 && S_ISBLK(st.st_mode));
  bool currently_present = is_block;
  bool current_media = false;

  if (currently_present) {
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd >= 0) {
      int status = ioctl(fd, CDROM_DRIVE_STATUS, 0);
      if (status == CDS_DISC_OK) {
        current_media = true;
        if (!cdrom_states[index].media_present) {
          // Media just inserted, identify it
          cdrom_states[index].disc_type = identify_disc(fd);
        }
      } else {
        cdrom_states[index].disc_type = DISC_UNKNOWN;
      }
      close(fd);
    }
  } else {
    cdrom_states[index].disc_type = DISC_UNKNOWN;
  }

  // DEBUG: Logar estado no console para verificar o que está acontecendo
  // printf("[CDROM] Check %s: stat=%d, is_block=%d, present=%d, media=%d\n",
  // path, stat_res, is_block, currently_present, current_media);

  // Se o estado mudou (presença ou mídia)
  if (currently_present != cdrom_states[index].present ||
      current_media != cdrom_states[index].media_present) {
    cdrom_states[index].present = currently_present;
    cdrom_states[index].media_present = current_media;
    strcpy(cdrom_states[index].path, path);

    char msg[64];
    if (currently_present) {
      sprintf(msg, "CD-ROM CONNECTED: %s", path);
    } else {
      sprintf(msg, "CD-ROM REMOVED: %s", path);
    }

    // printf("[CDROM] OSD Message: %s\n", msg);
    // OsdWrite(16, "                  ", 1); // Limpar linha
    // OsdWrite(16, "DEBUG: CD CHANGE", 1);
    // OsdWrite(17, msg, 1);
    // OsdWrite(18, "", 1);

    return true;
  }

  return false;
}

// Thread de monitoramento
static void *cdrom_monitor_thread(void *arg) {
  (void)arg; // Unused parameter
  printf("[CDROM] Monitor Thread Started\n");
  // OsdWrite(15, "Debug: CD Thread ON", 1); // Aviso visual que a thread
  // iniciou

  while (monitoring_active) {
    // bool changes = false; // Unused variable removed

    for (int i = 0; i < 4; i++) {
      bool old_state = cdrom_states[i].present;
      if (check_cdrom_state(i)) {
        // changes = true;
        // Se houve mudança de estado, notificar callback
        if (old_state != cdrom_states[i].present) {
          pthread_mutex_lock(&monitor_mutex);
          if (active_callback) {
            active_callback(i, cdrom_states[i].present);
          }
          pthread_mutex_unlock(&monitor_mutex);
        }
      }
    }

    // Dormir por CHECK_INTERVAL segundos
    sleep(CHECK_INTERVAL);
  }
  printf("[CDROM] Monitor Thread Stopped\n");
  return NULL;
}

// Iniciar monitoramento com callback
void startCDROMMonitoring(CDROMStatusCallback callback) {
  printf("[CDROM] Requesting startCDROMMonitoring...\n");
  pthread_mutex_lock(&monitor_mutex);
  if (!monitoring_active) {
    active_callback = callback;
    monitoring_active = true;
    pthread_create(&monitor_thread, NULL, cdrom_monitor_thread, NULL);
    printf("[CDROM] Thread created.\n");
  } else {
    printf("[CDROM] Thread already active.\n");
  }
  pthread_mutex_unlock(&monitor_mutex);
}

// Parar monitoramento
void stopCDROMMonitoring() {
  pthread_mutex_lock(&monitor_mutex);
  if (monitoring_active) {
    monitoring_active = false;
    pthread_mutex_unlock(&monitor_mutex);
    pthread_join(monitor_thread, NULL);
    pthread_mutex_lock(&monitor_mutex);
    active_callback = nullptr;
  }
  pthread_mutex_unlock(&monitor_mutex);
}

// Verifica se existe um dispositivo CD-ROM USB conectado
bool isCDROMPresent(int index) {
  if (index < 0 || index >= 4)
    return false;
  return cdrom_states[index].present;
}

bool hasCDROMMedia(int index) {
  if (index < 0 || index >= 4)
    return false;
  return cdrom_states[index].media_present;
}

DiscType getCDROMType(int index) {
  if (index < 0 || index >= 4)
    return DISC_UNKNOWN;
  return cdrom_states[index].disc_type;
}