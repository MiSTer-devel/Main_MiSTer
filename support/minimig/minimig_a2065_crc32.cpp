/*
 * crc32.cpp — CRC-32 for Ethernet FCS
 *
 * Standard Ethernet CRC32 (polynomial 0xEDB88320, reflected).
 * Used to append FCS bytes to received frames before writing to RX ring,
 * since AF_PACKET does not include FCS.
 */

#include <stdint.h>

static uint32_t crc32_table[256];
static int table_built = 0;

static void build_table(void)
{
    for (int i = 0; i < 256; i++) {
        uint32_t c = (uint32_t)i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    table_built = 1;
}

uint32_t crc32_compute(const uint8_t *data, int len)
{
    if (!table_built) build_table();
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++)
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
