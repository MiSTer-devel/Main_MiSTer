#include "cdrom_io.h"
#include "osd.h"
#include <pthread.h>
#include <unistd.h>

// Variáveis globais
static CDROMState cdrom_states[4] = {};
static bool monitoring_active = false;
static pthread_t monitor_thread;
static CDROMStatusCallback active_callback = nullptr;
static pthread_mutex_t monitor_mutex = PTHREAD_MUTEX_INITIALIZER;

// Verifica mudanças no estado de um CD-ROM específico
bool check_cdrom_state(int index) {
  char path[32];
  sprintf(path, "/dev/sr%d", index);

  struct stat st;
  int stat_res = stat(path, &st);
  bool is_block = (stat_res == 0 && S_ISBLK(st.st_mode));
  bool currently_present = is_block;

  // DEBUG: Logar estado no console para verificar o que está acontecendo
  printf("[CDROM] Check %s: stat=%d, is_block=%d, present=%d (stored=%d)\n",
         path, stat_res, is_block, currently_present,
         cdrom_states[index].present);

  // Se o estado mudou
  if (currently_present != cdrom_states[index].present) {
    cdrom_states[index].present = currently_present;
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