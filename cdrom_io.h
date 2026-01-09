#ifndef CDROM_IO_H
#define CDROM_IO_H

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

// Tipo de callback para notificação de mudanças
typedef void (*CDROMStatusCallback)(int index, bool present);

// Estrutura para armazenar o estado dos CD-ROMs
struct CDROMState {
  bool present;
  bool media_present;
  char path[32];
  time_t last_check;
};

// Constantes
const int CHECK_INTERVAL = 2; // Intervalo em segundos entre verificações

// Funções
bool check_cdrom_state(int index);
bool hasCDROMMedia(int index);
int isCDROMPresent();

// Funções de monitoramento hot-plug
void startCDROMMonitoring(CDROMStatusCallback callback);
void stopCDROMMonitoring();
bool isCDROMPresent(int index);

#endif // CDROM_IO_H