#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "saturn_ramcart.h"

bool saturn_ramcart_make_uuid(const uint8_t *boot_header, char *uuid, size_t uuid_size)
{
	if (!boot_header || !uuid || !uuid_size) return false;

	uuid[0] = 0;
	if (memcmp(boot_header, "SEGA SEGASATURN ", 16)) return false;

	size_t len = 10;
	while (len && (boot_header[0x20 + len - 1] == ' ' || !boot_header[0x20 + len - 1])) len--;
	if (!len || len + 1 > uuid_size) return false;

	for (size_t i = 0; i < len; i++)
	{
		unsigned char c = boot_header[0x20 + i];
		if (isspace(c) || !c) return false;
		uuid[i] = toupper(c);
	}

	uuid[len] = 0;
	return true;
}

bool saturn_ramcart_parse_db_line(const char *line, const char *uuid, uint32_t *cart_type)
{
	if (!line || !uuid || !cart_type) return false;

	char db_uuid[32];
	char db_cart[16];

	const char *s = line;
	if (!strncmp(s, "\xEF\xBB\xBF", 3)) s += 3;
	while (*s && isspace((unsigned char)*s)) s++;
	if (!*s || *s == '#' || *s == ';') return false;

	int end;
	if (sscanf(s, "%31s %15[^ \t\r\n\v\f#;]%n", db_uuid, db_cart, &end) != 2) return false;
	s += end;
	while (*s && isspace((unsigned char)*s)) s++;
	if (*s && *s != '#' && *s != ';') return false;
	if (strcasecmp(db_uuid, uuid)) return false;

	if (!strcasecmp(db_cart, "NONE"))
	{
		*cart_type = SATURN_RAM_CART_NONE;
		return true;
	}

	if (!strcasecmp(db_cart, "1M"))
	{
		*cart_type = SATURN_RAM_CART_1M;
		return true;
	}

	if (!strcasecmp(db_cart, "4M"))
	{
		*cart_type = SATURN_RAM_CART_4M;
		return true;
	}

	return false;
}

const char *saturn_ramcart_type_name(uint32_t cart_type)
{
	switch (cart_type)
	{
	case SATURN_RAM_CART_1M:
		return "DRAM 1M";
	case SATURN_RAM_CART_4M:
		return "DRAM 4M";
	default:
		return "None";
	}
}
