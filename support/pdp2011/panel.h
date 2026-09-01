#pragma once

#include <stddef.h>

#define UIO_PDP_PANEL 0x50

/* Fill lines[0] (PC/PSW/RUN) and lines[1] (*PC, MA, D). Returns 1 if
 * either line should be redrawn. */
int pdp2011_panel_banner(char lines[2][32], int force);
