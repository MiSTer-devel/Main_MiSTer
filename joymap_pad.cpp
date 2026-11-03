#include <string.h>

#include "joymap_pad.h"
#include "joymap_pads.h"
#include "input.h"
#include "osd.h"

uint32_t joymap_pad_controls_for_pos(int pos)
{
	switch (pos)
	{
	case -6: return PAD_BIT(PAD_RIGHT);
	case -5: return PAD_BIT(PAD_DOWN);
	case -4: return PAD_BIT(PAD_L3_RIGHT);
	case -3: return PAD_BIT(PAD_L3_DOWN);
	case -2: return PAD_BIT(PAD_R3_RIGHT);
	case -1: return PAD_BIT(PAD_R3_DOWN);
	}

	if (pos >= SYS_BTN_RIGHT && pos <= SYS_BTN_R) return PAD_BIT(PAD_RIGHT + pos - SYS_BTN_RIGHT);
	if (pos == SYS_MAP_BTN_L2 + 2) return PAD_BIT(PAD_L3) | PAD_BIT(PAD_L3_RIGHT) | PAD_BIT(PAD_L3_DOWN);
	if (pos == SYS_MAP_BTN_L2 + 3) return PAD_BIT(PAD_R3) | PAD_BIT(PAD_R3_RIGHT) | PAD_BIT(PAD_R3_DOWN);
	if (pos >= SYS_MAP_BTN_L2 && pos < SYS_MAP_BTN_SELECT) return PAD_BIT(PAD_L2 + pos - SYS_MAP_BTN_L2);
	if (pos == SYS_MAP_BTN_SELECT) return PAD_BIT(PAD_SELECT);
	if (pos == SYS_MAP_BTN_SELECT + 1) return PAD_BIT(PAD_START);
	if (pos == SYS_MAP_POS_OSD) return PAD_BIT(PAD_HOME);
	return 0;
}

uint32_t joymap_pad_controls_for_map(const uint32_t *map)
{
	if (!map) return 0;

	uint32_t controls = 0;
	for (int i = SYS_BTN_RIGHT; i <= SYS_BTN_R; i++) if (map[i]) controls |= PAD_BIT(PAD_RIGHT + i - SYS_BTN_RIGHT);
	for (int i = 0; i < 4; i++) if (map[SYS_BTN_L2 + i]) controls |= PAD_BIT(PAD_L2 + i);
	if (map[SYS_BTN_SELECT]) controls |= PAD_BIT(PAD_SELECT);
	if (map[SYS_BTN_START]) controls |= PAD_BIT(PAD_START);
	if (map[SYS_BTN_OSD_1] || map[SYS_BTN_OSD_2]) controls |= PAD_BIT(PAD_HOME);
	if (map[SYS_AXIS1_X]) controls |= PAD_BIT(PAD_L3_RIGHT);
	if (map[SYS_AXIS1_Y]) controls |= PAD_BIT(PAD_L3_DOWN);
	if (map[SYS_AXIS2_X]) controls |= PAD_BIT(PAD_R3_RIGHT);
	if (map[SYS_AXIS2_Y]) controls |= PAD_BIT(PAD_R3_DOWN);
	return controls;
}

int joymap_pad_compose_row(int pad, int row, uint32_t flash, uint8_t *cols)
{
	const pad_bitmap *p = &joymap_pads[pad];
	memcpy(cols, p->pixels + row * p->w, p->w);

	for (int i = 0; i < p->count; i++)
	{
		const pad_control *c = &p->controls[i];
		if (!(flash & PAD_BIT(c->id)) || row < c->row || row >= c->row + c->rows) continue;

		const uint8_t *fill = c->fill + (row - c->row) * c->w;
		const uint8_t *glyph = c->glyph + (row - c->row) * c->w;
		for (int x = 0; x < c->w; x++) cols[c->x + x] = (cols[c->x + x] | fill[x]) ^ glyph[x];
	}

	return p->w;
}

void joymap_pad_draw(int pad, int first_row, uint32_t flash)
{
	if (pad < 0 || pad >= JOYMAP_PAD_COUNT) pad = JOYMAP_PAD_PSX;

	uint8_t cols[256];
	for (int row = 0; row < joymap_pads[pad].rows; row++)
	{
		const int w = joymap_pad_compose_row(pad, row, flash, cols);
		OsdDrawBitmapRow(first_row + row, cols, w);
	}
}
