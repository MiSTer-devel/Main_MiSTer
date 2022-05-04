#ifndef __INPUT_SOCKET_H
#define __INPUT_SOCKET_H

#include <linux/input.h>
#include <stdint.h>
#include "input.h"


void input_socket_init(void);
void input_socket_send(uint8_t, struct input_event *i, devInput *);
int input_socket_poll(int);
void input_socket_destroy(void);

#endif
