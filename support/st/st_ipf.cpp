// st_ipf.cpp - IPF images for the Atari ST core, through a temporary SCP
//
// Copyright (C) 2026 Manfredi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vector>
#include <dlfcn.h>

#include "../minimig/capsapi/Comtype.h"
#include "../minimig/capsapi/CapsAPI.h"
#include "st_ipf.h"

#define IPF_REVS   3
#define TICK_NS    25.0

typedef SDWORD (*CAPSINIT)(void);
typedef SDWORD (*CAPSADDIMAGE)(void);
typedef SDWORD (*CAPSREMIMAGE)(SDWORD);
typedef SDWORD (*CAPSLOCKIMAGE)(SDWORD, PCHAR);
typedef SDWORD (*CAPSUNLOCKIMAGE)(SDWORD);
typedef SDWORD (*CAPSLOADIMAGE)(SDWORD, UDWORD);
typedef SDWORD (*CAPSGETIMAGEINFO)(PCAPSIMAGEINFO, SDWORD);
typedef SDWORD (*CAPSLOCKTRACK)(void *, SDWORD, UDWORD, UDWORD, UDWORD);
typedef SDWORD (*CAPSUNLOCKTRACK)(SDWORD, UDWORD, UDWORD);
typedef SDWORD (*CAPSUNLOCKALLTRACKS)(SDWORD);
typedef SDWORD (*CAPSSETREVOLUTION)(SDWORD, UDWORD);

static struct
{
	void *lib;
	bool tried;
	CAPSINIT init;
	CAPSADDIMAGE add;
	CAPSREMIMAGE rem;
	CAPSLOCKIMAGE lock;
	CAPSUNLOCKIMAGE unlock;
	CAPSLOADIMAGE load;
	CAPSGETIMAGEINFO info;
	CAPSLOCKTRACK lock_track;
	CAPSUNLOCKTRACK unlock_track;
	CAPSUNLOCKALLTRACKS unlock_all;
	CAPSSETREVOLUTION set_rev;
} caps;

static bool caps_load(char *err, size_t errlen)
{
	if (caps.lib) return true;
	if (caps.tried) { snprintf(err, errlen, "libcapsimg.so not available"); return false; }
	caps.tried = true;

	const char *env = getenv("ST_CAPSIMG");
	const char *paths[] = { env, "/media/fat/linux/libcapsimg.so", "/media/fat/libcapsimg.so", "/media/fat/linux/CAPSIMG/libcapsimg.so" };
	for (const char *p : paths)
	{
		if (p && (caps.lib = dlopen(p, RTLD_NOW))) break;
	}
	if (!caps.lib) { snprintf(err, errlen, "cannot load libcapsimg.so (%s)", dlerror()); return false; }

	caps.init = (CAPSINIT)dlsym(caps.lib, "CAPSInit");
	caps.add = (CAPSADDIMAGE)dlsym(caps.lib, "CAPSAddImage");
	caps.rem = (CAPSREMIMAGE)dlsym(caps.lib, "CAPSRemImage");
	caps.lock = (CAPSLOCKIMAGE)dlsym(caps.lib, "CAPSLockImage");
	caps.unlock = (CAPSUNLOCKIMAGE)dlsym(caps.lib, "CAPSUnlockImage");
	caps.load = (CAPSLOADIMAGE)dlsym(caps.lib, "CAPSLoadImage");
	caps.info = (CAPSGETIMAGEINFO)dlsym(caps.lib, "CAPSGetImageInfo");
	caps.lock_track = (CAPSLOCKTRACK)dlsym(caps.lib, "CAPSLockTrack");
	caps.unlock_track = (CAPSUNLOCKTRACK)dlsym(caps.lib, "CAPSUnlockTrack");
	caps.unlock_all = (CAPSUNLOCKALLTRACKS)dlsym(caps.lib, "CAPSUnlockAllTracks");
	caps.set_rev = (CAPSSETREVOLUTION)dlsym(caps.lib, "CAPSSetRevolution");

	if (!caps.init || !caps.add || !caps.rem || !caps.lock || !caps.unlock || !caps.load || !caps.info ||
	    !caps.lock_track || !caps.unlock_track || !caps.unlock_all || caps.init() != imgeOk)
	{
		snprintf(err, errlen, "libcapsimg.so: missing functions or init failed");
		dlclose(caps.lib);
		caps.lib = nullptr;
		return false;
	}
	return true;
}

static void put32(std::vector<uint8_t> &v, size_t at, uint32_t x)
{
	v[at] = x; v[at + 1] = x >> 8; v[at + 2] = x >> 16; v[at + 3] = x >> 24;
}

