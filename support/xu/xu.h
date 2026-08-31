#pragma once

/*
 * XU (DEUNA/DELUA emulation) native networking, Phase 3: the ARM-side
 * daemon that plays the missing ENC424J600 hardware for
 * rtl/xu_enc424j600_shim.vhd, in the chip's own real terms (Microchip
 * DS39935C S9.0).
 *
 * Mirrors minimig_a2065's start/stop/poll lifecycle shape, but PDP2011 is
 * an ordinary CORE_TYPE_8BIT core (no dedicated boot-path hook the way
 * Minimig has) -- so unlike a2065_start(), which is called explicitly
 * from specific Minimig boot-path call sites, xu_poll() is fully
 * self-contained: it checks the loaded core's name and its own OSD
 * status bit on every call and starts/stops itself accordingly. The only
 * external hook needed is an unconditional xu_stop() from
 * user_io_init() (core-switch cleanup, mirrors a2065_stop()'s call
 * there), plus xu_poll() itself called from the same per-pass poll loop
 * a2065_poll() already runs from.
 */

void xu_poll(void);
void xu_stop(void);
