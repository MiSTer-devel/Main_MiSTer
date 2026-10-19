// HP 9000 family (HP9K715, HP9K385): user_io hooks, each gated on the family.

#ifndef HP9K_H
#define HP9K_H

#include <stdint.h>
#include "../../file_io.h"

enum hp9k_model_t
{
	HP9K_NONE,
	HP9K_715,
	HP9K_385,
};

hp9k_model_t hp9k_model();
char is_hp9k_family();

const char *hp9k_net_status();

int hp9k_cdrom_slot();

// bit n: slot n is behind the hard-disk write buffer
#define HP9K_DISK_SLOTS 4
int hp9k_buffered_disks();

// Returns 0 to fail the mount; a translated CD clears *writable and sets f->size.
int hp9k_mount_hook(int index, const char *name, fileTYPE *f, int *writable);
void hp9k_unmount(int index);

void hp9k_poll();

// 0 = not ours, 1 = serviced, -1 = ours but unsupported (caller breaks the loop).
int hp9k_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);

#endif
