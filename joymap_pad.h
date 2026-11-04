#ifndef JOYMAP_PAD_H
#define JOYMAP_PAD_H

#include <stdint.h>

enum { JOYMAP_PAD_PSX, JOYMAP_PAD_COUNT };

enum
{
	PAD_RIGHT, PAD_LEFT, PAD_DOWN, PAD_UP, PAD_A, PAD_B, PAD_X, PAD_Y, PAD_L, PAD_R,
	PAD_SELECT, PAD_START, PAD_L2, PAD_R2, PAD_L3, PAD_R3, PAD_HOME,
	PAD_L3_RIGHT, PAD_L3_DOWN, PAD_R3_RIGHT, PAD_R3_DOWN, PAD_CONTROL_COUNT
};

#define PAD_BIT(c) (1u << (c))

typedef struct
{
	uint8_t id;
	uint8_t x, w, row, rows;
	const uint8_t *fill, *glyph;
} pad_control;

typedef struct
{
	uint8_t w, rows;
	const uint8_t *pixels;
	const pad_control *controls;
	uint8_t count;
} pad_bitmap;

uint32_t joymap_pad_controls_for_pos(int pos);
uint32_t joymap_pad_controls_for_map(const uint32_t *map);

int joymap_pad_compose_row(int pad, int row, uint32_t flash, uint8_t *cols);

void joymap_pad_draw(int pad, int first_row, uint32_t flash);

#endif
