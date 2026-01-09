#ifndef CDROM_IO_H
#define CDROM_IO_H

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

// Tipo de callback para notificação de mudanças
typedef void (*CDROMStatusCallback)(int index, bool present);

enum DiscType {
  DISC_UNKNOWN = 0,
  DISC_MEGACD,
  DISC_SATURN,
  DISC_PSX,
  DISC_NEOGEO
};

// Estrutura para armazenar o estado dos CD-ROMs
struct CDROMState {
  bool present;
  bool media_present;
  DiscType disc_type;
  char path[32];
  time_t last_check;
};

// Estrutura simplificada para TOC (limitada ao uso basico)
struct CDROM_TOC {
  int min, sec, frame;
};

struct CDROM_TrackInfo {
  int start_lba;
  int end_lba;
  int type; // 0=audio, 1=data (simplificado)
};

// Constantes
const int CHECK_INTERVAL = 2; // Intervalo em segundos entre verificações

// Funções
bool check_cdrom_state(int index);
bool hasCDROMMedia(int index);
DiscType getCDROMType(int index);
int isCDROMPresent();

// Funções de monitoramento hot-plug
void startCDROMMonitoring(CDROMStatusCallback callback);
void stopCDROMMonitoring();
bool isCDROMPresent(int index);

// Função para ler setor do CD físico
int read_cdrom_sector(int index, int lba, unsigned char *buffer,
                      int sector_size);

// Função para ler TOC do CD físico (retorna numero de tracks, preenche array)
int read_cdrom_toc(int index, CDROM_TrackInfo *tracks, int max_tracks);

#endif // CDROM_IO_H