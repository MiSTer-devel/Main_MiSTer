#ifndef GAME_DOCS_H
#define GAME_DOCS_H

#include <stdint.h>

void game_docs_init(const char *rom_path, uint32_t romcrc);
int game_docs_manual_available();
const char *game_docs_get_manual();

#endif
