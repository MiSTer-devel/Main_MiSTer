#ifndef HDMI_CEC_H
#define HDMI_CEC_H

#include <stdbool.h>

bool cec_init(bool enable);
void cec_deinit(void);
void cec_poll(void);
bool cec_is_enabled(void);
bool cec_send_standby(void);
bool cec_send_wake(void);

#endif
