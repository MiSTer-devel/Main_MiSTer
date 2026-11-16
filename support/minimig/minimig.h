
#ifndef __MINIMIG_H__
#define __MINIMIG_H__

#include "minimig_config.h"
#include "minimig_boot.h"
#include "minimig_fdd.h"
#include "minimig_share.h"
#include "minimig_a2065.h"

char is_minimig();
void minimig_send_keycode(unsigned short key, int press);
void minimig_rtc_reset();

#endif
