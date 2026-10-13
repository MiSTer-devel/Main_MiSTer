#include <strings.h>
#include "../../user_io.h"
#include "pc88.h"

char is_pc88()
{
	return !strcasecmp(user_io_get_core_name(1), "PC8801");
}
