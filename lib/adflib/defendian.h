//#include "config.h"

#ifndef LITT_ENDIAN
 #if defined(__hppa__) || \
     defined(__m68k__) || defined(mc68000) || defined(_M_M68K) || \
     (defined(__MIPS__) && defined(__MISPEB__)) || \
     defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
     defined(__sparc__)
 #else
  #define LITT_ENDIAN 1
 #endif
#endif

