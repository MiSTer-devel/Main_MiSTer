#ifdef LIBCO_C

/*[amd64, arm, ppc, x86]:
   by default, co_swap_function is marked as a text (code) section
   if not supported, uncomment the below line to use mprotect instead */
/* #define LIBCO_MPROTECT */

/*[amd64]:
   Win64 only: provides a substantial speed-up, but will thrash XMM regs
   do not use this unless you are certain your application won't use SSE */
/* #define LIBCO_NO_SSE */

#ifdef LIBCO_C
  #ifdef LIBCO_MP
    #define thread_local __thread
  #else
    #define thread_local
  #endif
#endif

#if __STDC_VERSION__ >= 201112L
  #ifndef _MSC_VER
    #include <stdalign.h>
  #endif
#else
  #define alignas(bytes)
#endif

#ifndef _MSC_VER
  #define section(name) __attribute__((section("." #name "#")))
#else
  #define section(name) __declspec(allocate("." #name))
#endif

/* ifdef LIBCO_C */
#endif
