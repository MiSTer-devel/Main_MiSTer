#include "restrict.h"
#include "input.h"

#include <string.h>

enum Restrictions
{
	SETTINGS = 1 << 0,
	BROWSING = 1 << 1,
	OPTIONS = 1 << 2,
	CORES = 1 << 3,
	LOAD = 1 << 4,
	CHEATS = 1 << 5,
	DIP_SWITCHES = 1 << 6,
	VOLUME = 1 << 7,
	MAPPING = 1 << 8,
};

static uint32_t restrictions;
static bool restrictions_enabled = true;
static char unlockCode[24];
static char enteredUnlockCode[24];

void Restrict_Init(const char *config, const char *code)
{
	restrictions = 0;

	strncpy( unlockCode, code, sizeof(unlockCode) - 1);

	const char *end = config + strlen(config);

	const char *p = config;

	while( p < end )
	{
		const char *e = strchr(p, ',');
		if(e == nullptr) e = end;

		int len = e - p;
		if( !strncasecmp(p, "settings", len) )
		{
			restrictions |= SETTINGS;
		}
		else if( !strncasecmp(p, "browsing", len) )
		{
			restrictions |= BROWSING;
		}
		else if( !strncasecmp(p, "options", len) )
		{
			restrictions |= OPTIONS;
		}
		else if( !strncasecmp(p, "cores", len) )
		{
			restrictions |= CORES;
		}
		else if( !strncasecmp(p, "cheats", len) )
		{
			restrictions |= CHEATS;
		}
		else if( !strncasecmp(p, "dip_switches", len) )
		{
			restrictions |= DIP_SWITCHES;
		}
		else if( !strncasecmp(p, "volume", len) )
		{
			restrictions |= VOLUME;
		}
		else if( !strncasecmp(p, "mapping", len) )
		{
			restrictions |= MAPPING;
		}
		else if( !strncasecmp(p, "load", len) )
		{
			restrictions |= LOAD;
		}
		else if( !strncasecmp(p, "all", len) )
		{
			restrictions = ~0;
		}

		p = e + 1;
	}
}

void Restrict_Enable()
{
	restrictions_enabled = true;
}

void Restrict_Disable()
{
	restrictions_enabled = false;
}

bool Restrict_AnySpecified()
{
	return restrictions != 0;
}

bool Restrict_Enabled()
{
	return restrictions_enabled;
}

bool Restrict_Settings()
{
	return restrictions_enabled && ( restrictions & SETTINGS );
}

bool Restrict_Unlock()
{
	return unlockCode[0] == '\0';
}

bool Restrict_FileBrowsing()
{
	return restrictions_enabled && ( restrictions & BROWSING );
}

bool Restrict_Cores()
{
	return restrictions_enabled && ( restrictions & CORES );
}

bool Restrict_Cheats()
{
	return restrictions_enabled && ( restrictions & CHEATS );
}

bool Restrict_DIPSwitches()
{
	return restrictions_enabled && ( restrictions & DIP_SWITCHES );
}

bool Restrict_Volume()
{
	return restrictions_enabled && ( restrictions & VOLUME );
}

bool Restrict_Mapping()
{
	return restrictions_enabled && ( restrictions & MAPPING );
}

bool Restrict_Load()
{
	return restrictions_enabled && ( restrictions & LOAD );
}

bool Restrict_Options( RestrictOverride override )
{
	if( !restrictions_enabled )
	{
		return false;
	}

	switch( override )
	{
		case RestrictOverride::None: return ( restrictions & OPTIONS );
		case RestrictOverride::Allowed: return false;
		case RestrictOverride::Restricted: return ( restrictions & OPTIONS );
	}
	return false;
}

bool Restrict_Toggle( RestrictOverride override )
{
	if( !restrictions_enabled )
	{
		return false;
	}

	switch( override )
	{
		case RestrictOverride::None: return false;
		case RestrictOverride::Allowed: return false;
		case RestrictOverride::Restricted: return ( restrictions & OPTIONS );
	}
	return false;
}

void Restrict_StartUnlock()
{
	enteredUnlockCode[0] = '\0';
}

int Restrict_UnlockLength()
{
	return strlen(unlockCode);
}


int Restrict_HandleUnlock(uint32_t keycode)
{
	int len = strlen(enteredUnlockCode);
	if( keycode == 0 )
	{
		return len;
	}

	if( keycode & UPSTROKE )
	{
		if( !strcmp( unlockCode, enteredUnlockCode ) )
		{
			Restrict_Disable();
			return -1;
		}

		if( len >= (int)strlen( unlockCode ) )
		{
			return -1;
		}

		return len;
	}

	char c = 0;
	switch( keycode )
	{
		case KEY_UP: c = 'U'; break;
		case KEY_DOWN: c = 'D'; break;
		case KEY_LEFT: c = 'L'; break;
		case KEY_RIGHT: c = 'R'; break;
		case KEY_ENTER:
		case KEY_SPACE:
		case KEY_KPENTER:
			c = 'A';
			break;
		case KEY_BACK:
		case KEY_BACKSPACE:
		case KEY_ESC:
			c = 'B';
			break;
		default: break;
	}

	if( c )
	{
		enteredUnlockCode[len] = c;
		enteredUnlockCode[len + 1] = '\0';
		len++;
	}

	return len;
}
