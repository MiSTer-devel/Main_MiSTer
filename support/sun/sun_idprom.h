#ifndef SUN_IDPROM_H
#define SUN_IDPROM_H

// Sends the 32-byte ID PROM on index 64: boot1.rom, else one built from this MiSTer's MAC.
// The SPARCstation takes only the MAC's low three bytes, as a blank NVRAM's serial.
void sun_idprom_send(void);

#endif
