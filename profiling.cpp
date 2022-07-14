#ifdef PROFILING

#include "profiling.h"

#include "str_util.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

struct Event
{
	const char *name;
	uint32_t begin_idx;
	struct timespec ts;
};

static constexpr int MAX_EVENTS = 512; // must be pow2
static Event s_events[MAX_EVENTS]; // circular buffer
static uint32_t s_event_tail = 0;

static constexpr Event *get_event(uint32_t idx)
{
	return &s_events[idx % MAX_EVENTS];
}

uint32_t profiling_event_begin(const char *name)
{
	Event *newEvent = get_event(s_event_tail);
	newEvent->begin_idx = s_event_tail;
	newEvent->name = name;
	clock_gettime(CLOCK_MONOTONIC, &newEvent->ts);

	uint32_t r = s_event_tail;
	s_event_tail++;
	return r;
}

void profiling_event_end(uint32_t begin_idx, const char *name)
{
	Event *newEvent = get_event(s_event_tail);
	newEvent->begin_idx = begin_idx;
	newEvent->name = name;
	clock_gettime(CLOCK_MONOTONIC, &newEvent->ts);
	s_event_tail++;
}

// result_ns = a - b
static uint64_t delta_ns(const struct timespec *a, const struct timespec *b)
{
	struct timespec ts;

	ts.tv_sec = a->tv_sec - b->tv_sec;
	ts.tv_nsec = a->tv_nsec - b->tv_nsec;
	if (ts.tv_nsec < 0)
	{
		ts.tv_nsec += 1000000000;
		ts.tv_sec -= 1;
	}

	uint64_t delta = ts.tv_sec * 1000000000ULL;
	delta += ts.tv_nsec;
	return delta;
}


// Bookkeeping data for spike report
static uint64_t inclusive_times[MAX_EVENTS];
static uint64_t other_times[MAX_EVENTS];
static uint32_t pair_stack[MAX_EVENTS / 2];

void profiling_spike_report(uint32_t begin_idx, uint32_t spike_us)
{
	int stack_pos = 0;

	if ((s_event_tail - begin_idx) < 2) return; // not enough events
	if ((s_event_tail - begin_idx) > MAX_EVENTS) return; // too many events

	const uint64_t total_ns = delta_ns(&get_event(s_event_tail - 1)->ts, &get_event(begin_idx)->ts);

	if (total_ns < (spike_us * 1000ULL)) return; // below threshold

	for (uint32_t idx = begin_idx; idx != s_event_tail; idx++)
	{
		const uint32_t cyc_idx = idx % MAX_EVENTS;
		Event *event = get_event(idx);

		if (event->begin_idx == idx)
		{
			pair_stack[stack_pos] = cyc_idx;
			inclusive_times[cyc_idx] = 0;
			other_times[cyc_idx] = 0;
			stack_pos++;
		}
		else
		{
			stack_pos--;
			uint32_t span_idx = pair_stack[stack_pos];
			const uint64_t inclusive_ns = delta_ns(&event->ts, &get_event(span_idx)->ts);
			inclusive_times[span_idx] = inclusive_ns;
			if (stack_pos > 0) other_times[pair_stack[stack_pos-1]] += inclusive_ns;
		}
	}

	char label[256];
	int indent = 0;
	printf("\n%lluus spike over %uus limit.\n", total_ns / 1000ULL, spike_us);
	printf("+----- Name -----------------------------------------+ Inc(us) + Exc(us) +\n");
	for (uint32_t idx = begin_idx; idx != s_event_tail; idx++)
	{
		const uint32_t cyc_idx = idx % MAX_EVENTS;
		Event *event = get_event(idx);

		if (event->begin_idx == idx)
		{
			memset(label, ' ', indent);
			strcpyz(label + indent, sizeof(label) - indent, event->name);
			printf("| %-50s | %7llu | %7llu |\n", label, inclusive_times[cyc_idx] / 1000ULL, (inclusive_times[cyc_idx] - other_times[cyc_idx]) / 1000ULL);
			indent += 2;
		}
		else
		{
			indent -= 2;
		}
	}
	printf("+----------------------------------------------------+---------+---------+\n\n");
	fflush(stdout);
}

#endif // PROFILING