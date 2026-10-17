#ifndef SATURN_RAMCART_H
#define SATURN_RAMCART_H

#include <stddef.h>
#include <stdint.h>

#define SATURN_RAM_CART_NONE 0
#define SATURN_RAM_CART_1M   2
#define SATURN_RAM_CART_4M   3

#define SATURN_RAM_CART_BOOT_OFFSET 0x100
#define SATURN_RAM_CART_BOOT_TAG    0xA1
#define SATURN_RAM_CART_DB_PATH "saturn_ramcart.txt"
#define SATURN_RAM_CART_USER_DB_PATH "saturn_ramcart_user.txt"

bool saturn_ramcart_make_uuid(const uint8_t *boot_header, char *uuid, size_t uuid_size);
bool saturn_ramcart_parse_db_line(const char *line, const char *uuid, uint32_t *cart_type);
const char *saturn_ramcart_type_name(uint32_t cart_type);

#endif
