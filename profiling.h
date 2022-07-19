#ifndef PROFILING_H
#define PROFILING_H 1

#include <inttypes.h>

#ifdef PROFILING

uint32_t profiling_event_begin(const char *name);
void profiling_event_end(uint32_t begin_idx, const char *name);
void profiling_spike_report(uint32_t begin_idx, uint32_t spike_us);

struct ProfilingScopedEvent
{
	const char *name;
	uint32_t spike_us;
	uint32_t begin_idx;

	ProfilingScopedEvent(const char *name)
		: name(name)
		, spike_us(0)
	{
		begin_idx = profiling_event_begin(name);
	}

	ProfilingScopedEvent(const char *name, uint32_t spike_us)
		: name(name)
		, spike_us(spike_us)
	{
		begin_idx = profiling_event_begin(name);
	}

	~ProfilingScopedEvent()
	{
		profiling_event_end(begin_idx, name);
		if (spike_us > 0) profiling_spike_report(begin_idx, spike_us);
	}
};

#define PROFILE_SCOPE(name) ProfilingScopedEvent __scope_timer(name)
#define PROFILE_FUNCTION() ProfilingScopedEvent __scope_timer(__FUNCTION__)
#define SPIKE_SCOPE(name, us) ProfilingScopedEvent __scope_timer(name, us)
#define SPIKE_FUNCTION(us) ProfilingScopedEvent __scope_timer(__FUNCTION__, us)

#else // PROFILING

#define PROFILE_SCOPE(name) 
#define PROFILE_FUNCTION()
#define SPIKE_SCOPE(name, us)
#define SPIKE_FUNCTION(us)

#endif // PROFILING

#endif // PROFILING_H
