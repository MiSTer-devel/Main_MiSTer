#if !defined( RESTRICT_H )
#define RESTRICT_H 1

#include <stdint.h>

enum class RestrictOverride
{
	None,
	Restricted,
	Allowed
};

void Restrict_Init(const char *config, const char *code);

bool Restrict_FileBrowsing();
bool Restrict_Settings();
bool Restrict_Cores();
bool Restrict_Unlock();
bool Restrict_Cheats();
bool Restrict_DIPSwitches();
bool Restrict_Volume();
bool Restrict_Mapping();
bool Restrict_Load();

bool Restrict_Options( RestrictOverride override );
bool Restrict_Toggle( RestrictOverride override );

void Restrict_StartUnlock();
int Restrict_HandleUnlock(uint32_t keycode);
int Restrict_UnlockLength();

void Restrict_Enable();
void Restrict_Disable();
bool Restrict_AnySpecified();
bool Restrict_Enabled();

#endif