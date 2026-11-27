#include <strings.h>
#include "folder_step.h"

static int pending_code = FOLDER_NONE;
static int core_active = 0;
static int core_slot = 0;
static int last_counter = -1;

int folder_step(const std::vector<std::string> &list, int current, int code, unsigned rnd)
{
	const int n = (int)list.size();
	if (n <= 0) return -1;
	if (code < FOLDER_NEXT || code > FOLDER_FIRST) return -1;

	if (code == FOLDER_FIRST) return 0;

	if (current < 0 || current >= n)
	{
		if (code == FOLDER_RANDOM) return (int)(rnd % (unsigned)n);
		return 0;
	}

	switch (code)
	{
	case FOLDER_NEXT:
		return (current + 1) % n;
	case FOLDER_PREV:
		return (current + n - 1) % n;
	default: // FOLDER_RANDOM
		if (n == 1) return 0;
		{
			int r = (int)(rnd % (unsigned)(n - 1));
			return (r >= current) ? r + 1 : r;
		}
	}
}

int folder_find(const std::vector<std::string> &list, const char *name)
{
	int pos = -1;
	for (int i = 0; i < (int)list.size(); i++)
	{
		if (list[i] == name) return i;
		if (pos < 0 && !strcasecmp(list[i].c_str(), name)) pos = i;
	}
	return pos;
}

int folder_poll(uint16_t reply, uint16_t (*next_word)(void))
{
	if ((reply & 0xF0) != 0xA0) return 0;

	core_active = 1;
	core_slot = reply >> 8;

	int counter = reply & 0xF;
	if (counter != last_counter)
	{
		int code = next_word() & 0xF;
		last_counter = counter;
		if (code >= FOLDER_NEXT) pending_code = code;
	}
	return 1;
}

int folder_request_take()
{
	int code = pending_code;
	pending_code = FOLDER_NONE;
	return code;
}

int folder_slot()
{
	return core_slot;
}

int folder_core_active()
{
	return core_active;
}

void folder_reset()
{
	pending_code = FOLDER_NONE;
	core_active = 0;
	core_slot = 0;
	last_counter = -1;
}
