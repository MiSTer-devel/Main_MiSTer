#include <string.h>

#include "../../user_io.h"
#include "core_ids.h"

char is_cpc()
{
	static int is_cpc_type = 0;
	if (!is_cpc_type && user_io_get_core_name(1)[0]) is_cpc_type = strcasecmp(user_io_get_core_name(1), "amstrad") ? 2 : 1;
	return (is_cpc_type == 1);
}

char is_zx81()
{
	static int is_zx81_type = 0;
	if (!is_zx81_type && user_io_get_core_name(1)[0]) is_zx81_type = strcasecmp(user_io_get_core_name(1), "zx81") ? 2 : 1;
	return (is_zx81_type == 1);
}

char is_gba()
{
	static int is_gba_type = 0;
	if (!is_gba_type && user_io_get_core_name(1)[0]) is_gba_type = strcasecmp(user_io_get_core_name(1), "GBA") ? 2 : 1;
	return (is_gba_type == 1);
}

char is_electron()
{
	static int is_electron_type = 0;
	if (!is_electron_type && user_io_get_core_name(1)[0]) is_electron_type = strcasecmp(user_io_get_core_name(1), "AcornElectron") ? 2 : 1;
	return (is_electron_type == 1);
}

char is_uneon()
{
	static int is_uneon_type = 0;
	if (!is_uneon_type && user_io_get_core_name(1)[0]) is_uneon_type = strcasecmp(user_io_get_core_name(1), "Uneon") ? 2 : 1;
	return (is_uneon_type == 1);
}
