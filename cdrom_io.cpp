#include "cdrom_io.h"
#include "osd.h"
#include <pthread.h>

// Variáveis globais
static CDROMState cdrom_states[4] = {0};
static time_t last_global_check = 0;
static bool monitoring_active = false;
static pthread_t monitor_thread;
static CDROMStatusCallback active_callback = nullptr;
static pthread_mutex_t monitor_mutex = PTHREAD_MUTEX_INITIALIZER;

// Thread de monitoramento
static void* cdrom_monitor_thread(void* arg) {
    while (monitoring_active) {
        bool changes = false;
        
        for (int i = 0; i < 4; i++) {
            bool old_state = cdrom_states[i].present;
            if (check_cdrom_state(i)) {
                changes = true;
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
    return NULL;
}

// Iniciar monitoramento com callback
void startCDROMMonitoring(CDROMStatusCallback callback) {
    pthread_mutex_lock(&monitor_mutex);
    if (!monitoring_active) {
        active_callback = callback;
        monitoring_active = true;
        pthread_create(&monitor_thread, NULL, cdrom_monitor_thread, NULL);
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

// Verifica mudanças no estado de um CD-ROM específico
bool check_cdrom_state(int index) {
    char path[32];
    sprintf(path, "/dev/sr%d", index);
    
    struct stat st;
    bool currently_present = (stat(path, &st) == 0 && S_ISBLK(st.st_mode));
    
    // Se o estado mudou
    if (currently_present != cdrom_states[index].present) {
        cdrom_states[index].present = currently_present;
        strcpy(cdrom_states[index].path, path);
        
        char msg[64];
        if (currently_present) {
            sprintf(msg, "CD-ROM conectado em %s", path);
        } else {
            sprintf(msg, "CD-ROM desconectado de %s", path);
        }
        
        OsdWrite(16, "", 1);
        OsdWrite(17, msg, 1);
        OsdWrite(18, "", 1);
        
        return true;
    }
    
    return false;
}

// Verifica se existe um dispositivo CD-ROM USB conectado
int isCDROMPresent() {
    time_t current_time = time(NULL);
    
    // Só verifica a cada CHECK_INTERVAL segundos
    if (difftime(current_time, last_global_check) >= CHECK_INTERVAL) {
        last_global_check = current_time;
        
        bool any_present = false;
        for (int i = 0; i < 4; i++) {
            if (check_cdrom_state(i)) {
                // Estado mudou, notificar o sistema
                if (cdrom_states[i].present) {
                    any_present = true;
                }
            } else if (cdrom_states[i].present) {
                any_present = true;
            }
        }
        
        if (!any_present) {
            OsdWrite(16, "", 1);
            OsdWrite(17, "Nenhum CD-ROM detectado", 1);
            OsdWrite(18, "", 1);
        }
        
        return any_present ? 1 : 0;
    }
    
    // Retorna o último estado conhecido se não for hora de verificar
    for (int i = 0; i < 4; i++) {
        if (cdrom_states[i].present) return 1;
    }
    
    return 0;
} 