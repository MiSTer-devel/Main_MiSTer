// this file allows to enabled and disable rs232 debugging on a detailed basis
#ifndef DEBUG_H
#define DEBUG_H

#include "hardware.h"


// ------------ generic debugging -----------
#if 0
#define menu_debugf(...) printf(__VA_ARGS__)
#else
#define menu_debugf(...)
#endif

#if 1
// ini_parser debug output
#define ini_parser_debugf(a, ...) printf("\033[1;32mINI_PARSER : " a "\033[0m\n",## __VA_ARGS__)
#else
#define ini_parser_debugf(...)
#endif


// ----------- minimig debugging -------------
#if 0
#define hdd_debugf(a, ...) printf("\033[1;32mHDD: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define hdd_debugf(...)
#endif

#if 0
#define fdd_debugf(...) iprintf(__VA_ARGS__)
#else
#define fdd_debugf(...)
#endif


// -------------- TOS debugging --------------
#if 1
#define tos_debugf(a, ...) printf("\033[1;32mTOS: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define tos_debugf(...)
#endif

#if 1
// ikbd debug output in red
#define IKBD_DEBUG
#define ikbd_debugf(a, ...) printf("\033[1;32mIKBD: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define ikbd_debugf(...)
#endif

#endif // DEBUG_H