static void track_flux(const CapsTrackInfoT2 &ti, double &carry, std::vector<uint8_t> &data, double &rev_ns, uint32_t &seed)
{
	rev_ns = 0;
	auto emit = [&](void) {
		uint32_t t = (uint32_t)(carry / TICK_NS + 0.5);
		carry -= t * TICK_NS;
		while (t >= 65536) { data.push_back(0); data.push_back(0); t -= 65536; }
		if (!t) t = 1;
		data.push_back(t >> 8);
		data.push_back(t & 0xFF);
	};

	if ((ti.type & CTIT_MASK_TYPE) == ctitNoise || !ti.trackbuf || !ti.tracklen)
	{
		while (rev_ns < 200e6)
		{
			seed = seed * 1103515245u + 12345u;
			double t = 2000.0 + (seed >> 8) % 8000;
			rev_ns += t;
			carry += t;
			emit();
		}
		return;
	}

	double scale = 1.0;
	bool var = (ti.type & CTIT_MASK_TYPE) == ctitVar && ti.timebuf && ti.timelen;
	if (!var)
	{
		double total = 0;
		if (ti.timebuf && ti.timelen)
			for (UDWORD i = 0; i < ti.timelen && i * 8 < ti.tracklen; i++) total += ti.timebuf[i];
		else total = ti.tracklen / 8.0 * 1000.0;
		total = total * 2000.0 / 1000.0 * 8.0;
		if (total > 0) scale = 200e6 / total;
	}
	for (UDWORD b = 0; b < ti.tracklen; b++)
	{
		UDWORD byte = b >> 3;
		double ns = 2000.0;
		if (ti.timebuf && byte < ti.timelen) ns = 2000.0 * ti.timebuf[byte] / 1000.0;
		ns *= scale;
		carry += ns;
		rev_ns += ns;
		if (ti.trackbuf[byte] & (0x80 >> (b & 7))) emit();
	}
}

bool st_ipf_to_scp(const char *ipf_path, const char *scp_path, char *err, size_t errlen)
{
	if (!caps_load(err, errlen)) return false;

	SDWORD id = caps.add();
	if (id < 0) { snprintf(err, errlen, "CAPSAddImage failed"); return false; }
	bool ok = false;
	FILE *f = nullptr;

	do
	{
		if (caps.lock(id, (PCHAR)ipf_path) != imgeOk) { snprintf(err, errlen, "not an IPF image"); break; }
		const UDWORD lflags = DI_LOCK_DENVAR | DI_LOCK_UPDATEFD | DI_LOCK_TYPE | DI_LOCK_TRKBIT | DI_LOCK_OVLBIT;
		if (caps.load(id, lflags) != imgeOk) { snprintf(err, errlen, "CAPSLoadImage failed"); caps.unlock(id); break; }
		CapsImageInfo ii;
		memset(&ii, 0, sizeof(ii));
		if (caps.info(&ii, id) != imgeOk) { snprintf(err, errlen, "CAPSGetImageInfo failed"); caps.unlock(id); break; }

		int cyls = (int)ii.maxcylinder + 1;
		if (cyls > 84) cyls = 84;
		std::vector<uint8_t> out(0x10 + 168 * 4, 0);
		memcpy(out.data(), "SCP", 3);
		out[3] = 0x19;
		out[4] = 0x14;
		out[5] = IPF_REVS;
		out[6] = 0;
		out[7] = (uint8_t)(cyls * 2 - 1);
		out[8] = 0x01;
		out[9] = 0;
		out[10] = 0;
		out[11] = 0;

		uint32_t seed = 0x5EED1234;
		for (int cyl = 0; cyl < cyls; cyl++)
			for (int head = 0; head < 2; head++)
			{
				if (cyl < (int)ii.mincylinder || head < (int)ii.minhead || head > (int)ii.maxhead) continue;
				int tn = cyl * 2 + head;
				size_t th = out.size();
				put32(out, 0x10 + tn * 4, (uint32_t)th);
				out.resize(th + 4 + IPF_REVS * 12, 0);
				out[th] = 'T'; out[th + 1] = 'R'; out[th + 2] = 'K'; out[th + 3] = (uint8_t)tn;

				double carry = 0;
				for (int r = 0; r < IPF_REVS; r++)
				{
					CapsTrackInfoT2 ti;
					memset(&ti, 0, sizeof(ti));
					ti.type = 2;
					seed = seed * 1103515245u + 12345u;
					ti.wseed = seed;
					if (caps.set_rev) caps.set_rev(id, r);
					std::vector<uint8_t> data;
					double rev_ns = 0;
					if (caps.lock_track(&ti, id, cyl, head, lflags | DI_LOCK_SETWSEED) == imgeOk)
					{
						track_flux(ti, carry, data, rev_ns, seed);
						caps.unlock_track(id, cyl, head);
					}
					else
					{
						CapsTrackInfoT2 noise;
						memset(&noise, 0, sizeof(noise));
						noise.type = ctitNoise;
						track_flux(noise, carry, data, rev_ns, seed);
					}
					size_t e = th + 4 + r * 12;
					put32(out, e, (uint32_t)(rev_ns / TICK_NS + 0.5));
					put32(out, e + 4, (uint32_t)(data.size() / 2));
					put32(out, e + 8, (uint32_t)(out.size() - th));
					out.insert(out.end(), data.begin(), data.end());
				}
			}

		caps.unlock_all(id);
		caps.unlock(id);

		uint32_t sum = 0;
		for (size_t i = 0x10; i < out.size(); i++) sum += out[i];
		put32(out, 12, sum);

		f = fopen(scp_path, "wb");
		if (!f) { snprintf(err, errlen, "cannot write %s", scp_path); break; }
		ok = fwrite(out.data(), 1, out.size(), f) == out.size();
		fclose(f);
		if (!ok) snprintf(err, errlen, "write error on %s", scp_path);
	} while (0);

	caps.rem(id);
	return ok;
}
