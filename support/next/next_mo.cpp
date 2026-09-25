#include <string.h>
#include "next_mo.h"
#include "next_rs.h"

static uint8_t ecc[NEXT_RS_DISK];
static uint8_t ecc_fail;
static uint8_t ecc_count;

void next_mo_command(uint32_t lba, const uint8_t *buf, int sz)
{
	if (sz < NEXT_MO_ECC_BYTES) return;
	unsigned op = (lba >> 8) & 0xFF;
	memcpy(ecc, buf, NEXT_MO_ECC_BYTES);
	ecc_fail = 0;
	ecc_count = 0;
	if (op == NEXT_MO_ECC_ENCODE) next_rs_encode(ecc);
	else if (op == NEXT_MO_ECC_DECODE)
	{
		int e = next_rs_decode(ecc);
		if (e < 0) ecc_fail = 1;
		else ecc_count = (uint8_t)((e > 255) ? 255 : e);
	}
}

void next_mo_fill(uint32_t lba, uint8_t *buf, int sz)
{
	memset(buf, 0, sz);
	if (sz < NEXT_MO_ECC_COUNT + 1) return;
	if ((lba & 0xFF000000u) != NEXT_MO_ECC_BLK) return;
	memcpy(buf, ecc, NEXT_MO_ECC_BYTES);
	buf[NEXT_MO_ECC_FAIL]  = ecc_fail;
	buf[NEXT_MO_ECC_COUNT] = ecc_count;
}
